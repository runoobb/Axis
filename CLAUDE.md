# Agent开发环境
当前项目代码文件位于WSL中，编译与运行面向Linux平台，Claude Agent运行在Windows，通过PowerShell CLI访问WSL。

1. **环境隔离约束**：
   - 切勿在 Windows PowerShell CLI 中直接运行WSL中任何可执行文件。
   - **允许**在 Windows PowerShell CLI 中直接读写WSL中的文件。
   - 编译与运行、git的相关命令必须通过WSL环境中的Bash执行, 在Windows PowerShell CLI使用`wsl bash -lc ''`对相关命令进行包装。

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
进行实现时，**必须**严格遵守以下规范：

- 派生硬件模块继承 `BaseHW`。`BaseHW` 只作为配置/约束基类，保存 `input_count_`、`output_count_`、`input_interval_`、`function_latency_`、`clock_period_`，并提供 `cycles_to_time()`。
- `input_interval_` 与 `function_latency_` 的单位均为时钟周期，不是 `sc_core::sc_time` 仿真时间。
- 派生类需要继承并使用 `BaseHW` 的 `input_interval_`、`function_latency_` 等配置特性。

## valid-ready握手协议

- BaseHW派生类对上游和下游都采用valid-ready握手模式。
- 模块之间使用Port `sc_in valid, sc_out ready` 建模valid-ready握手协议。
  - 与上游的valid-ready握手信号Port：`sc_in fus_valid, sc_out tus_ready`
  - 与下游的valid-ready握手信号Port：`sc_in fds_ready, sc_out tds_valid`
- valid-ready Port中Channel类型使用 `sc_signal`。
- input-output Port中Channel类型使用 `sc_fifo`。
- 不再在 `BaseHW` 中声明 `hw_enque_()`、`hw_deque_()`、`hw_pipe_sim_()` 虚接口；派生类按自身结构注册 SystemC 进程。

## 统一流水线建模模式

派生硬件模块应以 `include/fifo.hpp` 的当前实现为模范：

- 每个模块使用一个时钟驱动的流水线主线程（通常命名为 `hw_pipe_sim_`）统一拥有并更新内部流水线状态。
- 流水线主线程在时钟边沿后按确定顺序处理：
  1. 若输出端 `tds_valid` 与所有下游 `fds_ready` 完成握手，向所有下游 `sc_fifo` 写出尾级数据、清空尾级并通知 `deque_`。
  2. 从尾到头移动流水线中可前进的数据。
  3. 若输入端 `fus_valid` 与 `tus_ready` 完成握手且首级可接收，读取输入 `sc_fifo`、执行组合功能、写入首级并通知 `enque_`。
  4. 通知 `pipe_updated_`，使ready/valid展示线程更新信号。
- `inport_ready_` / `outport_valid_` 等线程只负责握手信号展示，不直接修改流水线状态，不直接读写数据 FIFO。
- `enque_`、`deque_`、`pipe_updated_` 等事件用于ready/valid展示线程与流水线主线程同步。

## function_latency_

- `function_latency_` 建模流水线延时。
- 实现function功能的组合电路在流水线级之间切分，流水线容量/打拍寄存器个数等于 `function_latency_`。
- 当所有输入端口完成握手后触发计算，经过 `function_latency_` 对应的流水线推进后，输出端口 `tds_valid` 拉高。

## input_interval_(输入端口)

- 每一个输入端口都具有独立握手冷却特性，`input_interval_[port]` 表示该输入端口完成一次握手后，到该端口再次允许拉高 `tus_ready` 之间需要等待的时钟周期数。
- `input_interval_` 必须通过时钟边沿计数实现，不应使用 `sc_time` 延时替代。
- `tus_ready` 除受 `input_interval_` 冷却影响外，还受流水线容量/首级可接收状态影响。当流水线因下游阻塞无法接收新输入时，输入端口必须保持 not-ready，直到流水线腾出空间。
- 对多输入模块，只有满足该模块function语义所需的输入端口均可完成握手时，才应提交一次有效function计算。

参考结构：
```cpp
SC_THREAD(hw_pipe_sim_);
SC_THREAD(inport_ready_);
SC_THREAD(outport_valid_);

void hw_pipe_sim_() {
    while (true) {
        wait(clk.posedge_event());
        wait(sc_core::SC_ZERO_TIME);

        if (hw_pipe_.back() && tds_valid.read() && all_downstream_ready()) {
            write_outputs(*hw_pipe_.back());
            hw_pipe_.back().reset();
            deque_.notify(sc_core::SC_ZERO_TIME);
        }

        move_pipeline_tail_to_head();

        if (input_handshake_complete() && !hw_pipe_.front()) {
            hw_pipe_.front() = hw_function_(read_inputs());
            enque_.notify(sc_core::SC_ZERO_TIME);
        }

        pipe_updated_.notify(sc_core::SC_ZERO_TIME);
    }
}
```

## 输出端口

- 不存在 `output_interval_`。
- 对于输出端口，存在连接到多个下游输入端口的情况，这时输出端口的 `tds_valid` 需要和所有下游输入的 `fds_ready` 同时握手，即多个下游 `ready` 信号进行 and 运算。
- 输出 `valid` 展示线程只根据尾级是否有效拉高 `tds_valid`，并在 `deque_` 事件后拉低，不直接写出数据 FIFO。

参考结构：
```cpp
void outport_valid_() {
    tds_valid.write(false);
    wait(sc_core::SC_ZERO_TIME);

    while (true) {
        if (hw_pipe_.back()) {
            tds_valid.write(true);
            wait(deque_);
            tds_valid.write(false);
        } else {
            tds_valid.write(false);
            wait(pipe_updated_);
        }
    }
}
```

## 延时单元

- 延时单元是一类特殊的 `BaseHW` 派生类，行为与 FIFO 一致，一个输入端口，一个输出端口。
- 输入端口的 `input_interval_` 为0，即ready信号是否拉高只与容量/首级可接收状态有关。
- 输出端口的valid信号行为与其他 `BaseHW` 派生类一致。

# 示例系统的构建

- 为了构建 `test/main.cpp` 中的测试系统，需要生成source和sink两个类，这两个类无需从 `BaseHW` 派生，独立开发。
- source类需要具有产生数据 `interval_` 特性，并采用ready-valid握手机制。source应先持有待发送值并拉高valid，仅在时钟采样点确认所有下游ready后，才向所有下游 `sc_fifo` 写入该值。
- sink类只需要具有消费数据 `interval_` 特性，并采用ready-valid握手机制。sink应由单一时钟线程拥有输入 FIFO 读取和消费状态更新，ready信号只展示容量/冷却状态。
