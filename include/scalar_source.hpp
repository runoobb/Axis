#pragma once

#include "module_logger.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <systemc>
#include <utility>
#include <vector>

template <typename T = int>
class ScalarSource : public sc_core::sc_module {
public:
    sc_core::sc_in<bool> clk;
    sc_core::sc_vector<sc_core::sc_out<T>> out_data;
    sc_core::sc_out<bool> tds_valid;
    sc_core::sc_vector<sc_core::sc_in<bool>> fds_ready;

    SC_HAS_PROCESS(ScalarSource);

    ScalarSource(sc_core::sc_module_name name,
                 std::vector<T> values,
                 std::size_t output_interval,
                 std::size_t function_latency,
                 sc_core::sc_time clock_period,
                 std::size_t output_count,
                 ModuleLogOptions log_options = {})
        : sc_core::sc_module(name),
          clk("clk"),
          out_data("out_data", output_count),
          tds_valid("tds_valid"),
          fds_ready("fds_ready", output_count),
          values_(std::move(values)),
          output_interval_(output_interval),
          function_latency_(function_latency),
          hw_pipe_(function_latency_),
          output_interval_remaining_(output_interval_) {
        assert(function_latency_ > 0);
        if (output_count == 0) {
            throw std::invalid_argument("ScalarSource requires at least one output");
        }
        logger_.configure(this->name(), log_options, clock_period);

        SC_METHOD(hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();
    }

    void end_of_elaboration() override {
        tds_valid.write(false);
    }

private:
    std::vector<T> values_;
    std::size_t next_value_{0};
    std::size_t output_interval_;
    std::size_t function_latency_;
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;
    std::size_t output_interval_remaining_{0};

    void hw_pipe_sim_() {
        const bool do_deque = tds_valid.read() && all_downstream_ready();
        // const bool do_enque = !hw_pipe_.back().has_value() && all_downstream_ready();

        if (do_deque) {
            hw_pipe_.back().reset();
        }

        // ------------------------------------------------------------
        // Move pipeline stages.
        //
        // IMPORTANT:
        // Iterate from back to front so that every item moves
        // at most ONE stage in one clock cycle.
        // ------------------------------------------------------------
        if(function_latency_ > 1) {
            for (std::size_t i = function_latency_ - 1; i > 0; --i) {
                if (!hw_pipe_[i].has_value() &&
                    hw_pipe_[i - 1].has_value()) {

                    hw_pipe_[i] =
                        std::move(hw_pipe_[i - 1]);

                    hw_pipe_[i - 1].reset();
                }
            }
        }

        // do_enque should be calculated here after the state of hw_pipe_ has been updated for this cycle
        const bool do_enque = !hw_pipe_.front().has_value() && next_value_ < values_.size()
                              && output_interval_remaining_ == 0;   

        if (do_enque) {
            hw_pipe_.front() = values_[next_value_++];
            output_interval_remaining_ = output_interval_;
        } else if (output_interval_remaining_ > 0) {
            --output_interval_remaining_;
        }

        if (hw_pipe_.back().has_value()) {
            for (auto& data : out_data) {
                data.write(*hw_pipe_.back());
            }
        }
        
        tds_valid.write(hw_pipe_.back().has_value() && all_downstream_ready());
        logger_.log_pipeline(hw_pipe_, do_deque, do_enque);
    }


    bool all_downstream_ready() const {
        return std::all_of(fds_ready.begin(), fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }

};
