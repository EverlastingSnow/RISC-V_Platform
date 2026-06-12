# RISC-V_Platform 后端说明

`RISC-V_Platform` 是 RISC-V 五级流水线虚拟仿真实验平台的后端工程，负责程序编译、ELF 加载、RV64 流水线模拟、教学差分测试、WebSocket 会话管理和使用统计。前端通过 HTTP 编译接口与 WebSocket 控制接口访问本后端，逐周期获取 IF、ID、EX、MEM、WB 各阶段信号并进行可视化展示。

## 项目定位

- **核心目标**：为教学实验提供可逐周期观察的 RV64 五级流水线模拟后端。
- **服务对象**：Vue 前端、WebSocket 客户端、教学差分测试面板、RISC-V ELF 测试脚本。
- **主要职责**：编译汇编源码、加载 ELF/教学用例、执行流水线、输出结构化 JSON 信号、隔离多用户模拟器状态。
- **复位地址**：`0x80000000`。
- **默认内存**：`256 MiB`。
- **寄存器宽度**：`XLEN = 64`，共 `32` 个通用寄存器。

## 技术栈

| 类别          | 技术                               | 说明                                                      |
| ------------- | ---------------------------------- | --------------------------------------------------------- |
| 模拟核心      | C++20                              | `RISCVSimulator`、译码、CSR、内存、寄存器堆、流水线寄存器 |
| 构建系统      | CMake 3.20+                        | 构建静态库和 3 个可执行程序                               |
| 服务层        | Python 3                           | WebSocket、HTTP 编译服务、统计服务、兼容 HTTP 示例服务    |
| 通信协议      | stdin/stdout JSON + WebSocket JSON | Python 服务通过子进程命令驱动 C++ 模拟器                  |
| 数据存储      | SQLite                             | `stats.py` 使用 `stats.db` 记录访问次数和会话时长         |
| RISC-V 工具链 | `riscv-none-elf-gcc`               | 编译前端提交的汇编源码为 ELF                              |
| Node 工具     | `wscat`                            | WebSocket 调试依赖，记录在 `package.json`                 |

## 架构概览

```text
┌──────────────────────────────────────┐
│ 前端 / WebSocket 客户端               │
│ 单步、运行、差分测试、查看寄存器       │
└───────────────────┬──────────────────┘
                    │ JSON over WebSocket
┌───────────────────▼──────────────────┐
│ api_websocket_server.py               │
│ 每个用户创建独立 riscv_sim_server 进程 │
│ 同时启动 /api/stats 统计 HTTP 服务     │
└───────────────────┬──────────────────┘
                    │ stdin/stdout 命令
┌───────────────────▼──────────────────┐
│ src/riscv_sim_server.cpp              │
│ load / step / run / signals / difftest│
└───────────────────┬──────────────────┘
                    │ C++ API
┌───────────────────▼──────────────────┐
│ riscv_core                            │
│ simulator / decoder / csr / memory    │
└──────────────────────────────────────┘

┌──────────────────────────────────────┐
│ api_compile_server.py                 │
│ /api/compile 编译汇编源码并返回 ELF    │
└──────────────────────────────────────┘
```

### 多用户隔离

`api_websocket_server.py` 为每个 WebSocket 连接生成独立 `client_id`，并创建单独的 `CppSimulator` 子进程。这样不同用户加载程序、运行状态、差分测试状态互不影响。默认最大并发模拟器数量为 `100`，断开连接后会自动停止并销毁对应进程。

### C++ 核心边界

`riscv_core` 是后端的核心库，只负责模拟器状态与指令执行。HTTP、WebSocket、文件上传、统计和前端协议适配放在 Python 层，不混入核心模拟逻辑。

## 功能特性

