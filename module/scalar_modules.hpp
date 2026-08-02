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
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sc_core;
using namespace tlm;

template <typename T>
std::string trace_deque(const std::deque<T>& q)
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

template <typename T, typename Op = std::plus<T>>
class BinaryScalarOp : public sc_module {
public:
    tlm_utils::simple_target_socket<BinaryScalarOp> in0;
    tlm_utils::simple_target_socket<BinaryScalarOp> in1;
    tlm_utils::simple_initiator_socket<BinaryScalarOp> out;

    SC_HAS_PROCESS(BinaryScalarOp);

    BinaryScalarOp(
        sc_module_name name,
        sc_time interval,
        sc_time compute_latency,
        std::size_t in0_capacity,
        std::size_t in1_capacity,
        std::size_t out_capacity,
        sc_time transfer_latency,
        Op op = Op{})
        : sc_module(name),
          in0("in0"),
          in1("in1"),
          out("out"),
          interval_(interval),
          compute_latency_(compute_latency),
          transfer_latency_(transfer_latency),
          in_capacity_{in0_capacity, in1_capacity},
          out_capacity_(out_capacity),
          op_(op),
          trace_(std::string(this->name()) + ".txt")
    {
        if (in0_capacity == 0 || in1_capacity == 0 || out_capacity == 0) {
            throw std::invalid_argument("FIFO capacity must be greater than zero");
        }

        in0.register_b_transport(this, &BinaryScalarOp::input0_b_transport);
        in1.register_b_transport(this, &BinaryScalarOp::input1_b_transport);

        log_state("CREATE BinaryScalarOp");
        SC_THREAD(function_thread);
        SC_THREAD(transfer_thread);
    }

private:
    sc_time interval_;
    sc_time compute_latency_;
    sc_time transfer_latency_;
    std::size_t in_capacity_[2];
    std::size_t out_capacity_;
    Op op_;
    std::ofstream trace_;

    std::deque<T> in_q_[2];
    std::deque<T> out_q_;

    std::size_t input_reserved_[2] = {0, 0};
    std::size_t output_reserved_ = 0;

    sc_event input_space_ev_[2];
    sc_event input_data_ev_[2];
    sc_event output_space_ev_;
    sc_event output_data_ev_;

    void log_state(const std::string& event)
    {
        trace_ << sc_time_stamp() << " | " << event
               << " | in_q0=" << trace_deque(in_q_[0])
               << " in_q1=" << trace_deque(in_q_[1])
               << " out_q=" << trace_deque(out_q_)
               << " input_reserved0=" << input_reserved_[0]
               << " input_reserved1=" << input_reserved_[1]
               << " output_reserved=" << output_reserved_
               << std::endl;
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
            oss << "INPUT_REQUEST in" << port_id << " value=" << value << " arrival_delay=" << delay;
            log_state(oss.str());
        }

        while (in_q_[port_id].size() + input_reserved_[port_id] >= in_capacity_[port_id]) {
            std::ostringstream oss;
            oss << "WAIT_START input_space in" << port_id << " value=" << value;
            log_state(oss.str());
            wait(input_space_ev_[port_id]);
            oss.str("");
            oss << "WAIT_END input_space in" << port_id << " value=" << value;
            log_state(oss.str());
        }

        input_reserved_[port_id]++;
        log_state("INPUT_RESERVED");

        sc_time arrival_delay = delay;
        delay = SC_ZERO_TIME;

        sc_spawn([this, port_id, value, arrival_delay]() {
            {
                std::ostringstream oss;
                oss << "WAIT_START input_arrival_latency in" << port_id << " value=" << value << " delay=" << arrival_delay;
                log_state(oss.str());
            }
            wait(arrival_delay);
            {
                std::ostringstream oss;
                oss << "WAIT_END input_arrival_latency in" << port_id << " value=" << value;
                log_state(oss.str());
            }

            input_reserved_[port_id]--;
            in_q_[port_id].push_back(value);
            std::cout << sc_time_stamp() << " " << name() << ".in" << port_id
                      << " accepted " << value << std::endl;

            {
                std::ostringstream oss;
                oss << "INPUT_ARRIVED in" << port_id << " value=" << value;
                log_state(oss.str());
            }
            input_data_ev_[port_id].notify(SC_ZERO_TIME);
        });

