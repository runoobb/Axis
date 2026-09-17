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
    sc_core::sc_vector<sc_core::sc_out<bool>> tus_ready;
    sc_core::sc_vector<sc_core::sc_out<T>> out_data;
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
          tus_ready("tus_ready", input_count_),
          out_data("out_data", output_count_),
          fds_ready("fds_ready", output_count_),
          tds_valid("tds_valid"),
          op_(std::move(op)),
          hw_pipe_(function_latency_),
          input_cooldown_remaining_(input_count_, 0),
          pending_inputs_(input_count_) {
        assert(function_latency_ > 0);
        if (output_count_ == 0) {
            throw std::invalid_argument("ALU requires at least one output");
        }
        logger_.configure(this->name(), log_options, clock_period_);

        SC_METHOD(hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();
    }

    void end_of_elaboration() override {
        for (auto& ready : tus_ready) {
            ready.write(true);
        }
        tds_valid.write(false);
    }

private:
    Op op_;
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;
    std::vector<std::size_t> input_cooldown_remaining_;
    std::vector<std::optional<T>> pending_inputs_;

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

        const bool front_can_accept = !hw_pipe_.front().has_value();
        for (std::size_t port = 0; port < input_count_; ++port) {
            if (tus_ready[port].read() && fus_valid[port].read() && front_can_accept
                && !pending_inputs_[port].has_value()) {
                pending_inputs_[port] = in_data[port].read();
                input_cooldown_remaining_[port] = input_interval_[port];
            } else if (input_cooldown_remaining_[port] > 0) {
                --input_cooldown_remaining_[port];
            }
        }

        const bool do_enque = front_can_accept && pending_inputs_[0].has_value()
                              && pending_inputs_[1].has_value();
        if (do_enque) {
            hw_pipe_.front() = static_cast<T>(op_(*pending_inputs_[0], *pending_inputs_[1]));
            for (auto& input : pending_inputs_) {
                input.reset();
            }
        }

        if (hw_pipe_.back().has_value()) {
            for (auto& data : out_data) {
                data.write(*hw_pipe_.back());
            }
        }
        tds_valid.write(hw_pipe_.back().has_value() && all_downstream_ready());

        for (std::size_t port = 0; port < input_count_; ++port) {
            tus_ready[port].write(front_can_accept && !pending_inputs_[port].has_value()
                                  && input_cooldown_remaining_[port] == 0);
        }

        logger_.log_pipeline(hw_pipe_, do_deque, do_enque);
    }

    bool all_downstream_ready() const {
        return std::all_of(fds_ready.begin(), fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }
};