- **RV64 流水线模拟**：维护 IF/ID、ID/EX、EX/MEM、MEM/WB 四类流水线寄存器。
- **逐周期状态输出**：输出 `cycle`、`pc`、流水线寄存器、执行阶段、访存阶段、写回阶段、寄存器堆和数据内存信号。
- **ELF 加载**：支持识别 ELF32/ELF64，小端 RISC-V ELF，并加载 `PT_LOAD` 段或可分配 section。
- **汇编编译服务**：将前端提交的 `.S`/汇编文本编译为 ELF，并返回 Base64 编码 ELF 和 linker map/list 文本。
- **教学测试用例**：内置指令序列测试和 `teaching/*.elf` 文件，覆盖寄存器写、ALU 操作数、内存访问、分支和综合流水线场景。
- **差分测试**：支持 `RegWrite`、`ALUSrc`、`MemRead`、`MemWrite`、`Branch` 信号教学输入与 golden 结果对比。
- **统计服务**：记录访问次数、会话时长和按日期聚合的使用数据。
- **RISC-V 测试入口**：`run_riscv_tests` 可运行 `rv64ui`、`rv64um`、`rv64mi`、`rv64si` 组或指定 ELF 文件。

## 指令与模拟范围

当前译码器和模拟器围绕 RV64 设计，实际支持范围应以 `include/riscv/decoder.h`、`src/decoder.cpp` 和 `src/simulator.cpp` 为准。

### 已覆盖的主要指令类型

- **RV64I 基础整数**：`LUI`、`AUIPC`、`JAL`、`JALR`、条件分支、Load/Store、立即数 ALU、寄存器 ALU。
- **RV64 W 指令**：`ADDIW`、`SLLIW`、`SRLIW`、`SRAIW`、`ADDW`、`SUBW`、`SLLW`、`SRLW`、`SRAW`。
- **M 扩展**：`MUL`、`MULH`、`MULHSU`、`MULHU`、`DIV`、`DIVU`、`REM`、`REMU` 及 W 变体。
- **系统与 CSR**：`ECALL`、`EBREAK`、`MRET`、`SRET`、`WFI`、`SFENCE.VMA`、`CSRRW`、`CSRRS`、`CSRRC` 及立即数变体。
- **内存宽度**：支持 8/16/32/64 位读写，包含非对齐访问的慢路径处理。

### 需要注意的边界

- `api_server.py` 返回模拟数据，适合早期联调或兼容用途，不是当前真实模拟链路的主入口。
- `scripts/README_riscv_tests.md` 中部分说明较早，实际代码已经包含 M 扩展、CSR 和系统指令相关实现；若测试失败，应结合当前实现和具体 ELF 行为判断。
- 流水线实现面向教学可视化，重点是周期状态展示与控制信号教学，不等同于完整工业级处理器模型。

## 目录结构

```text
RISC-V_Platform/
├── include/riscv/
│   ├── types.h                # 基础整数类型、XLEN、内存大小、复位地址
│   ├── decoder.h              # 指令格式、指令类型、译码结果声明
│   ├── pipeline.h             # 流水线寄存器、用户控制信号、阶段状态
│   ├── simulator.h            # RISCVSimulator 主类
│   ├── memory.h               # 内存模型
│   ├── register_file.h        # 32 个通用寄存器
│   ├── csr.h                  # CSR 与特权模式状态
│   ├── elf_loader.h           # ELF32/ELF64 加载器
│   ├── teaching_tests.h       # 内置教学指令序列
│   ├── teaching_elf_config.h  # 教学 ELF 列表
│   └── riscv_config.h         # 教学目录配置
├── src/
│   ├── simulator.cpp          # 五级流水线执行核心
│   ├── decoder.cpp            # RV64 指令译码和汇编字符串转换
│   ├── memory.cpp             # 内存读写与地址转换
│   ├── register_file.cpp      # 寄存器堆实现
│   ├── csr.cpp                # CSR 读写与中断状态
│   ├── elf_loader.cpp         # ELF 加载实现
│   ├── main.cpp               # 命令行二进制程序模拟入口
│   ├── riscv_sim_server.cpp   # stdin/stdout JSON 模拟器服务
│   └── run_riscv_tests.cpp    # riscv-tests 运行入口
├── teaching/                  # 教学用 ELF 文件
├── isa/                       # riscv-tests、链接脚本和教学测试构建脚本
├── scripts/                   # 测试说明文档
├── api_websocket_server.py    # 主 WebSocket 服务，管理多用户模拟器实例
├── api_compile_server.py      # 汇编编译 HTTP 服务
├── api_server.py              # 早期模拟数据 HTTP 服务
├── api_web_old.py             # 旧版 WebSocket 服务实现
├── stats.py                   # SQLite 统计模块
├── config.py                  # 端口、工具链、目录和编译参数配置
├── test_compile.py            # 工具链编译冒烟测试
├── CMakeLists.txt             # C++ 构建配置
├── .env.example               # 环境变量示例
└── package.json               # wscat 调试依赖
```

