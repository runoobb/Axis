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
               sc_core::sc_time,
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
          hw_pipe_(port_count, std::vector<std::optional<T>>(function_latency_)),
          interval_remaining_(port_count, 0) {
        assert(function_latency_ > 0);
        if (expected_.size() != port_count) {
            throw std::invalid_argument("expected sequence count must match port_count");
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
    }

    bool complete() const {
        for (std::size_t port = 0; port < expected_.size(); ++port) {
            if (!complete(port)) {
                return false;
            }
        }
        return true;
    }

    bool complete(std::size_t port) const {
        return port < expected_.size() && observed_count_[port] == expected_[port].size();
    }

private:
    std::size_t interval_;
    std::size_t function_latency_;
    std::vector<std::vector<T>> expected_;
    std::vector<std::size_t> observed_count_;
    std::vector<std::vector<std::optional<T>>> hw_pipe_;
    ModuleLogger logger_;
    std::size_t log_cycle_{0};
    std::vector<std::size_t> interval_remaining_;

    void hw_pipe_sim_() {
        bool dequeued = false;
        bool enqueued = false;

        for (std::size_t port = 0; port < hw_pipe_.size(); ++port) {
            auto& pipe = hw_pipe_[port];
            const bool do_deque = pipe.back() && !complete(port);
            const bool do_enque = !pipe.front() && fus_valid[port].read()
                                  && tus_ready[port].read() && !complete(port);

            if (do_deque) {
                consume(port, *pipe.back());
                pipe.back().reset();
                dequeued = true;
            }

            for (std::size_t index = function_latency_ - 1; index > 0; --index) {
                if (!pipe[index] && pipe[index - 1]) {
                    pipe[index] = std::move(pipe[index - 1]);
                    pipe[index - 1].reset();
                }
            }

            if (do_enque) {
                pipe.front() = in_data[port].read();
                interval_remaining_[port] = interval_;
                enqueued = true;
            } else if (interval_remaining_[port] > 0) {
                --interval_remaining_[port];
            }

            tus_ready[port].write(!complete(port) && !pipe.front()
                                  && interval_remaining_[port] == 0);
        }

        logger_.log_pipeline(sc_core::sc_time_stamp(), ++log_cycle_, hw_pipe_, dequeued, enqueued);
    }

    void report_mismatch(std::size_t port, const T& actual, const T& expected) const {
        std::ostringstream message;
        message << "port " << port << " expected " << expected << " but received " << actual;
        SC_REPORT_ERROR(name(), message.str().c_str());
    }

    void consume(std::size_t port, const T& observed) {
        const auto& expected = expected_[port][observed_count_[port]];
        if (observed != expected) {
            report_mismatch(port, observed, expected);
        }
        ++observed_count_[port];
    }
};
