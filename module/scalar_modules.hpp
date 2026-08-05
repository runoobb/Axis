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
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
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

template <typename>
struct dependent_false : std::false_type {};

template <typename T>
struct function_traits;

template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> {
    static constexpr std::size_t arity = sizeof...(Args);
};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...) const> {
    static constexpr std::size_t arity = sizeof...(Args);
};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...)> {
    static constexpr std::size_t arity = sizeof...(Args);
};

template <typename Op, typename = void>
struct callable_arity {
    static_assert(dependent_false<Op>::value,
        "ScalarOp requires a non-overloaded, non-generic callable with detectable arity");
};

template <typename Op>
struct callable_arity<Op, std::void_t<decltype(&std::remove_reference_t<Op>::operator())>>
    : function_traits<decltype(&std::remove_reference_t<Op>::operator())> {};

template <typename R, typename... Args>
struct callable_arity<R (*)(Args...), void> : function_traits<R (*)(Args...)> {};

template <typename Op>
inline constexpr std::size_t callable_arity_v = callable_arity<Op>::arity;

template <typename T, typename Op = std::plus<T>>
class ScalarOp : public sc_module {
public:
    sc_vector<tlm_utils::simple_target_socket_tagged<ScalarOp>> in;
    tlm_utils::simple_initiator_socket<ScalarOp> out;

    SC_HAS_PROCESS(ScalarOp);

    ScalarOp(
        sc_module_name name,
        std::size_t input_count,
        sc_time function_interval,
        sc_time function_latency,
        std::size_t function_pipe_capacity,
        sc_time transfer_latency,
        Op op = Op{})
        : sc_module(name),
          in("in"),
          out("out"),
          function_interval_(function_interval),
          function_latency_(function_latency),
          transfer_latency_(transfer_latency),
          function_pipe_capacity_(function_pipe_capacity),
          op_(op),
          trace_(std::string(this->name()) + ".txt"),
          in_latch_(input_count)
    {
        if (input_count == 0) {
            throw std::invalid_argument("input count must be greater than zero");
        }

        if (input_count != callable_arity_v<Op>) {
            std::ostringstream oss;
            oss << "input count " << input_count
                << " does not match callable arity " << callable_arity_v<Op>;
            throw std::invalid_argument(oss.str());
        }

        if (function_pipe_capacity_ == 0) {
            throw std::invalid_argument("function pipeline capacity must be greater than zero");
        }

        in.init(input_count);
        for (std::size_t i = 0; i < input_count; ++i) {
            in[i].register_b_transport(this, &ScalarOp::input_b_transport, static_cast<int>(i));
            in_latch_produced_ev_.push_back(std::make_unique<sc_event>());
            in_latch_consumed_ev_.push_back(std::make_unique<sc_event>());
        }

        {
            std::ostringstream oss;
            oss << "CREATE ScalarOp inputs=" << input_count;
            log_state(oss.str());
        }
        SC_THREAD(function_enque_thread);
        SC_THREAD(transfer_thread);
    }

private:

    sc_time function_interval_;
    sc_time function_latency_;
    sc_time transfer_latency_;
    std::size_t function_pipe_capacity_;
    std::size_t function_pipe_inflight_ = 0;
    Op op_;
    std::ofstream trace_;

    std::vector<std::deque<T>> in_latch_;
    std::deque<T> out_latch_;
    std::deque<T> function_pipe_;

    sc_event out_latch_produced_ev_; // outport valid signal
    sc_event out_latch_consumed_ev_; // marks when the out_latch_ has been sampled and function pipe can advance
    std::vector<std::unique_ptr<sc_event>> in_latch_produced_ev_; // inport valid signal
    std::vector<std::unique_ptr<sc_event>> in_latch_consumed_ev_; // inport ready signal

    std::string function_pipe_trace() const
    {
        std::ostringstream oss;
        oss << trace_deque(function_pipe_) << "/inflight=" << function_pipe_inflight_;
        return oss.str();
    }

    void log_state(const std::string& event)
    {
        trace_log_prefix(trace_, event);
        trace_ << " | " << std::left;
        for (std::size_t i = 0; i < in_latch_.size(); ++i) {
            std::ostringstream label;
            label << "in_latch" << i << "=" << trace_deque(in_latch_[i]);
            trace_ << std::setw(24) << label.str();
        }
        trace_ << std::setw(36) << ("function_pipe=" + function_pipe_trace())
               << "out_latch=" << trace_deque(out_latch_)
               << std::right << std::endl;
    }