## 快速开始

### 1. 准备环境

需要安装：

- CMake `3.20+`。
- 支持 C++20 的编译器。
- Python `3.9+`。
- RISC-V 裸机工具链，提供 `riscv-none-elf-gcc`、`riscv-none-elf-objcopy`。
- Node.js，仅在需要 `wscat` 调试时使用。

Python 依赖至少包括：

```bash
pip install websockets
```

如需 WebSocket 命令行调试：

```bash
npm install
```

### 2. 配置环境变量

默认配置位于 `config.py`：

```text
RISCV_TOOLCHAIN_PATH = RISC-V_Platform/xpack-riscv-none-elf-gcc-15.2.0-1/bin
API_SERVER_PORT      = 8080
WEBSOCKET_PORT       = 8081
STATS_PORT           = 8082
COMPILE_SERVER_PORT  = 8083
ARCH                 = rv64g
ABI                  = lp64d
```

Windows PowerShell 示例：

```powershell
$env:RISCV_TOOLCHAIN_PATH="E:\rv64Pipeline\RISC-V_Platform\xpack-riscv-none-elf-gcc-15.2.0-1\bin"
$env:SIM_SERVER_PATH="E:\rv64Pipeline\RISC-V_Platform\build\riscv_sim_server.exe"
$env:WEBSOCKET_PORT="8081"
$env:STATS_PORT="8082"
$env:COMPILE_SERVER_PORT="8083"
```

Linux/macOS 示例：

```bash
export RISCV_TOOLCHAIN_PATH=/opt/riscv/bin
export SIM_SERVER_PATH=$PWD/build/riscv_sim_server
export WEBSOCKET_PORT=8081
export STATS_PORT=8082
export COMPILE_SERVER_PORT=8083
```

也可以参考 `.env.example` 中列出的变量，但当前 Python 脚本不会自动加载 `.env` 文件，需要在 shell 中显式设置。

### 3. 构建 C++ 后端

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

构建产物：

- `build/libriscv_core.a`：模拟核心静态库。
- `build/riscv_sim` 或 `build/riscv_sim.exe`：命令行模拟器。
- `build/riscv_sim_server` 或 `build/riscv_sim_server.exe`：WebSocket 服务调用的模拟器子进程。
- `build/run_riscv_tests` 或 `build/run_riscv_tests.exe`：RISC-V ELF 测试运行器。

### 4. 启动 WebSocket 模拟服务

```bash
python api_websocket_server.py
```

默认监听：

- WebSocket：`ws://localhost:8081`
- 统计 HTTP：`http://localhost:8082/api/stats`

启动后每个客户端连接都会创建一个独立模拟器进程。若找不到 `riscv_sim_server`，检查 `SIM_SERVER_PATH` 或确认 CMake 构建已完成。

### 5. 启动编译服务

```bash
python api_compile_server.py
```

默认接口：

- `POST http://localhost:8083/api/compile`

请求可使用 JSON：

```json
{
  "source": ".text\n.globl _start\n_start:\n  addi x1, x0, 1\n  ebreak\n",
  "filename": "main.S"
}
```

响应成功时会返回：

- `success: true`
- `elf_data`：Base64 编码 ELF。
- `filename`：源文件名。
- `list_content`：链接 map/list 内容。

