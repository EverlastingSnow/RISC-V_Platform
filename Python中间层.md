# RISC-V_Platform Python 中间层说明

本文档完整梳理 `RISC-V_Platform` 目录下的 Python 中间层代码。Python 层位于前端 Vue 应用和 C++ RISC-V 模拟核心之间，负责 WebSocket 会话管理、C++ 模拟器进程生命周期、HTTP 编译接口、使用统计、跨域响应和开发期模拟接口。

## 角色定位

Python 中间层不是模拟器核心。它的主要职责是把浏览器端请求转换为后端 C++ 模拟器可理解的命令，并把 C++ 输出的 JSON 结果转发给前端。

```text
┌─────────────────────────────────────────┐
│ Vue 前端                                 │
│ /ws, /api/compile, /api/stats            │
└───────────────────┬─────────────────────┘
                    │ HTTP / WebSocket JSON
┌───────────────────▼─────────────────────┐
│ Python 中间层                             │
│ api_websocket_server.py                   │
│ api_compile_server.py                     │
│ stats.py                                  │
└───────────────────┬─────────────────────┘
                    │ stdin/stdout JSON 命令
┌───────────────────▼─────────────────────┐
│ C++ 模拟器服务                            │
│ build/riscv_sim_server                    │
└─────────────────────────────────────────┘
```

## 文件总览

| 文件 | 定位 | 当前用途 |
|------|------|----------|
| `config.py` | 配置中心 | 统一提供项目路径、工具链路径、端口、C++ 模拟器路径和编译参数 |
| `api_websocket_server.py` | 主 WebSocket 中间层 | 每个用户创建独立 C++ 模拟器进程，转发运行、加载、差分测试等命令 |
| `api_compile_server.py` | HTTP 编译服务 | 接收前端汇编源码，调用 RISC-V GCC 编译 ELF 并返回 Base64 数据 |
| `stats.py` | 使用统计模块 | 使用 SQLite 记录每日访问次数和会话时长 |
| `api_server.py` | 旧式模拟 HTTP API | 返回固定模拟数据，不连接真实模拟器，适合早期联调或兼容用途 |
| `api_web_old.py` | 旧版 WebSocket 服务 | 单模拟器共享架构，存在多用户状态串扰风险，不建议作为主入口 |
| `test_compile.py` | 工具链冒烟测试 | 临时编译一段汇编并反汇编，验证 RISC-V 工具链可用 |

## 配置中心：`config.py`

`config.py` 是所有 Python 服务共同依赖的配置入口，直接从环境变量读取可变配置，并提供默认值。

### 路径配置

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `PROJECT_ROOT` | 当前文件所在目录 | `RISC-V_Platform` 根目录 |
| `RISCV_TOOLCHAIN_PATH` | `xpack-riscv-none-elf-gcc-15.2.0-1/bin` | RISC-V GCC 工具链目录 |
| `TEACHING_DIR` | `PROJECT_ROOT / teaching` | 教学 ELF 目录 |
| `COMPILE_OUTPUT_DIR` | `PROJECT_ROOT / compile_output` | 预留编译输出目录，当前编译服务主要使用临时目录 |
| `BUILD_DIR` | `PROJECT_ROOT / build` | C++ 构建目录 |
| `SOURCE_DIR` | `PROJECT_ROOT / isa` | 汇编编译依赖的 `isa` 目录 |
| `LINKER_SCRIPT` | `isa/link.ld` | 编译服务使用的链接脚本 |
| `START_FILE` | `isa/start.S` | 编译服务使用的启动文件 |

### 端口配置

| 环境变量 | 默认端口 | 使用者 |
|----------|----------|--------|
| `API_SERVER_PORT` | `8080` | `api_server.py` 旧式模拟接口 |
| `WEBSOCKET_PORT` | `8081` | `api_websocket_server.py` |
| `STATS_PORT` | `8082` | `api_websocket_server.py` 内嵌统计 HTTP 服务 |
| `COMPILE_SERVER_PORT` | `8083` | `api_compile_server.py` |

