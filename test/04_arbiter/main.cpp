#include "arbiter.hpp"
#include "scalar_sink.hpp"
#include "scalar_source.hpp"

#include "../test_utils.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <systemc>
#include <vector>


int sc_main(int, char**) {
    const std::vector<int> high_priority_values{100, 101, 102};
    const std::vector<int> mid_priority_values{200, 201, 202};
    const std::vector<int> low_priority_values{300, 301, 302};
    const std::vector<int> expected{100, 101, 102, 200, 201, 202, 300, 301, 302};
    const sc_core::sc_time clock_period(10, sc_core::SC_NS);

    sc_core::sc_clock clk("clk", clock_period);

    const ModuleLogOptions high_source_log{true, axis_test::trace_path("high_source.txt")};
    const ModuleLogOptions mid_source_log{true, axis_test::trace_path("mid_source.txt")};
    const ModuleLogOptions low_source_log{true, axis_test::trace_path("low_source.txt")};
    const ModuleLogOptions arbiter_log{true, axis_test::trace_path("arbiter.txt")};
    const ModuleLogOptions fast_sink_log{true, axis_test::trace_path("fast_sink.txt")};
    const ModuleLogOptions slow_sink_log{true, axis_test::trace_path("slow_sink.txt")};

    ScalarSource<int> high_source("high_source", high_priority_values, 0, 1, clock_period, 1,
                                  high_source_log);
    ScalarSource<int> mid_source("mid_source", mid_priority_values, 0, 1, clock_period, 1,
                                 mid_source_log);
    ScalarSource<int> low_source("low_source", low_priority_values, 0, 1, clock_period, 1,
                                 low_source_log);
    Arbiter<int> arbiter("arbiter", 3, 2, {0, 0, 0}, 1, clock_period, arbiter_log);
    ScalarSink<int> fast_sink("fast_sink", 0, 1, clock_period, fast_sink_log);
    ScalarSink<int> slow_sink("slow_sink", 1, 1, clock_period, slow_sink_log);

    high_source.clk(clk);
    mid_source.clk(clk);
    low_source.clk(clk);
    arbiter.clk(clk);
    fast_sink.clk(clk);
    slow_sink.clk(clk);

    sc_core::sc_buffer<int> high_data;
    sc_core::sc_buffer<int> mid_data;
    sc_core::sc_buffer<int> low_data;
    sc_core::sc_buffer<bool> high_valid;
    sc_core::sc_buffer<bool> mid_valid;
    sc_core::sc_buffer<bool> low_valid;
    sc_core::sc_buffer<bool> high_ready;
    sc_core::sc_buffer<bool> mid_ready;
    sc_core::sc_buffer<bool> low_ready;
    sc_core::sc_buffer<bool> high_transfer;
    sc_core::sc_buffer<bool> mid_transfer;
    sc_core::sc_buffer<bool> low_transfer;

    sc_core::sc_buffer<int> arbiter_data;
    sc_core::sc_buffer<bool> arbiter_valid;
    sc_core::sc_buffer<bool> arbiter_transfer;
    sc_core::sc_buffer<bool> fast_ready;
    sc_core::sc_buffer<bool> slow_ready;

    high_source.out_data(high_data);
    high_source.tds_valid(high_valid);
    high_source.fds_ready[0](high_ready);
    high_source.transfer_tds(high_transfer);
    arbiter.in_data[0](high_data);
    arbiter.fus_valid[0](high_valid);
    arbiter.transfer_fus[0](high_transfer);
    arbiter.tus_ready[0](high_ready);

    mid_source.out_data(mid_data);
    mid_source.tds_valid(mid_valid);
    mid_source.fds_ready[0](mid_ready);
    mid_source.transfer_tds(mid_transfer);
    arbiter.in_data[1](mid_data);
    arbiter.fus_valid[1](mid_valid);
    arbiter.transfer_fus[1](mid_transfer);
    arbiter.tus_ready[1](mid_ready);

    low_source.out_data(low_data);
    low_source.tds_valid(low_valid);
    low_source.fds_ready[0](low_ready);
    low_source.transfer_tds(low_transfer);
    arbiter.in_data[2](low_data);
    arbiter.fus_valid[2](low_valid);
    arbiter.transfer_fus[2](low_transfer);
    arbiter.tus_ready[2](low_ready);

    arbiter.out_data(arbiter_data);
    arbiter.tds_valid(arbiter_valid);
    arbiter.transfer_tds(arbiter_transfer);
    arbiter.fds_ready[0](fast_ready);
    arbiter.fds_ready[1](slow_ready);

    fast_sink.in_data(arbiter_data);
    fast_sink.fus_valid(arbiter_valid);
    fast_sink.tus_ready(fast_ready);
    fast_sink.transfer_fus(arbiter_transfer);
    slow_sink.in_data(arbiter_data);
    slow_sink.fus_valid(arbiter_valid);
    slow_sink.tus_ready(slow_ready);
    slow_sink.transfer_fus(arbiter_transfer);

    sc_core::sc_start(sc_core::sc_time(1000, sc_core::SC_NS));


    return axis_test::report_status_code();
}
