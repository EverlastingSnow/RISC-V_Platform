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
import uuid

import config
import stats

print(f"[STATS] stats module imported successfully. File: {stats.__file__}")
print(f"[CONFIG] config loaded from: {config.__file__}")

PORT = config.WEBSOCKET_PORT
HOST = os.environ.get("HOST", "0.0.0.0")
SIM_SERVER_PATH = config.SIM_SERVER_PATH
MAX_CONCURRENT_SIMULATORS = 100


class CppSimulator:
    def __init__(self, exe_path: str = None):
        if exe_path is None:
            base_dir = os.path.dirname(os.path.abspath(__file__))
            exe_path = os.path.join(base_dir, "build", "riscv_sim_server")
            if not os.path.exists(exe_path):
                exe_path = os.path.join(base_dir, "build", "Debug", "riscv_sim_server")
        self.exe_path = exe_path
        self.process: Optional[subprocess.Popen] = None
        self.buffer = ""

    def start(self) -> bool:
        if not os.path.exists(self.exe_path):
            print(f"Error: Simulator executable not found at {self.exe_path}")
            return False
        try:
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
            for _ in range(100):
                try:
                    line = self.process.stdout.readline()
                except (OSError, IOError) as e:
                    print(f"[DEBUG] Read error: {e}")
                    break
                if not line:
                    time.sleep(0.05)
                    continue
                if isinstance(line, bytes):
                    line = line.decode('utf-8', errors='replace')
                stripped = line.strip()
                if stripped:
                    print(f"[DEBUG] C++ stdout: {stripped[:200] if stripped else 'None'}")
                    if stripped.startswith('[DEBUG'):
                        continue
                    buffer += stripped
                    if stripped.startswith('{'):
                        brace_count = buffer.count('{') - buffer.count('}')
                        if brace_count == 0 and buffer.endswith('}'):
                            return buffer
            if buffer.startswith('{'):
                return buffer
            return None
        except Exception as e:
            print(f"[DEBUG] Exception: {e}")
            return None

    def read_response(self) -> Optional[str]:
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
        result = self.send_command(f"load {filepath}")
        if result:
            try:
                data = json.loads(result)
                return data.get("status") == "ok"
            except:
                pass
        return False

    def step(self) -> Optional[Dict[str, Any]]:
        result = self.send_command("step")
        if result:
            try:
                return json.loads(result)
            except:
                pass
        return None

    def run(self, max_cycles: int = 10000) -> list:
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
        result = self.send_command("reset")
        if result:
            try:
                data = json.loads(result)
                return data.get("status") == "ok"
            except:
                pass
        return False

    def get_signals(self) -> Optional[Dict[str, Any]]:
        result = self.send_command("signals")
        if result:
            try:
                return json.loads(result)
            except:
                pass
        return None

    def get_registers(self) -> Optional[Dict[str, Any]]:
        result = self.send_command("registers")
        if result:
            try:
                return json.loads(result)
            except:
                pass
        return None

    def stop(self):
        if self.process:
            try:
                self.process.stdin.write(b"quit\n")
                self.process.stdin.flush()
                self.process.terminate()
                self.process.wait(timeout=2)
            except:
                try:
                    self.process.kill()
                except:
                    pass
            self.process = None


class StatsHTTPHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        print(f"[STATS] HTTP GET request: {self.path}")
        if self.path.startswith("/api/stats"):
            import urllib.parse
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
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
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
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def log_message(self, format, *args):
        pass


class StatsHTTPServer(HTTPServer):
    def __init__(self, port, ws_server):
        super().__init__(("", port), StatsHTTPHandler)
        self.ws_server = ws_server


