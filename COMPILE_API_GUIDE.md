# RISC-V 汇编编译器接口说明

> 本文档描述如何将 RISC-V 流水线教学系统的汇编代码编译功能集成到其他项目中。

**版本**: 1.0.0
**日期**: 2026-04-02

---

## 1. 快速开始

### 1.1 服务地址

| 服务 | 地址 | 协议 |
|------|------|------|
| 编译服务 | `http://localhost:8083/api/compile` | HTTP POST |

### 1.2 最简示例

```html
<!DOCTYPE html>
<html>
<body>
  <textarea id="code">addi x1, x0, 5</textarea>
  <button onclick="compile()">编译</button>

  <script>
    async function compile() {
      const source = document.getElementById('code').value;
      const resp = await fetch('http://localhost:8083/api/compile', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ source, filename: 'test.S' })
      });
      const data = await resp.json();
      if (data.success) {
        console.log('ELF Base64:', data.elf_data);
      } else {
        console.error('编译错误:', data.error);
      }
    }
  </script>
</body>
</html>
```

---

## 2. 接口详情

### 2.1 编译接口

**端点**: `POST /api/compile`

**地址**: `http://localhost:8083/api/compile`

#### 请求

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

#### 成功响应

```json
{
  "success": true,
  "elf_data": "//uQnABJRU5ErkJggg==",
  "filename": "test.S",
  "list_content": "test.S:     file format elf64-littleriscv\n\nDisassembly of section .text:\n..."
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `success` | boolean | 编译是否成功 |
| `elf_data` | string | **Base64 编码的 ELF 文件** |
| `filename` | string | 原始文件名 |
| `list_content` | string | 反汇编内容（可选）|

#### 失败响应

```json
{
  "success": false,
  "error": "Error: invalid instruction 'addx' at line 3\n",
  "line_errors": ["Error: invalid instruction 'addx' at line 3"],
  "filename": "test.S"
}
```

---

## 3. 完整集成示例

### 3.1 前端完整代码

```html
<!DOCTYPE html>
<html>
<head>
  <title>RISC-V 汇编编译示例</title>
  <style>
    textarea { width: 100%; height: 150px; }
    #output { white-space: pre-wrap; background: #f5f5f5; padding: 10px; }
  </style>
</head>
<body>
  <h1>RISC-V 汇编编译器</h1>

  <h3>输入汇编代码</h3>
  <textarea id="asmCode">.text
.globl _start
_start:
    addi x1, x0, 5
    add x2, x1, x1
    ecall</textarea>

  <br><br>
  <button onclick="compileAndLoad()">编译并加载</button>

  <h3>输出</h3>
  <div id="output"></div>

  <script>
    const WS_URL = 'ws://localhost:8081';
    const COMPILE_URL = 'http://localhost:8083/api/compile';

    const output = document.getElementById('output');
    let ws = null;

    function log(msg) {
      output.textContent += msg + '\n';
    }

    // Step 1: 编译汇编代码
    async function compile(source) {
      log('正在编译...');
      const resp = await fetch(COMPILE_URL, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ source, filename: 'test.S' })
      });
      const data = await resp.json();

      if (!data.success) {
        log('编译失败: ' + data.error);
        return null;
      }

      log('编译成功! ELF大小: ' + atob(data.elf_data).length + ' bytes');
      return data.elf_data;
    }

    // Step 2: 连接WebSocket并加载ELF
    async function connectAndLoad(elfBase64) {
      log('正在连接模拟器...');

      return new Promise((resolve, reject) => {
        ws = new WebSocket(WS_URL);

        ws.onopen = async () => {
          log('已连接，开始加载ELF...');

          // 发送加载命令
          ws.send(JSON.stringify({
            command: 'load_elf_binary',
            elf_data: elfBase64
          }));
        };

        ws.onmessage = (event) => {
          const data = JSON.parse(event.data);
          log('收到响应: ' + JSON.stringify(data, null, 2));

          if (data.status === 'ok' && data.message && data.message.includes('Loaded')) {
            log('ELF加载成功!');
          }
          ws.close();
          resolve();
        };

        ws.onerror = (err) => {
          log('WebSocket错误: ' + err);
          reject(err);
        };
      });
    }

    // 主流程
    async function compileAndLoad() {
      try {
        const source = document.getElementById('asmCode').value;
        const elfBase64 = await compile(source);

        if (elfBase64) {
          await connectAndLoad(elfBase64);
        }
      } catch (err) {
        log('错误: ' + err);
      }
    }
  </script>
