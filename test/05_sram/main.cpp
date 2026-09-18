#include "sram.hpp"
#include "scalar_sink.hpp"
#include "scalar_source.hpp"

#include "../test_utils.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <systemc>
#include <vector>

namespace {


} // namespace

int sc_main(int, char**) {
    const std::vector<std::size_t> write_addresses{0, 1, 2};
    const std::vector<int> write_values{42, 77, 99};
    const std::vector<std::size_t> read_addresses{0, 1, 2};
    const std::vector<int> expected{42, 77, 99};
    const sc_core::sc_time clock_period(10, sc_core::SC_NS);

    sc_core::sc_clock clk("clk", clock_period);

    const ModuleLogOptions write_addr_source_log{true, axis_test::trace_path("write_addr_source.txt")};
    const ModuleLogOptions write_data_source_log{true, axis_test::trace_path("write_data_source.txt")};
    const ModuleLogOptions read_addr_source_log{true, axis_test::trace_path("read_addr_source.txt")};
    const ModuleLogOptions sram_read_log{true, axis_test::trace_path("sram_read.txt")};
    const ModuleLogOptions sram_write_log{true, axis_test::trace_path("sram_write.txt")};
    const ModuleLogOptions sink_log{true, axis_test::trace_path("sink.txt")};

    ScalarSource<std::size_t> write_addr_source("write_addr_source", write_addresses, 1,
                                                clock_period, 1, write_addr_source_log);
    ScalarSource<int> write_data_source("write_data_source", write_values, 1, clock_period, 1,
                                        write_data_source_log);
    ScalarSource<std::size_t> read_addr_source("read_addr_source", read_addresses, 1,
                                               clock_period, 1, read_addr_source_log);
    SRAM<std::size_t, int> sram("sram", 8, {0, 0, 0}, 1, clock_period, 1, 0, sram_read_log,
                                sram_write_log);
    ScalarSink<int> sink("sink", 0, 1, clock_period, sink_log);

    write_addr_source.clk(clk);
    write_data_source.clk(clk);
    read_addr_source.clk(clk);
    sram.clk(clk);
    sink.clk(clk);

    sc_core::sc_buffer<std::size_t> write_addr_signal;
    sc_core::sc_buffer<int> write_data_signal;
    sc_core::sc_buffer<bool> write_addr_valid;
    sc_core::sc_buffer<bool> write_data_valid;
    sc_core::sc_buffer<bool> write_addr_transfer;
    sc_core::sc_buffer<bool> write_data_transfer;
    // Single shared ready line: the addr and data sources handshake jointly with the SRAM.
    sc_core::sc_buffer<bool> write_ready;

    sc_core::sc_buffer<std::size_t> read_addr_signal;
    sc_core::sc_buffer<bool> read_addr_valid;
    sc_core::sc_buffer<bool> read_addr_ready;
    sc_core::sc_buffer<bool> read_addr_transfer;

    sc_core::sc_buffer<int> read_data_signal;
    sc_core::sc_buffer<bool> read_data_valid;
    sc_core::sc_buffer<bool> read_data_transfer;
    sc_core::sc_buffer<bool> sink_ready;

    write_addr_source.out_data(write_addr_signal);
    write_addr_source.tds_valid(write_addr_valid);
    write_addr_source.transfer_tds(write_addr_transfer);
    write_addr_source.fds_ready[0](write_ready);
    sram.write_addr(write_addr_signal);
    sram.write_fus_valid[0](write_addr_valid);
    sram.write_transfer_fus[0](write_addr_transfer);
    sram.write_tus_ready(write_ready);

    write_data_source.out_data(write_data_signal);
    write_data_source.tds_valid(write_data_valid);
    write_data_source.transfer_tds(write_data_transfer);
    write_data_source.fds_ready[0](write_ready);
    sram.write_data(write_data_signal);
    sram.write_fus_valid[1](write_data_valid);
    sram.write_transfer_fus[1](write_data_transfer);

    read_addr_source.out_data(read_addr_signal);
    read_addr_source.tds_valid(read_addr_valid);
    read_addr_source.transfer_tds(read_addr_transfer);
    read_addr_source.fds_ready[0](read_addr_ready);
    sram.read_addr(read_addr_signal);
    sram.read_fus_valid(read_addr_valid);
    sram.read_transfer_fus(read_addr_transfer);
    sram.read_tus_ready(read_addr_ready);

    sram.read_data(read_data_signal);
    sram.read_tds_valid(read_data_valid);
    sram.read_transfer_tds(read_data_transfer);
    sram.read_fds_ready[0](sink_ready);
    sink.in_data(read_data_signal);
    sink.fus_valid(read_data_valid);
    sink.tus_ready(sink_ready);
    sink.transfer_fus(read_data_transfer);

    sc_core::sc_start(sc_core::sc_time(1000, sc_core::SC_NS));


    return axis_test::report_status_code();
}
