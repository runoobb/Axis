#pragma once

#include "base_hw.hpp"
#include "module_logger.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <optional>
#include <stdexcept>
#include <systemc>
#include <utility>
#include <vector>

template <typename T, typename Op = std::plus<T>>
class ALU : public BaseHW {
public:
    sc_core::sc_in<bool> clk;
    sc_core::sc_vector<sc_core::sc_in<T>> in_data;
    sc_core::sc_vector<sc_core::sc_in<bool>> fus_valid;
    // A multi-input ALU exposes a single ready line shared by every upstream port: the inputs
    // handshake jointly, so no upstream can retire its payload ahead of its partners.
    sc_core::sc_out<bool> tus_ready;
    sc_core::sc_out<T> out_data;
    sc_core::sc_vector<sc_core::sc_in<bool>> fds_ready;
    sc_core::sc_out<bool> tds_valid;

    SC_HAS_PROCESS(ALU);

    ALU(sc_core::sc_module_name name,
        std::vector<std::size_t> input_interval,
        std::size_t function_latency,
        sc_core::sc_time clock_period,
        std::size_t output_count,
        Op op = Op{},
        ModuleLogOptions log_options = {})
        : BaseHW(name, 2, output_count, std::move(input_interval), function_latency, clock_period),
          clk("clk"),
          in_data("in_data", input_count_),
          fus_valid("fus_valid", input_count_),
          tus_ready("tus_ready"),
          out_data("out_data"),
          fds_ready("fds_ready", output_count_),
          tds_valid("tds_valid"),
          op_(std::move(op)),
          hw_pipe_(function_latency_),
          input_cooldown_remaining_(input_count_, 0) {
        assert(function_latency_ > 0);
        if (output_count_ == 0) {
            throw std::invalid_argument("ALU requires at least one output");
        }
        logger_.configure(this->name(), log_options, clock_period_);

        SC_METHOD(hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();

        SC_METHOD(hw_transfer_sim_);
        sensitive << tds_valid;
        for (auto& valid : fus_valid) {
            sensitive << valid;
        }
        for (auto& ready : fds_ready) {
            sensitive << ready;
        }
        dont_initialize();
    }

    void end_of_elaboration() override {
        // Ready may only rise once every upstream port has been observed valid together,
        // otherwise an early handshake would drop a payload that has nowhere to be held.
        tus_ready.write(false);
        tds_valid.write(false);
    }

private:
    Op op_;
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;
    std::vector<std::size_t> input_cooldown_remaining_;
    // Pipeline-side half of the tus_ready condition, owned by hw_pipe_sim_ and consumed by the
    // delta-cycle re-evaluation below.

    void hw_pipe_sim_() {
        const bool do_deque = tds_valid.read() && all_downstream_ready();

        if (do_deque) {
            hw_pipe_.back().reset();
        }

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

        const bool do_enque = tus_ready.read() && std::all_of(fus_valid.begin(), fus_valid.end(), [](const auto& valid) {
            return valid.read();
        });

        if (do_enque) {
            hw_pipe_.front() = static_cast<T>(op_(in_data[0].read(), in_data[1].read()));
        }

        for (std::size_t port = 0; port < input_count_; ++port) {
            if (do_enque) {
                input_cooldown_remaining_[port] = input_interval_[port];
            } else if (input_cooldown_remaining_[port] > 0) {
                --input_cooldown_remaining_[port];
            }
        }

        if (hw_pipe_.back().has_value()) {
            out_data.write(hw_pipe_.back().value());
        }
        tds_valid.write(hw_pipe_.back().has_value());

        logger_.log_pipeline(hw_pipe_, do_deque, do_enque);
    }

    // Why need this update_tus_ready_next_cycle() function?
    // Because for sc_signal, at end of current simulation time, ensure that they are up to date at current cycle
    // For a joint handshake, tus_ready is combinational logic to fus_valid, affected by upstream modules.
    // So it need to be decoupled from hw_pipe_sim_(), re-evaluate at delta cycle
    void hw_transfer_sim_() {

        const bool all_upstream_valid = std::all_of(fus_valid.begin(), fus_valid.end(), [](const auto& valid) {
            return valid.read();
        });

        const bool all_cooldowns_expired =
            std::all_of(input_cooldown_remaining_.begin(), input_cooldown_remaining_.end(), [](std::size_t remaining) { 
                return remaining == 0; 
        });

        const bool hw_pipe_not_full = 
            std::any_of(hw_pipe_.begin(), hw_pipe_.end(), [](const auto& stage) {
                return !stage.has_value();
        });

        tus_ready.write(((tds_valid.read() && all_downstream_ready()) || hw_pipe_not_full) && all_cooldowns_expired && all_upstream_valid);
    }

    bool all_downstream_ready() const {
        return std::all_of(fds_ready.begin(), fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }
};
