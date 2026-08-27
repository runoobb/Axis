#include "base_hw.hpp"

#include <utility>

BaseHW::BaseHW(sc_core::sc_module_name name,
               std::size_t input_count,
               std::size_t output_count,
               std::size_t function_interval,
               std::size_t function_latency,
               sc_core::sc_time clock_period,
               std::vector<std::size_t> transfer_latency)
    : sc_core::sc_module(name),
      input_count_(input_count),
      output_count_(output_count),
      function_interval_(function_interval),
      function_latency_(function_latency),
      clock_period_(clock_period),
      transfer_latency_(std::move(transfer_latency)) {}

BaseHW::~BaseHW() = default;

sc_core::sc_time BaseHW::cycles_to_time(std::size_t cycles) const {
    return clock_period_ * static_cast<double>(cycles);
}
