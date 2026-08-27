#include "alu.hpp"
#include "fifo.hpp"
#include "scalar_sink.hpp"
#include "scalar_source.hpp"

int sc_main(int, char**) {
    const std::vector<int> expected{11, 22, 33, 44, 55};
    const sc_core::sc_time clock_period(1, sc_core::SC_NS);

    ScalarSource op1_src("op1_src", {1, 2, 3, 4, 5}, 4, clock_period, {1});
    ScalarSource op2_src(
        "op2_src", {10, 20, 30, 40, 50}, 7, clock_period, {1});
    FIFO<int> fifo("fifo", 2, clock_period, {2});
    ALU<int, std::plus<int>> alu(
        "alu", 30, 2, clock_period, {3, 7, 5, 5}, std::plus<int>{});
    ScalarSink fast_sink("fast_sink", 2, 10, clock_period, expected);
    ScalarSink slow_sink("slow_sink", 1, 100, clock_period, expected);
    ScalarSink audit_sink(
        "audit_sink", 2, 2, 10, clock_period, {expected, expected});

    op1_src.out[0].bind(alu.in[0]);
    op2_src.out[0].bind(fifo.in);
    fifo.out[0].bind(alu.in[1]);
    alu.out[0].bind(fast_sink.in[0]);
    alu.out[1].bind(slow_sink.in[0]);
    alu.out[2].bind(audit_sink.in[0]);
    alu.out[3].bind(audit_sink.in[1]);

    sc_core::sc_start(sc_core::sc_time(3000, sc_core::SC_NS));

    if (!fast_sink.complete() || !slow_sink.complete()
        || !audit_sink.complete(0) || !audit_sink.complete(1)) {
        SC_REPORT_ERROR("sc_main",
                        "one or more broadcast sink sequences are incomplete");
    }

    return sc_core::sc_report_handler::get_count(sc_core::SC_ERROR) == 0
                   && sc_core::sc_report_handler::get_count(sc_core::SC_FATAL) == 0
               ? 0
               : 1;
}
