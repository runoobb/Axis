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
3. **示例系统正确性验证**：
    - `test/main.cpp` 示例系统的正确性必须从导出的 sink log/text 文件检查，不应依赖 `ScalarSink::complete()` 或任何 completion-like API。

# 派生硬件模块的开发原则
进行实现时，**必须**严格遵守以下规范：

- 派生硬件模块继承 `BaseHW`。`BaseHW` 只作为配置/约束基类，保存 `input_count_`、`output_count_`、`input_interval_`、`function_latency_`、`clock_period_`，并提供 `cycles_to_time()`。
- `input_interval_` 与 `function_latency_` 的单位均为时钟周期，不是 `sc_core::sc_time` 仿真时间。
- 派生类需要继承并使用 `BaseHW` 的 `input_interval_`、`function_latency_` 等配置特性。
- **不要变动alu.hpp, fifo.hpp, scalar_sink.hpp, scalar_source.hpp中的代码**

## 端口valid-ready握手协议与input_interval_语义

- BaseHW派生类对上游和下游都采用valid-ready握手模式。
- 模块之间使用Port `valid` / `ready` 建模valid-ready握手协议。
  - 与上游的valid-ready握手信号Port：from upstream(fus) `fus_valid`，to upstream(tus) `tus_ready`。
  - 与下游的valid-ready握手信号Port：from downstream(fds) `fds_ready`， to downstream(tds) `tds_valid`。

- 输入方面，一个模块可以有一个或多个输入端口，因此 `fus_valid` 使用按输入端口组织的 `sc_vector<sc_in<bool>>`。
  - 单个输入端口时，只要该输入端口的input_interval_冷却后(且当前模块流水线未满)，与上游模块握手成功后，上游数据可进入流水线。(fifo模块) 
  - 多个输入端口时，需要所有输入端口的input_interval_**全部**冷却后(且当前模块流水线未满)，与上游模块握手成功后，上游数据可进入流水线。输入端口之间存在互相等待的情况。(alu模块)

- 输出方面，一个模块只有一个输出端口。但是该输出端口可以连接至一个或多个下游模块的输入端口。
  - 输出端口连接至单个下游输入端口时，下游模块输入端口的ready信号直接用于握手。
  - 输出端口连接多个下游输入端口时，当下游模块输入端口的ready信号全部为true时，当前模块才能与所有下游模块共同握手。输出端口之间存在互相等待的情况。

- valid-ready port中channel类型使用 `sc_signal`。data Port中，channel类型也使用 `sc_signal` 。


## 统一流水线建模模式与正确性

派生硬件模块应以 `include/fifo.hpp` 的当前实现为模范：

- 每个模块使用一个时钟驱动的流水线主进程（通常命名为 `hw_pipe_sim_`）统一拥有并更新内部流水线状态。
- 当前实现中，`hw_pipe_sim_` 同时负责流水线状态推进、数据端口写出以及 ready/valid 信号更新，不再拆分额外的维护 ready/valid SC_METHOD函数进程。
- 流水线主进程的代码实现在时钟边沿后按流水线末级到首级的顺序建模，hw_pipe_sim_执行完成一次后，数据结构中存储的值模拟的是下一个时钟周期时序寄存器将要采样到的信号，而握手信号采用sc_signal作为实现的channel，由于SystemC的lazy-update特性，在下一仿真时间时下游组件在调用hw_pipe_sim_时，port.read()读取到的值是上一仿真时间时上游组件在hw_pipe_sim_中port.write()写入的值。
- 通过以上统一的流水线建模模式，确保在同一仿真时间下，各个组件SC_METHOD对clk.pos()信号敏感，在仿真内核不确定执行的背景下，维护了建模结果的正确性。


## function_latency_

- `function_latency_` 建模流水线延时。
- 实现function功能的组合电路在流水线级之间切分，流水线容量/打拍寄存器个数等于 `function_latency_`。
- 当流水线末级存在有效数据时，输出端口 `tds_valid` 拉高。




# 示例系统的构建

- 为了构建 `test/main.cpp` 中的测试系统，需要scalar_source和scalar_sink两个类，这两个类无需从 `BaseHW` 派生，独立开发。
- source类需要具有产生数据 `output_interval_` 和`function_latency_`特性，每隔`output_interval_`时钟周期，初始化值将进入hw_pipe_流水线。
- sink类需要具有消费数据 `input_interval_`和`function_latency_` 特性，每隔`input_interval_`时钟周期，输入端口ready信号设置为true，握手成功后，ready信号设置为false。数据进入流水线后经过function_latency_时钟周期，在流水线中沉没。

