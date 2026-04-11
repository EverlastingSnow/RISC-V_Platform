# RISC-V 流水线教学系统 - 接口文档

> 本文档描述 RISC-V 流水线可视化教学系统的后端服务接口，供其他项目集成使用。

**文档版本**: 1.0.0
**更新日期**: 2026-04-02

---

## 目录

1. [系统概述](#1-系统概述)
2. [服务端口配置](#2-服务端口配置)
3. [编译服务 API](#3-编译服务-api)
4. [WebSocket 通信协议](#4-websocket-通信协议)
5. [数据类型定义](#5-数据类型定义)
6. [错误处理](#6-错误处理)
7. [前端集成示例](#7-前端集成示例)
8. [配置说明](#8-配置说明)
9. [依赖环境](#9-依赖环境)

---

## 1. 系统概述

### 1.1 系统架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              用户浏览器                                   │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                    ┌───────────────┴───────────────┐
                    │                               │
              HTTP POST                           WebSocket
              :8083                               :8081
                    │                               │
                    ▼                               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           Python 服务层                                  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐ │
│  │ api_compile_    │  │ api_websocket_   │  │ Stats HTTP Server       │ │
│  │ server.py       │  │ server.py        │  │ (可选)                  │ │
│  │ (Port 8083)     │  │ (Port 8081)      │  │ (Port 8082)            │ │
│  └─────────────────┘  └────────┬────────┘  └─────────────────────────┘ │
│                                 │                                        │
│                    subprocess.stdin/stdout                               │
│                                 ▼                                        │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                   C++ 模拟器 (riscv_sim_server.exe)               │   │
│  │                      5-Stage Pipeline                             │   │
│  └─────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 服务职责

| 服务 | 端口 | 职责 |
|------|------|------|
| `api_compile_server.py` | 8083 | RISC-V 汇编代码编译服务 |
| `api_websocket_server.py` | 8081 | 主控制通道，模拟器管理 |
| `api_server.py` | 8080 | 简单 API（模拟，已弃用）|
| `Stats HTTP` | 8082 | 使用统计（可选）|

---

## 2. 服务端口配置

### 2.1 端口分配

| 端口 | 服务 | 协议 | 状态 |
|------|------|------|------|
| 8080 | api_server.py | HTTP | 模拟用 |
| **8081** | api_websocket_server.py | **WebSocket** | **主通道** |
| 8082 | Stats HTTP Server | HTTP | 可选 |
| **8083** | api_compile_server.py | **HTTP POST** | **编译服务** |

### 2.2 环境变量配置

```bash
# 可选：覆盖默认端口
export API_SERVER_PORT=8080
export WEBSOCKET_PORT=8081
export STATS_PORT=8082
export COMPILE_SERVER_PORT=8083

# 工具链路径（通常不需要修改）
export RISCV_TOOLCHAIN_PATH=/path/to/xpack-riscv-none-elf-gcc-15.2.0-1/bin
```

---

## 3. 编译服务 API

### 3.1 编译汇编代码

编译 RISC-V 汇编代码为 ELF 可执行文件。

**端点**: `POST /api/compile`

**地址**: `http://localhost:8083/api/compile`

#### 请求头

| 头信息 | 值 | 说明 |
|--------|-----|------|
| `Content-Type` | `application/json` | JSON 格式请求 |
| 或 `Content-Type` | `multipart/form-data` | 表单格式请求 |

#### 请求体 (JSON 格式)

```json
{
  "source": "addi x1, x0, 5\nadd x2, x1, x1",
  "filename": "test.S"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `source` | string | 是 | RISC-V 汇编源代码 |
| `filename` | string | 否 | 文件名，默认 `uploaded.S` |

#### 请求体 (multipart/form-data)

```
---------------------------Boundary
Content-Disposition: form-data; name="source"; filename="test.S"

addi x1, x0, 5
add x2, x1, x1
---------------------------Boundary--
```

#### 成功响应 (HTTP 200)

```json
{
  "success": true,
  "elf_data": "//uQnABJRU5ErkJggg==",
  "filename": "test.S",
  "list_content": "test.S:     file format elf64-littleriscv\n\nDisassembly of section .text:\n...\n"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `success` | boolean | 编译是否成功 |
| `elf_data` | string | Base64 编码的 ELF 文件 |
| `filename` | string | 原始文件名 |
| `list_content` | string | 汇编列表内容（可选）|

#### 失败响应 (HTTP 200)

```json
{
  "success": false,
  "error": "Error: invalid instruction 'addx' at line 3\n",
  "line_errors": [
    "Error: invalid instruction 'addx' at line 3"
  ],
  "filename": "test.S"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `success` | boolean | false |
| `error` | string | 完整错误信息 |
| `line_errors` | array | 提取的行级错误 |
| `filename` | string | 原始文件名 |

#### 响应头

```http
HTTP/1.1 200 OK
Content-Type: application/json; charset=utf-8
Access-Control-Allow-Origin: *
Content-Length: 1234
```

### 3.2 编译选项

编译使用以下默认选项：

| 选项 | 值 | 说明 |
|------|-----|------|
| `-march` | `rv64g` | 目标架构 |
| `-mabi` | `lp64d` | ABI |
| `-static` | - | 静态链接 |
| `-mcmodel=medany` | - | 代码模型 |
| `-fvisibility=hidden` | - | 隐藏符号 |
| `-nostdlib` | - | 不链接标准库 |
| `-nostartfiles` | - | 不链接启动文件 |

---

## 4. WebSocket 通信协议

### 4.1 连接信息

**地址**: `ws://localhost:8081`

**协议**: JSON 文本消息

### 4.2 客户端 → 服务器命令

#### 4.2.1 加载 ELF 测试

```json
{
  "command": "load_elf_test",
  "testName": "teaching_regwrite"
}
```

**响应**:
```json
{
  "status": "ok",
  "message": "Loaded ELF test: teaching_regwrite",
  "signals": { ... }
}
```

#### 4.2.2 加载 ELF 二进制

```json
{
  "command": "load_elf_binary",
  "elf_data": "//uQnABJRU5ErkJggg=="
}
```

**说明**: `elf_data` 为 Base64 编码的 ELF 文件内容

#### 4.2.3 单步执行

```json
{
  "command": "step"
}
```

**响应** (正常):
```json
{
  "status": "ok",
  "signals": {
    "cycle": 1,
    "pc": "0x80000000",
    "if_id": { ... },
    "id_ex": { ... },
    "ex_mem": { ... },
    "mem_wb": { ... },
    "writeback": { ... },
    "regfile": { ... },
    "datamem": { ... },
    "halted": false
  }
}
```

#### 4.2.4 重置模拟器

```json
{
  "command": "reset"
}
```

**响应**:
```json
{
  "status": "ok",
  "message": "Reset"
}
```

#### 4.2.5 获取信号状态

```json
{
  "command": "get_signals"
}
```

#### 4.2.6 获取寄存器

```json
{
  "command": "get_registers"
}
```

**响应**:
```json
{
  "status": "ok",
  "registers": {
    "registers": [
      {"addr": 0, "value": 0},
      {"addr": 1, "value": 5},
      ...
    ]
  }
}
```

#### 4.2.7 启用 Difftest

```json
{
  "command": "enable_difftest",
  "signals": "RegWrite ALUSrc MemRead MemWrite Branch",
  "shadowMode": false
}
```

**参数**:
- `signals`: 空格分隔的信号名称
- `shadowMode`: 是否启用影子模式

#### 4.2.8 设置用户信号

```json
{
  "command": "set_user_signal",
  "signalName": "RegWrite",
  "value": true
}
```

#### 4.2.9 列出可用测试

```json
{
  "command": "list_tests",
  "scenario": "scenario1"
}
```

**响应**:
```json
{
  "status": "ok",
  "tests": [
    {
      "name": "teaching_regwrite",
      "description": "RegWrite信号基础",
      "scenario": "scenario1"
    }
  ]
}
```

#### 4.2.10 列出 ELF 测试

```json
{
  "command": "list_elf_tests",
  "scenario": "all"
}
```

### 4.3 服务器 → 客户端消息

#### 4.3.1 需要用户输入信号

```json
{
  "type": "need_signal_input",
  "needInput": {
    "pc": "0x80000000",
    "signals": [
      {"name": "RegWrite", "expectedValue": "1"},
      {"name": "ALUSrc", "expectedValue": "0"}
    ],
    "instruction": "ADD"
  }
}
```

**客户端应**: 显示信号输入界面，等待用户输入

#### 4.3.2 检测到差异

```json
{
  "type": "diff_detected",
  "diffResult": {
    "detected": true,
    "stage": "WB",
    "goldenPC": "0x80000000",
    "userPC": "0x80000000",
    "goldenResult": {
      "regWrite": true,
      "waddr": 1,
      "wdata": "0x5"
    },
    "userResult": {
      "regWrite": false,
      "waddr": 0,
      "wdata": "0x0"
    },
    "message": "WB阶段差异: 写使能不匹配 (Golden: 1, User: 0)"
  }
}
```

#### 4.3.3 程序暂停

```json
{
  "type": "halted",
  "halt_reason": "ecall"
}
```

---

## 5. 数据类型定义

### 5.1 流水线信号结构

```typescript
interface PipelineSignals {
  cycle: number;
  pc: string;

  // IF/ID 流水线寄存器
  if_id: {
    pc: string;
    valid: boolean;
    inst: number;
    instruction: string;
    asm: string;
    target: string;
    taken: boolean;
    PC_next: string;
    allow_to_go: boolean;
  };

  // ID/EX 流水线寄存器
  id_ex: {
    pc: string;
    inst: number;
    src1_raddr: number;
    src1_rdata: string;
    src2_raddr: number;
    src2_rdata: string;
    imm: string;
    instruction: string;
    asm: string;
  };

  // 执行阶段
  execute: {
    pc: string;
    valid: boolean;
    alu_src1: string;
    alu_src2: string;
    alu_result: string;
    fu_type: string;
    asm: string;
  };

  // EX/MEM 流水线寄存器
  ex_mem: {
    pc: string;
    valid: boolean;
    inst: number;
    instruction: string;
    asm: string;
    alu_result: string;
    branch_taken: boolean;
    branch_target: string;
    mem_addr: string;
    mem_wen: boolean;
    mem_ren: boolean;
    mem_wdata: string;
  };

  // MEM/WB 流水线寄存器
  mem_wb: {
    pc: string;
    valid: boolean;
    inst: number;
    instruction: string;
    asm: string;
    wb_value: string;
    rf_wen: boolean;
    rf_waddr: number;
  };

  // 提交阶段
  writeback: {
    pc: string;
    valid: boolean;
    instruction: string;
    asm: string;
    debug_commit: boolean;
    debug_pc: string;
    debug_wb_rf_wen: boolean;
    debug_wb_rf_waddr: number;
    debug_wb_rf_wdata: string;
  };

  // 寄存器文件
  regfile: {
    src1_raddr: number;
    src1_rdata: string;
    src2_raddr: number;
    src2_rdata: string;
    reg_wen: boolean;
    reg_waddr: number;
    reg_wdata: string;
  };

  // 数据内存
  datamem: {
    DataMEM_en: boolean;
    DataMEM_wen: boolean;
    DataMEM_addr: string;
    DataMEM_rdata: number;
    DataMEM_wdata: string;
  };

  halted: boolean;
  halt_reason?: string;
}
```

### 5.2 可控信号列表

| 信号名 | 说明 | 适用指令类型 |
|--------|------|-------------|
| `RegWrite` | 寄存器写使能 | R/I/U/J 型 |
| `ALUSrc` | ALU 操作数选择 | I/S/U 型 |
| `MemRead` | 内存读使能 | Load 型 |
| `MemWrite` | 内存写使能 | Store 型 |
| `Branch` | 分支使能 | B 型 |

---

## 6. 错误处理

### 6.1 HTTP 错误码

| 状态码 | 说明 |
|--------|------|
| 200 | 请求成功（业务成功或失败）|
| 404 | 端点不存在 |
| 500 | 服务器内部错误 |

### 6.2 编译错误处理

```javascript
async function compileWithErrorHandling(source) {
  try {
    const response = await fetch('http://localhost:8083/api/compile', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ source, filename: 'test.S' })
    });

    const data = await response.json();

    if (!data.success) {
      // 处理编译错误
      console.error('编译失败:', data.error);
      console.error('行错误:', data.line_errors);
      return { error: data.error, lineErrors: data.line_errors };
    }

    return { elfData: data.elf_data };
  } catch (error) {
    // 处理网络错误
    console.error('网络错误:', error);
    return { error: error.message };
  }
}
```

### 6.3 WebSocket 错误处理

```javascript
function connectWebSocket() {
  const ws = new WebSocket('ws://localhost:8081');

  ws.onerror = (error) => {
    console.error('WebSocket 错误:', error);
  };

  ws.onclose = () => {
    console.log('连接断开，3秒后重连...');
    setTimeout(connectWebSocket, 3000);
  };

  return ws;
}
```

---

## 7. 前端集成示例

### 7.1 编译服务集成

```html
<!DOCTYPE html>
<html>
<head>
  <title>RISC-V 汇编编译示例</title>
</head>
<body>
  <textarea id="asmCode" rows="10" cols="50">
    .text
    .globl _start
_start:
    addi x1, x0, 5
    add x2, x1, x1
  </textarea>
  <button onclick="compile()">编译</button>
  <pre id="output"></pre>

  <script>
    async function compile() {
      const source = document.getElementById('asmCode').value;
      const output = document.getElementById('output');

      try {
        const response = await fetch('http://localhost:8083/api/compile', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ source, filename: 'test.S' })
        });

        const data = await response.json();

        if (data.success) {
          // 将 Base64 转换为 Uint8Array
          const elfBytes = Uint8Array.from(
            atob(data.elf_data),
            c => c.charCodeAt(0)
          );
          output.textContent = `编译成功！ELF 大小: ${elfBytes.length} bytes`;

          // TODO: 将 ELF 发送给 WebSocket 服务器加载
          // loadElfBinary(data.elf_data);
        } else {
          output.textContent = `编译失败: ${data.error}`;
        }
      } catch (error) {
        output.textContent = `错误: ${error.message}`;
      }
    }
  </script>
</body>
</html>
```

### 7.2 WebSocket 完整集成

```javascript
class RISCVSimulator {
  constructor(wsUrl = 'ws://localhost:8081') {
    this.wsUrl = wsUrl;
    this.ws = null;
    this.pendingRequests = new Map();
  }

  connect() {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.wsUrl);

      this.ws.onopen = () => {
        console.log('WebSocket 已连接');
        resolve();
      };

      this.ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        this.handleMessage(data);
      };

      this.ws.onerror = (error) => {
        reject(error);
      };
    });
  }

  send(command, data = {}) {
    return new Promise((resolve, reject) => {
      const message = JSON.stringify({ command, ...data });
      this.ws.send(message);

      // 简化处理：直接等待响应
      const handler = (event) => {
        const response = JSON.parse(event.data);
        this.ws.removeEventListener('message', handler);
        resolve(response);
      };

      this.ws.addEventListener('message', handler);
    });
  }

  handleMessage(data) {
    // 处理服务器主动推送的消息
    if (data.type === 'need_signal_input') {
      console.log('需要用户输入信号:', data.needInput);
      // 显示信号输入界面
    } else if (data.type === 'diff_detected') {
      console.log('检测到差异:', data.diffResult);
    } else if (data.type === 'halted') {
      console.log('程序已暂停:', data.halt_reason);
    }
  }

  async step() {
    return this.send('step');
  }

  async reset() {
    return this.send('reset');
  }

  async loadElfBinary(elfBase64) {
    return this.send('load_elf_binary', { elf_data: elfBase64 });
  }

  async enableDifftest(signals, shadowMode = false) {
    return this.send('enable_difftest', {
      signals: signals.join(' '),
      shadowMode
    });
  }

  async setUserSignal(signalName, value) {
    return this.send('set_user_signal', { signalName, value });
  }
}

// 使用示例
async function main() {
  const sim = new RISCVSimulator();
  await sim.connect();

  // 加载 ELF
  await sim.loadElfBinary(elfBase64);

  // 单步执行
  for (let i = 0; i < 10; i++) {
    const result = await sim.step();
    console.log('Cycle', result.signals.cycle, 'PC:', result.signals.pc);
  }
}
```

---

## 8. 配置说明

### 8.1 config.py 关键配置

```python
# RISC-V_Platform/config.py

# 工具链路径
RISCV_TOOLCHAIN_PATH = "xpack-riscv-none-elf-gcc-15.2.0-1/bin"

# 端口配置
API_SERVER_PORT = 8080
WEBSOCKET_PORT = 8081
STATS_PORT = 8082
COMPILE_SERVER_PORT = 8083

# RISC-V 编译配置
ARCH = "rv64g"       # 目标架构: rv64gc (支持压缩指令)
ABI = "lp64d"        # ABI: lp64d (单精度+双精度浮点)

# 编译选项
GCC_OPTS = "-static -mcmodel=medany -fvisibility=hidden -nostdlib -nostartfiles"

# 源码和链接脚本
SOURCE_DIR = "isa"
LINKER_SCRIPT = "isa/link.ld"
START_FILE = "isa/start.S"
```

### 8.2 工具链要求

需要安装 `xpack-riscv-none-elf-gcc`，包含以下工具：

| 工具 | 用途 |
|------|------|
| `riscv-none-elf-gcc` | C/汇编编译器 |
| `riscv-none-elf-objcopy` | 目标文件转换 |

---

## 9. 依赖环境

### 9.1 Python 依赖

```
# requirements.txt
websockets>=10.0
```

### 9.2 系统依赖

| 依赖 | 版本 | 说明 |
|------|------|------|
| Python | 3.8+ | 运行环境 |
| RISC-V Toolchain | 15.2.0+ | 编译工具链 |

### 9.3 目录结构

```
RISC-V_Platform/
├── api_server.py              # 简单 API（模拟）
├── api_websocket_server.py     # WebSocket 主服务
├── api_compile_server.py      # 编译服务
├── config.py                  # 配置文件
├── stats.py                   # 统计模块
│
├── src/                       # C++ 源码
│   ├── riscv_sim_server.cpp   # 主程序
│   ├── simulator.cpp          # 流水线模拟器
│   └── ...
│
├── include/riscv/             # C++ 头文件
│   ├── simulator.h
│   └── ...
│
├── teaching/                  # 教学测试用例
│   ├── teaching_pipeline.elf
│   └── ...
│
└── build/                     # 编译输出
    └── riscv_sim_server.exe
```

---

## 附录 A: 完整命令列表

| 命令 | 方向 | 说明 |
|------|------|------|
| `load_elf_test` | C→S | 加载内置 ELF 测试 |
| `load_elf_binary` | C→S | 加载 Base64 ELF |
| `step` | C→S | 单步执行 |
| `run` | C→S | 连续运行 |
| `stop` | C→S | 停止运行 |
| `reset` | C→S | 重置模拟器 |
| `signals` | C→S | 获取信号状态 |
| `registers` | C→S | 获取寄存器 |
| `enable_difftest` | C→S | 启用差分测试 |
| `disable_difftest` | C→S | 禁用差分测试 |
| `set_user_signal` | C→S | 设置用户信号 |
| `list_tests` | C→S | 列出测试 |
| `list_elf_tests` | C→S | 列出 ELF 测试 |
| `need_signal_input` | S→C | 请求信号输入 |
| `diff_detected` | S→C | 检测到差异 |
| `halted` | S→C | 程序暂停 |

---

## 附录 B: 联系方式

如有问题，请联系项目维护者。

---

*本文档由系统自动生成，最后更新于 2026-04-02*
