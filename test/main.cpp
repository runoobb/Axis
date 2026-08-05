#include "scalar_modules.hpp"

struct Double {
    int operator()(int x) const { return x * 2; }
};

struct FifoPassThrough {
    int operator()(int x) const { return x; }
};

struct Add3 {
    int operator()(int a, int b, int c) const { return a + b + c; }
};

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
    ScalarOp<int, std::plus<int>> add2(
        "add2",
        2,                  // input port number
        sc_time(30, SC_NS), // function interval
        sc_time(12, SC_NS), // function latency
        2,                  // function pipeline capacity
        sc_time(3, SC_NS)); // transfer latency
    ScalarSink add_sink(
        "add_sink",
        1,
        sc_time(100, SC_NS),
        {11, 22, 33, 44, 55});

    ScalarSource double_src(
        "double_src",
        {1, 2, 3, 4, 5},
        sc_time(5, SC_NS),
        sc_time(1, SC_NS));
    ScalarOp<int, Double> double_op(
        "double_op",
        1,
        sc_time(25, SC_NS),
        sc_time(10, SC_NS),
        2,
        sc_time(3, SC_NS));
    ScalarSink double_sink(
        "double_sink",
        1,
        sc_time(90, SC_NS),
        {2, 4, 6, 8, 10});

    ScalarSource fifo_src(
        "fifo_src",
        {5, 4, 3, 2, 1},
        sc_time(5, SC_NS),
        sc_time(1, SC_NS));
    ScalarOp<int, FifoPassThrough> fifo_op(
        "fifo_op",
        1,
        sc_time(20, SC_NS),
        sc_time(8, SC_NS),
        2,
        sc_time(3, SC_NS));
    ScalarSink fifo_sink(
        "fifo_sink",
        1,
        sc_time(80, SC_NS),
        {5, 4, 3, 2, 1});

    ScalarSource add3_src0(
        "add3_src0",
        {1, 2, 3, 4, 5},
        sc_time(4, SC_NS),
        sc_time(1, SC_NS));
    ScalarSource add3_src1(
        "add3_src1",
        {10, 20, 30, 40, 50},
        sc_time(6, SC_NS),
        sc_time(1, SC_NS));
    ScalarSource add3_src2(
        "add3_src2",
        {100, 200, 300, 400, 500},
        sc_time(8, SC_NS),
        sc_time(1, SC_NS));
    ScalarOp<int, Add3> add3(
        "add3",
        3,
        sc_time(35, SC_NS),
        sc_time(14, SC_NS),
        2,
        sc_time(3, SC_NS));
    ScalarSink add3_sink(
        "add3_sink",
        1,
        sc_time(110, SC_NS),
        {111, 222, 333, 444, 555});

    add_src0.out.bind(add2.in[0]);
    add_src1.out.bind(add2.in[1]);
    add2.out.bind(add_sink.in);

    double_src.out.bind(double_op.in[0]);
    double_op.out.bind(double_sink.in);

    fifo_src.out.bind(fifo_op.in[0]);
    fifo_op.out.bind(fifo_sink.in);

    add3_src0.out.bind(add3.in[0]);
    add3_src1.out.bind(add3.in[1]);
    add3_src2.out.bind(add3.in[2]);
    add3.out.bind(add3_sink.in);

    sc_start(sc_time(3000, SC_NS));
    return 0;
}
