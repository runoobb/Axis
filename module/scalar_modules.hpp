#pragma once

#ifndef SC_INCLUDE_DYNAMIC_PROCESSES
#define SC_INCLUDE_DYNAMIC_PROCESSES
#endif

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sc_core;
using namespace tlm;

template <typename Container>
std::string trace_deque(const Container& q)
{
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < q.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << q[i];
    }
    oss << "]";
    return oss.str();
}

inline std::string trace_time()
{
    std::ostringstream oss;
    oss << sc_time_stamp();
    return oss.str();
}

inline void trace_log_prefix(std::ofstream& trace, const std::string& event)
{
    trace << std::left << std::setw(12) << trace_time()
          << " | " << std::setw(56) << event << std::right;
}

template <typename T, typename Op = std::plus<T>>
class BinaryScalarOp : public sc_module {
public:
    tlm_utils::simple_target_socket<BinaryScalarOp> in0;
    tlm_utils::simple_target_socket<BinaryScalarOp> in1;
    tlm_utils::simple_initiator_socket<BinaryScalarOp> out;

    SC_HAS_PROCESS(BinaryScalarOp);

    BinaryScalarOp(
        sc_module_name name,
        sc_time function_interval,
        sc_time function_latency,
        std::size_t function_pipe_capacity,
        sc_time transfer_latency,
        Op op = Op{})
        : sc_module(name),
          in0("in0"),
          in1("in1"),
          out("out"),
          function_interval_(function_interval),
          function_latency_(function_latency),
          transfer_latency_(transfer_latency),
          function_pipe_capacity_(function_pipe_capacity),
          op_(op),
          trace_(std::string(this->name()) + ".txt")
    {
        if (function_pipe_capacity_ == 0) {
            throw std::invalid_argument("function pipeline capacity must be greater than zero");
        }

        in0.register_b_transport(this, &BinaryScalarOp::input0_b_transport);
        in1.register_b_transport(this, &BinaryScalarOp::input1_b_transport);

        log_state("CREATE BinaryScalarOp");
        SC_THREAD(function_thread);
        SC_THREAD(transfer_thread);
    }

private:
    struct PipeEntry {
        T result;
        sc_time ready_time;
    };

    sc_time function_interval_;
    sc_time function_latency_;
    sc_time transfer_latency_;
    std::size_t function_pipe_capacity_;
    Op op_;
    std::ofstream trace_;

    std::deque<T> in_latch_[2];
    std::deque<T> out_latch_;
    std::deque<PipeEntry> function_pipe_;

    sc_event out_latch_produced_ev_;
    sc_event out_latch_consumed_ev_;
    sc_event in_latch_produced_ev_[2];
    sc_event in_latch_consumed_ev_[2];

    std::string function_pipe_trace() const
    {
        std::ostringstream oss;
        oss << "[";
        for (std::size_t i = 0; i < function_pipe_.size(); ++i) {
            if (i != 0) {
                oss << ", ";
            }
            oss << "{" << function_pipe_[i].result
                << "@" << function_pipe_[i].ready_time << "}";
        }
        oss << "]";
        return oss.str();
    }

    void log_state(const std::string& event)
    {
        trace_log_prefix(trace_, event);
        trace_ << " | " << std::left
               << std::setw(24) << ("in_latch0=" + trace_deque(in_latch_[0]))
               << std::setw(24) << ("in_latch1=" + trace_deque(in_latch_[1]))
               << std::setw(36) << ("function_pipe=" + function_pipe_trace())
               << "out_latch=" << trace_deque(out_latch_)
               << std::right << std::endl;
    }

    void input0_b_transport(tlm_generic_payload& trans, sc_time& delay)
    {
        input_b_transport(0, trans, delay);
    }

    void input1_b_transport(tlm_generic_payload& trans, sc_time& delay)
    {
        input_b_transport(1, trans, delay);
    }

