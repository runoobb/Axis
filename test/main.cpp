#include "scalar_modules.hpp"

#include <functional>

struct Mul {
    int operator()(int a, int b) const
    {
        return a * b;
    }
};

struct Sub {
    int operator()(int a, int b) const
    {
        return a - b;
    }
};

int sc_main(int, char**)
{
    // Case 1: one BinaryScalarOp add, direct source -> op -> sink.
    ScalarSource add_src0(
        "add_src0",
        {1, 2, 3, 4, 5},
        sc_time(4, SC_NS),  // src interval
        sc_time(1, SC_NS)); // src latency
    ScalarSource add_src1(
        "add_src1",
        {10, 20, 30, 40, 50},
        sc_time(7, SC_NS),
        sc_time(1, SC_NS));
    BinaryScalarOp<int> add(
        "add",
        sc_time(30, SC_NS),  // function interval
        sc_time(12, SC_NS), // compute latency
        2,                  // in0 capacity
        2,                  // in1 capacity
        2,                  // out capacity
        sc_time(3, SC_NS)); // transfer latency

    ScalarSink add_sink(
        "add_sink",
        1,                  // capacity
        sc_time(100, SC_NS), // sink interval
        {11, 22, 33, 44, 55});

    add_src0.out.bind(add.in0);
    add_src1.out.bind(add.in1);
    add.out.bind(add_sink.in);

    // // Case 2: cascaded graph: sum = a + b, final = sum - c.
    // ScalarSource chain_src0(
    //     "chain_src0",
    //     {1, 2, 3, 4},
    //     sc_time(2, SC_NS),
    //     sc_time(1, SC_NS));
    // ScalarSource chain_src1(
    //     "chain_src1",
    //     {10, 20, 30, 40},
    //     sc_time(3, SC_NS),
    //     sc_time(1, SC_NS));
    // ScalarSource chain_src2(
    //     "chain_src2",
    //     {5, 6, 7, 8},
    //     sc_time(4, SC_NS),
    //     sc_time(1, SC_NS));
    // BinaryScalarOp<int> chain_add(
    //     "chain_add",
    //     sc_time(3, SC_NS), // function interval
    //     sc_time(5, SC_NS), // compute latency
    //     2,                 // in0 capacity
    //     2,                 // in1 capacity
    //     2,                 // out capacity
    //     sc_time(2, SC_NS));
    // BinaryScalarOp<int, Sub> chain_sub(
    //     "chain_sub",
    //     sc_time(4, SC_NS), // function interval
    //     sc_time(7, SC_NS), // compute latency
    //     2,                 // in0 capacity
    //     2,                 // in1 capacity
    //     2,                 // out capacity
    //     sc_time(2, SC_NS));
    // ScalarSink chain_sink(
    //     "chain_sink",
    //     1,
    //     sc_time(15, SC_NS),
    //     {6, 16, 26, 36});

    // chain_src0.out.bind(chain_add.in0);
    // chain_src1.out.bind(chain_add.in1);
    // chain_add.out.bind(chain_sub.in0);
    // chain_src2.out.bind(chain_sub.in1);
    // chain_sub.out.bind(chain_sink.in);

    sc_start(sc_time(3000, SC_NS));
    return 0;
}
