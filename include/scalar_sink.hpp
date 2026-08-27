#pragma once

#ifndef SC_INCLUDE_DYNAMIC_PROCESSES
#define SC_INCLUDE_DYNAMIC_PROCESSES
#endif

#include "base_hw.hpp"

#include <deque>
#include <memory>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

class ScalarSink : public BaseHW {
public:
    sc_core::sc_vector<tlm_utils::simple_target_socket_tagged<ScalarSink>> in;

    SC_HAS_PROCESS(ScalarSink);

    ScalarSink(sc_core::sc_module_name name,
               std::size_t input_count,
               std::size_t queue_capacity,
               std::size_t sink_interval,
               sc_core::sc_time clock_period,
               std::vector<std::vector<int>> expected = {});
    ScalarSink(sc_core::sc_module_name name,
               std::size_t queue_capacity,
               std::size_t sink_interval,
               sc_core::sc_time clock_period,
               std::vector<int> expected = {});

    bool can_enque() const override;
    bool can_deque() const override;
    bool can_accept(std::size_t port_id) const override;
    void function_enque_thread() override;
    void function_deque_thread() override;
    void transfer_thread() override;
    bool complete(std::size_t port_id = 0) const;
    const std::vector<int>& sunk(std::size_t port_id = 0) const;

private:
    std::size_t queue_capacity_;
    std::vector<std::deque<int>> queues_;
    std::vector<std::vector<int>> expected_;
    std::vector<std::vector<int>> sunk_;
    std::vector<std::unique_ptr<sc_core::sc_event>> produced_;
    std::vector<std::unique_ptr<sc_core::sc_event>> consumed_;

    void input_b_transport(int port_id,
                           tlm::tlm_generic_payload& trans,
                           sc_core::sc_time& delay);
    void consume_port(std::size_t port_id);
};
