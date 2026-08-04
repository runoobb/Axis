#include "scalar_modules.hpp"

int sc_main(int, char**)
{
    ScalarSource add_src0(
        "add_src0",
        {1, 2, 3, 4, 5},
        sc_time(4, SC_NS),
        sc_time(1, SC_NS));
    ScalarSource add_src1(
        "add_src1",
        {10, 20, 30, 40, 50},
        sc_time(7, SC_NS),
        sc_time(1, SC_NS));
    BinaryScalarOp<int> add(
        "add",
        sc_time(30, SC_NS),
        sc_time(12, SC_NS),
        2,
        sc_time(3, SC_NS));
    ScalarSink add_sink(
        "add_sink",
        1,
        sc_time(100, SC_NS),
        {11, 22, 33, 44, 55});

    add_src0.out.bind(add.in0);
    add_src1.out.bind(add.in1);
    add.out.bind(add_sink.in);

    sc_start(sc_time(3000, SC_NS));
    return 0;
}
