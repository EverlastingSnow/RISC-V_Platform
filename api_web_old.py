"""
旧版 WebSocket 主服务（已弃用，请使用 api_websocket_server.py）。

与新版相比，差异点：
- 全局只维护单个 C++ 模拟器（不支持多用户并发仿真）
- WebSocket 仅绑定到 localhost
- 缺少 stats HTTP 服务与丰富的 difftest 扩展

保留该文件仅为历史参考与回滚。
"""

import asyncio
import websockets
import json
import subprocess
import os
import signal
import time
from typing import Optional, Dict, Any
from http.server import HTTPServer, BaseHTTPRequestHandler
import threading

import config
import stats

# 启动时打印模块路径，便于定位 import 错误
print(f"[STATS] stats module imported successfully. File: {stats.__file__}")
print(f"[CONFIG] config loaded from: {config.__file__}")

# WebSocket 服务监听端口（来自 config 模块）
PORT = config.WEBSOCKET_PORT
# C++ 模拟器可执行文件路径（来自 config 模块）
SIM_SERVER_PATH = config.SIM_SERVER_PATH


class CppSimulator:
    """
    旧版 C++ 模拟器进程封装。

    通过 stdin/stdout 管道与 riscv_sim_server 通信。
    新版（api_websocket_server.py）已重构此类以支持更高并发。
    """

    def __init__(self, exe_path: str = None):
        """
        初始化模拟器封装，自动探测可执行文件路径。

        Args:
            exe_path (str, optional): 模拟器可执行文件绝对路径，None 时自动在 build/ 下查找。
        """
        if exe_path is None:
            base_dir = os.path.dirname(os.path.abspath(__file__))
            exe_path = os.path.join(base_dir, "build", "riscv_sim_server.exe")
            if not os.path.exists(exe_path):
                # 兼容 Visual Studio 默认输出子目录
                exe_path = os.path.join(base_dir, "build", "Debug", "riscv_sim_server.exe")
        self.exe_path = exe_path
        # 子进程对象，start() 后才赋值
        self.process: Optional[subprocess.Popen] = None
        self.buffer = ""

    def start(self) -> bool:
        """
        启动 C++ 模拟器子进程。

        Returns:
            bool: 启动成功返回 True，可执行文件不存在或启动异常返回 False。
        """
        if not os.path.exists(self.exe_path):
            print(f"Error: Simulator executable not found at {self.exe_path}")
            return False
        try:
            # PIPE 模式建立双向文本通道；stderr 重定向到 stdout 便于统一读取
            self.process = subprocess.Popen(
                [self.exe_path],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT
            )
            return True
        except Exception as e:
            print(f"Error starting simulator: {e}")
            return False

    def send_command(self, cmd: str) -> Optional[str]:
        """
        向 C++ 模拟器发送一行命令并读取 JSON 响应。

        协议约定：
        - 调试日志以 `[DEBUG` 开头，需跳过
        - 业务响应必须是一段以 `{` 开头、括号配对正确的 JSON

        Args:
            cmd (str): 不含换行的命令字符串。

        Returns:
            Optional[str]: 成功返回完整 JSON 字符串；失败/超时返回 None。
        """
        if not self.process or self.process.stdin is None:
            return None

        try:
            print(f"[DEBUG] C++ stdin: sending '{cmd}'")
            try:
                self.process.stdin.write((cmd + "\n").encode('utf-8'))
                self.process.stdin.flush()
            except (OSError, IOError) as e:
                print(f"[DEBUG] Write error: {e}, attempting to reconnect...")
                return None

            buffer = ""
            # 最多读 100 行，覆盖 C++ 端的多行调试输出
            for _ in range(100):
                try:
                    line = self.process.stdout.readline()
                except (OSError, IOError) as e:
                    print(f"[DEBUG] Read error: {e}")
                    break
                if not line:
                    # 没有数据时短睡避免 CPU 空转
                    time.sleep(0.05)
                    continue
                if isinstance(line, bytes):
                    line = line.decode('utf-8', errors='replace')
                stripped = line.strip()
                if stripped:
                    print(f"[DEBUG] C++ stdout: {stripped[:200] if stripped else 'None'}")
                    # 跳过以 [DEBUG 开头的 C++ 调试日志
                    if stripped.startswith('[DEBUG'):
                        continue
                    buffer += stripped
                    # 通过花括号配对判断 JSON 是否完整
                    if stripped.startswith('{'):
                        brace_count = buffer.count('{') - buffer.count('}')
                        if brace_count == 0 and buffer.endswith('}'):
                            return buffer
            # 兜底：buffer 起始为 { 即视为已拿到部分响应
            if buffer.startswith('{'):
                return buffer
            return None
        except Exception as e:
            print(f"[DEBUG] Exception: {e}")
            return None

    def read_response(self) -> Optional[str]:
        """
        从 stdout 同步读取一行原始响应（不解析 JSON）。

        Returns:
            Optional[str]: 一行字符串；无数据或异常返回 None。
        """
        if not self.process or self.process.stdout is None:
            return None

        try:
            line = self.process.stdout.readline()
            if line:
                if isinstance(line, bytes):
                    line = line.decode('utf-8', errors='replace')
                return line.strip()
        except Exception as e:
            print(f"Error reading response: {e}")
        return None

    def load(self, filepath: str) -> bool:
        """
        加载 ELF/可执行文件到模拟器。

        Args:
            filepath (str): 文件绝对路径。

        Returns:
            bool: C++ 端返回 status=='ok' 时为 True。
        """
        result = self.send_command(f"load {filepath}")
        if result:
            try:
                data = json.loads(result)
                return data.get("status") == "ok"
            except:
                pass
        return False

    def step(self) -> Optional[Dict[str, Any]]:
        """
        单步执行一条指令并返回解析后的 JSON 响应。

        Returns:
            Optional[Dict[str, Any]]: 成功为解析后的 dict，失败为 None。
        """
        result = self.send_command("step")
        if result:
            try:
                return json.loads(result)
            except:
                pass
        return None

    def run(self, max_cycles: int = 10000) -> list:
        """
        连续执行直到 halted 或达到 max_cycles 上限。

        Args:
            max_cycles (int): 最大执行周期数（防止无限循环）。

        Returns:
            list: 每周期 step 返回的 dict 列表。
        """
        results = []
        for _ in range(max_cycles):
            data = self.step()
            if data:
                results.append(data)
                if data.get("halted", False):
                    break
            else:
                break
        return results

    def reset(self) -> bool:
        """
        复位模拟器。

        Returns:
            bool: C++ 端返回 status=='ok' 时为 True。
        """
        result = self.send_command("reset")
        if result:
            try:
                data = json.loads(result)
                return data.get("status") == "ok"
            except:
                pass
        return False

    def get_signals(self) -> Optional[Dict[str, Any]]:
        """
        获取当前所有信号。

        Returns:
            Optional[Dict[str, Any]]: 解析后的信号 dict，失败为 None。
        """
        result = self.send_command("signals")
        if result:
            try:
                return json.loads(result)
            except:
                pass
        return None

    def get_registers(self) -> Optional[Dict[str, Any]]:
        """
        获取当前寄存器快照。

        Returns:
            Optional[Dict[str, Any]]: 解析后的寄存器 dict，失败为 None。
        """
        result = self.send_command("registers")
        if result:
            try:
                return json.loads(result)
            except:
                pass
        return None

    def stop(self):
        """优雅停止子进程：先发 quit，再 terminate，必要时 kill。"""
        if self.process:
            try:
                self.process.stdin.write(b"quit\n")
                self.process.stdin.flush()
                self.process.terminate()
                # 给 2s 优雅退出时间
                self.process.wait(timeout=2)
            except:
                # 兜底强杀
                try:
                    self.process.kill()
                except:
                    pass
            self.process = None