### C++ 模拟器路径解析

`SIM_SERVER_PATH` 优先读取环境变量。如果没有设置，则按顺序查找：

1. `build/riscv_sim_server`
2. `build/riscv_sim_server.exe`
3. 兜底为 `build/riscv_sim_server.exe`

需要注意：`api_websocket_server.py` 当前定义了全局 `SIM_SERVER_PATH = config.SIM_SERVER_PATH`，但 `CppSimulator()` 默认构造时没有实际使用这个全局变量，而是在类内部再次按 `build/riscv_sim_server`、`build/Debug/riscv_sim_server` 查找。若需要严格使用 `SIM_SERVER_PATH`，应显式把路径传入 `CppSimulator(config.SIM_SERVER_PATH)`。

### 编译参数

```text
ARCH = rv64g
ABI = lp64d
GCC_OPTS = -static -mcmodel=medany -fvisibility=hidden -nostdlib -nostartfiles
```

这些参数由 `api_compile_server.py` 组合到 `riscv-none-elf-gcc` 命令中。

## 主服务：`api_websocket_server.py`

`api_websocket_server.py` 是当前真实前后端联调的主入口。它同时承担两个职责：

- 启动 WebSocket 服务，默认监听 `0.0.0.0:8081`。
- 启动一个后台线程提供统计 HTTP 服务，默认监听 `8082`。

### 核心类结构

| 类 | 职责 |
|----|------|
| `CppSimulator` | 包装 C++ `riscv_sim_server` 子进程，通过 stdin/stdout 发送命令和读取 JSON |
| `StatsHTTPHandler` | 处理 `/api/stats` 和 `/api/stats/reset` |
| `StatsHTTPServer` | 统计 HTTP Server 包装类 |
| `WebSocketServer` | 管理 WebSocket 连接、模拟器实例、运行状态和命令分发 |

### 启动流程

```text
python api_websocket_server.py
        │
        ▼
WebSocketServer.__init__()
        │
        ├── stats.init_stats_db()
        ├── 初始化 simulators 字典
        ├── 初始化 running_states 字典
        └── _start_http_server() 启动统计 HTTP 后台线程
        │
        ▼
asyncio.run(server.start())
        │
        └── websockets.serve(handle_client, HOST, PORT)
```

默认 `HOST` 来自环境变量 `HOST`，未设置时为 `0.0.0.0`。这适合服务器部署，但本地调试时也会监听所有网卡。

### 多用户隔离模型

新版本使用“每个 WebSocket 客户端一个 C++ 模拟器进程”的模型：

```text
用户 A ── WebSocket ── CppSimulator A ── riscv_sim_server 进程 A
用户 B ── WebSocket ── CppSimulator B ── riscv_sim_server 进程 B
用户 C ── WebSocket ── CppSimulator C ── riscv_sim_server 进程 C
```

关键字段：

- `simulators: Dict[str, CppSimulator]`：按 `client_id` 保存模拟器实例。
- `running_states: Dict[str, bool]`：按 `client_id` 保存连续运行状态。
- `MAX_CONCURRENT_SIMULATORS = 100`：最多允许 100 个并发模拟器。
- `client_id = str(uuid.uuid4())[:8]`：每个连接生成 8 位短 ID。

连接建立时调用 `create_simulator(client_id)`，断开时在 `finally` 中调用 `destroy_simulator(client_id)`。

### C++ 子进程通信

`CppSimulator.start()` 使用：

```python
subprocess.Popen(
    [self.exe_path],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT
)
```

`send_command()` 的流程：

1. 向 stdin 写入命令和换行。
2. 从 stdout 逐行读取响应。
3. 跳过以 `[DEBUG` 开头的调试行。
4. 拼接 JSON 片段。
5. 用大括号数量判断 JSON 是否完整。
6. 返回完整 JSON 字符串或 `None`。

该实现依赖 C++ 服务每条命令输出一段完整 JSON。若 C++ 输出多行非 JSON 日志或 JSON 中包含复杂字符串，可能影响拼接判断。

