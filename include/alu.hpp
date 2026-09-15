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
          input_cooldown_remaining_(input_count_, 0) {
        assert(function_latency_ > 0);
        if (output_count_ == 0) {
            throw std::invalid_argument("ALU requires at least one output");
        }
        logger_.configure(this->name(), log_options);

        SC_METHOD(hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();
    }

    void before_end_of_elaboration() override {
        for (auto& ready : tus_ready) {
            ready.write(false);
        }
        tds_valid.write(false);
    }

private:
    Op op_;
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;
    std::size_t log_cycle_{0};
    std::vector<std::size_t> input_cooldown_remaining_;

    void hw_pipe_sim_() {
        const bool do_deque = tds_valid.read() && hw_pipe_.back() && all_downstream_ready();
        const bool do_enque = !hw_pipe_.front() && all_inputs_handshaking();

        if (do_deque) {
            for (auto& data : out_data) {
                data.write(*hw_pipe_.back());
            }
            hw_pipe_.back().reset();
        }

        for (std::size_t index = function_latency_ - 1; index > 0; --index) {
            if (!hw_pipe_[index] && hw_pipe_[index - 1]) {
                hw_pipe_[index] = std::move(hw_pipe_[index - 1]);
                hw_pipe_[index - 1].reset();
            }
        }

        if (do_enque) {
            const T left = in_data[0].read();
            const T right = in_data[1].read();
            hw_pipe_.front() = static_cast<T>(op_(left, right));
            for (std::size_t port = 0; port < input_cooldown_remaining_.size(); ++port) {
                input_cooldown_remaining_[port] = input_interval_[port];
            }
        } else {
            for (auto& cooldown : input_cooldown_remaining_) {
                if (cooldown > 0) {
                    --cooldown;
                }
            }
        }

        tds_valid.write(hw_pipe_.back().has_value());
        for (std::size_t port = 0; port < tus_ready.size(); ++port) {
            tus_ready[port].write(!hw_pipe_.front().has_value()
                                  && input_cooldown_remaining_[port] == 0);
        }

        logger_.log_pipeline(sc_core::sc_time_stamp(), ++log_cycle_, hw_pipe_, do_deque, do_enque);
    }

    bool all_downstream_ready() const {
        return std::all_of(fds_ready.begin(), fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }

    bool all_inputs_valid() const {
        return std::all_of(fus_valid.begin(), fus_valid.end(), [](const auto& valid) {
            return valid.read();
        });
    }

    bool all_inputs_ready() const {
        return std::all_of(tus_ready.begin(), tus_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }

    bool all_inputs_handshaking() const {
        return all_inputs_valid() && all_inputs_ready();
    }

};