### 6. 命令行模拟器运行

`riscv_sim` 直接加载二进制文件，不加载 ELF：

```bash
./build/riscv_sim program.bin 10000
```

Windows 示例：

```powershell
.\build\riscv_sim.exe program.bin 10000
```

## WebSocket 与模拟器命令

### WebSocket JSON 命令

`api_websocket_server.py` 接收 JSON，并转发为 `riscv_sim_server` 的文本命令。

| 命令               | 参数                    | 说明                                      |
| ------------------ | ----------------------- | ----------------------------------------- |
| `step`             | 无                      | 执行一个时钟周期                          |
| `run`              | 无                      | 连续运行，服务端持续推送 `status: update` |
| `stop`             | 无                      | 停止连续运行                              |
| `reset`            | 无                      | 重置当前客户端模拟器                      |
| `load`             | `path`                  | 加载服务器本地 ELF 文件                   |
| `load_elf_binary`  | `elf_data`              | 加载 Base64 编码 ELF                      |
| `get_signals`      | 无                      | 获取当前流水线信号                        |
| `get_registers`    | 无                      | 获取 32 个通用寄存器                      |
| `enable_difftest`  | `signals`, `shadowMode` | 启用差分测试                              |
| `disable_difftest` | 无                      | 禁用差分测试                              |
| `set_user_signal`  | `signalName`, `value`   | 提交用户判断的控制信号                    |
| `continue`         | 无                      | 差异后继续                                |
| `load_test`        | `testName`              | 加载内置教学指令序列                      |
| `list_tests`       | 无                      | 列出内置教学指令序列                      |
| `load_elf_test`    | `testName`              | 加载 `teaching` 目录中的教学 ELF          |
| `list_elf_tests`   | 无                      | 列出教学 ELF                              |

示例：

```json
{"command":"list_elf_tests"}
```

```json
{"command":"load_elf_test","testName":"teaching_regwrite"}
```

```json
{"command":"enable_difftest","signals":"RegWrite ALUSrc MemRead MemWrite Branch","shadowMode":true}
```

### C++ stdio 命令

`riscv_sim_server` 直接从 stdin 读取命令，每行一个命令，stdout 输出单行 JSON。

| 命令                                      | 说明                    |
| ----------------------------------------- | ----------------------- |
| `load <path>`                             | 加载 ELF 文件           |
| `load_test <name>`                        | 加载内置教学指令序列    |
| `list_tests [scenario]`                   | 列出内置教学测试        |
| `load_elf_test <name>`                    | 加载教学 ELF            |
| `list_elf_tests [scenario]`               | 列出教学 ELF            |
| `step`                                    | 执行一个周期            |
| `run`                                     | 运行到停止              |
| `reset`                                   | 重置模拟器和差分状态    |
| `signals`                                 | 输出当前流水线信号 JSON |
| `registers`                               | 输出寄存器数组          |
| `enable_difftest [--shadow] <signals...>` | 启用差分测试            |
| `disable_difftest`                        | 关闭差分测试            |
| `set_user_signal <name> <true|false>`     | 设置用户控制信号        |
| `skip_signal_input`                       | 跳过当前信号输入        |
| `continue`                                | 清除差异状态继续        |
| `load_elf_binary <hex>`                   | 加载十六进制字节串      |
| `ping`                                    | 返回 `pong`             |
| `quit`                                    | 退出服务进程            |

## 流水线状态 JSON

`signals` 和 `step` 返回的数据供前端直接渲染。主要字段包括：

- `cycle`：当前周期数。
- `pc`：当前取指 PC。
- `if_id`：取指到译码流水线寄存器，包含 `pc`、`inst`、`instruction`、`asm`、跳转目标、是否 allow-to-go。
- `id_ex`：译码到执行流水线寄存器，包含源寄存器地址和值、目的寄存器、立即数、汇编字符串。
- `execute`：执行阶段 ALU 输入、ALU 结果和功能类型。
- `ex_mem`：执行到访存流水线寄存器，包含分支结果、访存地址、写数据、访存读写使能。
- `mem_wb`：访存到写回流水线寄存器，包含写回值、写回地址、写回使能。
- `writeback`：调试提交信息，如 `debug_commit`、`debug_pc`、`debug_wb_rf_wdata`。
- `regfile`：当前读写寄存器端口信息。
- `datamem`：数据内存使能、地址、读写数据。
- `halted` 和 `halt_reason`：程序是否停止以及停止原因。

