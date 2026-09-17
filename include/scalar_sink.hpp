#pragma once

#include "module_logger.hpp"

#include <cassert>
#include <cstddef>
#include <optional>
#include <systemc>
#include <utility>
#include <vector>

template <typename T = int>
class ScalarSink : public sc_core::sc_module {
public:
    sc_core::sc_in<bool> clk;
    sc_core::sc_in<T> in_data;
    sc_core::sc_in<bool> fus_valid;
    sc_core::sc_out<bool> tus_ready;

    SC_HAS_PROCESS(ScalarSink);

    ScalarSink(sc_core::sc_module_name name,
               std::size_t input_interval,
               std::size_t function_latency,
               sc_core::sc_time clock_period,
               ModuleLogOptions log_options = {})
        : sc_core::sc_module(name),
          clk("clk"),
          in_data("in_data"),
          fus_valid("fus_valid"),
          tus_ready("tus_ready"),
          input_interval_(input_interval),
          function_latency_(function_latency),
          hw_pipe_(function_latency) {
        assert(function_latency_ > 0);
        logger_.configure(this->name(), log_options, clock_period);

        SC_METHOD(hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();
    }

    void end_of_elaboration() override {
        tus_ready.write(true);
    }

private:
    std::size_t input_interval_;
    std::size_t function_latency_;
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;
    std::size_t input_cooldown_remaining_{0};

    void hw_pipe_sim_() {
        const bool do_deque = hw_pipe_.back().has_value();

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
            input_cooldown_remaining_ = input_interval_;
        } else if (input_cooldown_remaining_ > 0) {
            --input_cooldown_remaining_;
        }

        tus_ready.write(front_can_accept && input_cooldown_remaining_ == 0);

        logger_.log_pipeline(hw_pipe_, do_deque, do_enque);
    }
};
