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
    sc_core::sc_fifo<T>* in_data;
    sc_core::sc_in<bool> fus_valid;
    sc_core::sc_out<bool> tus_ready;
    std::vector<sc_core::sc_fifo<T>*> out_data;
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
          in_data(nullptr),
          fus_valid("fus_valid"),
          tus_ready("tus_ready"),
          out_data(output_count, nullptr),
          fds_ready("fds_ready", output_count),
          tds_valid("tds_valid"),
          hw_pipe_(function_latency_) {
        assert(function_latency_ > 0);
        if (output_count_ == 0) {
            throw std::invalid_argument("FIFO requires at least one output");
        }
        logger_.configure(this->name(), log_options);

        SC_THREAD(hw_pipe_sim_);
        SC_THREAD(inport_ready_);
        SC_THREAD(outport_valid_);
    }

    void before_end_of_elaboration() override {
        tus_ready.write(false);
        tds_valid.write(false);
    }

    void hw_pipe_sim_() {
        while (true) {
            wait(clk.posedge_event());
            wait(sc_core::SC_ZERO_TIME);

            const bool do_deque = hw_pipe_.back() && tds_valid.read() && all_downstream_ready();
            const bool do_enque = !hw_pipe_.front() && fus_valid.read() && tus_ready.read();

            if (do_deque) {
                for (auto& data : out_data) {
                    if (!data->nb_write(*hw_pipe_.back())) {
                        SC_REPORT_FATAL(name(), "failed to write output data FIFO");
                    }
                }
                hw_pipe_.back().reset();
                // deque_.notify(sc_core::SC_ZERO_TIME);
            }

            for (std::size_t index = function_latency_ - 1; index > 0; --index) {
                if (!hw_pipe_[index] && hw_pipe_[index - 1]) {
                    hw_pipe_[index] = std::move(hw_pipe_[index - 1]);
                    hw_pipe_[index - 1].reset();
                }
            }

            if (do_enque) {
                hw_pipe_.front() = in_data->read();
                enque_.notify(sc_core::SC_ZERO_TIME);
            }

            logger_.log_pipeline(sc_core::sc_time_stamp(), ++log_cycle_, hw_pipe_, do_deque, do_enque);
            pipe_updated_.notify(sc_core::SC_ZERO_TIME);
        }
    }

private:
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;
    std::size_t log_cycle_{0};
    sc_core::sc_event enque_;
    sc_core::sc_event deque_;
    sc_core::sc_event pipe_updated_;

    void wait_cycles(std::size_t cycles) {
        for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
            wait(clk.posedge_event());
        }
    }

    void inport_ready_() {
        tus_ready.write(false);
        wait(sc_core::SC_ZERO_TIME);

        while (true) {
            wait(clk->posedge_event());
            while (hw_pipe_.front()) {
                tus_ready.write(false);
                wait(pipe_updated_);
            }

            tus_ready.write(true);
            wait(enque_);
            tus_ready.write(false);
            wait_cycles(input_interval_[0]);
        }
    }

    void outport_valid_() {
        tds_valid.write(false);
        wait(sc_core::SC_ZERO_TIME);

        while (true) {
            wait(clk->posedge_event());
            wait(pipe_updated_);
            if (hw_pipe_.back()) {
                tds_valid.write(true);
            } else {
                tds_valid.write(false);
            }
            valid_updated_.notify(sc_core::SC_ZERO_TIME);
        }
    }

    bool all_downstream_ready() const {
        return std::all_of(fds_ready.begin(), fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }
};
