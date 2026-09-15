#include "base_hw.hpp"

#include <stdexcept>
#include <utility>

BaseHW::BaseHW(sc_core::sc_module_name name,
               std::size_t input_count,
               std::size_t output_count,
               std::vector<std::size_t> input_interval,
               std::size_t function_latency,
               sc_core::sc_time clock_period)
    : sc_core::sc_module(name),
      input_count_(input_count),
      output_count_(output_count),
      input_interval_(std::move(input_interval)),
      function_latency_(function_latency),
      clock_period_(clock_period) {
    if (input_interval_.size() != input_count_) {
        throw std::invalid_argument("input_interval size must match input_count");
    }
}

BaseHW::~BaseHW() = default;

sc_core::sc_time BaseHW::cycles_to_time(std::size_t cycles) const {
    return clock_period_ * static_cast<double>(cycles);
}
