#include "scalar_sink.hpp"
#include "scalar_source.hpp"

#include "../test_utils.hpp"

#include <systemc>
#include <vector>

int sc_main(int, char**) {
    const std::vector<int> values{3, 6, 9, 12};
    const sc_core::sc_time clock_period(10, sc_core::SC_NS);

    sc_core::sc_clock clk("clk", clock_period);

    const ModuleLogOptions source_log{true, axis_test::trace_path("source.txt")};
    const ModuleLogOptions sink_log{true, axis_test::trace_path("sink.txt")};

    ScalarSource<int> source("source", values, 0, 2, clock_period, 1, source_log);
    ScalarSink<int> sink("sink", 0, 1, clock_period, sink_log);

    source.clk(clk);
    sink.clk(clk);

    sc_core::sc_signal<int> data;
    sc_core::sc_signal<bool> valid;
    sc_core::sc_signal<bool> ready;

    source.out_data[0](data);
    source.tds_valid(valid);
    source.fds_ready[0](ready);

    sink.in_data(data);
    sink.fus_valid(valid);
    sink.tus_ready(ready);

    sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));

    // axis_test::verify_sink_log(sink_log.file_path, values);
    return axis_test::report_status_code();
}
