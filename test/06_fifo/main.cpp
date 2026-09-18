#include "fifo.hpp"
#include "scalar_sink.hpp"
#include "scalar_source.hpp"

#include "../test_utils.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <systemc>
#include <vector>


int sc_main(int, char**) {
    const std::vector<int> values{2, 4, 6, 8, 10};
    const sc_core::sc_time clock_period(10, sc_core::SC_NS);
    constexpr std::size_t function_latency = 3;

    sc_core::sc_clock clk("clk", clock_period);

    const ModuleLogOptions source_log{true, axis_test::trace_path("source.txt")};
    const ModuleLogOptions fifo0_log{true, axis_test::trace_path("fifo0.txt")};
    const ModuleLogOptions fifo1_log{true, axis_test::trace_path("fifo1.txt")};
    const ModuleLogOptions sink_log{true, axis_test::trace_path("sink.txt")};

    ScalarSource<int> source("source", values, function_latency, clock_period, 1, source_log);
    FIFO<int> fifo0("fifo0", 2, function_latency, clock_period, 1, fifo0_log);
    FIFO<int> fifo1("fifo1", 0, function_latency, clock_period, 1, fifo1_log);
    ScalarSink<int> sink("sink", 0, function_latency, clock_period, sink_log);

    source.clk(clk);
    fifo0.clk(clk);
    fifo1.clk(clk);
    sink.clk(clk);

    sc_core::sc_buffer<int> source_to_fifo0_data;
    sc_core::sc_buffer<bool> source_to_fifo0_valid;
    sc_core::sc_buffer<bool> source_to_fifo0_ready;
    sc_core::sc_buffer<bool> source_to_fifo0_transfer;

    sc_core::sc_buffer<int> fifo0_to_fifo1_data;
    sc_core::sc_buffer<bool> fifo0_to_fifo1_valid;
    sc_core::sc_buffer<bool> fifo0_to_fifo1_ready;
    sc_core::sc_buffer<bool> fifo0_to_fifo1_transfer;

    sc_core::sc_buffer<int> fifo1_to_sink_data;
    sc_core::sc_buffer<bool> fifo1_to_sink_valid;
    sc_core::sc_buffer<bool> fifo1_to_sink_ready;
    sc_core::sc_buffer<bool> fifo1_to_sink_transfer;

    source.out_data(source_to_fifo0_data);
    source.tds_valid(source_to_fifo0_valid);
    source.fds_ready[0](source_to_fifo0_ready);
    source.transfer_tds(source_to_fifo0_transfer);

    fifo0.in_data(source_to_fifo0_data);
    fifo0.fus_valid(source_to_fifo0_valid);
    fifo0.tus_ready(source_to_fifo0_ready);
    fifo0.transfer_fus(source_to_fifo0_transfer);
    fifo0.out_data(fifo0_to_fifo1_data);
    fifo0.tds_valid(fifo0_to_fifo1_valid);
    fifo0.fds_ready[0](fifo0_to_fifo1_ready);
    fifo0.transfer_tds(fifo0_to_fifo1_transfer);

    fifo1.in_data(fifo0_to_fifo1_data);
    fifo1.fus_valid(fifo0_to_fifo1_valid);
    fifo1.tus_ready(fifo0_to_fifo1_ready);
    fifo1.transfer_fus(fifo0_to_fifo1_transfer);
    fifo1.out_data(fifo1_to_sink_data);
    fifo1.tds_valid(fifo1_to_sink_valid);
    fifo1.fds_ready[0](fifo1_to_sink_ready);
    fifo1.transfer_tds(fifo1_to_sink_transfer);

    sink.in_data(fifo1_to_sink_data);
    sink.fus_valid(fifo1_to_sink_valid);
    sink.tus_ready(fifo1_to_sink_ready);
    sink.transfer_fus(fifo1_to_sink_transfer);

    sc_core::sc_start(sc_core::sc_time(300, sc_core::SC_NS));

    return axis_test::report_status_code();
}
