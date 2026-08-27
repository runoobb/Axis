#pragma once

#ifndef SC_INCLUDE_DYNAMIC_PROCESSES
#define SC_INCLUDE_DYNAMIC_PROCESSES
#endif

#include "base_hw.hpp"

#include <memory>
#include <tlm_utils/simple_initiator_socket.h>
#include <vector>

class ScalarSource : public BaseHW {
public:
    sc_core::sc_vector<tlm_utils::simple_initiator_socket<ScalarSource>> out;

    SC_HAS_PROCESS(ScalarSource);

    ScalarSource(sc_core::sc_module_name name,
                 std::vector<int> values,
                 std::size_t interval,
                 sc_core::sc_time clock_period,
                 std::vector<std::size_t> transfer_latency);

    bool can_enque() const override;
    bool can_deque() const override;
    bool can_accept(std::size_t port_id) const override;
    void function_enque_thread() override;
    void function_deque_thread() override;
    void transfer_thread() override;

private:
    std::vector<int> values_;
    bool active_ = false;

    void broadcast(int value);
    void run();
};
