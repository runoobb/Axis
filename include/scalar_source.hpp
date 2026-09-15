#pragma once

#include "module_logger.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <systemc>
#include <utility>
#include <vector>

template <typename T = int>
class ScalarSource : public sc_core::sc_module {
public:
    sc_core::sc_in<bool> clk;
    std::vector<sc_core::sc_fifo<T>*> out_data;
    sc_core::sc_vector<sc_core::sc_out<bool>> tds_valid;
    sc_core::sc_vector<sc_core::sc_in<bool>> fds_ready;

    SC_HAS_PROCESS(ScalarSource);

    ScalarSource(sc_core::sc_module_name name,
                 std::vector<T> values,
                 std::size_t input_interval,
                 sc_core::sc_time,
                 std::size_t output_count,
                 ModuleLogOptions log_options = {})
        : sc_core::sc_module(name),
          clk("clk"),
          out_data(output_count, nullptr),
          tds_valid("tds_valid", output_count),
          fds_ready("fds_ready", output_count),
          values_(std::move(values)),
          input_interval_(input_interval),
          hw_pipe_(1) {
        if (output_count == 0) {
            throw std::invalid_argument("ScalarSource requires at least one output");
        }
        logger_.configure(this->name(), log_options);

        SC_THREAD(hw_pipe_sim_);
        SC_THREAD(outport_valid_);
    }

    void before_end_of_elaboration() override {
        write_valid(false);
    }

private:
    std::vector<T> values_;
    std::size_t next_value_{0};
    std::size_t input_interval_;
    std::size_t cycle_{0};
    std::size_t next_input_cycle_{0};
    std::vector<std::optional<T>> hw_pipe_;
    ModuleLogger logger_;
    std::size_t log_cycle_{0};
    sc_core::sc_event enque_;
    sc_core::sc_event deque_;
    sc_core::sc_event pipe_updated_;

    bool all_ready() const {
        return std::all_of(fds_ready.begin(), fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }

    void write_valid(bool value) {
        for (auto& valid : tds_valid) {
            valid.write(value);
        }
    }

    void write_outputs(const T& value) {
        for (auto& data : out_data) {
            if (!data->nb_write(value)) {
                SC_REPORT_FATAL(name(), "failed to write output data FIFO");
            }
        }
    }

    void hw_pipe_sim_() {
        while (true) {
            wait(clk.posedge_event());
            wait(sc_core::SC_ZERO_TIME);
            ++cycle_;

            const bool do_deque = hw_pipe_.back() && all_ready();
            const bool do_enque = !hw_pipe_.front() && next_value_ < values_.size()
                                  && cycle_ >= next_input_cycle_;

            if (do_deque) {
                write_outputs(*hw_pipe_.back());
                hw_pipe_.back().reset();
                deque_.notify(sc_core::SC_ZERO_TIME);
            }

            for (std::size_t index = hw_pipe_.size() - 1; index > 0; --index) {
                if (!hw_pipe_[index] && hw_pipe_[index - 1]) {
                    hw_pipe_[index] = std::move(hw_pipe_[index - 1]);
                    hw_pipe_[index - 1].reset();
                }
            }

            if (do_enque) {
                hw_pipe_.front() = values_[next_value_++];
                next_input_cycle_ = cycle_ + input_interval_;
                enque_.notify(sc_core::SC_ZERO_TIME);
            }

            logger_.log_pipeline(sc_core::sc_time_stamp(), ++log_cycle_, hw_pipe_, do_deque, do_enque);
            pipe_updated_.notify(sc_core::SC_ZERO_TIME);
        }
    }

    void outport_valid_() {
        write_valid(false);
        wait(sc_core::SC_ZERO_TIME);

        while (true) {
            if (hw_pipe_.back()) {
                write_valid(true);
                wait(deque_);
                write_valid(false);
            } else {
                write_valid(false);
                wait(pipe_updated_);
            }
        }
    }
};
