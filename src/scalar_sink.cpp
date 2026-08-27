#include "scalar_sink.hpp"

#include <stdexcept>
#include <utility>

ScalarSink::ScalarSink(sc_core::sc_module_name name,
                       std::size_t input_count,
                       std::size_t queue_capacity,
                       std::size_t sink_interval,
                       sc_core::sc_time clock_period,
                       std::vector<std::vector<int>> expected)
    : BaseHW(name,
             input_count,
             0,
             sink_interval,
             0,
             clock_period,
             {}),
      in("in"),
      queue_capacity_(queue_capacity),
      queues_(input_count),
      expected_(std::move(expected)),
      sunk_(input_count) {
    if (!input_count || !queue_capacity_) {
        throw std::invalid_argument(
            "ScalarSink input count and capacity must be greater than zero");
    }

    if (!expected_.empty() && expected_.size() != input_count) {
        throw std::invalid_argument(
            "expected sequences must match input count");
    }

    in.init(input_count_);
    for (std::size_t i = 0; i < input_count_; ++i) {
        produced_.push_back(std::make_unique<sc_core::sc_event>());
        consumed_.push_back(std::make_unique<sc_core::sc_event>());
        in[i].register_b_transport(
            this, &ScalarSink::input_b_transport, static_cast<int>(i));
        sc_core::sc_spawn(sc_core::sc_bind(&ScalarSink::consume_port, this, i));
    }
}

ScalarSink::ScalarSink(sc_core::sc_module_name name,
                       std::size_t queue_capacity,
                       std::size_t sink_interval,
                       sc_core::sc_time clock_period,
                       std::vector<int> expected)
    : ScalarSink(name,
                 1,
                 queue_capacity,
                 sink_interval,
                 clock_period,
                 expected.empty() ? std::vector<std::vector<int>>{}
                                  : std::vector<std::vector<int>>{
                                        std::move(expected)}) {}

bool ScalarSink::can_enque() const {
    for (const auto& queue : queues_) {
        if (queue.size() >= queue_capacity_) {
            return false;
        }
    }

    return true;
}

bool ScalarSink::can_deque() const {
    for (const auto& queue : queues_) {
        if (!queue.empty()) {
            return true;
        }
    }

    return false;
}

bool ScalarSink::can_accept(std::size_t port_id) const {
    return port_id < queues_.size()
           && queues_[port_id].size() < queue_capacity_;
}

void ScalarSink::function_enque_thread() {}

void ScalarSink::function_deque_thread() {}

void ScalarSink::transfer_thread() {}

bool ScalarSink::complete(std::size_t port_id) const {
    return port_id < sunk_.size() && !expected_.empty()
           && sunk_[port_id] == expected_[port_id];
}

const std::vector<int>& ScalarSink::sunk(std::size_t port_id) const {
    if (port_id >= sunk_.size()) {
        throw std::out_of_range("input port id");
    }

    return sunk_[port_id];
}

void ScalarSink::input_b_transport(int port_id,
                                   tlm::tlm_generic_payload& trans,
                                   sc_core::sc_time& delay) {
    if (port_id < 0 || static_cast<std::size_t>(port_id) >= queues_.size()) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    if (trans.get_command() != tlm::TLM_WRITE_COMMAND) {
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    if (trans.get_data_length() != sizeof(int) || !trans.get_data_ptr()) {
        trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return;
    }

    int value = BaseHW::read_value<int>(trans);
    wait(delay);
    delay = sc_core::SC_ZERO_TIME;

    while (!can_accept(static_cast<std::size_t>(port_id))) {
        wait(*consumed_[port_id]);
    }

    queues_[port_id].push_back(value);
    produced_[port_id]->notify(sc_core::SC_ZERO_TIME);
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

void ScalarSink::consume_port(std::size_t port_id) {
    while (true) {
        while (queues_[port_id].empty()) {
            wait(*produced_[port_id]);
        }

        wait(cycles_to_time(function_interval_));
        const int value = queues_[port_id].front();
        queues_[port_id].pop_front();
        sunk_[port_id].push_back(value);
        consumed_[port_id]->notify(sc_core::SC_ZERO_TIME);

        if (!expected_.empty()) {
            const auto count = sunk_[port_id].size();
            if (count > expected_[port_id].size()
                || value != expected_[port_id][count - 1]) {
                SC_REPORT_ERROR(name(), "received sequence differs from expected");
            }
        }
    }
}
