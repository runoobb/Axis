#pragma once

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <systemc>
#include <vector>

#ifndef AXIS_TEST_TRACE_DIR
#error "AXIS_TEST_TRACE_DIR must be defined by CMake for each test case"
#endif

namespace axis_test {

inline std::string trace_path(const char* file_name) {
    return std::string(AXIS_TEST_TRACE_DIR) + "/" + file_name;
}

inline void report_sink_log_error(const std::string& file_path, const std::string& detail) {
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

inline std::vector<int> read_sink_log(const std::string& file_path) {
    std::ifstream file(file_path);
    std::vector<int> values;
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
        std::string index_token;
        std::string value_token;
        std::string extra_token;
        if (!(parser >> sink_token >> index_token >> value_token) || parser >> extra_token) {
            report_sink_log_error(file_path, "malformed sink record: " + line);
            continue;
        }

        std::size_t index = 0;
        int value = 0;
        if (sink_token != "sink" || !parse_prefixed(index_token, "index=", index)
            || !parse_prefixed(value_token, "value=", value)) {
            report_sink_log_error(file_path, "malformed sink record: " + line);
            continue;
        }
        if (index != values.size()) {
            report_sink_log_error(file_path, "out-of-order or missing sink index: " + line);
        }
        values.push_back(value);
    }

    return values;
}

inline void verify_sink_log(const std::string& file_path, const std::vector<int>& expected) {
    const auto values = read_sink_log(file_path);
    if (values.size() < expected.size()) {
        report_sink_log_error(file_path, "missing consumed values");
    }
    if (values.size() > expected.size()) {
        report_sink_log_error(file_path, "extra consumed values");
    }
    const std::size_t count = std::min(values.size(), expected.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (values[index] != expected[index]) {
            std::ostringstream message;
            message << "index " << index << " expected " << expected[index]
                    << " but found " << values[index];
            report_sink_log_error(file_path, message.str());
        }
    }
}

inline int report_status_code() {
    return sc_core::sc_report_handler::get_count(sc_core::SC_ERROR) == 0
                   && sc_core::sc_report_handler::get_count(sc_core::SC_FATAL) == 0
               ? 0
               : 1;
}

} // namespace axis_test
