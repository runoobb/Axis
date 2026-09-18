#pragma once

#include "module_logger.hpp"

#include <cassert>
#include <cstddef>
#include <optional>
#include <systemc>
#include <utility>
#include <vector>

// ScalarSink models the data consumer of the example system.
//
// Unified pipeline modelling pattern:
// a single clock driven process, hw_pipe_sim_, owns hw_pipe_ and is written
// from the last stage towards the first stage. After one invocation hw_pipe_
// holds the values the sequential registers will sample on the next clock
// edge, while tus_ready is an sc_signal whose lazy update lets the upstream
// module read the value written during the previous evaluation.
template <typename T = int>
class ScalarSink : public sc_core::sc_module {
public:
    sc_core::sc_in<bool> clk;
    sc_core::sc_in<T> in_data;
    sc_core::sc_in<bool> fus_valid;
    sc_core::sc_out<bool> tus_ready;
    sc_core::sc_in<bool> transfer_fus;

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
          transfer_fus("transfer_fus"),
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
        // ------------------------------------------------------------
        // Last stage: retire the data.
        //
        // The sink owns no downstream port, so the last stage always has
        // room to sink its data and never stalls the pipeline.
        // ------------------------------------------------------------
        constexpr bool do_deque = true;
        hw_pipe_.back().reset();

        // ------------------------------------------------------------
        // Intermediate stages: advance the pipeline.
        //
        // IMPORTANT:
        // Iterate from the last stage towards the first one so that every
        // item moves at most ONE stage in one clock cycle.
        // ------------------------------------------------------------
        for (std::size_t i = function_latency_ - 1; i > 0; --i) {
            if (!hw_pipe_[i].has_value() && hw_pipe_[i - 1].has_value()) {
                hw_pipe_[i] = std::move(hw_pipe_[i - 1]);
                hw_pipe_[i - 1].reset();
            }
        }

        // ------------------------------------------------------------
        // First stage: upstream valid-ready handshake.
        //
        // tus_ready already carries the "stage 0 has room" condition, so the
        // handshake reduces to a plain valid && ready test. Because the last
        // stage retires unconditionally and the shift above runs back to
        // front, stage 0 is always free at this point; the only backpressure
        // the sink applies is the input_interval_ cooldown.
        // ------------------------------------------------------------
        const bool do_enque = transfer_fus.read();

        if (do_enque) {
            hw_pipe_.front() = in_data.read();
            input_cooldown_remaining_ = input_interval_;
        } else if (input_cooldown_remaining_ > 0) {
            --input_cooldown_remaining_;
        }

        // ------------------------------------------------------------
        // Drive the handshake signal the upstream module reads on the next
        // clock edge.
        // ------------------------------------------------------------
        tus_ready.write(input_cooldown_remaining_ == 0);

        logger_.log_pipeline(hw_pipe_, do_deque, do_enque);
    }
};
