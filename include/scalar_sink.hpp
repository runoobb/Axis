#pragma once

#include "module_logger.hpp"

#include <cassert>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <systemc>
#include <utility>
#include <vector>

template <typename T = int>
class ScalarSink : public sc_core::sc_module {
public:
    sc_core::sc_in<bool> clk;
    sc_core::sc_vector<sc_core::sc_in<T>> in_data;
    sc_core::sc_vector<sc_core::sc_in<bool>> fus_valid;
    sc_core::sc_vector<sc_core::sc_out<bool>> tus_ready;

    SC_HAS_PROCESS(ScalarSink);

    ScalarSink(sc_core::sc_module_name name,
               std::size_t interval,
               std::size_t function_latency,
               sc_core::sc_time clock_period,
               std::vector<T> expected,
               ModuleLogOptions log_options = {})
        : ScalarSink(name,
                     1,
                     interval,
                     function_latency,
                     clock_period,
                     std::vector<std::vector<T>>{std::move(expected)},
                     log_options) {}

    ScalarSink(sc_core::sc_module_name name,
               std::size_t port_count,
               std::size_t interval,
               std::size_t function_latency,
               sc_core::sc_time clock_period,
               std::vector<std::vector<T>> expected,
               ModuleLogOptions log_options = {})
        : sc_core::sc_module(name),
          clk("clk"),
          in_data("in_data", port_count),
          fus_valid("fus_valid", port_count),
          tus_ready("tus_ready", port_count),
          interval_(interval),
          function_latency_(function_latency),
          expected_(std::move(expected)),
          observed_count_(port_count, 0),
          accepted_count_(port_count, 0),
          accepted_current_valid_(port_count, false),
          hw_pipe_(port_count, std::vector<std::optional<T>>(function_latency_)),
          interval_remaining_(port_count, 0) {
        assert(function_latency_ > 0);
        if (expected_.size() != port_count) {
            throw std::invalid_argument("expected sequence count must match port_count");
        }
        logger_.configure(this->name(), log_options, clock_period);

        SC_METHOD(hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();
    }

    void before_end_of_elaboration() override {
        for (auto& ready : tus_ready) {
            ready.write(false);
        }
    }


private:
    std::size_t interval_;
    std::size_t function_latency_;
    std::vector<std::vector<T>> expected_;
    std::vector<std::size_t> observed_count_;
    std::vector<std::size_t> accepted_count_;
    std::vector<bool> accepted_current_valid_;
    std::vector<std::vector<std::optional<T>>> hw_pipe_;
    ModuleLogger logger_;
    std::vector<std::size_t> interval_remaining_;

    void hw_pipe_sim_() {
        bool dequeued = false;
        bool enqueued = false;

        for (std::size_t port = 0; port < hw_pipe_.size(); ++port) {
            auto& pipe = hw_pipe_[port];
            if (!fus_valid[port].read()) {
                accepted_current_valid_[port] = false;
            }
            const bool do_enque = !accepted_current_valid_[port] && !pipe.front()
                                  && fus_valid[port].read() && tus_ready[port].read()
                                  && can_accept(port);

            if (pipe.back()) {
                const T value = *pipe.back();
                const std::size_t index = observed_count_[port]++;
                if (index < expected_[port].size()) {
                    const T& expected = expected_[port][index];
                    const bool match = value == expected;
                    logger_.log_sink_consumed(port, index, value, expected, match);
                    if (!match) {
                        report_mismatch(port, value, expected);
                    }
                } else {
                    logger_.log_sink_unexpected(port, index, value);
                    report_mismatch(port, value);
                }
                pipe.back().reset();
                dequeued = true;
            }

            for (std::size_t index = function_latency_ - 1; index > 0; --index) {
                if (pipe.back() && index == function_latency_ - 1) {
                    continue;
                }
                if (!pipe[index] && pipe[index - 1]) {
                    pipe[index] = std::move(pipe[index - 1]);
                    pipe[index - 1].reset();
                }
            }

            if (do_enque) {
                pipe.front() = in_data[port].read();
                ++accepted_count_[port];
                accepted_current_valid_[port] = true;
                interval_remaining_[port] = interval_;
                enqueued = true;
            } else if (interval_remaining_[port] > 0) {
                --interval_remaining_[port];
            }

            tus_ready[port].write(accepted_current_valid_[port]
                                  || (can_accept(port) && !pipe.front()
                                      && interval_remaining_[port] == 0));
        }

        logger_.log_pipeline(hw_pipe_, dequeued, enqueued);
    }

    bool can_accept(std::size_t port) const {
        return accepted_count_[port] < expected_[port].size();
    }

    void report_mismatch(std::size_t port, const T& actual, const T& expected) const {
        std::ostringstream message;
        message << "port " << port << " expected " << expected << " but received " << actual;
        SC_REPORT_ERROR(name(), message.str().c_str());
    }

    void report_mismatch(std::size_t port, const T& actual) const {
        std::ostringstream message;
        message << "port " << port << " received unexpected extra value " << actual;
        SC_REPORT_ERROR(name(), message.str().c_str());
    }

};
