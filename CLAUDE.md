# Agent开发环境
当前项目代码文件位于WSL中，编译与运行面向Linux平台，Claude Agent运行在Windows，通过PowerShell CLI访问WSL。

1. **环境隔离约束**：
   - 切勿在 Windows PowerShell CLI 中直接运行WSL中任何可执行文件。
   - **允许**在 Windows PowerShell CLI 中直接读写WSL中的文件。
   - 编译与运行、git的相关命令必须通过WSL环境中的Bash执行, 在Windows PowerShell CLI使用`wsl bash lc '' `对相关命令进行包装。

# 编译与验证规范 (Build & Verification Guidelines)

## 自动编译规则
当你需要通过编译代码来验证修改时，**必须**严格遵守以下规范，

1. **统一编译入口**：
    - 必须通过执行命令 `wsl bash -lc 'cd /home/kaywang/workspace/Axis && cmake -S . -B build && cmake --build build'` 进行编译。
    - 代码迭代后，必要时更改CMakeLists.txt
2. **SystemC安装路径**
    - SystemC 安装路径为：
    ```
    /opt/systemc
    ```

# 派生硬件模块的开发原则
进行实现时，**必须**严格遵守以下规范
    - 继承BaseHW类
    - 实现BaseHW类中function_enque_thread, function_deque_thread 和 transfer_thread的接口函数，这三个函数声明为overide
    - can_enque() can_deque() can_accept()这三个函数由派生类独立实现
    - 继承BaseHW类function_latency_, function_interval_, transfer_latency_的特性
    - 使用in_latch_ out_latch_ 描述模块之间的信号


# 派生类硬件模块的参考
''
template <typename T, typename Op = std::plus<T>>
class BinaryScalarOp : public BaseHW, sc_module {
public:
    tlm_utils::simple_target_socket<BinaryScalarOp> in0;
    tlm_utils::simple_target_socket<BinaryScalarOp> in1;
    tlm_utils::simple_initiator_socket<BinaryScalarOp> out;

    SC_HAS_PROCESS(BinaryScalarOp);

    BinaryScalarOp(
        sc_module_name name,
        sc_time function_interval,
        sc_time function_latency,
        std::size_t function_pipe_capacity,
        sc_time transfer_latency,
        Op op = Op{})
        : sc_module(name),
          in0("in0"),
          in1("in1"),
          out("out"),
          function_interval_(function_interval),
          function_latency_(function_latency),
          transfer_latency_(transfer_latency),
          function_pipe_capacity_(function_pipe_capacity),
          op_(op),
          trace_(std::string(this->name()) + ".txt")
    {
        if (function_pipe_capacity_ == 0) {
            throw std::invalid_argument("function pipeline capacity must be greater than zero");
        }

        in0.register_b_transport(this, &BinaryScalarOp::input0_b_transport);
        in1.register_b_transport(this, &BinaryScalarOp::input1_b_transport);

        log_state("CREATE BinaryScalarOp");
        SC_THREAD(function_trigger_thread);
        SC_THREAD(transfer_thread);
    }

private:

    sc_time function_interval_;
    sc_time function_latency_;
    sc_time transfer_latency_;
    std::size_t function_pipe_capacity_;
    std::size_t function_pipe_inflight_ = 0;
    Op op_;
    std::ofstream trace_;

    std::deque<T> in_latch_[2];
    std::deque<T> out_latch_;
    std::deque<T> function_pipe_;

    sc_event out_latch_produced_ev_; // outport valid signal
    sc_event out_latch_consumed_ev_; // marks when the out_latch_ has been sampled and function pipe can advance
    sc_event in_latch_produced_ev_[2]; // inport valid signal
    sc_event in_latch_consumed_ev_[2]; // inport ready signal

    std::string function_pipe_trace() const
    {
        std::ostringstream oss;
        oss << trace_deque(function_pipe_) << "/inflight=" << function_pipe_inflight_;
        return oss.str();
    }

    void log_state(const std::string& event)
    {
        trace_log_prefix(trace_, event);
        trace_ << " | " << std::left
               << std::setw(24) << ("in_latch0=" + trace_deque(in_latch_[0]))
               << std::setw(24) << ("in_latch1=" + trace_deque(in_latch_[1]))
               << std::setw(36) << ("function_pipe=" + function_pipe_trace())
               << "out_latch=" << trace_deque(out_latch_)
               << std::right << std::endl;
    }

    void input0_b_transport(tlm_generic_payload& trans, sc_time& delay)
    {
        input_b_transport(0, trans, delay);
    }

    void input1_b_transport(tlm_generic_payload& trans, sc_time& delay)
    {
        input_b_transport(1, trans, delay);
    }