    void input_b_transport(int port_id, tlm_generic_payload& trans, sc_time& delay)
    {
        if (port_id < 0 || static_cast<std::size_t>(port_id) >= in_latch_.size()) {
            trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

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

        while (!can_accept(static_cast<std::size_t>(port_id))) {
            std::ostringstream oss;
            oss << "INPUT_WAIT in" << port_id << " value=" << value;
            log_state(oss.str());
            wait(*in_latch_consumed_ev_[port_id]);
        }

        in_latch_[port_id].push_back(value);
        {
            std::ostringstream oss;
            oss << "INPUT_ACCEPTED in" << port_id << " value=" << value;
            log_state(oss.str());
        }
        in_latch_produced_ev_[port_id]->notify(SC_ZERO_TIME);
        trans.set_response_status(TLM_OK_RESPONSE);
    }

    void function_enque_thread()
    {
        while (true) {
            wait(function_interval_);

            while (!can_enque()) {
                sc_event_or_list wait_events;
                for (const auto& event : in_latch_produced_ev_) {
                    wait_events |= *event;
                }
                wait_events |= out_latch_consumed_ev_;
                wait(wait_events);
            }

            std::vector<T> inputs;
            inputs.reserve(in_latch_.size());
            for (std::size_t i = 0; i < in_latch_.size(); ++i) {
                inputs.push_back(in_latch_[i].front());
                in_latch_[i].pop_front();
                in_latch_consumed_ev_[i]->notify(SC_ZERO_TIME);
            }

            T result = invoke_op(inputs, std::make_index_sequence<callable_arity_v<Op>>{});
            function_pipe_.push_back(result);
            ++function_pipe_inflight_;
            {
                std::ostringstream oss;
                oss << "FUNCTION_START inputs=" << trace_deque(inputs)
                    << " result=" << result
                    << " latency=" << function_latency_;
                log_state(oss.str());
            }

            sc_spawn(sc_bind(&ScalarOp::function_deque_thread, this));
        }
    }

    template <std::size_t... I>
    T invoke_op(const std::vector<T>& inputs, std::index_sequence<I...>)
    {
        return static_cast<T>(op_(inputs[I]...));
    }

    void function_deque_thread()
    {
        wait(function_latency_);
        
        /*
        cases when backpressure occurs: adjacent interval function_deque_thread spawned by sc_spawn() is waked up 
        by same out_latch_consumed_ev_ event
        */ 
        while (!can_deque()) {
            {
                std::ostringstream oss;
                oss << "FUNCTION_READY_BLOCKED result=" << function_pipe_.front();
                log_state(oss.str());
            }
            wait(out_latch_consumed_ev_);
            /*
            If current function_deque_thread fail to acquire out_latch_, it need wait another function_interval_ to  
            simulate pipeline stall due to backpressure.
            Due to SystemC is not truly concurrent, out_latch_ do not need to be set to mutex.          
            */ 
            if(!can_deque()) wait(1); // set pipeline latency as 1ns
        }

        T result = function_pipe_.front();
        function_pipe_.pop_front();
        --function_pipe_inflight_;
        out_latch_.push_back(result);
        {
            std::ostringstream oss;
            oss << "FUNCTION_FINISH result=" << result;
            log_state(oss.str());
        }
        out_latch_produced_ev_.notify(SC_ZERO_TIME);
    }

    void transfer_thread()
    {
        while (true) {
            wait(out_latch_produced_ev_);

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

    bool can_enque() const
    {
        if (function_pipe_inflight_ >= function_pipe_capacity_) {
            return false;
        }

        for (const auto& latch : in_latch_) {
            if (latch.empty()) {
                return false;
            }
        }
        return true;
    }

    bool can_deque() const
    {
        return out_latch_.empty() && function_pipe_inflight_ > 0;
    }

    bool can_accept(std::size_t port_id) const
    {
        return in_latch_[port_id].empty();
    }
};

template <typename T, typename Op = std::plus<T>>
using BinaryScalarOp = ScalarOp<T, Op>;

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
