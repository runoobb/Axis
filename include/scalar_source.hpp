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

// ScalarSource models the data producer of the example system.
//
// Unified pipeline modelling pattern:
// a single clock driven process, hw_pipe_sim_, owns hw_pipe_ and is written
// from the last stage towards the first stage. After one invocation hw_pipe_
// holds the values the sequential registers will sample on the next clock
// edge, while out_data / tds_valid are sc_signals whose lazy update lets the
// downstream modules read the values written during the previous evaluation.
//
// do_deque is derived directly from tds_valid and the downstream ready
// signals inside hw_pipe_sim_.
template <typename T = int>
class ScalarSource : public sc_core::sc_module {
public:
    sc_core::sc_in<bool> clk;
    sc_core::sc_out<T> out_data;
    sc_core::sc_out<bool> tds_valid;
    sc_core::sc_vector<sc_core::sc_in<bool>> fds_ready;

    SC_HAS_PROCESS(ScalarSource);

    ScalarSource(sc_core::sc_module_name name,
                 std::vector<T> values,
                 std::size_t function_latency,
                 sc_core::sc_time clock_period,
                 std::size_t output_count,
                 ModuleLogOptions log_options = {})
        : sc_core::sc_module(name),
          clk("clk"),
          out_data("out_data"),
          tds_valid("tds_valid"),
          fds_ready("fds_ready", output_count),
          values_(std::move(values)),
          function_latency_(function_latency),
          hw_pipe_(function_latency){
        assert(function_latency_ > 0);
        if (output_count == 0) {
            throw std::invalid_argument("ScalarSource requires at least one output");
        }
        logger_.configure(this->name(), log_options, clock_period);

        SC_METHOD(hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();

        // do_deque is a pure function of the handshake signals, so it is
        // recomputed in the delta cycle whenever one of them changes rather
        // than being sampled on the clock edge.
        SC_METHOD(hw_transfer_sim_);
        sensitive << tds_valid;
        for (auto& ready : fds_ready) {
            sensitive << ready;
        }
        dont_initialize();
    }

    void end_of_elaboration() override {
        tds_valid.write(false);
    }

private:
    std::vector<T> values_;
    std::size_t next_value_{0};
    std::size_t function_latency_;
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;

    void hw_pipe_sim_() {

        const bool do_deque = tds_valid.read() && all_downstream_ready();
        
        if (do_deque) 
            hw_pipe_.back().reset();


        for (std::size_t i = function_latency_ - 1; i > 0; --i) {
            if (!hw_pipe_[i].has_value() && hw_pipe_[i - 1].has_value()) {
                hw_pipe_[i] = std::move(hw_pipe_[i - 1]);
                hw_pipe_[i - 1].reset();
            }
        }

        const bool do_enque = !hw_pipe_.front().has_value();

        if(do_enque && next_value_ < values_.size()){
            hw_pipe_.front() = values_[next_value_++];
        }


        if (hw_pipe_.back().has_value()) {
            out_data.write(*hw_pipe_.back());
        }
        
        tds_valid.write(hw_pipe_.back().has_value());

        logger_.log_pipeline(hw_pipe_, do_deque, do_enque);
    }

    void hw_transfer_sim_() {
        // tus_ready.write(tds_valid.read() && all_downstream_ready() | hw_pipe_.front().has_value());
    }

    bool all_downstream_ready() const {
        return std::all_of(fds_ready.begin(), fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }
};
