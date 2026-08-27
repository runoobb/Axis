#include "scalar_source.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

ScalarSource::ScalarSource(sc_core::sc_module_name name,
                           std::vector<int> values,
                           std::size_t interval,
                           sc_core::sc_time clock_period,
                           std::vector<std::size_t> transfer_latency)
    : BaseHW(name,
             0,
             transfer_latency.size(),
             interval,
             0,
             clock_period,
             transfer_latency),
      out("out"),
      values_(std::move(values)) {
    if (transfer_latency_.empty()) {
        throw std::invalid_argument("ScalarSource requires at least one output");
    }

    out.init(output_count_);
    SC_THREAD(run);
}

bool ScalarSource::can_enque() const {
    return !active_;
}

bool ScalarSource::can_deque() const {
    return active_;
}

bool ScalarSource::can_accept(std::size_t) const {
    return false;
}

void ScalarSource::broadcast(int value) {
    std::vector<std::unique_ptr<sc_core::sc_event>> completed;
    sc_core::sc_event_and_list all_done;

    for (std::size_t i = 0; i < out.size(); ++i) {
        completed.push_back(std::make_unique<sc_core::sc_event>());
        all_done &= *completed.back();
        auto* done = completed.back().get();

        sc_core::sc_spawn([this, value, i, done] {
            unsigned char data[sizeof(int)];
            std::memcpy(data, &value, sizeof(int));

            tlm::tlm_generic_payload trans;
            BaseHW::prepare_write<int>(trans, data);
            sc_core::sc_time delay = cycles_to_time(transfer_latency_[i]);
            out[i]->b_transport(trans, delay);

            if (trans.is_response_error()) {
                SC_REPORT_ERROR(name(), trans.get_response_string().c_str());
            }

            done->notify(sc_core::SC_ZERO_TIME);
        });
    }

    wait(all_done);
}

void ScalarSource::function_enque_thread() {}

void ScalarSource::function_deque_thread() {}

void ScalarSource::transfer_thread() {}

void ScalarSource::run() {
    for (int value : values_) {
        wait(cycles_to_time(function_interval_));
        active_ = true;
        broadcast(value);
        active_ = false;
    }
}
