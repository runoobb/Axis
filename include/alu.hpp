#pragma once

#ifndef SC_INCLUDE_DYNAMIC_PROCESSES
#define SC_INCLUDE_DYNAMIC_PROCESSES
#endif

#include "base_hw.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace axis_detail {

template <typename>
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
    static_assert(!std::is_same_v<Op, Op>,
                  "ALU requires a non-overloaded, non-generic callable "
                  "with detectable arity");
};

template <typename Op>
struct callable_arity<
    Op,
    std::void_t<decltype(&std::remove_reference_t<Op>::operator())>>
    : function_traits<decltype(&std::remove_reference_t<Op>::operator())> {};

template <typename R, typename... Args>
struct callable_arity<R (*)(Args...), void>
    : function_traits<R (*)(Args...)> {};

template <typename Op>
inline constexpr std::size_t callable_arity_v = callable_arity<Op>::arity;

}  // namespace axis_detail

template <typename T, typename Op = std::plus<T>>
class ALU : public BaseHW {
public:
    sc_core::sc_vector<tlm_utils::simple_target_socket_tagged<ALU>> in;
    sc_core::sc_vector<tlm_utils::simple_initiator_socket<ALU>> out;

    SC_HAS_PROCESS(ALU);

    ALU(sc_core::sc_module_name name,
        std::size_t function_interval,
        std::size_t function_latency,
        sc_core::sc_time clock_period,
        std::vector<std::size_t> transfer_latency,
        Op op = Op{})
        : BaseHW(name,
                 axis_detail::callable_arity_v<Op>,
                 transfer_latency.size(),
                 function_interval,
                 checked_latency(function_latency),
                 clock_period,
                 transfer_latency),
          in("in"),
          out("out"),
          op_(std::move(op)),
          in_latch_(input_count_) {
        if (transfer_latency_.empty()) {
            throw std::invalid_argument("ALU requires at least one output");
        }

        in.init(input_count_);
        out.init(output_count_);
        for (std::size_t i = 0; i < input_count_; ++i) {
            in[i].register_b_transport(
                this, &ALU::input_b_transport, static_cast<int>(i));
            in_latch_produced_ev_.push_back(
                std::make_unique<sc_core::sc_event>());
            in_latch_consumed_ev_.push_back(
                std::make_unique<sc_core::sc_event>());
        }

        SC_THREAD(function_enque_thread);
        SC_THREAD(transfer_thread);
    }

    bool can_enque() const override {
        for (const auto& latch : in_latch_) {
            if (latch.empty()) {
                return false;
            }
        }

        return true;
    }

    bool can_deque() const override {
        return out_latch_.empty();
    }

    bool can_accept(std::size_t port_id) const override {
        return port_id < in_latch_.size() && in_latch_[port_id].empty();
    }

    void function_enque_thread() override {
        while (true) {
            wait(cycles_to_time(function_interval_));
            while (!can_enque()) {
                sc_core::sc_event_or_list produced;
                for (const auto& event : in_latch_produced_ev_) {
                    produced |= *event;
                }
                wait(produced);
            }

            std::vector<T> values;
            values.reserve(in_latch_.size());
            for (std::size_t i = 0; i < in_latch_.size(); ++i) {
                values.push_back(in_latch_[i].front());
                in_latch_[i].pop_front();
                in_latch_consumed_ev_[i]->notify(sc_core::SC_ZERO_TIME);
            }

            T result = invoke(
                values,
                std::make_index_sequence<axis_detail::callable_arity_v<Op>>{});
            sc_core::sc_spawn(sc_core::sc_bind(
                static_cast<void (ALU::*)(T)>(&ALU::function_deque_thread),
                this,
                result));
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
                "ALU function latency cycles must be greater than zero");
        }

        return cycles + 1;
    }

    Op op_;
    std::vector<std::deque<T>> in_latch_;
    std::deque<T> out_latch_;
    std::vector<std::unique_ptr<sc_core::sc_event>> in_latch_produced_ev_;
    std::vector<std::unique_ptr<sc_core::sc_event>> in_latch_consumed_ev_;
    sc_core::sc_event out_latch_produced_ev_;
    sc_core::sc_event out_latch_consumed_ev_;

    void input_b_transport(int id,
                           tlm::tlm_generic_payload& trans,
                           sc_core::sc_time& delay) {
        if (id < 0 || static_cast<std::size_t>(id) >= in_latch_.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        if (trans.get_command() != tlm::TLM_WRITE_COMMAND) {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        if (trans.get_data_length() != sizeof(T) || !trans.get_data_ptr()) {
            trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }

        const T value = BaseHW::read_value<T>(trans);
        wait(delay);
        delay = sc_core::SC_ZERO_TIME;

        while (!can_accept(static_cast<std::size_t>(id))) {
            wait(*in_latch_consumed_ev_[id]);
        }

        in_latch_[id].push_back(value);
        in_latch_produced_ev_[id]->notify(sc_core::SC_ZERO_TIME);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    template <std::size_t... I>
    T invoke(const std::vector<T>& values, std::index_sequence<I...>) {
        return static_cast<T>(op_(values[I]...));
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
