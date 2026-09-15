#include "alu.hpp"
#include "fifo.hpp"
#include "module_logger.hpp"
#include "scalar_sink.hpp"
#include "scalar_source.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <systemc>
#include <vector>

namespace {

void report_sink_log_error(const std::string& file_path, const std::string& detail) {
    std::ostringstream message;
    message << file_path << ": " << detail;
    SC_REPORT_ERROR("sc_main", message.str().c_str());
}

template <typename Value>
bool parse_prefixed(const std::string& token, const char* prefix, Value& value) {
    const std::string prefix_text(prefix);
    if (token.rfind(prefix_text, 0) != 0) {
        return false;
    }

    std::istringstream parser(token.substr(prefix_text.size()));
    parser >> value;
    return parser && parser.eof();
}

std::vector<std::vector<int>> read_sink_log(const std::string& file_path, std::size_t port_count) {
    std::ifstream file(file_path);
    std::vector<std::vector<int>> values(port_count);
    if (!file) {
        report_sink_log_error(file_path, "missing sink log file");
        return values;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("sink ", 0) != 0) {
            continue;
        }

        std::istringstream parser(line);
        std::string sink_token;
        std::string port_token;
        std::string index_token;
        std::string value_token;
        std::string expected_token;
        std::string match_token;
        std::string extra_token;
        if (!(parser >> sink_token >> port_token >> index_token >> value_token >> expected_token >> match_token)
            || parser >> extra_token) {
            report_sink_log_error(file_path, "malformed sink record: " + line);
            continue;
        }

        std::size_t port = 0;
        std::size_t index = 0;
        int value = 0;
        int expected = 0;
        int match = 0;
        if (sink_token != "sink" || !parse_prefixed(port_token, "port=", port)
            || !parse_prefixed(index_token, "index=", index)
            || !parse_prefixed(value_token, "value=", value)
            || !parse_prefixed(expected_token, "expected=", expected)
            || !parse_prefixed(match_token, "match=", match)) {
            report_sink_log_error(file_path, "malformed sink record: " + line);
            continue;
        }
        if (port >= port_count) {
            report_sink_log_error(file_path, "extra sink port record: " + line);
            continue;
        }
        if (index != values[port].size()) {
            report_sink_log_error(file_path, "out-of-order or missing sink index: " + line);
        }
        if (match != 1 || value != expected) {
            report_sink_log_error(file_path, "mismatched sink record: " + line);
        }
        values[port].push_back(value);
    }

    return values;
}