class StatsHTTPHandler(BaseHTTPRequestHandler):
    """HTTP 处理器：对外暴露 /api/stats 查询与 /api/stats/reset 重置。"""

    def do_GET(self):
        """
        处理 GET /api/stats?start=YYYY-MM-DD&end=YYYY-MM-DD。

        Returns:
            None: 响应写入 wfile。
        """
        print(f"[STATS] HTTP GET request: {self.path}")
        if self.path.startswith("/api/stats"):
            import urllib.parse
            # 解析 query string 提取日期过滤参数
            parsed = urllib.parse.urlparse(self.path)
            params = urllib.parse.parse_qs(parsed.query)

            start_date = params.get("start", [None])[0]
            end_date = params.get("end", [None])[0]

            if start_date or end_date:
                data = stats.get_daily_stats(start_date, end_date)
            else:
                data = stats.get_stats()

            print(f"[STATS] Returning stats: {data}")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.end_headers()
            self.wfile.write(json.dumps(data).encode())
        else:
            # 非统计路径返回 404
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        """
        处理 POST /api/stats/reset：清空全部统计数据。

        Returns:
            None: 响应写入 wfile。
        """
        if self.path == "/api/stats/reset":
            stats.reset_stats()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok"}).encode())
        else:
            self.send_response(404)
            self.end_headers()

    def do_OPTIONS(self):
        """CORS 预检响应：放行 GET/POST/OPTIONS。"""
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def log_message(self, format, *args):
        """静默父类默认访问日志（避免污染控制台）。"""
        pass


