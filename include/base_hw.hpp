#pragma once

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <systemc>
#include <tlm>
#include <vector>

class BaseHW : public sc_core::sc_module {
public:
    BaseHW(sc_core::sc_module_name name,
           std::size_t input_count,
           std::size_t output_count,
           std::size_t function_interval,
           std::size_t function_latency,
           sc_core::sc_time clock_period,
           std::vector<std::size_t> transfer_latency);
    ~BaseHW() override;

    virtual void function_enque_thread() = 0;
    virtual void function_deque_thread() = 0;
    virtual void transfer_thread() = 0;
    virtual bool can_enque() const = 0;
    virtual bool can_deque() const = 0;
    virtual bool can_accept(std::size_t port_id) const = 0;

    template <typename T>
    static T read_value(const tlm::tlm_generic_payload& trans) {
        T value{};
        std::memcpy(&value, trans.get_data_ptr(), sizeof(T));
        return value;
    }

    template <typename T>
    static void prepare_write(tlm::tlm_generic_payload& trans,
                              unsigned char* data) {
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(0);
        trans.set_data_ptr(data);
        trans.set_data_length(sizeof(T));
        trans.set_streaming_width(sizeof(T));
        trans.set_byte_enable_ptr(nullptr);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    }

    std::size_t input_count_;
    std::size_t output_count_;
    std::size_t function_interval_;
    std::size_t function_latency_;
    sc_core::sc_time clock_period_;
    std::vector<std::size_t> transfer_latency_;

protected:
    sc_core::sc_time cycles_to_time(std::size_t cycles) const;
};
