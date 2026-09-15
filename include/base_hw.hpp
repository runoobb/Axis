#pragma once

#include <cstddef>
#include <systemc>
#include <vector>

class BaseHW : public sc_core::sc_module {
public:
    BaseHW(sc_core::sc_module_name name,
           std::size_t input_count,
           std::size_t output_count,
           std::vector<std::size_t> input_interval,
           std::size_t function_latency,
           sc_core::sc_time clock_period);
    ~BaseHW() override;

protected:
    sc_core::sc_time cycles_to_time(std::size_t cycles) const;

    std::size_t input_count_;
    std::size_t output_count_;
    std::vector<std::size_t> input_interval_;
    std::size_t function_latency_;
    sc_core::sc_time clock_period_;
};