## 编译服务细节

`api_compile_server.py` 支持两种请求格式：

- `application/json`：读取 `source` 或 `code` 字段。
- `multipart/form-data`：读取上传文件内容。

编译命令由 `config.py` 生成，关键参数：

```text
-march=rv64g
-mabi=lp64d
-static
-mcmodel=medany
-fvisibility=hidden
-nostdlib
-nostartfiles
-T isa/link.ld
-I isa
isa/start.S
```

编译超时时间为 `30` 秒。失败时返回 `error` 和 `line_errors`，成功时返回 ELF 和 list 内容。

## 教学测试与差分测试

### 内置教学指令序列

`include/riscv/teaching_tests.h` 内置了无需 ELF 文件的测试序列：

| 名称                | 场景        | 目的                           |
| ------------------- | ----------- | ------------------------------ |
| `regwrite_basics`   | `scenario1` | 观察哪些指令写寄存器           |
| `alusrc_operands`   | `scenario2` | 区分寄存器操作数和立即数操作数 |
| `mem_load_store`    | `scenario3` | 观察 Load/Store 控制信号       |
| `branch_control`    | `scenario4` | 观察条件分支和 PC 变化         |
| `jump_jal`          | `scenario4` | 观察 JAL 跳转和返回地址写入    |
| `pipeline_overview` | `all`       | 综合观察流水线执行             |
| `load_variants`     | `scenario3` | 观察不同 Load 宽度和扩展方式   |
| `store_variants`    | `scenario3` | 观察不同 Store 宽度            |

### 教学 ELF 文件

`include/riscv/teaching_elf_config.h` 映射 `teaching/` 目录下的 ELF：

- `teaching_regwrite`、`teaching_regwrite_v2`
- `teaching_alusrc`、`teaching_alusrc_v2`
- `teaching_mem`、`teaching_mem_v2`
- `teaching_branch`、`teaching_branch_v2`
- `teaching_pipeline`

如果教学 ELF 不在默认目录，可设置：

```bash
export RISCV_TEACHING_DIR=/path/to/teaching
```

Windows PowerShell：

```powershell
$env:RISCV_TEACHING_DIR="E:\rv64Pipeline\RISC-V_Platform\teaching"
```

### 差分测试信号

当前差分测试聚焦以下控制信号：

- `RegWrite`：指令是否写回通用寄存器。
- `ALUSrc`：ALU 第二操作数是否来自立即数。
- `MemRead`：是否读数据内存。
- `MemWrite`：是否写数据内存。
- `Branch`：是否为条件分支。

普通用户输入模式会在需要时暂停并返回 `need_signal_input`；shadow 模式会建立影子模拟器，对比用户信号影响下的写回或执行结果。

## 测试

### 构建验证

```bash
cmake --build build --config Release
```

### 工具链冒烟测试

```bash
python test_compile.py
```

该脚本会创建临时汇编文件，使用 `riscv-none-elf-gcc` 编译，并调用 `riscv-none-elf-objdump` 输出反汇编片段。

### RISC-V ELF 测试

运行指定 ELF：

```bash
./build/run_riscv_tests path/to/test.elf
```

运行 `isa` 目录下指定组：

```bash
./build/run_riscv_tests --isa rv64ui
./build/run_riscv_tests --isa rv64ui,rv64um
```

运行目录下全部 ELF：

```bash
./build/run_riscv_tests --dir isa
```

Windows 示例：

```powershell
.\build\run_riscv_tests.exe --isa rv64ui
.\build\run_riscv_tests.exe --dir .\isa
```