class WebSocketServer:
    def __init__(self):
        self.simulators: Dict[str, CppSimulator] = {}
        self.running_states: Dict[str, bool] = {}
        stats.init_stats_db()
        self.client_start_times: Dict[str, float] = {}
        self.session_counter = 0
        self._lock = asyncio.Lock()
        self._start_http_server()

    def _start_http_server(self):
        HTTP_PORT = config.STATS_PORT
        try:
            server = StatsHTTPServer(HTTP_PORT, self)
            thread = threading.Thread(target=server.serve_forever)
            thread.daemon = True
            thread.start()
            print(f"HTTP stats server running on port {HTTP_PORT}")
        except Exception as e:
            print(f"Failed to start HTTP server on port {HTTP_PORT}: {e}")

    def create_simulator(self, client_id: str) -> Optional[CppSimulator]:
        sim = CppSimulator()
        if sim.start():
            self.simulators[client_id] = sim
            print(f"[SESSION] Created simulator for client {client_id}. Total: {len(self.simulators)}")
            return sim
        return None

    def destroy_simulator(self, client_id: str):
        if client_id in self.simulators:
            self.simulators[client_id].stop()
            del self.simulators[client_id]
            print(f"[SESSION] Destroyed simulator for client {client_id}. Remaining: {len(self.simulators)}")

    async def handle_client(self, websocket):
        client_id = str(uuid.uuid4())[:8]
        start_time = time.time()
        duration_recorded = False
        stats.increment_visit()

        print(f"[SESSION] New client {client_id} connected. Total active: {len(self.simulators)}")

        if len(self.simulators) >= MAX_CONCURRENT_SIMULATORS:
            print(f"[WARNING] Max concurrent simulators reached: {MAX_CONCURRENT_SIMULATORS}")
            await websocket.send(json.dumps({
                'status': 'error',
                'message': f'Server at maximum capacity ({MAX_CONCURRENT_SIMULATORS} users)'
            }))
            await websocket.close()
            return

        if not self.create_simulator(client_id):
            print(f"[ERROR] Failed to create simulator for client {client_id}")
            await websocket.send(json.dumps({
                'status': 'error',
                'message': 'Failed to initialize simulator'
            }))
            await websocket.close()
            return

        self.running_states[client_id] = False

        try:
            async for message in websocket:
                try:
                    data = json.loads(message)
                    command = data.get('command', '')
                    sim = self.simulators.get(client_id)

                    if command == 'step':
                        if not sim:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        else:
                            result = sim.send_command("step")
                            if result:
                                result = result.strip()
                                while result and not result.startswith('{'):
                                    newline_idx = result.find('\n')
                                    if newline_idx == -1:
                                        break
                                    result = result[newline_idx+1:].strip()

                                if result.startswith('{'):
                                    try:
                                        parsed_data = json.loads(result)
                                        if parsed_data.get('type') == 'need_signal_input':
                                            await websocket.send(json.dumps(parsed_data))
                                            continue
                                        elif parsed_data.get('type') == 'diff_detected':
                                            await websocket.send(json.dumps(parsed_data))
                                            continue
                                        elif parsed_data.get('type') == 'halted':
                                            await websocket.send(json.dumps(parsed_data))
                                            continue
                                        if 'cycle' in parsed_data:
                                            response = {'status': 'ok', 'signals': parsed_data}
                                        else:
                                            response = {'status': 'ok', 'data': parsed_data}
                                    except json.JSONDecodeError:
                                        response = {'status': 'ok'}
                                else:
                                    response = {'status': 'ok'}
                            else:
                                response = {'status': 'error', 'message': 'Failed to step'}
                        await websocket.send(json.dumps(response))

                    elif command == 'run':
                        self.running_states[client_id] = True
                        response = {'status': 'ok', 'message': 'Running...'}
                        await websocket.send(json.dumps(response))

                        sim = self.simulators.get(client_id)
                        while self.running_states.get(client_id, False) and sim:
                            result = sim.send_command("step")
                            if result:
                                try:
                                    parsed_data = json.loads(result)
                                    if parsed_data.get('type') == 'need_signal_input':
                                        self.running_states[client_id] = False
                                        await websocket.send(json.dumps(parsed_data))
                                        break
                                    elif parsed_data.get('type') == 'diff_detected':
                                        self.running_states[client_id] = False
                                        await websocket.send(json.dumps(parsed_data))
                                        break
                                    elif parsed_data.get('type') == 'halted':
                                        self.running_states[client_id] = False
                                        await websocket.send(json.dumps(parsed_data))
                                        break
                                    if 'cycle' in parsed_data:
                                        await websocket.send(json.dumps({
                                            'status': 'update',
                                            'signals': parsed_data
                                        }))
                                        if parsed_data.get('halted', False):
                                            self.running_states[client_id] = False
                                            break
                                except:
                                    pass
                            await asyncio.sleep(0.05)

                    elif command == 'stop':
                        self.running_states[client_id] = False
                        response = {'status': 'ok', 'message': 'Stopped'}
                        await websocket.send(json.dumps(response))

                    elif command == 'trigger_interrupt':
                        bit = int(data.get('bit', 3))
                        sim = self.simulators.get(client_id)
                        if not sim:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        else:
                            result = sim.send_command(f"trigger_interrupt {bit}")
                            if result:
                                try:
                                    response = json.loads(result)
                                except:
                                    response = {'status': 'ok', 'message': f'Interrupt {bit} triggered'}
                                # ★ 触发后立刻拉取最新 signals（C++ 端只在 step/signals 响应里附带 csr 字段），
                                # 否则前端 mip 仍是触发前的旧值，看不到 0x0 → 0x8 的跳变
                                latest = sim.get_signals()
                                if latest:
                                    response['signals'] = latest
                            else:
                                response = {'status': 'error', 'message': 'Failed to trigger interrupt'}
                        await websocket.send(json.dumps(response))

                    elif command == 'reset':
                        sim = self.simulators.get(client_id)
                        if not sim:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        else:
                            sim.reset()
                            signals = sim.get_signals()
                            response = {'status': 'ok', 'message': 'Reset', 'signals': signals}
                        await websocket.send(json.dumps(response))

                    elif command == 'load':
                        filepath = data.get('path', '')
                        sim = self.simulators.get(client_id)
                        if not sim:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        else:
                            result = sim.send_command(f"load {filepath}")
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    response = resp_data
                                except:
                                    response = {'status': 'ok', 'message': f'Loaded {filepath}'}
                            else:
                                response = {'status': 'error', 'message': f'Failed to load {filepath}'}
                        await websocket.send(json.dumps(response))

                    elif command == 'get_signals':
                        sim = self.simulators.get(client_id)
                        if not sim:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        else:
                            signals = sim.get_signals()
                            if signals:
                                response = {'status': 'ok', 'signals': signals}
                            else:
                                response = {'status': 'error', 'message': 'Failed to get signals'}
                        await websocket.send(json.dumps(response))

                    elif command == 'enable_difftest':
                        signals_str = data.get('signals', '')
                        shadow_mode = data.get('shadowMode', False)
                        cmd = 'enable_difftest'
                        if shadow_mode:
                            cmd += ' --shadow'
                        cmd += ' ' + signals_str

                        sim = self.simulators.get(client_id)
                        if sim:
                            result = sim.send_command(cmd)
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    response = {'status': 'ok', 'message': resp_data.get('message', 'Difftest enabled')}
                                except:
                                    response = {'status': 'ok', 'message': f'Difftest enabled: {cmd}'}
                            else:
                                response = {'status': 'error', 'message': 'Failed to enable difftest'}
                        else:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        await websocket.send(json.dumps(response))

                    elif command == 'disable_difftest':
                        sim = self.simulators.get(client_id)
                        if sim:
                            result = sim.send_command('disable_difftest')
                            if result:
                                response = {'status': 'ok', 'message': 'Difftest disabled'}
                            else:
                                response = {'status': 'error', 'message': 'Failed to disable difftest'}
                        else:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        await websocket.send(json.dumps(response))

                    elif command == 'set_user_signal':
                        signal_name = data.get('signalName', '')
                        value = data.get('value', False)
                        cmd = f'set_user_signal {signal_name} {"true" if value else "false"}'
                        sim = self.simulators.get(client_id)
                        if sim:
                            result = sim.send_command(cmd)
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    if resp_data.get('type') == 'diff_detected':
                                        await websocket.send(json.dumps(resp_data))
                                    elif resp_data.get('type') == 'need_signal_input':
                                        await websocket.send(json.dumps(resp_data))
                                    elif resp_data.get('type') == 'halted':
                                        await websocket.send(json.dumps(resp_data))
                                    elif resp_data.get('cycle') is not None:
                                        await websocket.send(json.dumps(resp_data))
                                    else:
                                        response = {'status': 'ok', 'message': f'Signal {signal_name} set to {value}'}
                                        await websocket.send(json.dumps(response))
                                except json.JSONDecodeError:
                                    response = {'status': 'ok', 'message': f'Signal {signal_name} set to {value}'}
                                    await websocket.send(json.dumps(response))
                            else:
                                response = {'status': 'error', 'message': 'Failed to set signal'}
                                await websocket.send(json.dumps(response))
                        else:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                            await websocket.send(json.dumps(response))

                    elif command == 'continue':
                        sim = self.simulators.get(client_id)
                        if sim:
                            result = sim.send_command('continue')
                            if result:
                                response = {'status': 'ok', 'message': 'Continuing'}
                            else:
                                response = {'status': 'error', 'message': 'Failed to continue'}
                        else:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        await websocket.send(json.dumps(response))

                    elif command == 'load_test':
                        test_name = data.get('testName', '')
                        cmd = f'load_test {test_name}'
                        sim = self.simulators.get(client_id)
                        if sim:
                            result = sim.send_command(cmd)
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    if resp_data.get('status') == 'ok':
                                        signals = sim.get_signals()
                                        response = {'status': 'ok', 'message': resp_data.get('message', f'Loaded test {test_name}'), 'signals': signals}
                                    else:
                                        response = resp_data
                                except:
                                    response = {'status': 'ok', 'message': f'Loaded test {test_name}'}
                            else:
                                response = {'status': 'error', 'message': f'Failed to load test {test_name}'}
                        else:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        await websocket.send(json.dumps(response))

                    elif command == 'list_tests':
                        sim = self.simulators.get(client_id)
                        if sim:
                            result = sim.send_command('list_tests')
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    response = resp_data
                                except:
                                    response = {'status': 'ok', 'tests': []}
                            else:
                                response = {'status': 'ok', 'tests': []}
                        else:
                            response = {'status': 'ok', 'tests': []}
                        await websocket.send(json.dumps(response))

                    elif command == 'list_elf_tests':
                        sim = self.simulators.get(client_id)
                        if sim:
                            result = sim.send_command('list_elf_tests')
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    response = resp_data
                                except:
                                    response = {'status': 'ok', 'elfTests': []}
                            else:
                                response = {'status': 'ok', 'elfTests': []}
                        else:
                            response = {'status': 'ok', 'elfTests': []}
                        await websocket.send(json.dumps(response))

                    elif command == 'load_elf_test':
                        test_name = data.get('testName', '')
                        cmd = f'load_elf_test {test_name}'
                        sim = self.simulators.get(client_id)
                        if sim:
                            result = sim.send_command(cmd)
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    if resp_data.get('status') == 'ok':
                                        signals = sim.get_signals()
                                        response = {'status': 'ok', 'message': resp_data.get('message', f'Loaded ELF test {test_name}'), 'signals': signals}
                                    else:
                                        response = resp_data
                                except:
                                    response = {'status': 'ok', 'message': f'Loaded ELF test {test_name}'}
                            else:
                                response = {'status': 'error', 'message': f'Failed to load ELF test {test_name}'}
                        else:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        await websocket.send(json.dumps(response))

                    elif command == 'load_elf_binary':
                        elf_base64 = data.get('elf_data', '')
                        if not elf_base64:
                            response = {'status': 'error', 'message': 'No ELF data provided'}
                            await websocket.send(json.dumps(response))
                        else:
                            import base64
                            import tempfile
                            try:
                                elf_bytes = base64.b64decode(elf_base64)
                                temp_elf = tempfile.NamedTemporaryFile(delete=False, suffix='.elf')
                                temp_elf.write(elf_bytes)
                                temp_elf.close()

                                sim = self.simulators.get(client_id)
                                if sim:
                                    cmd = f'load {temp_elf.name}'
                                    result = sim.send_command(cmd)
                                    os.unlink(temp_elf.name)
                                    if result:
                                        try:
                                            resp_data = json.loads(result)
                                            if resp_data.get('status') == 'ok':
                                                signals = sim.get_signals()
                                                response = {'status': 'ok', 'message': 'Loaded ELF binary', 'signals': signals}
                                            else:
                                                response = resp_data
                                        except:
                                            response = {'status': 'ok', 'message': 'Loaded ELF binary'}
                                    else:
                                        response = {'status': 'error', 'message': 'Failed to load ELF'}
                                else:
                                    response = {'status': 'error', 'message': 'Simulator not initialized'}
                                await websocket.send(json.dumps(response))
                            except Exception as e:
                                response = {'status': 'error', 'message': f'Failed to decode ELF: {str(e)}'}
                                await websocket.send(json.dumps(response))

                    elif command == 'get_registers':
                        sim = self.simulators.get(client_id)
                        if not sim:
                            response = {'status': 'error', 'message': 'Simulator not initialized'}
                        else:
                            registers = sim.get_registers()
                            if registers:
                                response = {'status': 'ok', 'registers': registers}
                            else:
                                response = {'status': 'error', 'message': 'Failed to get registers'}
                        await websocket.send(json.dumps(response))

                    else:
                        response = {'status': 'error', 'message': 'Unknown command'}
                        await websocket.send(json.dumps(response))

                except json.JSONDecodeError:
                    response = {'status': 'error', 'message': 'Invalid JSON'}
                    await websocket.send(json.dumps(response))
                except Exception as e:
                    response = {'status': 'error', 'message': str(e)}
                    await websocket.send(json.dumps(response))

        except websockets.exceptions.ConnectionClosed:
            print(f"[SESSION] Client {client_id} disconnected")
        finally:
            self.running_states[client_id] = False
            self.destroy_simulator(client_id)
            if client_id in self.running_states:
                del self.running_states[client_id]

            if not duration_recorded:
                duration_recorded = True
                duration = int(time.time() - start_time)
                if duration > 0:
                    stats.add_duration(duration)
                    stats.flush_all()
                    print(f"[STATS] Client {client_id} session duration: {duration}s. Active sessions: {len(self.simulators)}")

    async def start(self):
        print(f"[SERVER] WebSocket server initializing on ws://{HOST}:{PORT}")
        print(f"[SERVER] Max concurrent simulators: {MAX_CONCURRENT_SIMULATORS}")
        print(f"[SERVER] Each user gets an independent simulator instance")

        async with websockets.serve(self.handle_client, HOST, PORT):
            print(f"[SERVER] WebSocket server running on ws://{HOST}:{PORT}")
            await asyncio.Future()

    async def shutdown(self):
        print("[SERVER] Shutting down all simulators...")
        async with self._lock:
            for client_id in list(self.simulators.keys()):
                self.destroy_simulator(client_id)
        print("[SERVER] All simulators stopped")


if __name__ == "__main__":
    server = WebSocketServer()
    try:
        asyncio.run(server.start())
    except KeyboardInterrupt:
        print("\n[SERVER] Shutting down...")
        asyncio.run(server.shutdown())
