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
                 std::size_t interval,
                 std::size_t function_latency,
                 sc_core::sc_time,
                 std::size_t output_count,
                 ModuleLogOptions log_options = {})
        : sc_core::sc_module(name),
          clk("clk"),
          out_data("out_data", output_count),
          tds_valid("tds_valid"),
          fds_ready("fds_ready", output_count),
          values_(std::move(values)),
          interval_(interval),
          function_latency_(function_latency),
          hw_pipe_(function_latency_),
          interval_remaining_(interval_) {
        assert(function_latency_ > 0);
        if (output_count == 0) {
            throw std::invalid_argument("ScalarSource requires at least one output");
        }
        logger_.configure(this->name(), log_options);

        SC_METHOD(hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();
    }

    void before_end_of_elaboration() override {
        tds_valid.write(false);
    }

private:
    std::vector<T> values_;
    std::size_t next_value_{0};
    std::size_t interval_;
    std::size_t function_latency_;
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;
    std::size_t log_cycle_{0};
    std::size_t interval_remaining_{0};

    void hw_pipe_sim_() {
        const bool do_deque = tds_valid.read() && hw_pipe_.back() && all_ready();
        const bool do_enque = !hw_pipe_.front() && next_value_ < values_.size()
                              && interval_remaining_ == 0;

        if (do_deque) {
            write_outputs(*hw_pipe_.back());
            hw_pipe_.back().reset();
        }

        for (std::size_t index = function_latency_ - 1; index > 0; --index) {
            if (!hw_pipe_[index] && hw_pipe_[index - 1]) {
                hw_pipe_[index] = std::move(hw_pipe_[index - 1]);
                hw_pipe_[index - 1].reset();
            }
        }

        if (do_enque) {
            hw_pipe_.front() = values_[next_value_++];
            interval_remaining_ = interval_;
        } else if (interval_remaining_ > 0) {
            --interval_remaining_;
        }

        tds_valid.write(hw_pipe_.back().has_value());

        logger_.log_pipeline(sc_core::sc_time_stamp(), ++log_cycle_, hw_pipe_, do_deque, do_enque);
    }

    bool all_ready() const {
        return std::all_of(fds_ready.begin(), fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }

    void write_outputs(const T& value) {
        for (auto& data : out_data) {
            data.write(value);
        }
    }
};