    void input_b_transport(int port_id, tlm_generic_payload& trans, sc_time& delay)
    {
        if (trans.get_command() != TLM_WRITE_COMMAND) {
            trans.set_response_status(TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        if (trans.get_data_length() != sizeof(T) || trans.get_data_ptr() == nullptr) {
            trans.set_response_status(TLM_BURST_ERROR_RESPONSE);
            return;
        }

        T value{};
        std::memcpy(&value, trans.get_data_ptr(), sizeof(T));
        {
            std::ostringstream oss;
            oss << "INPUT_REQUEST in" << port_id << " value=" << value << " delay=" << delay;
            log_state(oss.str());
        }

        wait(delay);
        delay = SC_ZERO_TIME;

        while (!can_accept(port_id)) {
            std::ostringstream oss;
            oss << "INPUT_WAIT in" << port_id << " value=" << value;
            log_state(oss.str());
            wait(in_latch_consumed_ev_[port_id]);
        }

        in_latch_[port_id].push_back(value);
        {
            std::ostringstream oss;
            oss << "INPUT_ACCEPTED in" << port_id << " value=" << value;
            log_state(oss.str());
        }
        in_latch_produced_ev_[port_id].notify(SC_ZERO_TIME);
        trans.set_response_status(TLM_OK_RESPONSE);
    }

    void function_thread()
    {
        while (true) {
            wait(function_interval_);

            if (!function_pipe_.empty() && function_pipe_.front().ready_time <= sc_time_stamp()) {
                const T result = function_pipe_.front().result;
                if (out_latch_.empty()) {
                    out_latch_.push_back(result);
                    function_pipe_.pop_front();
                    {
                        std::ostringstream oss;
                        oss << "FUNCTION_FINISH result=" << result;
                        log_state(oss.str());
                    }
                    out_latch_produced_ev_.notify(SC_ZERO_TIME);
                } else {
                    std::ostringstream oss;
                    oss << "FUNCTION_READY_BLOCKED result=" << result;
                    log_state(oss.str());
                }
            }

            if (can_trigger_function()) {
                T a = in_latch_[0].front();
                T b = in_latch_[1].front();
                in_latch_[0].pop_front();
                in_latch_[1].pop_front();
                in_latch_consumed_ev_[0].notify(SC_ZERO_TIME);
                in_latch_consumed_ev_[1].notify(SC_ZERO_TIME);

                T result = op_(a, b);
                function_pipe_.push_back({result, sc_time_stamp() + function_latency_});
                std::ostringstream oss;
                oss << "FUNCTION_START in0=" << a << " in1=" << b
                    << " result=" << result
                    << " ready_time=" << function_pipe_.back().ready_time;
                log_state(oss.str());
            }
        }
    }

    void transfer_thread()
    {
        while (true) {
            while (out_latch_.empty()) {
                wait(out_latch_produced_ev_);
            }

            T value = out_latch_.front();
            unsigned char data[sizeof(T)];
            std::memcpy(data, &value, sizeof(T));

            tlm_generic_payload trans;
            trans.set_command(TLM_WRITE_COMMAND);
            trans.set_address(0);
            trans.set_data_ptr(data);
            trans.set_data_length(sizeof(T));
            trans.set_streaming_width(sizeof(T));
            trans.set_byte_enable_ptr(nullptr);
            trans.set_dmi_allowed(false);
            trans.set_response_status(TLM_INCOMPLETE_RESPONSE);

            sc_time delay = transfer_latency_;
            {
                std::ostringstream oss;
                oss << "TRANSFER_START value=" << value << " delay=" << delay;
                log_state(oss.str());
            }
            out->b_transport(trans, delay);

            if (trans.is_response_error()) {
                SC_REPORT_ERROR(name(), trans.get_response_string().c_str());
            }

            out_latch_.pop_front();
            {
                std::ostringstream oss;
                oss << "TRANSFER_END value=" << value;
                log_state(oss.str());
            }
            {
                std::ostringstream oss;
                oss << "OUTPUT_CONSUMED value=" << value;
                log_state(oss.str());
            }
            out_latch_consumed_ev_.notify(SC_ZERO_TIME);
        }
    }

    bool can_trigger_function() const
    {
        return !in_latch_[0].empty()
            && !in_latch_[1].empty()
            && function_pipe_.size() < function_pipe_capacity_;
    }

    bool can_accept(int port_id) const
    {
        return in_latch_[port_id].empty();
    }
};

class ScalarSource : public sc_module {
public:
    tlm_utils::simple_initiator_socket<ScalarSource> out;

    SC_HAS_PROCESS(ScalarSource);

    ScalarSource(sc_module_name name, std::vector<int> values, sc_time interval, sc_time transfer_latency)
        : sc_module(name),
          out("out"),
          values_(std::move(values)),
          interval_(interval),
          transfer_latency_(transfer_latency),
          trace_(std::string(this->name()) + ".txt")
    {
        log_state("CREATE ScalarSource");
        SC_THREAD(src_thread);
    }

private:
    std::vector<int> values_;
    sc_time interval_;
    sc_time transfer_latency_;
    std::deque<int> output_latch_;
    std::ofstream trace_;
    sc_event output_latch_consumed_ev_;

    void log_state(const std::string& event)
    {
        trace_log_prefix(trace_, event);
        trace_ << " | output_latch=" << trace_deque(output_latch_) << std::endl;
    }

    void src_thread()
    {
        for (int value : values_) {
            wait(interval_);

            while (!output_latch_.empty()) {
                wait(output_latch_consumed_ev_);
            }

            output_latch_.push_back(value);
            {
                std::ostringstream oss;
                oss << "GENERATE value=" << value;
                log_state(oss.str());
            }

            unsigned char data[sizeof(int)];
            std::memcpy(data, &value, sizeof(int));

            tlm_generic_payload trans;
            trans.set_command(TLM_WRITE_COMMAND);
            trans.set_address(0);
            trans.set_data_ptr(data);
            trans.set_data_length(sizeof(int));
            trans.set_streaming_width(sizeof(int));
            trans.set_byte_enable_ptr(nullptr);
            trans.set_dmi_allowed(false);
            trans.set_response_status(TLM_INCOMPLETE_RESPONSE);

            sc_time delay = transfer_latency_;
            {
                std::ostringstream oss;
                oss << "TRANSFER_START value=" << value << " delay=" << delay;
                log_state(oss.str());
            }
            out->b_transport(trans, delay);

            if (trans.is_response_error()) {
                SC_REPORT_ERROR(name(), trans.get_response_string().c_str());
            }

            output_latch_.pop_front();
            {
                std::ostringstream oss;
                oss << "TRANSFER_END value=" << value;
                log_state(oss.str());
            }
            output_latch_consumed_ev_.notify(SC_ZERO_TIME);
        }
    }
};

class ScalarSink : public sc_module {
public:
    tlm_utils::simple_target_socket<ScalarSink> in;

    SC_HAS_PROCESS(ScalarSink);

    ScalarSink(sc_module_name name, std::size_t capacity, sc_time sink_interval, std::vector<int> expected = {})
        : sc_module(name),
          in("in"),
          sink_interval_(sink_interval),
          expected_(std::move(expected)),
          trace_(std::string(this->name()) + ".txt")
    {
        if (capacity == 0) {
            throw std::invalid_argument("sink latch capacity must be greater than zero");
        }

        in.register_b_transport(this, &ScalarSink::b_transport);
        log_state(capacity == 1 ? "CREATE ScalarSink" : "CREATE ScalarSink capacity_arg_ignored_max1");
        SC_THREAD(sink_thread);
    }

private:
    sc_time sink_interval_;
    std::deque<int> input_latch_;
    std::vector<int> expected_;
    std::vector<int> sunk_;
    std::ofstream trace_;
    sc_event input_latch_produced_ev_;
    sc_event input_latch_consumed_ev_;

    void log_state(const std::string& event)
    {
        trace_log_prefix(trace_, event);
        trace_ << " | " << std::left
               << std::setw(24) << ("input_latch=" + trace_deque(input_latch_))
               << "sunk=" << trace_deque(sunk_)
               << std::right << std::endl;
    }

    void b_transport(tlm_generic_payload& trans, sc_time& delay)
    {
        if (trans.get_command() != TLM_WRITE_COMMAND) {
            trans.set_response_status(TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        if (trans.get_data_length() != sizeof(int) || trans.get_data_ptr() == nullptr) {
            trans.set_response_status(TLM_BURST_ERROR_RESPONSE);
            return;
        }

        int value{};
        std::memcpy(&value, trans.get_data_ptr(), sizeof(int));
        {
            std::ostringstream oss;
            oss << "INPUT_REQUEST value=" << value << " delay=" << delay;
            log_state(oss.str());
        }

        while (input_latch_.size() == 1) {
            std::ostringstream oss;
            oss << "INPUT_WAIT value=" << value;
            log_state(oss.str());
            wait(input_latch_consumed_ev_);
        }

        wait(delay);
        delay = SC_ZERO_TIME;

        input_latch_.push_back(value);
        {
            std::ostringstream oss;
            oss << "INPUT_ACCEPTED value=" << value;
            log_state(oss.str());
        }
        input_latch_produced_ev_.notify(SC_ZERO_TIME);
        trans.set_response_status(TLM_OK_RESPONSE);
    }

    void sink_thread()
    {
        while (true) {
            while (input_latch_.empty()) {
                wait(input_latch_produced_ev_);
            }

            wait(sink_interval_);

            int value = input_latch_.front();
            input_latch_.pop_front();
            sunk_.push_back(value);
            {
                std::ostringstream oss;
                oss << "SINK_CONSUME value=" << value;
                log_state(oss.str());
            }
            input_latch_consumed_ev_.notify(SC_ZERO_TIME);
            check_sunk_prefix();
        }
    }

    void check_sunk_prefix()
    {
        if (expected_.empty()) {
            return;
        }

        const std::size_t idx = sunk_.size() - 1;
        if (idx >= expected_.size() || sunk_[idx] != expected_[idx]) {
            std::ostringstream oss;
            oss << "expected " << trace_deque(expected_) << ", got " << trace_deque(sunk_);
            SC_REPORT_ERROR(name(), oss.str().c_str());
            return;
        }

        if (sunk_.size() == expected_.size()) {
            log_state("PASS");
            std::cout << name() << " PASS" << std::endl;
        }
    }
};
