#pragma once

#include "base_hw.hpp"
#include "module_logger.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <systemc>
#include <utility>
#include <vector>

template <typename AddrT, typename DataT>
struct SRAMReadStage {
    AddrT addr{};
    DataT data{};
};

template <typename AddrT, typename DataT>
struct SRAMWriteStage {
    AddrT addr{};
    DataT data{};
};

template <typename AddrT, typename DataT>
std::ostream& operator<<(std::ostream& out, const SRAMReadStage<AddrT, DataT>& stage) {
    return out << "addr=" << stage.addr << ":data=" << stage.data;
}

template <typename AddrT, typename DataT>
std::ostream& operator<<(std::ostream& out, const SRAMWriteStage<AddrT, DataT>& stage) {
    return out << "addr=" << stage.addr << ":data=" << stage.data;
}

// 1R1W SRAM.
//
// Two independent function channels, each owning its own pipeline register and its own
// valid-ready handshake, so the two channels never stall one another:
//   - read  channel: one input port (addr), one output port (data)  -> fifo-like
//   - write channel: two input ports (addr, data), no output port   -> 2-input alu + sink-like
//
// input_interval_ layout: [0] read addr, [1] write addr, [2] write data.
template <typename AddrT = std::size_t, typename DataT = int>
class SRAM : public BaseHW {
public:
    sc_core::sc_in<bool> clk;

    sc_core::sc_in<AddrT> read_addr;
    sc_core::sc_in<bool> read_fus_valid;
    sc_core::sc_out<bool> read_tus_ready;
    sc_core::sc_out<DataT> read_data;
    sc_core::sc_vector<sc_core::sc_in<bool>> read_fds_ready;
    sc_core::sc_out<bool> read_tds_valid;

    sc_core::sc_in<AddrT> write_addr;
    sc_core::sc_in<DataT> write_data;
    sc_core::sc_vector<sc_core::sc_in<bool>> write_fus_valid;
    // The write channel exposes a single ready line shared by both upstream ports: addr and
    // data handshake jointly, so neither upstream can retire its payload ahead of the other.
    sc_core::sc_out<bool> write_tus_ready;

    SC_HAS_PROCESS(SRAM);