### WebSocket 命令映射

前端发送 JSON：

```json
{
  "command": "step"
}
```

Python 中间层解析 `command` 后转发到 C++ stdin。主要命令如下：

| 前端命令 | 转发给 C++ | Python 处理逻辑 |
|----------|------------|----------------|
| `step` | `step` | 执行一个周期，特殊透传 `need_signal_input`、`diff_detected`、`halted` |
| `run` | 循环 `step` | 每 `0.05s` 执行一次，推送 `status: update` |
| `stop` | 无 | 将当前客户端 `running_states[client_id]` 设为 `False` |
| `reset` | `reset` 后 `signals` | 重置模拟器并返回当前信号 |
| `load` | `load <path>` | 加载服务器本地 ELF 文件 |
| `get_signals` | `signals` | 获取流水线信号 |
| `get_registers` | `registers` | 获取寄存器数组 |
| `enable_difftest` | `enable_difftest [--shadow] <signals>` | 启用差分测试 |
| `disable_difftest` | `disable_difftest` | 禁用差分测试 |
| `set_user_signal` | `set_user_signal <name> <true|false>` | 提交用户控制信号 |
| `continue` | `continue` | 清除差异/继续执行 |
| `load_test` | `load_test <testName>` | 加载内置教学指令序列后再取信号 |
| `list_tests` | `list_tests` | 返回内置教学测试列表 |
| `list_elf_tests` | `list_elf_tests` | 返回教学 ELF 列表 |
| `load_elf_test` | `load_elf_test <testName>` | 加载教学 ELF 后再取信号 |
| `load_elf_binary` | `load <temp_elf>` | Base64 解码成临时 ELF 文件，再交给 C++ 加载 |

### 特殊消息透传

当 C++ 返回以下类型时，Python 不包装为 `status: ok`，而是直接转发给前端：

- `type: need_signal_input`：差分测试需要用户输入控制信号。
- `type: diff_detected`：差分测试发现不一致。
- `type: halted`：模拟器停机。

这保证前端 `pipeline.ts` 可以直接识别并打开对应弹窗。

### `load_elf_binary` 处理流程

前端编译成功后会通过 WebSocket 发送 Base64 编码 ELF：

```json
{
  "command": "load_elf_binary",
  "elf_data": "...base64..."
}
```

Python 处理流程：

1. 检查 `elf_data` 是否存在。
2. `base64.b64decode()` 解码为 ELF 字节。
3. 使用 `tempfile.NamedTemporaryFile(delete=False, suffix='.elf')` 写临时文件。
4. 向当前客户端模拟器发送 `load <temp_elf.name>`。
5. 调用 `os.unlink(temp_elf.name)` 删除临时文件。
6. 如果加载成功，再调用 `sim.get_signals()` 返回初始信号。

### 统计服务嵌入

`WebSocketServer.__init__()` 调用 `_start_http_server()`，在 daemon 线程中启动 `StatsHTTPServer`。

接口：

| 路径 | 方法 | 说明 |
|------|------|------|
| `/api/stats` | GET | 获取全部统计 |
| `/api/stats?start=YYYY-MM-DD&end=YYYY-MM-DD` | GET | 按日期范围查询 |
| `/api/stats/reset` | POST | 清空统计 |
| 任意路径 | OPTIONS | 返回 CORS 预检响应 |

每次 WebSocket 新连接会调用 `stats.increment_visit()`。连接断开时统计会话秒数，并调用 `stats.add_duration(duration)` 与 `stats.flush_all()`。

## 编译服务：`api_compile_server.py`

`api_compile_server.py` 提供前端代码编辑器所需的 `POST /api/compile`。

### 启动方式

```bash
python api_compile_server.py
```

默认监听 `COMPILE_SERVER_PORT=8083`。

### HTTP Server 类型

服务使用：

```python
class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    allow_reuse_address = True
```

因此每个请求可由独立线程处理，适合多个用户同时提交编译请求。

