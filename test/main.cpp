#include "alu.hpp"
#include "fifo.hpp"
#include "module_logger.hpp"
#include "scalar_sink.hpp"
#include "scalar_source.hpp"

#include <functional>
#include <systemc>
#include <vector>

int sc_main(int, char**) {
    const std::vector<int> expected{11, 22, 33, 44, 55};
    const sc_core::sc_time clock_period(10, sc_core::SC_NS);

    sc_core::sc_clock clk("clk", clock_period);

    const ModuleLogOptions op1_src_log{true, "op1_src.txt"};
    const ModuleLogOptions op2_src_log{true, "op2_src.txt"};
    const ModuleLogOptions fifo_log{true, "fifo.txt"};
    const ModuleLogOptions alu_log{true, "alu.txt"};
    const ModuleLogOptions fast_sink_log{true, "fast_sink.txt"};
    const ModuleLogOptions slow_sink_log{true, "slow_sink.txt"};
    const ModuleLogOptions audit_sink_log{true, "audit_sink.txt"};

    ScalarSource<int> op1_src("op1_src", {1, 2, 3, 4, 5}, 4, clock_period, 1, op1_src_log);
    ScalarSource<int> op2_src("op2_src", {10, 20, 30, 40, 50}, 7, clock_period, 1, op2_src_log);
    FIFO<int> fifo("fifo", 2, clock_period, 1, fifo_log);
    ALU<int, std::plus<int>> alu("alu", {30, 30}, 2, clock_period, 4, std::plus<int>{}, alu_log);
    ScalarSink<int> fast_sink("fast_sink", 10, clock_period, expected, fast_sink_log);
    ScalarSink<int> slow_sink("slow_sink", 100, clock_period, expected, slow_sink_log);
    ScalarSink<int> audit_sink("audit_sink", 2, 10, clock_period, {expected, expected}, audit_sink_log);

    op1_src.clk(clk);
    op2_src.clk(clk);
    fifo.clk(clk);
    alu.clk(clk);
    fast_sink.clk(clk);
    slow_sink.clk(clk);
    audit_sink.clk(clk);

    sc_core::sc_fifo<int> op1_data;
    sc_core::sc_signal<bool> op1_valid;
    sc_core::sc_signal<bool> op1_ready;

    sc_core::sc_fifo<int> op2_data;
    sc_core::sc_signal<bool> op2_valid;
    sc_core::sc_signal<bool> op2_ready;

    sc_core::sc_fifo<int> fifo_data;
    sc_core::sc_signal<bool> fifo_valid;
    sc_core::sc_signal<bool> fifo_ready;

    sc_core::sc_fifo<int> alu_out0_data;
    sc_core::sc_fifo<int> alu_out1_data;
    sc_core::sc_fifo<int> alu_out2_data;
    sc_core::sc_fifo<int> alu_out3_data;
    sc_core::sc_signal<bool> alu_valid;
    sc_core::sc_signal<bool> fast_ready;
    sc_core::sc_signal<bool> slow_ready;
    sc_core::sc_signal<bool> audit0_ready;
    sc_core::sc_signal<bool> audit1_ready;

    op1_src.out_data[0] = &op1_data;
    op1_src.tds_valid[0](op1_valid);
    op1_src.fds_ready[0](op1_ready);
    alu.in_data[0] = &op1_data;
    alu.fus_valid[0](op1_valid);
    alu.tus_ready[0](op1_ready);

    op2_src.out_data[0] = &op2_data;
    op2_src.tds_valid[0](op2_valid);
    op2_src.fds_ready[0](op2_ready);
    fifo.in_data = &op2_data;
    fifo.fus_valid(op2_valid);
    fifo.tus_ready(op2_ready);

    fifo.out_data[0] = &fifo_data;
    fifo.tds_valid(fifo_valid);
    fifo.fds_ready[0](fifo_ready);
    alu.in_data[1] = &fifo_data;
    alu.fus_valid[1](fifo_valid);
    alu.tus_ready[1](fifo_ready);

    alu.out_data[0] = &alu_out0_data;
    alu.out_data[1] = &alu_out1_data;
    alu.out_data[2] = &alu_out2_data;
    alu.out_data[3] = &alu_out3_data;
    alu.tds_valid(alu_valid);
    alu.fds_ready[0](fast_ready);
    alu.fds_ready[1](slow_ready);
    alu.fds_ready[2](audit0_ready);
    alu.fds_ready[3](audit1_ready);

    fast_sink.in_data[0] = &alu_out0_data;
    fast_sink.fus_valid[0](alu_valid);
    fast_sink.tus_ready[0](fast_ready);

    slow_sink.in_data[0] = &alu_out1_data;
    slow_sink.fus_valid[0](alu_valid);
    slow_sink.tus_ready[0](slow_ready);

    audit_sink.in_data[0] = &alu_out2_data;
    audit_sink.fus_valid[0](alu_valid);
    audit_sink.tus_ready[0](audit0_ready);
    audit_sink.in_data[1] = &alu_out3_data;
    audit_sink.fus_valid[1](alu_valid);
    audit_sink.tus_ready[1](audit1_ready);

    sc_core::sc_start(sc_core::sc_time(10000, sc_core::SC_NS));

    if (!fast_sink.complete() || !slow_sink.complete()
        || !audit_sink.complete(0) || !audit_sink.complete(1)) {
        SC_REPORT_ERROR("sc_main", "one or more broadcast sink sequences are incomplete");
    }

    return sc_core::sc_report_handler::get_count(sc_core::SC_ERROR) == 0
                   && sc_core::sc_report_handler::get_count(sc_core::SC_FATAL) == 0
               ? 0
               : 1;
}