</body>
</html>
```

### 3.2 使用 Fetch API（Node.js/Browser）

```javascript
async function compileAssembly(source, filename = 'test.S') {
  const response = await fetch('http://localhost:8083/api/compile', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source, filename })
  });

  const result = await response.json();

  if (!result.success) {
    throw new Error(result.error);
  }

  // 将 Base64 转换为 Uint8Array
  const binaryString = atob(result.elf_data);
  const bytes = new Uint8Array(binaryString.length);
  for (let i = 0; i < binaryString.length; i++) {
    bytes[i] = binaryString.charCodeAt(i);
  }

  return bytes;
}

// 使用
const elfBytes = await compileAssembly(`
  .text
  .globl _start
_start:
  addi x1, x0, 5
  add x2, x1, x1
`);
console.log('ELF字节数组:', elfBytes);
```

### 3.3 使用 axios（推荐）

```javascript
import axios from 'axios';

async function compileAssembly(source, filename = 'test.S') {
  try {
    const response = await axios.post(
      'http://localhost:8083/api/compile',
      { source, filename },
      { headers: { 'Content-Type': 'application/json' } }
    );

    if (!response.data.success) {
      throw new Error(response.data.error);
    }

    // Base64 解码为 ArrayBuffer
    const binaryString = atob(response.data.elf_data);
    const bytes = new Uint8Array(binaryString.length);
    for (let i = 0; i < binaryString.length; i++) {
      bytes[i] = binaryString.charCodeAt(i);
    }

    return {
      elf: bytes,
      filename: response.data.filename,
      disassembly: response.data.list_content
    };
  } catch (error) {
    console.error('编译失败:', error);
    throw error;
  }
}
```

---

## 4. 工作流程图

```
┌─────────────────────────────────────────────────────────────────────┐
│                           前端 (浏览器)                               │
│                                                                      │
│  1. 用户输入汇编代码                                                   │
│         ↓                                                             │
│  2. fetch POST /api/compile  ──────────────────────────────────────┐ │
│                                                                     │ │
└─────────────────────────────────────────────────────────────────────┘ │
                                                                      │
                                         HTTP POST (JSON)              │
                                         Content-Type: application/json │
                                                                      ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Python 编译服务 (端口 8083)                           │
│                              api_compile_server.py                          │
│                                                                            │
│  3. 接收请求                                                              │
│         ↓                                                                 │
│  4. 写入临时文件 /tmp/test.S                                               │
│         ↓                                                                 │
│  5. 调用 riscv-none-elf-gcc 编译                                           │
│     riscv-none-elf-gcc -march=rv64g -mabi=lp64d -o test test.S             │
│         ↓                                                                 │
│  6. 调用 riscv-none-elf-objcopy 转换为 hex                                  │
│     riscv-none-elf-objcopy -O hex test test.hex                            │
│         ↓                                                                 │
│  7. 读取 hex 文件，Base64 编码                                             │
│         ↓                                                                 │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ JSON 响应
                                    │ { success: true, elf_data: "..." }
                                    ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                           前端 (浏览器)                                      │
│                                                                            │
│  8. 接收 Base64 编码的 ELF                                                 │
│         ↓                                                                  │
│  9. atob(elf_data) 解码为二进制                                             │
│         ↓                                                                  │
│ 10. WebSocket 发送 load_elf_binary 命令加载ELF                              │
│     ws://localhost:8081                                                    │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. 编译流程详解

### 5.1 整体流程