### 请求格式

支持两种请求格式：

#### JSON 请求

```json
{
  "source": ".text\n.globl _start\n_start:\n  addi x1, x0, 1\n  ebreak\n",
  "filename": "student_code.S"
}
```

也兼容 `code` 字段：

```json
{
  "code": "...assembly..."
}
```

#### Multipart 请求

用于上传文件。代码手动解析 `multipart/form-data` boundary，并从文件 part 中提取源码和文件名。

### 编译流程

```text
handle_compile()
        │
        ├── handle_json() 或 handle_multipart()
        │
        ▼
compile_source(source_code, filename)
        │
        ├── 创建 tempfile.mkdtemp()
        ├── 写入 source_file
        ├── 定位 riscv-none-elf-gcc / riscv-none-elf-gcc.exe
        ├── 组合编译命令
        ├── subprocess.run(timeout=30)
        ├── 读取 output.elf 并 Base64 编码
        ├── 读取 output.lst 作为 list_content
        └── shutil.rmtree(temp_dir)
```

### 编译命令结构

核心命令来自 `config.py`：

```text
riscv-none-elf-gcc
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
<source_file>
-o output.elf
-Wl,-Map=output.lst
-fno-pie
-no-pie
```

编译超时时间为 `30` 秒。

### 成功响应

```json
{
  "success": true,
  "elf_data": "...base64...",
  "filename": "student_code.S",
  "list_content": "..."
}
```

### 失败响应

```json
{
  "success": false,
  "error": "...compiler stderr...",
  "line_errors": ["..."],
  "filename": "student_code.S"
}
```

如果超时：

```json
{
  "success": false,
  "error": "Compilation timeout (30s)"
}
```

### 注意事项

- `bin_file` 和 `objcopy` 变量已定义但当前没有实际用于生成 `.bin`。
- `parse_qs` 被导入但当前未使用。
- multipart 解析是手写实现，足够简单场景使用，但不如标准库或框架实现健壮。
- 响应统一返回 HTTP 200，通过 JSON 内的 `success` 表示编译是否成功。

## 统计模块：`stats.py`

`stats.py` 负责 SQLite 持久化统计。

### 数据库文件

默认数据库：

```text
RISC-V_Platform/stats.db
```

SQLite 使用 WAL 模式，因此运行中可能出现：

- `stats.db`
- `stats.db-wal`
- `stats.db-shm`

### 表结构

`init_stats_db()` 创建两张表：

```sql
CREATE TABLE IF NOT EXISTS daily_stats (
    date TEXT PRIMARY KEY,
    total_time INTEGER DEFAULT 0,
    visit_count INTEGER DEFAULT 0
)
```

```sql
CREATE TABLE IF NOT EXISTS usage_stats (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    total_time INTEGER DEFAULT 0,
    visit_count INTEGER DEFAULT 0
)
```

当前查询主要使用 `daily_stats` 聚合结果；`usage_stats` 会初始化和 reset，但正常查询路径并不直接读取它。

### 连接策略

- 使用 `threading.local()` 保存每个线程自己的 SQLite 连接。
- 每个连接设置 `PRAGMA journal_mode=WAL`。
- 每个连接设置 `PRAGMA synchronous=NORMAL`。
- 每个连接设置 `PRAGMA cache_size=10000`。

### 写入策略

访问次数立即写入：

```python
increment_visit()
```

会话时长先累加到内存中的 `_pending_duration`，再由 `_flush_pending_duration()` 批量写入：

```python
add_duration(seconds)
flush_all()
```

`get_daily_stats()` 如果发现距离上次 flush 超过 `_flush_interval = 5` 秒，也会触发一次 flush。

### 对外函数

| 函数 | 说明 |
|------|------|
| `init_stats_db()` | 初始化数据库和表 |
| `get_daily_stats(start_date=None, end_date=None)` | 查询每日统计，可按日期过滤 |
| `get_stats()` | 当前等价于 `get_daily_stats()` |
| `increment_visit()` | 当日访问次数加 1 |
| `add_duration(seconds)` | 累加当日使用时长 |
| `reset_stats()` | 清空统计数据 |
| `flush_all()` | 刷新内存中待写入的时长统计 |

