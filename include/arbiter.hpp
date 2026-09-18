#pragma once

#include "base_hw.hpp"
#include "module_logger.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <systemc>
#include <utility>
#include <vector>

template <typename T>
class Arbiter : public BaseHW {
public:
    sc_core::sc_in<bool> clk;
    sc_core::sc_vector<sc_core::sc_in<T>> in_data;
    sc_core::sc_vector<sc_core::sc_in<bool>> fus_valid;
    sc_core::sc_vector<sc_core::sc_out<bool>> tus_ready;
    sc_core::sc_out<T> out_data;
    sc_core::sc_vector<sc_core::sc_in<bool>> fds_ready;
    sc_core::sc_out<bool> tds_valid;
    sc_core::sc_vector<sc_core::sc_in<bool>> transfer_fus;
    sc_core::sc_out<bool> transfer_tds;

    SC_HAS_PROCESS(Arbiter);

    Arbiter(sc_core::sc_module_name name,
            std::size_t input_count,
            std::size_t output_count,
            std::vector<std::size_t> input_interval,
            std::size_t function_latency,
            sc_core::sc_time clock_period,
            ModuleLogOptions log_options = {})
        : BaseHW(name,
                 input_count,
                 output_count,
                 std::move(input_interval),
                 function_latency,
                 clock_period),
          clk("clk"),
          in_data("in_data", input_count_),
          fus_valid("fus_valid", input_count_),
          tus_ready("tus_ready", input_count_),
          out_data("out_data"),
          fds_ready("fds_ready", output_count_),
          tds_valid("tds_valid"),
          transfer_fus("transfer_fus", input_count_),
          transfer_tds("transfer_tds"),
          hw_pipe_(function_latency_),
          input_cooldown_remaining_(input_count_, 0) {
        assert(function_latency_ > 0);
        if (input_count_ == 0) {
            throw std::invalid_argument("Arbiter requires at least one input");
        }
        if (output_count_ == 0) {
            throw std::invalid_argument("Arbiter requires at least one output");
        }
        assert(std::all_of(input_interval_.begin(), input_interval_.end(), [](std::size_t interval) {
            return interval == 0;
        }));
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
        for (std::size_t port = 0; port < input_count_; ++port) {
            tus_ready[port].write(false);
        }
        tds_valid.write(false);
        transfer_tds.write(false);
    }

private:
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;
    // Kept for structural parity with fifo.hpp / scalar_sink.hpp. Every arbiter interval is
    // asserted to be 0, so the decrement branch is never actually taken.
    std::vector<std::size_t> input_cooldown_remaining_;

    void hw_pipe_sim_() {
        const bool do_deque = transfer_tds.read();

        // AT CURRENT LINE, std::vector<> hw_pipe_ store values of previous cycle
        // if do_deque == true, then hw_pipe_.back() is sampled by downstream at clock posedge of current cycle by in_data.read()
        // so pipeline will advance at current cycle, hw_pipe_.back() will be set to value of its first last stage of pipeline 
        // (we ensure AT THE END LINE of hw_pipe_sim_(), data in port sc_out<> out_data is same as hw_pipe_.back())
        if (do_deque) {
            hw_pipe_.back().reset();
        }

        if (function_latency_ > 1) {
            for (std::size_t i = function_latency_ - 1; i > 0; --i) {
                if (!hw_pipe_[i].has_value() && hw_pipe_[i - 1].has_value()) {
                    hw_pipe_[i] = std::move(hw_pipe_[i - 1]);
                    hw_pipe_[i - 1].reset();
                }
            }
        }

        const auto grant = select_grant_();
        const bool do_enque = grant.has_value() && transfer_fus[*grant].read();
        if (do_enque) {
            hw_pipe_.front() = in_data[*grant].read();
        }

        for (std::size_t port = 0; port < input_count_; ++port) {
            if (do_enque && port == *grant) {
                input_cooldown_remaining_[port] = input_interval_[port];
            } else if (input_cooldown_remaining_[port] > 0) {
                --input_cooldown_remaining_[port];
            }
        }

        // AT CURRENT LINE, std::vector<> hw_pipe_ store values sampled at current cycle

        // ensure that data in port sc_out<> out_data is same as new value of hw_pipe_.back() sampled at current cycle
        if (hw_pipe_.back().has_value()) {
            out_data.write(*hw_pipe_.back());
        }

        tds_valid.write(hw_pipe_.back().has_value());

        logger_.log_pipeline(hw_pipe_, do_deque, do_enque);
    }

    bool all_downstream_ready_() const {
        return std::all_of(fds_ready.begin(), fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }

    std::optional<std::size_t> select_grant_() const {
        for (std::size_t port = 0; port < input_count_; ++port) {
            if (fus_valid[port].read() && input_cooldown_remaining_[port] == 0) {
                return port;
            }
        }
        return std::nullopt;
    }

    void hw_transfer_sim_() {
        transfer_tds.write(tds_valid.read() && all_downstream_ready_());

        const bool hw_pipe_not_full =
            std::any_of(hw_pipe_.begin(), hw_pipe_.end(), [](const auto& stage) {
                return !stage.has_value();
            });

        const auto grant = select_grant_();

        for (std::size_t port = 0; port < input_count_; ++port) {
            tus_ready[port].write(((tds_valid.read() && all_downstream_ready_())|| hw_pipe_not_full) && grant.has_value() && port == *grant
                                  && input_cooldown_remaining_[port] == 0);
        }
    }
};