```
用户提交汇编代码
       │
       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        api_compile_server.py                               │
│                                                                             │
│  Step 1: 接收 HTTP POST 请求                                               │
│     - 解析 JSON 请求体                                                      │
│     - 提取 source 和 filename 字段                                          │
│           │                                                                 │
│           ▼                                                                 │
│  Step 2: 创建临时工作目录                                                   │
│     - 使用 tempfile.mkdtemp() 创建临时目录                                  │
│     - 写入 source_code 到临时 .S 文件                                        │
│           │                                                                 │
│           ▼                                                                 │
│  Step 3: 构建编译命令                                                       │
│     riscv-none-elf-gcc \                                                   │
│       -march=rv64g \          # 目标架构                                    │
│       -mabi=lp64d \           # ABI                                        │
│       -static \                # 静态链接                                   │
│       -nostdlib \              # 不链接标准库                               │
│       -nostartfiles \          # 不链接启动文件                             │
│       -mcmodel=medany \        # 代码模型                                   │
│       -fvisibility=hidden \   # 隐藏符号                                   │
│       -T link.ld \             # 链接脚本                                   │
│       isa/start.S \           # 启动文件                                    │
│       uploaded.S \            # 用户代码                                   │
│       -o output.elf           # 输出 ELF                                    │
│           │                                                                 │
│           ▼                                                                 │
│  Step 4: 执行编译 (subprocess.run)                                         │
│     - 捕获 stdout 和 stderr                                                │
│     - 超时时间: 30秒                                                        │
│           │                                                                 │
│           ▼                                                                 │
│  Step 5: 处理编译结果                                                       │
│     - 成功: 读取 ELF 文件 → Base64 编码 → 返回 JSON                          │
│     - 失败: 解析错误信息 → 返回错误详情                                       │
│           │                                                                 │
│           ▼                                                                 │
│  Step 6: 清理临时文件                                                       │
│     - shutil.rmtree() 删除临时目录                                          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
       │
       ▼
   返回 JSON 响应
```

### 5.2 核心代码实现

编译服务核心逻辑位于 `api_compile_server.py`：

```python
# api_compile_server.py 中的关键代码

def compile_source(self, source_code: str, filename: str):
    # 1. 创建临时目录
    temp_dir = Path(tempfile.mkdtemp())
    source_file = temp_dir / filename
    elf_file = temp_dir / 'output.elf'
    list_file = temp_dir / 'output.lst'

    try:
        # 2. 写入临时文件
        source_file.write_text(source_code, encoding='utf-8')

        # 3. 构建编译命令
        compile_cmd = [
            str(riscv_gcc),                    # riscv-none-elf-gcc
            '-march=' + config.ARCH,           # rv64g
            '-mabi=' + config.ABI,             # lp64d
        ] + config.GCC_OPTS.split(' ') + [    # -static -nostdlib...
            '-T', str(linker_script),          # 链接脚本
            '-I', str(Path(config.SOURCE_DIR)),# 头文件目录
            str(start_file),                   # 启动文件
            str(source_file),                  # 用户代码
            '-o', str(elf_file),               # 输出 ELF
            '-Wl,-Map=' + str(list_file),      # 列表文件
        ]

        # 4. 执行编译
        result = subprocess.run(
            compile_cmd,
            capture_output=True,
            text=True,
            timeout=30
        )

        # 5. 处理结果
        if result.returncode != 0:
            # 编译失败
            self.send_json_response({
                'success': False,
                'error': result.stderr,
                'line_errors': [...],
                'filename': filename
            })
        else:
            # 编译成功，Base64 编码 ELF
            with open(elf_file, 'rb') as f:
                elf_data = base64.b64encode(f.read()).decode('ascii')

            self.send_json_response({
                'success': True,
                'elf_data': elf_data,
                'filename': filename,
                'list_content': list_content
            })

    finally:
        # 6. 清理临时文件
        shutil.rmtree(temp_dir, ignore_errors=True)
```

### 5.3 编译命令详解

完整编译命令示例：

```bash
xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc \
  -march=rv64g \
  -mabi=lp64d \
  -static \
  -mcmodel=medany \
  -fvisibility=hidden \
  -nostdlib \
  -nostartfiles \
  -T isa/link.ld \
  -I isa \
  isa/start.S \
  uploaded.S \
  -o output.elf \
  -Wl,-Map=output.lst \
  -fno-pie -no-pie
```

