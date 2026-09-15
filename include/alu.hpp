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
    std::vector<sc_core::sc_fifo<T>*> in_data;
    sc_core::sc_vector<sc_core::sc_in<bool>> fus_valid;
    sc_core::sc_vector<sc_core::sc_out<bool>> tus_ready;
    std::vector<sc_core::sc_fifo<T>*> out_data;
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
          in_data(input_count_, nullptr),
          fus_valid("fus_valid", input_count_),
          tus_ready("tus_ready", input_count_),
          out_data(output_count_, nullptr),
          fds_ready("fds_ready", output_count_),
          tds_valid("tds_valid"),
          op_(std::move(op)),
          hw_pipe_(function_latency_) {
        assert(function_latency_ > 0);
        if (output_count_ == 0) {
            throw std::invalid_argument("ALU requires at least one output");
        }
        logger_.configure(this->name(), log_options);

        SC_THREAD(hw_pipe_sim_);
        SC_THREAD(inport0_ready_);
        SC_THREAD(inport1_ready_);
        SC_THREAD(outport_valid_);
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
    sc_core::sc_event enque_;
    sc_core::sc_event deque_;
    sc_core::sc_event pipe_updated_;


    void hw_pipe_sim_() {
        while (true) {
            wait(clk.posedge_event());
            wait(sc_core::SC_ZERO_TIME);

            const bool do_deque = hw_pipe_.back() && tds_valid.read() && all_downstream_ready();
            const bool do_enque = !hw_pipe_.front() && all_inputs_handshaking();

            if (do_deque) {
                for (auto& data : out_data) {
                    if (!data->nb_write(*hw_pipe_.back())) {
                        SC_REPORT_FATAL(name(), "failed to write output data FIFO");
                    }
                }
                hw_pipe_.back().reset();
                deque_.notify(sc_core::SC_ZERO_TIME);
            }

            for (std::size_t index = function_latency_ - 1; index > 0; --index) {
                if (!hw_pipe_[index] && hw_pipe_[index - 1]) {
                    hw_pipe_[index] = std::move(hw_pipe_[index - 1]);
                    hw_pipe_[index - 1].reset();
                }
            }

            if (do_enque) {
                const T left = in_data[0]->read();
                const T right = in_data[1]->read();
                hw_pipe_.front() = static_cast<T>(op_(left, right));
                enque_.notify(sc_core::SC_ZERO_TIME);
            }

            logger_.log_pipeline(sc_core::sc_time_stamp(), ++log_cycle_, hw_pipe_, do_deque, do_enque);
            pipe_updated_.notify(sc_core::SC_ZERO_TIME);
        }
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


    void wait_cycles(std::size_t cycles) {
        for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
            wait(clk.posedge_event());
        }
    }

    void inport_ready_(std::size_t port) {
        tus_ready[port].write(false);
        wait(sc_core::SC_ZERO_TIME);

        while (true) {
            wait_cycles(input_interval_[port]);

            while (hw_pipe_.front()) {
                tus_ready[port].write(false);
                wait(pipe_updated_);
            }

            tus_ready[port].write(true);
            wait(enque_);
            tus_ready[port].write(false);

        }
    }

    void inport0_ready_() {
        inport_ready_(0);
    }

    void inport1_ready_() {
        inport_ready_(1);
    }

    void outport_valid_() {
        tds_valid.write(false);
        wait(sc_core::SC_ZERO_TIME);

        while (true) {
            if (hw_pipe_.back()) {
                tds_valid.write(true);
                wait(deque_);
                tds_valid.write(false);
            } else {
                tds_valid.write(false);
                wait(pipe_updated_);
            }
        }
    }
};