        trans.set_response_status(TLM_OK_RESPONSE);
    }

    void function_thread()
    {
        while (true) {
            log_state("WAIT_START function_interval");
            wait(interval_);
            log_state("WAIT_END function_interval");

            while (!can_trigger_function()) {
                log_state("WAIT_START function_backpressure");
                wait(input_data_ev_[0] | input_data_ev_[1] | output_space_ev_);
                log_state("WAIT_END function_backpressure");
            }

            T a = in_q_[0].front();
            T b = in_q_[1].front();
            in_q_[0].pop_front();
            in_q_[1].pop_front();
            input_space_ev_[0].notify(SC_ZERO_TIME);
            input_space_ev_[1].notify(SC_ZERO_TIME);

            output_reserved_++;
            std::cout << sc_time_stamp() << " " << name()
                      << " start compute " << a << ", " << b << std::endl;

            {
                std::ostringstream oss;
                oss << "FUNCTION_START consume_in0=" << a << " consume_in1=" << b;
                log_state(oss.str());
            }

            sc_spawn([this, a, b]() {
                {
                    std::ostringstream oss;
                    oss << "WAIT_START compute_latency inputs=(" << a << ", " << b << ") delay=" << compute_latency_;
                    log_state(oss.str());
                }
                wait(compute_latency_);
                {
                    std::ostringstream oss;
                    oss << "WAIT_END compute_latency inputs=(" << a << ", " << b << ")";
                    log_state(oss.str());
                }

                T result = op_(a, b);
                output_reserved_--;
                out_q_.push_back(result);

                std::cout << sc_time_stamp() << " " << name()
                          << " finish compute result " << result << std::endl;

                {
                    std::ostringstream oss;
                    oss << "FUNCTION_FINISH produce_out=" << result;
                    log_state(oss.str());
                }
                output_data_ev_.notify(SC_ZERO_TIME);
            });
        }
    }

    void transfer_thread()
    {
        while (true) {
            // wait(SC_ZERO_LATENCY); // output come and transfer at once
            // wait(transfer_interval_); // for future extension
            while (out_q_.empty()) {
                log_state("WAIT_START output_data");
                wait(output_data_ev_);
                log_state("WAIT_END output_data");
            }

            T value = out_q_.front();
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
                oss << "TRANSFER_START value=" << value << " transfer_delay=" << delay;
                log_state(oss.str());
            }
            out->b_transport(trans, delay);
            {
                std::ostringstream oss;
                oss << "TRANSFER_END value=" << value;
                log_state(oss.str());
            }

            if (trans.is_response_error()) {
                SC_REPORT_ERROR(name(), trans.get_response_string().c_str());
            }

            out_q_.pop_front();
            std::cout << sc_time_stamp() << " " << name()
                      << " output consumed " << value << std::endl;
            {
                std::ostringstream oss;
                oss << "OUTPUT_CONSUMED value=" << value;
                log_state(oss.str());
            }
            output_space_ev_.notify(SC_ZERO_TIME);
        }
    }

    bool can_trigger_function() const
    {
        return !in_q_[0].empty()
            && !in_q_[1].empty()
            && out_q_.size() + output_reserved_ < out_capacity_;
    }
};

class ScalarSource : public sc_module {
public:
    tlm_utils::simple_initiator_socket<ScalarSource> out;

    SC_HAS_PROCESS(ScalarSource);

    ScalarSource(sc_module_name name, std::vector<int> values, sc_time interval, sc_time transfer_latency)
        : sc_module(name), out("out"), values_(std::move(values)), interval_(interval), transfer_latency_(transfer_latency), trace_(std::string(this->name()) + ".txt")
    {
        log_state("CREATE ScalarSource");
        SC_THREAD(src_thread);
    }

private:
    std::vector<int> values_;
    sc_time interval_;
    sc_time transfer_latency_;
    std::ofstream trace_;

    void log_state(const std::string& event)
    {
        trace_ << sc_time_stamp() << " | " << event << std::endl;
    }

    void src_thread()
    {
        for (int value : values_) {
            {
                std::ostringstream oss;
                oss << "WAIT_START source_interval next_value=" << value << " delay=" << interval_;
                log_state(oss.str());
            }
            wait(interval_);
            {
                std::ostringstream oss;
                oss << "WAIT_END source_interval next_value=" << value;
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
            std::cout << sc_time_stamp() << " " << name() << " transfer " << value << std::endl;
            {
                std::ostringstream oss;
                oss << "OUTPUT_TRANSFER_START value=" << value << " transfer_delay=" << delay;
                log_state(oss.str());
            }
            out->b_transport(trans, delay);
            {
                std::ostringstream oss;
                oss << "OUTPUT_TRANSFER_END value=" << value;
                log_state(oss.str());
            }

            if (trans.is_response_error()) {
                SC_REPORT_ERROR(name(), trans.get_response_string().c_str());
            }
        }
    }
};

class ScalarSink : public sc_module {
public:
    tlm_utils::simple_target_socket<ScalarSink> in;

