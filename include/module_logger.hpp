#pragma once

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <optional>
#include <ostream>
#include <string>
#include <systemc>
#include <vector>

struct ModuleLogOptions {
    bool enabled{false};
    std::string file_path{};
};

class ModuleLogger {
public:
    void configure(const char* module_name,
                   const ModuleLogOptions& options,
                   sc_core::sc_time clock_period) {
        enabled_ = options.enabled;
        clock_period_ = clock_period;
        if (!enabled_) {
            return;
        }
        if (clock_period_ <= sc_core::SC_ZERO_TIME) {
            SC_REPORT_FATAL(module_name, "log clock period must be positive");
        }

        const std::string path = options.file_path.empty() ? std::string(module_name) + ".txt" : options.file_path;
        file_.open(path);
        if (!file_) {
            SC_REPORT_FATAL(module_name, "failed to open log file");
        }

        file_ << "cycle      time       dequeued enqueued pipe\n";
        file_.flush();
    }

    bool enabled() const {
        return enabled_;
    }

    template <typename Pipe>
    void log_pipeline(const Pipe& pipe, bool dequeued, bool enqueued) {
        if (!enabled_) {
            return;
        }

        const sc_core::sc_time time = sc_core::sc_time_stamp();
        const auto cycle = static_cast<std::size_t>(time / clock_period_);
        file_ << "cycle=" << std::setw(6) << cycle
              << " time=" << std::setw(10) << time
              << " dequeued=" << (dequeued ? 1 : 0)
              << " enqueued=" << (enqueued ? 1 : 0)
              << " pipe=[";

        for (std::size_t index = 0; index < pipe.size(); ++index) {
            if (index != 0) {
                file_ << ", ";
            }
            write_stage(file_, pipe[index]);
        }

        file_ << "]\n";
        file_.flush();
    }

    // template <typename T>
    // void log_sink_consumed(std::size_t index, const T& value) {
    //     if (!enabled_) {
    //         return;
    //     }

    //     file_ << "sink index=" << index
    //           << " value=" << value << '\n';
    //     file_.flush();
    // }

private:
    bool enabled_{false};
    sc_core::sc_time clock_period_{sc_core::SC_ZERO_TIME};
    std::ofstream file_;

    template <typename T>
    static void write_stage(std::ostream& out, const std::optional<T>& stage) {
        if (stage) {
            out << *stage;
        } else {
            out << '-';
        }
    }

    template <typename Stage>
    static void write_stage(std::ostream& out, const std::vector<Stage>& pipe) {
        out << '[';
        for (std::size_t index = 0; index < pipe.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            write_stage(out, pipe[index]);
        }
        out << ']';
    }
};