void verify_sink_log(const std::string& file_path, const std::vector<std::vector<int>>& expected) {
    const auto values = read_sink_log(file_path, expected.size());
    for (std::size_t port = 0; port < expected.size(); ++port) {
        if (values[port].size() < expected[port].size()) {
            report_sink_log_error(file_path, "missing consumed values on port " + std::to_string(port));
        }
        if (values[port].size() > expected[port].size()) {
            report_sink_log_error(file_path, "extra consumed values on port " + std::to_string(port));
        }
        const std::size_t count = std::min(values[port].size(), expected[port].size());
        for (std::size_t index = 0; index < count; ++index) {
            if (values[port][index] != expected[port][index]) {
                std::ostringstream message;
                message << "port " << port << " index " << index << " expected "
                        << expected[port][index] << " but found " << values[port][index];
                report_sink_log_error(file_path, message.str());
            }
        }
    }
}

} // namespace

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

    ScalarSource<int> op1_src("op1_src", {1, 2, 3, 4, 5}, 4, 1, clock_period, 1, op1_src_log);
    ScalarSource<int> op2_src("op2_src", {10, 20, 30, 40, 50}, 7, 1, clock_period, 1, op2_src_log);
    FIFO<int> fifo("fifo", 2, clock_period, 1, fifo_log);
    ALU<int, std::plus<int>> alu("alu", {30, 30}, 2, clock_period, 4, std::plus<int>{}, alu_log);
    ScalarSink<int> fast_sink("fast_sink", 10, 1, clock_period, expected, fast_sink_log);
    ScalarSink<int> slow_sink("slow_sink", 100, 1, clock_period, expected, slow_sink_log);
    ScalarSink<int> audit_sink("audit_sink", 2, 10, 1, clock_period, {expected, expected}, audit_sink_log);

    op1_src.clk(clk);
    op2_src.clk(clk);
    fifo.clk(clk);
    alu.clk(clk);
    fast_sink.clk(clk);
    slow_sink.clk(clk);
    audit_sink.clk(clk);

    sc_core::sc_signal<int> op1_data;
    sc_core::sc_signal<bool> op1_valid;
    sc_core::sc_signal<bool> op1_ready;

    sc_core::sc_signal<int> op2_data;
    sc_core::sc_signal<bool> op2_valid;
    sc_core::sc_signal<bool> op2_ready;

    sc_core::sc_signal<int> fifo_data;
    sc_core::sc_signal<bool> fifo_valid;
    sc_core::sc_signal<bool> fifo_ready;

    sc_core::sc_signal<int> alu_out0_data;
    sc_core::sc_signal<int> alu_out1_data;
    sc_core::sc_signal<int> alu_out2_data;
    sc_core::sc_signal<int> alu_out3_data;
    sc_core::sc_signal<bool> alu_valid;
    sc_core::sc_signal<bool> fast_ready;
    sc_core::sc_signal<bool> slow_ready;
    sc_core::sc_signal<bool> audit0_ready;
    sc_core::sc_signal<bool> audit1_ready;

    op1_src.out_data[0](op1_data);
    op1_src.tds_valid(op1_valid);
    op1_src.fds_ready[0](op1_ready);
    alu.in_data[0](op1_data);
    alu.fus_valid[0](op1_valid);
    alu.tus_ready[0](op1_ready);

    op2_src.out_data[0](op2_data);
    op2_src.tds_valid(op2_valid);
    op2_src.fds_ready[0](op2_ready);
    fifo.in_data(op2_data);
    fifo.fus_valid(op2_valid);
    fifo.tus_ready(op2_ready);

    fifo.out_data[0](fifo_data);
    fifo.tds_valid(fifo_valid);
    fifo.fds_ready[0](fifo_ready);
    alu.in_data[1](fifo_data);
    alu.fus_valid[1](fifo_valid);
    alu.tus_ready[1](fifo_ready);

    alu.out_data[0](alu_out0_data);
    alu.out_data[1](alu_out1_data);
    alu.out_data[2](alu_out2_data);
    alu.out_data[3](alu_out3_data);
    alu.tds_valid(alu_valid);
    alu.fds_ready[0](fast_ready);
    alu.fds_ready[1](slow_ready);
    alu.fds_ready[2](audit0_ready);
    alu.fds_ready[3](audit1_ready);

    fast_sink.in_data[0](alu_out0_data);
    fast_sink.fus_valid[0](alu_valid);
    fast_sink.tus_ready[0](fast_ready);

    slow_sink.in_data[0](alu_out1_data);
    slow_sink.fus_valid[0](alu_valid);
    slow_sink.tus_ready[0](slow_ready);

    audit_sink.in_data[0](alu_out2_data);
    audit_sink.fus_valid[0](alu_valid);
    audit_sink.tus_ready[0](audit0_ready);
    audit_sink.in_data[1](alu_out3_data);
    audit_sink.fus_valid[1](alu_valid);
    audit_sink.tus_ready[1](audit1_ready);

    sc_core::sc_start(sc_core::sc_time(10000, sc_core::SC_NS));

    verify_sink_log(fast_sink_log.file_path, {expected});
    verify_sink_log(slow_sink_log.file_path, {expected});
    verify_sink_log(audit_sink_log.file_path, {expected, expected});

    return sc_core::sc_report_handler::get_count(sc_core::SC_ERROR) == 0
                   && sc_core::sc_report_handler::get_count(sc_core::SC_FATAL) == 0
               ? 0
               : 1;
}