| 参数 | 说明 | 作用 |
|------|------|------|
| `-march=rv64g` | 目标架构 | RISC-V 64位通用架构(G=IMAFDC) |
| `-mabi=lp64d` | ABI | 64位 LP ABI，支持双精度浮点 |
| `-static` | 静态链接 | 不依赖动态链接库 |
| `-nostdlib` | 不链接标准库 | 避免 C 标准库依赖 |
| `-nostartfiles` | 不链接启动文件 | 使用自定义启动代码 |
| `-mcmodel=medany` | 代码模型 | 支持任意地址代码 |
| `-fvisibility=hidden` | 隐藏符号 | 减少符号冲突 |
| `-T link.ld` | 链接脚本 | 定义内存布局 |
| `-Wl,-Map=output.lst` | 链接映射 | 生成列表文件 |

### 5.4 链接脚本作用

链接脚本 (`isa/link.ld`) 定义了程序的内存布局：

```ld
/* isa/link.ld 示例 */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x80000000, LENGTH = 128M
  RAM (rwx)  : ORIGIN = 0x80020000, LENGTH = 128M
}

SECTIONS
{
  .text : {
    *(.text.startup)
    *(.text)
  } > FLASH

  .data : {
    *(.data)
  } > RAM AT > FLASH
}
```

### 5.5 启动文件作用

启动文件 (`isa/start.S`) 初始化运行环境：

```assembly
/* isa/start.S 示例 */
.section .text.startup
.globl _start
_start:
    # 初始化堆栈指针
    la sp, _stack_top
    
    # 初始化 .data 段
    # 零化 .bss 段
    
    # 跳转到 main
    tail main
```

---

## 6. 编译选项

| 选项 | 值 | 说明 |
|------|-----|------|
| `-march` | `rv64g` | RISC-V 64位通用架构 |
| `-mabi` | `lp64d` | 64位 LP  ABI (双精度浮点) |
| `-static` | - | 静态链接 |
| `-nostdlib` | - | 不链接标准库 |
| `-nostartfiles` | - | 不链接启动文件 |
| `-mcmodel=medany` | - | 代码模型 |
| `-fvisibility=hidden` | - | 隐藏符号 |

---

## 7. 错误处理

### 7.1 编译错误示例

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

### 7.2 推荐错误处理代码

```javascript
async function compileWithErrorHandling(source) {
  try {
    const resp = await fetch('http://localhost:8083/api/compile', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ source, filename: 'test.S' })
    });

    const data = await resp.json();

    if (!data.success) {
      return {
        success: false,
        error: data.error,
        lineErrors: data.line_errors
      };
    }

    return {
      success: true,
      elfData: data.elf_data
    };
  } catch (err) {
    return {
      success: false,
      error: `网络错误: ${err.message}`
    };
  }
}
```

---

## 8. 环境要求

### 8.1 依赖服务

| 服务 | 端口 | 必须 | 说明 |
|------|------|------|------|
| 编译服务 | 8083 | **是** | RISC-V 汇编编译 |
| WebSocket服务 | 8081 | 否 | 仅在需要加载ELF到模拟器时需要 |

### 8.2 系统依赖

| 依赖 | 版本 | 说明 |
|------|------|------|
| Python | 3.8+ | 运行环境 |
| xpack-riscv-none-elf-gcc | 15.2.0+ | RISC-V 交叉编译工具链 |

### 8.3 启动服务

```bash
cd RISC-V_Platform
python api_compile_server.py    # 启动编译服务 (端口 8083)
python api_websocket_server.py  # 启动WebSocket服务 (端口 8081)
```

---

## 9. 快速参考

### 9.1 请求模板

```bash
curl -X POST http://localhost:8083/api/compile \
  -H "Content-Type: application/json" \
  -d '{
    "source": ".text\n_start:\n  addi x1, x0, 5",
    "filename": "test.S"
  }'
```

### 9.2 响应字段速查

| 字段 | 说明 |
|------|------|
| `success` | true = 成功, false = 失败 |
| `elf_data` | Base64编码的ELF文件内容 |
| `filename` | 原始文件名 |
| `error` | 错误信息（失败时）|
| `line_errors` | 行级错误列表（失败时）|

### 9.3 Base64 解码

```javascript
// 浏览器环境
const bytes = Uint8Array.from(atob(elfBase64), c => c.charCodeAt(0));

// Node.js 环境
const buffer = Buffer.from(elfBase64, 'base64');
```

---

## 10. 联系方式

如有问题或需要技术支持，请联系项目维护者。

---

*文档更新于 2026-04-02*