## 旧式模拟接口：`api_server.py`

`api_server.py` 是早期 HTTP 模拟数据服务，默认端口来自 `API_SERVER_PORT=8080`。

它不连接 C++ 模拟器，也不编译代码。返回固定模拟数据：

| 路径 | 方法 | 返回内容 |
|------|------|----------|
| `/api/state` | GET | 固定 `cycle`、`pc` 和 32 个寄存器 |
| `/api/signals` | GET | 固定流水线阶段、寄存器堆和数据内存信号 |
| `/api/clock` | POST | 固定下一周期响应 |
| `/api/reset` | POST | 固定复位响应 |
| `/api/load` | POST | 固定加载响应 |

该文件适合前端早期 UI 联调或接口占位，不应作为当前真实仿真链路的主服务。

## 旧版 WebSocket：`api_web_old.py`

`api_web_old.py` 与当前 `api_websocket_server.py` 命令处理逻辑相似，但架构不同：

- 使用 `self.sim: Optional[CppSimulator]` 保存单个全局模拟器。
- 多个客户端共享同一个模拟器状态。
- 使用 `command_lock` 串行化所有客户端命令。
- 启动时尝试初始化一个 C++ 模拟器。

这会带来教学场景中的状态串扰问题：用户 A 加载程序或运行周期会影响用户 B。当前主入口已经改为每用户独立模拟器，`api_web_old.py` 应视为历史参考。

## 工具链冒烟测试：`test_compile.py`

`test_compile.py` 用于快速验证本机 RISC-V 工具链是否能编译和反汇编一个最小汇编程序。

流程：

1. 在临时目录写入 `test.S`。
2. 生成简单 `link.ld`，入口为 `main`，起始地址 `0x80000000`。
3. 使用 `config.RISCV_TOOLCHAIN_PATH / riscv-none-elf-gcc.exe` 编译。
4. 使用 `riscv-none-elf-objdump.exe -D -m riscv:rv64` 反汇编。
5. 打印返回码、错误片段和反汇编前 1500 字符。

注意：该脚本当前硬编码 `.exe` 后缀，更偏向 Windows 环境。Linux/macOS 下需要改为无 `.exe` 的工具名。

## 部署与反向代理

前端默认访问同源路径：

- `/ws`
- `/api/compile`
- `/api/stats`

Python 中间层默认服务端口：

- WebSocket：`8081`
- 统计：`8082`
- 编译：`8083`

因此生产部署通常需要反向代理。例如 Nginx 可以按如下逻辑转发：

```text
/ws           -> ws://127.0.0.1:8081
/api/stats    -> http://127.0.0.1:8082/api/stats
/api/compile  -> http://127.0.0.1:8083/api/compile
```

如果使用 Vite 开发服务器，也可以临时添加 `server.proxy`，但当前仓库 `vite.config.ts` 未内置该配置。

## 启动顺序建议

### 1. 构建 C++ 模拟器

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

确认存在：

```text
build/riscv_sim_server
```

或 Windows：

```text
build/riscv_sim_server.exe
```

### 2. 启动 WebSocket 与统计服务

```bash
python api_websocket_server.py
```

### 3. 启动编译服务

```bash
python api_compile_server.py
```

### 4. 启动或部署前端

前端需通过代理访问 `/ws`、`/api/compile`、`/api/stats`。

## 环境变量清单