    SRAM(sc_core::sc_module_name name,
         std::size_t depth,
         std::vector<std::size_t> input_interval,
         std::size_t function_latency,
         sc_core::sc_time clock_period,
         std::size_t read_output_count,
         DataT initial_value = DataT{},
         ModuleLogOptions read_log_options = {},
         ModuleLogOptions write_log_options = {})
        : BaseHW(name, 3, read_output_count, std::move(input_interval), function_latency,
                 clock_period),
          clk("clk"),
          read_addr("read_addr"),
          read_fus_valid("read_fus_valid"),
          read_tus_ready("read_tus_ready"),
          read_data("read_data"),
          read_fds_ready("read_fds_ready", output_count_),
          read_tds_valid("read_tds_valid"),
          write_addr("write_addr"),
          write_data("write_data"),
          write_fus_valid("write_fus_valid", 2),
          write_tus_ready("write_tus_ready"),
          memory_(depth, initial_value),
          read_pipe_(function_latency_),
          write_pipe_(function_latency_),
          write_cooldown_remaining_(2, 0) {
        if (depth == 0) {
            throw std::invalid_argument("SRAM requires positive depth");
        }
        if (output_count_ == 0) {
            throw std::invalid_argument("SRAM requires at least one read output");
        }
        if (input_interval_.size() != input_count_) {
            throw std::invalid_argument("SRAM requires three input intervals");
        }
        // A 1R1W SRAM is a single-cycle memory whose ports accept back-to-back requests.
        assert(function_latency_ == 1);
        assert(input_interval_[0] == 0);
        assert(input_interval_[1] == 0);
        assert(input_interval_[2] == 0);

        read_logger_.configure((std::string(this->name()) + "_read").c_str(), read_log_options,
                               clock_period_);
        write_logger_.configure((std::string(this->name()) + "_write").c_str(), write_log_options,
                                clock_period_);

        SC_METHOD(read_hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();

        SC_METHOD(write_hw_pipe_sim_);
        sensitive << clk.pos();
        dont_initialize();

        SC_METHOD(read_hw_transfer_sim_);
        sensitive << read_tds_valid;
        for (auto& ready : read_fds_ready) {
            sensitive << ready;
        }
        dont_initialize();

        SC_METHOD(write_hw_transfer_sim_);
        for (auto& valid : write_fus_valid) {
            sensitive << valid;
        }
        dont_initialize();
    }

    void end_of_elaboration() override {
        read_tus_ready.write(true);
        read_tds_valid.write(false);
        // Ready may only rise once both write ports have been observed valid together,
        // otherwise an early handshake would drop a payload that has nowhere to be held.
        write_tus_ready.write(false);
    }

private:
    using ReadStage = SRAMReadStage<AddrT, DataT>;
    using WriteStage = SRAMWriteStage<AddrT, DataT>;

    std::vector<DataT> memory_;
    std::vector<std::optional<ReadStage>> read_pipe_;
    std::vector<std::optional<WriteStage>> write_pipe_;
    // Kept for structural parity with fifo.hpp / scalar_sink.hpp. Every SRAM interval is
    // asserted to be 0, so the decrement branch is never actually taken.
    std::size_t read_cooldown_remaining_{0};
    std::vector<std::size_t> write_cooldown_remaining_;
    // Pipeline-side half of the write_tus_ready condition, owned by write_hw_pipe_sim_ and
    // consumed by the delta-cycle re-evaluation below.

    ModuleLogger read_logger_;
    ModuleLogger write_logger_;

    void read_hw_pipe_sim_() {
        const bool do_deque = read_tds_valid.read() && all_read_downstream_ready_();

        if (do_deque) {
            read_pipe_.back().reset();
        }

        const bool do_enque = read_tus_ready.read() && read_fus_valid.read();

        if (do_enque) {
            const AddrT addr = read_addr.read();
            read_pipe_.front() = ReadStage{addr, memory_[checked_index(addr)]};
            read_cooldown_remaining_ = input_interval_[0];
        } else if (read_cooldown_remaining_ > 0) {
            --read_cooldown_remaining_;
        }

        if(read_pipe_.back().has_value()) {
            read_data.write(read_pipe_.back()->data);
        }
        
        read_tds_valid.write(read_pipe_.back().has_value());

        read_logger_.log_pipeline(read_pipe_, do_deque, do_enque);
    }

    void write_hw_pipe_sim_() {
        // The write channel has no output port: its stage always retires into the memory
        // array, so the pipeline register frees itself every cycle.
        const bool do_deque = write_pipe_.back().has_value();

        if (do_deque) {
            const auto stage = *write_pipe_.back();
            memory_[checked_index(stage.addr)] = stage.data;
            write_pipe_.back().reset();
        }

        const bool do_enque = write_tus_ready.read() && std::all_of(write_fus_valid.begin(), write_fus_valid.end(), [](const auto& valid) {
            return valid.read();
        });

        if (do_enque) {
            write_pipe_.front() = WriteStage{write_addr.read(), write_data.read()};
        }

        for (std::size_t port = 0; port < write_cooldown_remaining_.size(); ++port) {
            if (do_enque) {
                write_cooldown_remaining_[port] = input_interval_[port + 1];
            } else if (write_cooldown_remaining_[port] > 0) {
                --write_cooldown_remaining_[port];
            }
        }

        write_logger_.log_pipeline(write_pipe_, do_deque, do_enque);
    }



    // Why need this read_hw_transfer_sim_() function?
    // Because for sc_signal, at end of current simulation time, ensure that they are up to date at current cycle
    // For the joint write handshake, write_tus_ready is combinational logic to write_fus_valid,
    // affected by upstream modules.
    // So it need to be decoupled from write_hw_pipe_sim_(), re-evaluate at delta cycle

    void read_hw_transfer_sim_() {
        const bool read_pipe_not_full =
            std::any_of(read_pipe_.begin(), read_pipe_.end(), [](const auto& stage) {
                return !stage.has_value();
            }); 

        read_tus_ready.write( ( (read_tds_valid.read() && all_read_downstream_ready_()) || read_pipe_not_full) && read_cooldown_remaining_ == 0);
    }

    void write_hw_transfer_sim_() {
        const bool write_pipe_can_accept =
            std::any_of(write_pipe_.begin(), write_pipe_.end(), [](const auto& stage) {
                return !stage.has_value();
            });
        write_tus_ready.write(write_pipe_can_accept && std::all_of(write_cooldown_remaining_.begin(), write_cooldown_remaining_.end(), [](const auto& cooldown) {
            return cooldown == 0;
        }));
    }

    std::size_t checked_index(const AddrT& addr) const {
        const auto index = static_cast<std::size_t>(addr);
        if (index >= memory_.size()) {
            std::ostringstream message;
            message << "SRAM address out of range: " << index;
            SC_REPORT_ERROR(this->name(), message.str().c_str());
            return 0;
        }
        return index;
    }

    bool all_read_downstream_ready_() const {
        return std::all_of(read_fds_ready.begin(), read_fds_ready.end(), [](const auto& ready) {
            return ready.read();
        });
    }
};
