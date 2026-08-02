# 该项目编译遵循 compile.md 中定义

## 编译环境

该项目使用 WSL 中的 CMake 工具链进行 Linux 编译。

SystemC 安装路径为：

```bash
/opt/systemc
```

项目的 `CMakeLists.txt` 已按该路径配置 SystemC 头文件和库路径。

## 编译命令

在 Windows 主机 PowerShell CLI 访问该项目时，通过执行以下命令完成配置和编译：

```bash
wsl bash -lc 'cd /home/kaywang/workspace/Axis && cmake -S . -B build && cmake --build build'
```

## 成功结果

成功编译时应看到类似输出：

```text
[100%] Built target axis_test
```

编译过程中可能出现 WSL 路径转换提示或 SystemC `SC_HAS_PROCESS` deprecated warning；只要最终出现 `[100%] Built target axis_test`，即表示编译成功。