    SC_HAS_PROCESS(ScalarSink);

    ScalarSink(sc_module_name name, std::size_t capacity, sc_time sink_interval, std::vector<int> expected = {})
        : sc_module(name), in("in"), capacity_(capacity), sink_interval_(sink_interval), expected_(std::move(expected)), trace_(std::string(this->name()) + ".txt")
    {
        if (capacity == 0) {
            throw std::invalid_argument("sink FIFO capacity must be greater than zero");
        }

        in.register_b_transport(this, &ScalarSink::b_transport);
        log_state("CREATE ScalarSink");
        SC_THREAD(sink_thread);
    }

private:
    std::size_t capacity_;
    sc_time sink_interval_;
    std::deque<int> q_;
    std::vector<int> expected_;
    std::vector<int> sunk_;
    std::ofstream trace_;
    sc_event space_ev_;
    sc_event data_ev_;

    void log_state(const std::string& event)
    {
        trace_ << sc_time_stamp() << " | " << event
               << " | q=" << trace_deque(q_)
               << " sunk=";
        trace_ << "[";
        for (std::size_t i = 0; i < sunk_.size(); ++i) {
            if (i != 0) {
                trace_ << ", ";
            }
            trace_ << sunk_[i];
        }
        trace_ << "]" << std::endl;
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

        while (q_.size() >= capacity_) {
            std::ostringstream oss;
            oss << "WAIT_START sink_input_space value=" << value;
            log_state(oss.str());
            wait(space_ev_);
            oss.str("");
            oss << "WAIT_END sink_input_space value=" << value;
            log_state(oss.str());
        }

        {
            std::ostringstream oss;
            oss << "WAIT_START sink_transfer_latency value=" << value << " delay=" << delay;
            log_state(oss.str());
        }
        wait(delay);
        {
            std::ostringstream oss;
            oss << "WAIT_END sink_transfer_latency value=" << value;
            log_state(oss.str());
        }
        delay = SC_ZERO_TIME;

        q_.push_back(value);
        std::cout << sc_time_stamp() << " " << name() << " accepted " << value << std::endl;
        {
            std::ostringstream oss;
            oss << "INPUT_ACCEPTED value=" << value;
            log_state(oss.str());
        }
        data_ev_.notify(SC_ZERO_TIME);
        trans.set_response_status(TLM_OK_RESPONSE);
    }

    void sink_thread()
    {
        while (true) {
            while (q_.empty()) {
                log_state("WAIT_START sink_data");
                wait(data_ev_);
                log_state("WAIT_END sink_data");
            }

            {
                std::ostringstream oss;
                oss << "WAIT_START sunk_interval delay=" << sink_interval_;
                log_state(oss.str());
            }
            wait(sink_interval_);
            log_state("WAIT_END sunk_interval");

            int value = q_.front();
            q_.pop_front();
            sunk_.push_back(value);
            std::cout << sc_time_stamp() << " " << name() << " sunk " << value << std::endl;
            {
                std::ostringstream oss;
                oss << "OUTPUT_SUNK value=" << value;
                log_state(oss.str());
            }
            check_sunk_prefix();
            space_ev_.notify(SC_ZERO_TIME);
        }
    }

    void check_sunk_prefix()
    {
        if (expected_.empty()) {
            return;
        }

        std::size_t idx = sunk_.size() - 1;
        if (idx >= expected_.size() || sunk_[idx] != expected_[idx]) {
            std::ostringstream oss;
            oss << "expected [";
            for (std::size_t i = 0; i < expected_.size(); ++i) {
                if (i != 0) {
                    oss << ", ";
                }
                oss << expected_[i];
            }
            oss << "], got [";
            for (std::size_t i = 0; i < sunk_.size(); ++i) {
                if (i != 0) {
                    oss << ", ";
                }
                oss << sunk_[i];
            }
            oss << "]";
            SC_REPORT_ERROR(name(), oss.str().c_str());
            return;
        }

        if (sunk_.size() == expected_.size()) {
            std::cout << name() << " PASS" << std::endl;
            log_state("PASS");
        }
    }
};
