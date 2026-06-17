"""
RISC-V 汇编编译服务（HTTP）。

接收前端上传的汇编源码（JSON 或 multipart/form-data），
调用 RISC-V GCC 工具链编译、链接并产出 ELF 二进制文件，
通过 base64 编码后返回给前端供下载/加载。
"""

import http.server
import socketserver
import json
import subprocess
import os
import tempfile
import base64
import re
from pathlib import Path
from urllib.parse import parse_qs

import config

# 编译服务监听端口（可通过环境变量 COMPILE_SERVER_PORT 覆盖）
COMPILE_SERVER_PORT = int(os.environ.get("COMPILE_SERVER_PORT", 8083))


class CompileRequestHandler(http.server.SimpleHTTPRequestHandler):
    """处理 /api/compile 端点的 HTTP 请求，同时复用父类提供静态文件兜底。"""

    def do_POST(self):
        """
        POST 请求入口：仅识别 /api/compile，其他路径返回 404。

        Returns:
            None: 响应直接写入 wfile。
        """
        if self.path == '/api/compile':
            self.handle_compile()
        else:
            self.send_error(404, 'Not Found')

    def do_OPTIONS(self):
        """CORS 预检响应：放行 POST/OPTIONS。"""
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def handle_compile(self):
        """
        编译请求总入口。

        根据 Content-Type 自动选择 JSON 或 multipart/form-data 处理分支，
        任何未捕获异常都会被转为失败响应返回。
        """
        try:
            content_type = self.headers.get('Content-Type', '')
            if 'multipart/form-data' in content_type:
                self.handle_multipart()
            else:
                self.handle_json()
        except Exception as e:
            # 兜底：把异常文本回给客户端，避免 500 静默
            self.send_json_response({'success': False, 'error': str(e)})

    def handle_multipart(self):
        """
        解析 multipart/form-data 格式的编译请求。

        1. 读取完整请求体
        2. 提取 boundary 并按 boundary 切分
        3. 提取文件名（Content-Disposition 中的 filename）以及源码部分
        4. 触发编译
        """
        content_length = int(self.headers['Content-Length'])
        body = self.rfile.read(content_length)

        # 海象操作符先取出 Content-Type，再从中解析 boundary
        boundary_match = re.search(r'boundary=(.+)', content_type) if (content_type := self.headers.get('Content-Type', '')) else None
        if not boundary_match:
            self.send_json_response({'success': False, 'error': 'No boundary found'})
            return

        boundary = boundary_match.group(1).encode()

        parts = body.split(b'--' + boundary)
        source_code = None
        filename = None

        # 遍历每个 part，识别文件字段名与源码体
        for part in parts:
            if b'Content-Disposition: form-data' not in part:
                continue
            if b'filename=' in part:
                # 提取 filename="xxx" 中的值
                filename_match = re.search(r'filename="([^"]+)"', part.decode('utf-8', errors='ignore'))
                if filename_match:
                    filename = filename_match.group(1)
            if b'\n\n' in part:
                # 跳过 part header，取 header 之后到下一个 boundary 之前的内容为源码
                header_end = part.index(b'\n\n')
                source_code = part[header_end + 2:].split(b'--')[0].decode('utf-8', errors='ignore')

        if source_code is None:
            self.send_json_response({'success': False, 'error': 'No source code found in request'})
            return

        self.compile_source(source_code, filename or 'uploaded.S')

    def handle_json(self):
        """
        解析 application/json 格式的编译请求。

        期望 JSON 字段：
        - source / code：汇编源码（任一即可）
        - filename：可选的输出文件名
        """
        content_length = int(self.headers['Content-Length'])
        body = self.rfile.read(content_length)
        data = json.loads(body)

        # 兼容 source / code 两种命名
        source_code = data.get('source') or data.get('code')
        if not source_code:
            self.send_json_response({'success': False, 'error': 'No source code provided'})
            return

        filename = data.get('filename', 'uploaded.S')
        self.compile_source(source_code, filename)

    def compile_source(self, source_code: str, filename: str):
        """
        核心编译流程：在临时目录中创建源文件、调用 GCC 编译、读取 ELF 产物。

        Args:
            source_code (str): 汇编源码文本。
            filename (str): 源文件名（用于 GCC 输入与最终响应回传）。

        Returns:
            None: 编译结果通过 self.send_json_response 写回。
        """
        # 每次编译使用独立的临时目录，避免并发请求互相污染
        temp_dir = Path(tempfile.mkdtemp())
        source_file = temp_dir / filename
        elf_file = temp_dir / 'output.elf'
        bin_file = temp_dir / 'output.bin'
        list_file = temp_dir / 'output.lst'

        try:
            source_file.write_text(source_code, encoding='utf-8')

            # 解析工具链可执行文件路径，兼容 .exe 后缀
            riscv_gcc = Path(config.RISCV_TOOLCHAIN_PATH) / 'riscv-none-elf-gcc'
            if not riscv_gcc.exists():
                riscv_gcc = Path(config.RISCV_TOOLCHAIN_PATH) / 'riscv-none-elf-gcc.exe'

            objcopy = riscv_gcc.parent / 'riscv-none-elf-objcopy'
            if not objcopy.exists():
                objcopy = riscv_gcc.parent / 'riscv-none-elf-objcopy.exe'

            linker_script = Path(config.LINKER_SCRIPT)
            start_file = Path(config.START_FILE)

            # 构造 GCC 编译命令：arch/abi + 通用选项 + 链接脚本 + 启动文件 + 源文件
            compile_cmd = [
                str(riscv_gcc),
                '-march=' + config.ARCH,
                '-mabi=' + config.ABI,
            ] + config.GCC_OPTS.split(' ') + [
                '-T', str(linker_script),
                '-I', str(Path(config.SOURCE_DIR)),
                str(start_file),
                str(source_file),
                '-o', str(elf_file),
                '-Wl,-Map=' + str(list_file),
                '-fno-pie', '-no-pie'
            ]

            # 过滤空字符串项（防御性：避免 GCC 收到空参数）
            compile_cmd = [c for c in compile_cmd if c]

            # 30s 超时防止恶意/超长编译阻塞服务
            result = subprocess.run(
                compile_cmd,
                capture_output=True,
                text=True,
                timeout=30
            )

            if result.returncode != 0:
                error_msg = result.stderr or result.stdout

                # 提取所有 error 行，方便前端按行号高亮展示
                line_errors = []
                for line in error_msg.split('\n'):
                    if 'error:' in line.lower() or 'Error' in line:
                        line_errors.append(line.strip())

                self.send_json_response({
                    'success': False,
                    'error': error_msg,
                    'line_errors': line_errors,
                    'filename': filename
                })
                return

            # 防御性检查：理论上 returncode==0 必有产物
            if not elf_file.exists():
                self.send_json_response({
                    'success': False,
                    'error': 'Compilation succeeded but ELF file not found'
                })
                return

            # 将 ELF 文件以 base64 形式返回（避免 HTTP 传输二进制兼容问题）
            with open(elf_file, 'rb') as f:
                elf_data = base64.b64encode(f.read()).decode('ascii')

            # 读取链接阶段产出的 Map 文件，便于前端展示内存布局
            list_content = ''
            if list_file.exists():
                list_content = list_file.read_text(encoding='utf-8', errors='ignore')

            self.send_json_response({
                'success': True,
                'elf_data': elf_data,
                'filename': filename,
                'list_content': list_content
            })

        except subprocess.TimeoutExpired:
            # 编译超时
            self.send_json_response({'success': False, 'error': 'Compilation timeout (30s)'})
        except Exception as e:
            # 其它异常统一捕获
            self.send_json_response({'success': False, 'error': str(e)})
        finally:
            # 无论成功失败，都清理临时目录
            import shutil
            shutil.rmtree(temp_dir, ignore_errors=True)

    def send_json_response(self, data):
        """
        统一的 JSON 响应出口，自动附加 CORS 头与 Content-Length。

        Args:
            data (dict): 待序列化为 JSON 的响应体。
        """
        response = json.dumps(data, ensure_ascii=False)
        self.send_response(200)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Content-Length', len(response))
        self.end_headers()
        self.wfile.write(response.encode('utf-8'))


class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    """多线程 HTTP 服务：每个请求由独立线程处理，避免编译阻塞其他请求。"""
    # 允许端口快速重用（避免 TIME_WAIT 导致重启失败）
    allow_reuse_address = True


if __name__ == '__main__':
    print(f"Compile server starting on port {COMPILE_SERVER_PORT}")
    print(f"Toolchain: {config.RISCV_TOOLCHAIN_PATH}")
    with ThreadedHTTPServer(('', COMPILE_SERVER_PORT), CompileRequestHandler) as httpd:
        print(f"Compile server running at http://localhost:{COMPILE_SERVER_PORT}")
        httpd.serve_forever()