class StatsHTTPServer(HTTPServer):
    """承载 StatsHTTPHandler 的 HTTP 服务容器，仅做端口绑定。"""

    def __init__(self, port, ws_server):
        """
        Args:
            port (int): 监听端口。
            ws_server: 对应的 WebSocketServer 引用（保留扩展位）。
        """
        super().__init__(("", port), StatsHTTPHandler)
        self.ws_server = ws_server


class WebSocketServer:
    """
    旧版 WebSocket 服务（单模拟器、串行处理命令）。

    新版（api_websocket_server.py）已重构为每客户端独立模拟器 + 并发命令分发。
    """

    def __init__(self):
        # 全局唯一的 C++ 模拟器实例
        self.sim: Optional[CppSimulator] = None
        # 后台连续运行任务（保留扩展位）
        self.run_task: Optional[asyncio.Task] = None
        # 连续运行标志位
        self.running = False
        # 命令处理互斥锁（同一时刻只处理一个客户端命令）
        self.command_lock = asyncio.Lock()
        # 初始化统计数据库
        stats.init_stats_db()
        # 客户端连接起始时间：key 为客户端 id
        self.client_start_times: Dict[str, float] = {}
        # 启动 HTTP 统计服务
        self._start_http_server()

    def _start_http_server(self):
        """在后台守护线程中启动 HTTP 统计服务（绑定 config.STATS_PORT）。"""
        HTTP_PORT = config.STATS_PORT
        try:
            server = StatsHTTPServer(HTTP_PORT, self)
            # daemon=True 保证主进程退出时该线程被回收
            thread = threading.Thread(target=server.serve_forever)
            thread.daemon = True
            thread.start()
            print(f"HTTP stats server running on port {HTTP_PORT}")
        except Exception as e:
            print(f"Failed to start HTTP server on port {HTTP_PORT}: {e}")

    def init_simulator(self) -> bool:
        """
        创建并启动 C++ 模拟器。

        Returns:
            bool: 启动成功为 True。
        """
        self.sim = CppSimulator()
        return self.sim.start()

    async def handle_client(self, websocket):
        """
        处理单个 WebSocket 客户端的整个生命周期。

        流程：进入时计访问 +1，断开时累加会话时长。
        主循环按 command 字段分发到不同的子分支。

        Args:
            websocket: websockets 库传入的连接对象。
        """
        # 用 id(websocket) 作为简易客户端标识（旧版无独立模拟器）
        client_id = str(id(websocket))
        start_time = time.time()
        duration_recorded = False
        # 访问次数 +1
        stats.increment_visit()
        print(f"[STATS] New client connected. Visit count incremented. Current: {stats.get_stats()}")
        try:
            async for message in websocket:
                # 同一时刻只处理一个命令，避免与全局单模拟器产生竞争
                async with self.command_lock:
                    try:
                        data = json.loads(message)
                        command = data.get('command', '')

                        if command == 'step':
                            if not self.sim:
                                response = {'status': 'error', 'message': 'Simulator not initialized'}
                            else:
                                print(f"[DEBUG] Sending step command to simulator...")
                                result = self.sim.send_command("step")
                                print(f"[DEBUG] Raw result from simulator: {result}")
                                if result:
                                    result = result.strip()
                                    # 跳过所有非 JSON 前缀行（如 [DEBUG ...]）
                                    while result and not result.startswith('{'):
                                        newline_idx = result.find('\n')
                                        if newline_idx == -1:
                                            break
                                        result = result[newline_idx+1:].strip()

                                    if result.startswith('{'):
                                        try:
                                            data = json.loads(result)
                                            print(f"[DEBUG] Parsed data: {data}")
                                            # 透传 C++ 端的特殊事件类型
                                            if data.get('type') == 'need_signal_input':
                                                print(f"[DEBUG] Received need_signal_input!")
                                                await websocket.send(json.dumps(data))
                                                continue
                                            elif data.get('type') == 'diff_detected':
                                                await websocket.send(json.dumps(data))
                                                continue
                                            elif data.get('type') == 'halted':
                                                await websocket.send(json.dumps(data))
                                                continue
                                            # 根据是否含 cycle 字段决定包装键名
                                            if 'cycle' in data:
                                                response = {'status': 'ok', 'signals': data}
                                            else:
                                                response = {'status': 'ok', 'data': data}
                                        except json.JSONDecodeError as e:
                                            print(f"JSON decode error: {e}, result: {result}")
                                            response = {'status': 'ok'}
                                    else:
                                        print(f"[DEBUG] No valid JSON found: {result}")
                                        response = {'status': 'ok'}
                                else:
                                    response = {'status': 'error', 'message': 'Failed to step'}
                            await websocket.send(json.dumps(response))

                        elif command == 'run':
                            # 进入连续运行模式
                            self.running = True
                            response = {'status': 'ok', 'message': 'Running...'}
                            await websocket.send(json.dumps(response))

                            # 循环 step 直到外部 stop / halted / 特殊事件
                            while self.running and self.sim:
                                print(f"[DEBUG] run: sending step...")
                                result = self.sim.send_command("step")
                                print(f"[DEBUG] run: received: {result}")
                                if result:
                                    try:
                                        data = json.loads(result)
                                        # 特殊事件直接转发并退出 run 循环
                                        if data.get('type') == 'need_signal_input':
                                            print(f"[DEBUG] run: Received need_signal_input!")
                                            self.running = False
                                            await websocket.send(json.dumps(data))
                                            break
                                        elif data.get('type') == 'diff_detected':
                                            self.running = False
                                            await websocket.send(json.dumps(data))
                                            break
                                        elif data.get('type') == 'halted':
                                            self.running = False
                                            await websocket.send(json.dumps(data))
                                            break
                                        # 常规周期信号：每周期主动推送 update
                                        if 'cycle' in data:
                                            await websocket.send(json.dumps({
                                                'status': 'update',
                                                'signals': data
                                            }))
                                            if data.get('halted', False):
                                                self.running = False
                                                break
                                    except:
                                        pass
                                # 短睡让出事件循环，避免忙等
                                await asyncio.sleep(0.05)

                        elif command == 'stop':
                            # 退出连续运行
                            self.running = False
                            response = {'status': 'ok', 'message': 'Stopped'}
                            await websocket.send(json.dumps(response))

                        elif command == 'reset':
                            if not self.sim:
                                response = {'status': 'error', 'message': 'Simulator not initialized'}
                            else:
                                self.sim.reset()
                                signals = self.sim.get_signals()
                                response = {'status': 'ok', 'message': 'Reset', 'signals': signals}
                            await websocket.send(json.dumps(response))

                        elif command == 'load':
                            filepath = data.get('path', '')
                            if not self.sim:
                                # load 时若模拟器未启动，尝试懒加载
                                if not self.init_simulator():
                                    response = {'status': 'error', 'message': 'Failed to initialize simulator'}
                                    await websocket.send(json.dumps(response))
                                    continue

                            result = self.sim.send_command(f"load {filepath}")
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    if resp_data.get('status') == 'ok':
                                        response = resp_data  # 直接透传 C++ 端响应（已含 signals）
                                    else:
                                        response = resp_data
                                except:
                                    response = {'status': 'ok', 'message': f'Loaded {filepath}'}
                            else:
                                response = {'status': 'error', 'message': f'Failed to load {filepath}'}
                            await websocket.send(json.dumps(response))

                        elif command == 'get_signals':
                            if not self.sim:
                                response = {'status': 'error', 'message': 'Simulator not initialized'}
                            else:
                                signals = self.sim.get_signals()
                                if signals:
                                    response = {'status': 'ok', 'signals': signals}
                                else:
                                    response = {'status': 'error', 'message': 'Failed to get signals'}
                            await websocket.send(json.dumps(response))

                        elif command == 'enable_difftest':
                            print(f"[DEBUG] enable_difftest command received: {data}")
                            signals_str = data.get('signals', '')
                            shadow_mode = data.get('shadowMode', False)
                            # 拼接命令：enable_difftest [--shadow] <signals>
                            cmd = 'enable_difftest'
                            if shadow_mode:
                                cmd += ' --shadow'
                            cmd += ' ' + signals_str
                            print(f"[DEBUG] Sending to C++: '{cmd}'")

                            result = self.sim.send_command(cmd)
                            print(f"[DEBUG] enable_difftest result: {result}")

                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    response = {'status': 'ok', 'message': resp_data.get('message', 'Difftest enabled')}
                                except:
                                    response = {'status': 'ok', 'message': f'Difftest enabled: {cmd}'}
                            else:
                                response = {'status': 'error', 'message': 'Failed to enable difftest'}
                            await websocket.send(json.dumps(response))

                        elif command == 'disable_difftest':
                            result = self.sim.send_command('disable_difftest')
                            if result:
                                response = {'status': 'ok', 'message': 'Difftest disabled'}
                            else:
                                response = {'status': 'error', 'message': 'Failed to disable difftest'}
                            await websocket.send(json.dumps(response))

                        elif command == 'set_user_signal':
                            signal_name = data.get('signalName', '')
                            value = data.get('value', False)
                            # 把布尔值序列化为 "true" / "false"
                            cmd = f'set_user_signal {signal_name} {"true" if value else "false"}'
                            result = self.sim.send_command(cmd)
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    # 透传特殊事件与周期信号
                                    if resp_data.get('type') == 'diff_detected':
                                        await websocket.send(result)
                                    elif resp_data.get('type') == 'need_signal_input':
                                        await websocket.send(result)
                                    elif resp_data.get('type') == 'halted':
                                        await websocket.send(result)
                                    elif resp_data.get('cycle') is not None:
                                        await websocket.send(result)
                                    else:
                                        response = {'status': 'ok', 'message': f'Signal {signal_name} set to {value}'}
                                        await websocket.send(json.dumps(response))
                                except json.JSONDecodeError:
                                    response = {'status': 'ok', 'message': f'Signal {signal_name} set to {value}'}
                                    await websocket.send(json.dumps(response))
                            else:
                                response = {'status': 'error', 'message': 'Failed to set signal'}
                                await websocket.send(json.dumps(response))

                        elif command == 'continue':
                            result = self.sim.send_command('continue')
                            if result:
                                response = {'status': 'ok', 'message': 'Continuing'}
                            else:
                                response = {'status': 'error', 'message': 'Failed to continue'}
                            await websocket.send(json.dumps(response))

                        elif command == 'load_test':
                            test_name = data.get('testName', '')
                            cmd = f'load_test {test_name}'
                            print(f"[DEBUG] Sending load_test command: '{cmd}'")
                            result = self.sim.send_command(cmd)
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    if resp_data.get('status') == 'ok':
                                        # 加载成功后立即拉取最新 signals
                                        signals = self.sim.get_signals()
                                        response = {'status': 'ok', 'message': resp_data.get('message', f'Loaded test {test_name}'), 'signals': signals}
                                    else:
                                        response = resp_data
                                except:
                                    response = {'status': 'ok', 'message': f'Loaded test {test_name}'}
                            else:
                                response = {'status': 'error', 'message': f'Failed to load test {test_name}'}
                            await websocket.send(json.dumps(response))

                        elif command == 'list_tests':
                            cmd = 'list_tests'
                            print(f"[DEBUG] Sending list_tests command")
                            result = self.sim.send_command(cmd)
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    if resp_data.get('status') == 'ok':
                                        response = resp_data
                                    else:
                                        response = {'status': 'ok', 'tests': []}
                                except:
                                    response = {'status': 'ok', 'tests': []}
                            else:
                                response = {'status': 'ok', 'tests': []}
                            await websocket.send(json.dumps(response))

                        elif command == 'list_elf_tests':
                            cmd = 'list_elf_tests'
                            print(f"[DEBUG] Sending list_elf_tests command")
                            result = self.sim.send_command(cmd)
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    if resp_data.get('status') == 'ok':
                                        response = resp_data
                                    else:
                                        response = {'status': 'ok', 'elfTests': []}
                                except:
                                    response = {'status': 'ok', 'elfTests': []}
                            else:
                                response = {'status': 'ok', 'elfTests': []}
                            await websocket.send(json.dumps(response))

                        elif command == 'load_elf_test':
                            test_name = data.get('testName', '')
                            cmd = f'load_elf_test {test_name}'
                            print(f"[DEBUG] Sending load_elf_test command: '{cmd}'")
                            result = self.sim.send_command(cmd)
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    if resp_data.get('status') == 'ok':
                                        signals = self.sim.get_signals()
                                        response = {'status': 'ok', 'message': resp_data.get('message', f'Loaded ELF test {test_name}'), 'signals': signals}
                                    else:
                                        response = resp_data
                                except:
                                    response = {'status': 'ok', 'message': f'Loaded ELF test {test_name}'}
                            else:
                                response = {'status': 'error', 'message': f'Failed to load ELF test {test_name}'}
                            await websocket.send(json.dumps(response))

                        elif command == 'load_elf_binary':
                            # 直接接收 base64 编码的 ELF 字节流
                            elf_base64 = data.get('elf_data', '')
                            if not elf_base64:
                                response = {'status': 'error', 'message': 'No ELF data provided'}
                                await websocket.send(json.dumps(response))
                            else:
                                import base64
                                import tempfile
                                import os
                                try:
                                    elf_bytes = base64.b64decode(elf_base64)
                                    print(f"[DEBUG] ELF decoded, size: {len(elf_bytes)} bytes")
                                    print(f"[DEBUG] ELF header: {elf_bytes[:16].hex()}")
                                    # 落盘为临时文件供 C++ 端读取
                                    temp_elf = tempfile.NamedTemporaryFile(delete=False, suffix='.elf')
                                    temp_elf.write(elf_bytes)
                                    temp_elf.close()
                                    print(f"[DEBUG] ELF saved to: {temp_elf.name}")
                                    cmd = f'load {temp_elf.name}'
                                    print(f"[DEBUG] Sending load command: {cmd}")
                                    result = self.sim.send_command(cmd)
                                    print(f"[DEBUG] load result: {result}")
                                    # 加载完即删除临时文件
                                    os.unlink(temp_elf.name)
                                    if result:
                                        try:
                                            resp_data = json.loads(result)
                                            if resp_data.get('status') == 'ok':
                                                signals = self.sim.get_signals()
                                                response = {'status': 'ok', 'message': 'Loaded ELF binary', 'signals': signals}
                                            else:
                                                response = resp_data
                                        except Exception as e:
                                            print(f"[DEBUG] parse error: {e}")
                                            response = {'status': 'ok', 'message': 'Loaded ELF binary'}
                                    else:
                                        response = {'status': 'error', 'message': 'Failed to load ELF - no response'}
                                except Exception as e:
                                    response = {'status': 'error', 'message': f'Failed to decode ELF: {str(e)}'}
                                await websocket.send(json.dumps(response))

                        elif command == 'get_registers':
                            if not self.sim:
                                response = {'status': 'error', 'message': 'Simulator not initialized'}
                            else:
                                registers = self.sim.get_registers()
                                if registers:
                                    response = {'status': 'ok', 'registers': registers}
                                else:
                                    response = {'status': 'error', 'message': 'Failed to get registers'}
                            await websocket.send(json.dumps(response))

                        else:
                            # 未知命令：返回错误
                            response = {'status': 'error', 'message': 'Unknown command'}
                            await websocket.send(json.dumps(response))

                    except json.JSONDecodeError:
                        response = {'status': 'error', 'message': 'Invalid JSON'}
                        await websocket.send(json.dumps(response))
                    except Exception as e:
                        # 兜底异常
                        response = {'status': 'error', 'message': str(e)}
                        await websocket.send(json.dumps(response))
        except websockets.exceptions.ConnectionClosed:
            # 客户端断开时停止连续运行
            self.running = False
        finally:
            # 客户端断开后累加本次会话时长（仅记录一次）
            if not duration_recorded:
                duration_recorded = True
                duration = int(time.time() - start_time)
                if duration > 0:
                    stats.add_duration(duration)
                    stats.flush_all()
                    print(f"[STATS] Client disconnected. Duration: {duration}s. Current: {stats.get_stats()}")

    async def start(self):
        """启动 WebSocket 服务并阻塞主协程。"""
        print(f"Initializing C++ simulator...")
        if not self.init_simulator():
            # 启动失败不阻塞 WebSocket：等客户端发 'load' 命令时再懒加载
            print("Warning: Failed to initialize simulator. Client must send 'load' command first.")

        # 旧版仅绑定 localhost，不对外暴露
        async with websockets.serve(self.handle_client, "localhost", PORT):
            print(f"WebSocket server running on ws://localhost:{PORT}")
            print(f"Simulator executable: {self.sim.exe_path if self.sim else 'N/A'}")
            # 永久挂起直到外部取消
            await asyncio.Future()


if __name__ == "__main__":
    server = WebSocketServer()
    try:
        asyncio.run(server.start())
    except KeyboardInterrupt:
        print("\nShutting down...")
        if server.sim:
            server.sim.stop()
