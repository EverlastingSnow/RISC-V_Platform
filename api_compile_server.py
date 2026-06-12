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

COMPILE_SERVER_PORT = int(os.environ.get("COMPILE_SERVER_PORT", 8083))

class CompileRequestHandler(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path == '/api/compile':
            self.handle_compile()
        else:
            self.send_error(404, 'Not Found')

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def handle_compile(self):
        try:
            content_type = self.headers.get('Content-Type', '')
            if 'multipart/form-data' in content_type:
                self.handle_multipart()
            else:
                self.handle_json()
        except Exception as e:
            self.send_json_response({'success': False, 'error': str(e)})

    def handle_multipart(self):
        content_length = int(self.headers['Content-Length'])
        body = self.rfile.read(content_length)

        boundary_match = re.search(r'boundary=(.+)', content_type) if (content_type := self.headers.get('Content-Type', '')) else None
        if not boundary_match:
            self.send_json_response({'success': False, 'error': 'No boundary found'})
            return

        boundary = boundary_match.group(1).encode()

        parts = body.split(b'--' + boundary)
        source_code = None
        filename = None

        for part in parts:
            if b'Content-Disposition: form-data' not in part:
                continue
            if b'filename=' in part:
                filename_match = re.search(r'filename="([^"]+)"', part.decode('utf-8', errors='ignore'))
                if filename_match:
                    filename = filename_match.group(1)
            if b'\n\n' in part:
                header_end = part.index(b'\n\n')
                source_code = part[header_end + 2:].split(b'--')[0].decode('utf-8', errors='ignore')

        if source_code is None:
            self.send_json_response({'success': False, 'error': 'No source code found in request'})
            return

        self.compile_source(source_code, filename or 'uploaded.S')

    def handle_json(self):
        content_length = int(self.headers['Content-Length'])
        body = self.rfile.read(content_length)
        data = json.loads(body)

        source_code = data.get('source') or data.get('code')
        if not source_code:
            self.send_json_response({'success': False, 'error': 'No source code provided'})
            return

        filename = data.get('filename', 'uploaded.S')
        self.compile_source(source_code, filename)

    def compile_source(self, source_code: str, filename: str):
        temp_dir = Path(tempfile.mkdtemp())
        source_file = temp_dir / filename
        elf_file = temp_dir / 'output.elf'
        bin_file = temp_dir / 'output.bin'
        list_file = temp_dir / 'output.lst'

        try:
            source_file.write_text(source_code, encoding='utf-8')

            riscv_gcc = Path(config.RISCV_TOOLCHAIN_PATH) / 'riscv-none-elf-gcc'
            if not riscv_gcc.exists():
                riscv_gcc = Path(config.RISCV_TOOLCHAIN_PATH) / 'riscv-none-elf-gcc.exe'

            objcopy = riscv_gcc.parent / 'riscv-none-elf-objcopy'
            if not objcopy.exists():
                objcopy = riscv_gcc.parent / 'riscv-none-elf-objcopy.exe'

            linker_script = Path(config.LINKER_SCRIPT)
            start_file = Path(config.START_FILE)

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

            compile_cmd = [c for c in compile_cmd if c]

            result = subprocess.run(
                compile_cmd,
                capture_output=True,
                text=True,
                timeout=30
            )

            if result.returncode != 0:
                error_msg = result.stderr or result.stdout

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

            if not elf_file.exists():
                self.send_json_response({
                    'success': False,
                    'error': 'Compilation succeeded but ELF file not found'
                })
                return

            with open(elf_file, 'rb') as f:
                elf_data = base64.b64encode(f.read()).decode('ascii')

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
            self.send_json_response({'success': False, 'error': 'Compilation timeout (30s)'})
        except Exception as e:
            self.send_json_response({'success': False, 'error': str(e)})
        finally:
            import shutil
            shutil.rmtree(temp_dir, ignore_errors=True)

    def send_json_response(self, data):
        response = json.dumps(data, ensure_ascii=False)
        self.send_response(200)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Content-Length', len(response))
        self.end_headers()
        self.wfile.write(response.encode('utf-8'))

class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    allow_reuse_address = True

if __name__ == '__main__':
    print(f"Compile server starting on port {COMPILE_SERVER_PORT}")
    print(f"Toolchain: {config.RISCV_TOOLCHAIN_PATH}")
    with ThreadedHTTPServer(('', COMPILE_SERVER_PORT), CompileRequestHandler) as httpd:
        print(f"Compile server running at http://localhost:{COMPILE_SERVER_PORT}")
        httpd.serve_forever()