    void input_b_transport(int port_id, tlm_generic_payload& trans, sc_time& delay)
    {
        if (trans.get_command() != TLM_WRITE_COMMAND) {
            trans.set_response_status(TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        if (trans.get_data_length() != sizeof(T) || trans.get_data_ptr() == nullptr) {
            trans.set_response_status(TLM_BURST_ERROR_RESPONSE);
            return;
        }

        T value{};
        std::memcpy(&value, trans.get_data_ptr(), sizeof(T));
        {
            std::ostringstream oss;
            oss << "INPUT_REQUEST in" << port_id << " value=" << value << " delay=" << delay;
            log_state(oss.str());
        }

        wait(delay);
        delay = SC_ZERO_TIME;

        while (!can_accept(port_id)) {
            std::ostringstream oss;
            oss << "INPUT_WAIT in" << port_id << " value=" << value;
            log_state(oss.str());
            wait(in_latch_consumed_ev_[port_id]);
        }

        in_latch_[port_id].push_back(value);
        {
            std::ostringstream oss;
            oss << "INPUT_ACCEPTED in" << port_id << " value=" << value;
            log_state(oss.str());
        }
        in_latch_produced_ev_[port_id].notify(SC_ZERO_TIME);
        trans.set_response_status(TLM_OK_RESPONSE);
    }

    void function_trigger_thread()
    {
        while (true) {
            wait(function_interval_);

            while (!can_trigger_function()) {
                wait(in_latch_produced_ev_[0] | in_latch_produced_ev_[1] | out_latch_consumed_ev_);
            }

            T a = in_latch_[0].front();
            T b = in_latch_[1].front();
            in_latch_[0].pop_front();
            in_latch_[1].pop_front();
            in_latch_consumed_ev_[0].notify(SC_ZERO_TIME);
            in_latch_consumed_ev_[1].notify(SC_ZERO_TIME);

            T result = op_(a, b);
            function_pipe_.push_back(result);
            ++function_pipe_inflight_;
            {
                std::ostringstream oss;
                oss << "FUNCTION_START in0=" << a << " in1=" << b
                    << " result=" << result
                    << " latency=" << function_latency_;
                log_state(oss.str());
            }

            sc_spawn(sc_bind(&BinaryScalarOp::function_pipe_thread, this));
        }
    }

    void function_pipe_thread()
    {
        wait(function_latency_);

        // Dequeue the result befor wait(out_latch_consumed_ev_) to keep order between different function_pipe_thread()
        T result = function_pipe_.front();
        function_pipe_.pop_front();

        while (!can_deque_function()) {
            {
                std::ostringstream oss;
                oss << "FUNCTION_READY_BLOCKED result=" << function_pipe_.front();
                log_state(oss.str());
            }
            wait(out_latch_consumed_ev_);
        }


        --function_pipe_inflight_;
        out_latch_.push_back(result);
        {
            std::ostringstream oss;
            oss << "FUNCTION_FINISH result=" << result;
            log_state(oss.str());
        }
        out_latch_produced_ev_.notify(SC_ZERO_TIME);
    }

    void transfer_thread()
    {
        while (true) {
            wait(out_latch_produced_ev_);

            T value = out_latch_.front();
            unsigned char data[sizeof(T)];
            std::memcpy(data, &value, sizeof(T));

            tlm_generic_payload trans;
            trans.set_command(TLM_WRITE_COMMAND);
            trans.set_address(0);
            trans.set_data_ptr(data);
            trans.set_data_length(sizeof(T));
            trans.set_streaming_width(sizeof(T));
            trans.set_byte_enable_ptr(nullptr);
            trans.set_dmi_allowed(false);
            trans.set_response_status(TLM_INCOMPLETE_RESPONSE);

            sc_time delay = transfer_latency_;
            {
                std::ostringstream oss;
                oss << "TRANSFER_START value=" << value << " delay=" << delay;
                log_state(oss.str());
            }
            out->b_transport(trans, delay);

            if (trans.is_response_error()) {
                SC_REPORT_ERROR(name(), trans.get_response_string().c_str());
            }

            out_latch_.pop_front();
            {
                std::ostringstream oss;
                oss << "TRANSFER_END value=" << value;
                log_state(oss.str());
            }
            {
                std::ostringstream oss;
                oss << "OUTPUT_CONSUMED value=" << value;
                log_state(oss.str());
            }
            out_latch_consumed_ev_.notify(SC_ZERO_TIME);
        }
    }

    bool can_enque_function() const
    {
        return !in_latch_[0].empty()
            && !in_latch_[1].empty()
            && function_pipe_inflight_ < function_pipe_capacity_;
    }

    bool can_deque_function() const{
        return out_latch_.empty() && function_pipe_inflight_ > 0;
    }

    bool can_accept(int port_id) const
    {
        return in_latch_[port_id].empty();
    }
};
''