通过判定逻辑：

- `ECALL` 停机且 `a0 == 0` 判定为 PASS。
- 非法指令、`EBREAK`、非零 `a0` 或超时会输出 FAIL/TIMEOUT/OTHER。
- 每个测试最大执行 `500000` 周期。

### WebSocket 手动测试

安装 `wscat` 后：

```bash
npx wscat -c ws://localhost:8081
```

然后发送：

```json
{"command":"list_tests"}
```

```json
{"command":"load_test","testName":"pipeline_overview"}
```

```json
{"command":"step"}
```

## 运行统计

`api_websocket_server.py` 启动时会初始化 `stats.db`，并在 `STATS_PORT` 上提供统计接口。

| 接口                                         | 方法 | 说明                               |
| -------------------------------------------- | ---- | ---------------------------------- |
| `/api/stats`                                 | GET  | 获取总访问时长、访问次数和每日统计 |
| `/api/stats?start=YYYY-MM-DD&end=YYYY-MM-DD` | GET  | 按日期范围查询                     |
| `/api/stats/reset`                           | POST | 清空统计数据                       |

统计模块使用 SQLite WAL 模式，并对会话时长做短周期聚合写入。

## 开发工作流

1. 修改 C++ 核心后，优先运行 `cmake --build build --config Release`。
2. 修改译码或执行语义后，运行 `run_riscv_tests` 的相关 ISA 组。
3. 修改 WebSocket 协议后，同时检查 `api_websocket_server.py` 和 `src/riscv_sim_server.cpp` 的命令/响应结构。
4. 修改编译参数后，运行 `python test_compile.py` 和一次前端编译请求。
5. 修改教学场景后，同步更新 `teaching_tests.h`、`teaching_elf_config.h`、`teaching/` 资源和前端场景文案。
6. 提交前不要依赖已有 `build/`、`__pycache__/`、`stats.db` 等本地生成物验证新环境可用性。

## 编码规范

- C++ 核心保持模块边界清晰：译码、执行、内存、CSR、ELF 加载不要相互混杂职责。
- 新增指令时同时更新 `InstructionKind`、`decode()`、`to_string()`、`to_asm_string()` 和 `simulator.cpp` 执行逻辑。
- 新增前端可视化字段时，应保持 JSON 字段稳定，避免破坏已有前端组件。
- Python 服务层只做协议、进程、会话和统计管理，不在 Python 中重复实现模拟器语义。
- 错误响应统一使用 JSON，至少包含 `status: "error"` 和 `message`。
- 教学差分信号名称应保持与前端一致，例如 `RegWrite`、`ALUSrc`、`MemRead`、`MemWrite`、`Branch`。

## 常见问题

### WebSocket 提示找不到模拟器可执行文件

先构建 C++ 工程，并确认 `build/riscv_sim_server` 或 `build/riscv_sim_server.exe` 存在。必要时设置 `SIM_SERVER_PATH`。

### 编译服务提示找不到 `riscv-none-elf-gcc`

检查 `RISCV_TOOLCHAIN_PATH` 是否指向工具链 `bin` 目录。Windows 下路径中反斜杠建议放在引号内。

### `run_riscv_tests` 出现 FAIL 或 TIMEOUT

先确认测试 ELF 与当前支持的 ISA 子集匹配。失败输出会给出停机原因、PC、指令和部分寄存器，可结合对应 `.dump` 文件定位。

### 前端连接后多个用户状态互相影响

当前主入口 `api_websocket_server.py` 已为每个用户创建独立模拟器。若仍出现状态串扰，确认没有误用旧入口 `api_web_old.py` 或单实例服务。

## 相关文档

- [独立模拟器架构测试指南](独立模拟器架构测试指南.md)
- [riscv-tests 使用说明](scripts/README_riscv_tests.md)

## License

当前目录未发现独立 LICENSE 文件。若需要对外发布或课程分发，建议补充许可证文件，并明确 C++ 后端、Python 服务、教学 ELF、测试资源和第三方依赖的授权范围。
