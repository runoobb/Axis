#pragma once

#include "base_hw.hpp"
#include "module_logger.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <stdexcept>
#include <systemc>
#include <utility>
#include <vector>

template <typename T>
class FIFO : public BaseHW {
public:
    sc_core::sc_in<bool> clk;
    sc_core::sc_in<T> in_data;
    sc_core::sc_in<bool> fus_valid;
    sc_core::sc_out<bool> tus_ready;
    sc_core::sc_vector<sc_core::sc_out<T>> out_data;
    sc_core::sc_vector<sc_core::sc_in<bool>> fds_ready;
    sc_core::sc_out<bool> tds_valid;

    SC_HAS_PROCESS(FIFO);

    FIFO(sc_core::sc_module_name name,
         std::size_t function_latency,
         sc_core::sc_time clock_period,
         std::size_t output_count,
         ModuleLogOptions log_options = {})
        : BaseHW(name, 1, output_count, {0}, function_latency, clock_period),
          clk("clk"),
          in_data("in_data"),
          fus_valid("fus_valid"),
          tus_ready("tus_ready"),
          out_data("out_data", output_count),
          fds_ready("fds_ready", output_count),
          tds_valid("tds_valid"),
          hw_pipe_(function_latency_) {
        assert(function_latency_ > 0);
        if (output_count_ == 0) {
            throw std::invalid_argument("FIFO requires at least one output");
        }
        logger_.configure(this->name(), log_options, clock_period_);

        SC_METHOD(hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();
    }

    void end_of_elaboration() override {
        tus_ready.write(true);
        tds_valid.write(false);
    }

    void hw_pipe_sim_() {
        const bool do_deque = tds_valid.read() && all_downstream_ready();

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

        const bool front_can_accept = !hw_pipe_.front().has_value();
        const bool do_enque = tus_ready.read() && fus_valid.read() && front_can_accept;

        if (do_enque) {
            hw_pipe_.front() = in_data.read();
            input_cooldown_remaining_ = input_interval_[0];
        } else if (input_cooldown_remaining_ > 0) {
            --input_cooldown_remaining_;
        }

        if (hw_pipe_.back().has_value()) {
            for (auto& data : out_data) {
                data.write(*hw_pipe_.back());
            }
        }
        tds_valid.write(hw_pipe_.back().has_value() && all_downstream_ready());
        tus_ready.write(front_can_accept && input_cooldown_remaining_ == 0);

        logger_.log_pipeline(hw_pipe_, do_deque, do_enque);
    }

private:
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;
    std::size_t input_cooldown_remaining_{0};

    bool all_downstream_ready() const {
        return std::all_of(fds_ready.begin(), fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }
};