| 环境变量 | 示例 | 说明 |
|----------|------|------|
| `HOST` | `0.0.0.0` | WebSocket 监听地址，仅 `api_websocket_server.py` 使用 |
| `WEBSOCKET_PORT` | `8081` | WebSocket 服务端口 |
| `STATS_PORT` | `8082` | 统计 HTTP 服务端口 |
| `COMPILE_SERVER_PORT` | `8083` | 编译 HTTP 服务端口 |
| `API_SERVER_PORT` | `8080` | 旧式模拟 HTTP 服务端口 |
| `RISCV_TOOLCHAIN_PATH` | `E:\...\bin` | RISC-V 工具链 bin 目录 |
| `SIM_SERVER_PATH` | `E:\...\riscv_sim_server.exe` | C++ 模拟器服务可执行文件路径 |
| `RISCV_TEACHING_DIR` | `E:\...\teaching` | C++ 教学 ELF 配置使用，Python 配置示例中保留 |

## 错误处理策略

### WebSocket 层

- JSON 解析失败返回 `{'status': 'error', 'message': 'Invalid JSON'}`。
- 未知命令返回 `{'status': 'error', 'message': 'Unknown command'}`。
- 模拟器未初始化返回 `{'status': 'error', 'message': 'Simulator not initialized'}`。
- 超过最大并发返回 `Server at maximum capacity` 并关闭连接。
- 连接断开时清理对应模拟器进程并统计会话时长。

### 编译层

- 所有编译结果都用 JSON 表示，HTTP 状态码通常为 200。
- 编译失败返回 `success: false` 和编译器 stderr。
- 超时返回 `Compilation timeout (30s)`。
- 临时目录在 `finally` 中删除。

### 统计层

- 查询时自动 flush 超过 5 秒的待写入时长。
- SQLite 写入异常会打印日志，但不会抛到 WebSocket 主循环。

## 已知限制与改进建议

- **`SIM_SERVER_PATH` 未完全生效**：`api_websocket_server.py` 的 `CppSimulator()` 默认构造没有传入 `config.SIM_SERVER_PATH`。
- **C++ stdout 解析偏朴素**：`send_command()` 依赖括号计数拼接 JSON，复杂日志或嵌套字符串可能造成解析风险。
- **编译服务 multipart 解析较简单**：建议后续使用成熟解析库或轻量 Web 框架。
- **统计表存在冗余**：`usage_stats` 初始化和 reset 存在，但查询主要走 `daily_stats`。
- **`api_server.py` 与 `api_web_old.py` 易误用**：建议在部署脚本和 README 中明确只使用 `api_websocket_server.py` 与 `api_compile_server.py`。
- **`test_compile.py` 偏 Windows**：当前硬编码 `.exe`，跨平台时需兼容无后缀工具链。
- **CORS 全开放**：当前服务返回 `Access-Control-Allow-Origin: *`，生产环境可按部署域名收紧。

## 开发规范建议

- 新增 WebSocket 命令时，同时更新前端 `stores/pipeline.ts`、`api_websocket_server.py` 和 C++ `riscv_sim_server.cpp`。
- 中间层不要实现指令语义，指令执行应保持在 C++ 模拟核心内。
- 命令响应尽量保持 JSON 单行输出，减少 Python stdout 拼接复杂度。
- 涉及用户上传或编译的路径应继续使用临时目录，避免污染工程目录。
- 对多用户相关修改必须验证不同客户端状态互不影响。
- 修改统计逻辑时同时验证 `/api/stats`、日期过滤和 `/api/stats/reset`。

## 快速排查

### WebSocket 连接成功但提示模拟器初始化失败

检查 `build/riscv_sim_server` 或 `build/riscv_sim_server.exe` 是否存在，并确认运行用户有执行权限。

### 前端编译按钮返回网络错误

检查 `api_compile_server.py` 是否启动，以及 `/api/compile` 是否被代理到 `COMPILE_SERVER_PORT`。

### 编译失败提示找不到工具链

检查 `RISCV_TOOLCHAIN_PATH` 是否指向包含 `riscv-none-elf-gcc` 的 `bin` 目录。

### 统计栏无数据

检查 `api_websocket_server.py` 是否启动，因为统计 HTTP 服务由它内嵌启动。单独启动 `api_compile_server.py` 不会提供 `/api/stats`。

### 多用户状态互相影响

确认运行的是 `api_websocket_server.py`，不是旧版 `api_web_old.py`。
