#include "alu.hpp"
#include "scalar_sink.hpp"
#include "scalar_source.hpp"

#include "../test_utils.hpp"

#include <fstream>
#include <functional>
#include <string>
#include <systemc>
#include <utility>
#include <vector>

int sc_main(int, char**) {
    const std::vector<int> left_values{1, 2, 3, 4};
    const std::vector<int> right_values{10, 20, 30, 40};
    const std::vector<int> expected{11, 22, 33, 44};
    const sc_core::sc_time clock_period(10, sc_core::SC_NS);

    sc_core::sc_clock clk("clk", clock_period);

    const ModuleLogOptions left_log{true, axis_test::trace_path("left_source.txt")};
    const ModuleLogOptions right_log{true, axis_test::trace_path("right_source.txt")};
    const ModuleLogOptions alu_log{true, axis_test::trace_path("alu.txt")};
    const ModuleLogOptions sink_log{true, axis_test::trace_path("sink.txt")};
    const ModuleLogOptions second_sink_log{true, axis_test::trace_path("second_sink.txt")};

    ScalarSource<int> left_source("left_source", left_values, 0, 1, clock_period, 1, left_log);
    ScalarSource<int> right_source("right_source", right_values, 0, 1, clock_period, 1, right_log);
    ALU<int, std::plus<int>> alu("alu", {0, 0}, 3, clock_period, 2, std::plus<int>{}, alu_log);
    ScalarSink<int> sink("sink", 0, 1, clock_period, sink_log);
    ScalarSink<int> second_sink("second_sink", 0, 1, clock_period, second_sink_log);

    left_source.clk(clk);
    right_source.clk(clk);
    alu.clk(clk);
    sink.clk(clk);
    second_sink.clk(clk);

    sc_core::sc_buffer<int> left_data;
    sc_core::sc_buffer<bool> left_valid;
    sc_core::sc_buffer<bool> left_transfer;

    sc_core::sc_buffer<int> right_data;
    sc_core::sc_buffer<bool> right_valid;
    sc_core::sc_buffer<bool> right_transfer;

    // Single shared ready line: both operand sources handshake jointly with the ALU.
    sc_core::sc_buffer<bool> operand_ready;

    sc_core::sc_buffer<int> result_data;
    sc_core::sc_buffer<bool> result_valid;
    sc_core::sc_buffer<bool> result_ready;
    sc_core::sc_buffer<bool> second_result_ready;
    sc_core::sc_buffer<bool> result_transfer;

    left_source.out_data(left_data);
    left_source.tds_valid(left_valid);
    left_source.fds_ready[0](operand_ready);
    left_source.transfer_tds(left_transfer);
    alu.in_data[0](left_data);
    alu.fus_valid[0](left_valid);
    alu.tus_ready(operand_ready);
    alu.transfer_fus[0](left_transfer);

    right_source.out_data(right_data);
    right_source.tds_valid(right_valid);
    right_source.fds_ready[0](operand_ready);
    right_source.transfer_tds(right_transfer);
    alu.in_data[1](right_data);
    alu.fus_valid[1](right_valid);
    alu.transfer_fus[1](right_transfer);

    alu.out_data(result_data);
    alu.tds_valid(result_valid);
    alu.fds_ready[0](result_ready);
    alu.fds_ready[1](second_result_ready);
    alu.transfer_tds(result_transfer);

    sink.in_data(result_data);
    sink.fus_valid(result_valid);
    sink.tus_ready(result_ready);
    sink.transfer_fus(result_transfer);
    second_sink.in_data(result_data);
    second_sink.fus_valid(result_valid);
    second_sink.tus_ready(second_result_ready);
    second_sink.transfer_fus(result_transfer);

    sc_core::sc_start(sc_core::sc_time(300, sc_core::SC_NS));

    return axis_test::report_status_code();
}
