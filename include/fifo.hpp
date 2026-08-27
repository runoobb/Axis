#pragma once

#ifndef SC_INCLUDE_DYNAMIC_PROCESSES
#define SC_INCLUDE_DYNAMIC_PROCESSES
#endif

#include "base_hw.hpp"

#include <deque>
#include <memory>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

template <typename T>
class FIFO : public BaseHW {
public:
    tlm_utils::simple_target_socket<FIFO> in;
    sc_core::sc_vector<tlm_utils::simple_initiator_socket<FIFO>> out;

    SC_HAS_PROCESS(FIFO);

    FIFO(sc_core::sc_module_name name,
         std::size_t function_latency,
         sc_core::sc_time clock_period,
         std::vector<std::size_t> transfer_latency)
        : BaseHW(name,
                 1,
                 transfer_latency.size(),
                 1,
                 checked_latency(function_latency),
                 clock_period,
                 transfer_latency),
          in("in"),
          out("out") {
        if (transfer_latency_.empty()) {
            throw std::invalid_argument("FIFO requires at least one output");
        }

        out.init(output_count_);
        in.register_b_transport(this, &FIFO::input_b_transport);
        SC_THREAD(function_enque_thread);
        SC_THREAD(transfer_thread);
    }

    bool can_enque() const override {
        return !in_latch_.empty();
    }

    bool can_deque() const override {
        return out_latch_.empty();
    }

    bool can_accept(std::size_t port_id) const override {
        return port_id == 0 && in_latch_.empty();
    }

    void function_enque_thread() override {
        while (true) {
            while (!can_enque()) {
                wait(in_latch_produced_ev_);
            }

            T value = in_latch_.front();
            in_latch_.pop_front();
            sc_core::sc_spawn(sc_core::sc_bind(
                static_cast<void (FIFO::*)(T)>(&FIFO::function_deque_thread),
                this,
                value));
            in_latch_consumed_ev_.notify(sc_core::SC_ZERO_TIME);
            wait(cycles_to_time(function_interval_));
        }
    }

    void function_deque_thread() override {}

    void function_deque_thread(T value) {
        wait(cycles_to_time(function_latency_));
        while (!can_deque()) {
            wait(out_latch_consumed_ev_);
        }

        out_latch_.push_back(value);
        out_latch_produced_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    void transfer_thread() override {
        while (true) {
            while (out_latch_.empty()) {
                wait(out_latch_produced_ev_);
            }

            T value = out_latch_.front();
            broadcast(value);
            out_latch_.pop_front();
            out_latch_consumed_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }

private:
    static std::size_t checked_latency(std::size_t cycles) {
        if (cycles == 0) {
            throw std::invalid_argument(
                "FIFO function latency cycles must be greater than zero");
        }

        return cycles;
    }

    std::deque<T> in_latch_;
    std::deque<T> out_latch_;
    sc_core::sc_event in_latch_produced_ev_;
    sc_core::sc_event in_latch_consumed_ev_;
    sc_core::sc_event out_latch_produced_ev_;
    sc_core::sc_event out_latch_consumed_ev_;

    void input_b_transport(tlm::tlm_generic_payload& trans,
                           sc_core::sc_time& delay) {
        if (trans.get_command() != tlm::TLM_WRITE_COMMAND) {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        if (trans.get_data_length() != sizeof(T) || !trans.get_data_ptr()) {
            trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }

        T value = BaseHW::read_value<T>(trans);
        wait(delay);
        delay = sc_core::SC_ZERO_TIME;

        while (!can_accept(0)) {
            wait(in_latch_consumed_ev_);
        }

        in_latch_.push_back(value);
        in_latch_produced_ev_.notify(sc_core::SC_ZERO_TIME);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void broadcast(T value) {
        std::vector<std::unique_ptr<sc_core::sc_event>> completed;
        sc_core::sc_event_and_list all_done;

        for (std::size_t i = 0; i < out.size(); ++i) {
            completed.push_back(std::make_unique<sc_core::sc_event>());
            all_done &= *completed.back();
            auto* done = completed.back().get();

            sc_core::sc_spawn([this, value, i, done] {
                unsigned char data[sizeof(T)];
                std::memcpy(data, &value, sizeof(T));

                tlm::tlm_generic_payload trans;
                BaseHW::prepare_write<T>(trans, data);
                sc_core::sc_time delay = cycles_to_time(transfer_latency_[i]);
                out[i]->b_transport(trans, delay);

                if (trans.is_response_error()) {
                    SC_REPORT_ERROR(name(), trans.get_response_string().c_str());
                }

                done->notify(sc_core::SC_ZERO_TIME);
            });
        }

        wait(all_done);
    }
};
