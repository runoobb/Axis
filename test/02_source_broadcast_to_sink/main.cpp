#include "scalar_sink.hpp"
#include "scalar_source.hpp"

#include "../test_utils.hpp"

#include <systemc>
#include <vector>

int sc_main(int, char**) {
    const std::vector<int> values{5, 10, 15, 20, 25};
    const sc_core::sc_time clock_period(10, sc_core::SC_NS);

    sc_core::sc_clock clk("clk", clock_period);

    const ModuleLogOptions source_log{true, axis_test::trace_path("source.txt")};
    const ModuleLogOptions slow_sink_log{true, axis_test::trace_path("slow_sink.txt")};
    const ModuleLogOptions fast_sink_log{true, axis_test::trace_path("fast_sink.txt")};

    ScalarSource<int> source("source", values, 5, 1, clock_period, 2, source_log);
    ScalarSink<int> slow_sink("slow_sink", 4, 1, clock_period, slow_sink_log);
    ScalarSink<int> fast_sink("fast_sink", 2, 1, clock_period, fast_sink_log);

    source.clk(clk);
    fast_sink.clk(clk);
    slow_sink.clk(clk);

    sc_core::sc_buffer<int> source_data;
    sc_core::sc_buffer<bool> source_valid;
    sc_core::sc_buffer<bool> fast_sink_ready;
    sc_core::sc_buffer<bool> slow_sink_ready;
    sc_core::sc_buffer<bool> source_transfer;


    source.out_data(source_data);
    source.tds_valid(source_valid);
    source.fds_ready[0](slow_sink_ready);
    source.fds_ready[1](fast_sink_ready);
    source.transfer_tds(source_transfer);

    slow_sink.in_data(source_data);
    fast_sink.in_data(source_data);

    slow_sink.fus_valid(source_valid);
    fast_sink.fus_valid(source_valid);

    slow_sink.tus_ready(slow_sink_ready);
    fast_sink.tus_ready(fast_sink_ready);

    slow_sink.transfer_fus(source_transfer);
    fast_sink.transfer_fus(source_transfer);


    sc_core::sc_start(sc_core::sc_time(200, sc_core::SC_NS));

    return axis_test::report_status_code();
}
