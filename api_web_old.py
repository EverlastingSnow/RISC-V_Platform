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

print(f"[STATS] stats module imported successfully. File: {stats.__file__}")
print(f"[CONFIG] config loaded from: {config.__file__}")

PORT = config.WEBSOCKET_PORT
SIM_SERVER_PATH = config.SIM_SERVER_PATH


class CppSimulator:
    def __init__(self, exe_path: str = None):
        if exe_path is None:
            base_dir = os.path.dirname(os.path.abspath(__file__))
            exe_path = os.path.join(base_dir, "build", "riscv_sim_server.exe")
            if not os.path.exists(exe_path):
                exe_path = os.path.join(base_dir, "build", "Debug", "riscv_sim_server.exe")
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
                    # Skip lines that start with [DEBUG - they are debug output, not JSON
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
        self.sim: Optional[CppSimulator] = None
        self.run_task: Optional[asyncio.Task] = None
        self.running = False
        self.command_lock = asyncio.Lock()
        stats.init_stats_db()
        self.client_start_times: Dict[str, float] = {}
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

    def init_simulator(self) -> bool:
        self.sim = CppSimulator()
        return self.sim.start()

    async def handle_client(self, websocket):
        client_id = str(id(websocket))
        start_time = time.time()
        duration_recorded = False
        stats.increment_visit()
        print(f"[STATS] New client connected. Visit count incremented. Current: {stats.get_stats()}")
        try:
            async for message in websocket:
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
                                    while result and not result.startswith('{'):
                                        newline_idx = result.find('\n')
                                        if newline_idx == -1:
                                            break
                                        result = result[newline_idx+1:].strip()
                                    
                                    if result.startswith('{'):
                                        try:
                                            data = json.loads(result)
                                            print(f"[DEBUG] Parsed data: {data}")
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
                            self.running = True
                            response = {'status': 'ok', 'message': 'Running...'}
                            await websocket.send(json.dumps(response))

                            while self.running and self.sim:
                                print(f"[DEBUG] run: sending step...")
                                result = self.sim.send_command("step")
                                print(f"[DEBUG] run: received: {result}")
                                if result:
                                    try:
                                        data = json.loads(result)
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
                                await asyncio.sleep(0.05)

                        elif command == 'stop':
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
                                if not self.init_simulator():
                                    response = {'status': 'error', 'message': 'Failed to initialize simulator'}
                                    await websocket.send(json.dumps(response))
                                    continue

                            result = self.sim.send_command(f"load {filepath}")
                            if result:
                                try:
                                    resp_data = json.loads(result)
                                    if resp_data.get('status') == 'ok':
                                        response = resp_data  # Use the signals from C++ directly
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
                            cmd = f'set_user_signal {signal_name} {"true" if value else "false"}'
                            result = self.sim.send_command(cmd)
                            if result:
                                try:
                                    resp_data = json.loads(result)
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
                                    temp_elf = tempfile.NamedTemporaryFile(delete=False, suffix='.elf')
                                    temp_elf.write(elf_bytes)
                                    temp_elf.close()
                                    print(f"[DEBUG] ELF saved to: {temp_elf.name}")
                                    cmd = f'load {temp_elf.name}'
                                    print(f"[DEBUG] Sending load command: {cmd}")
                                    result = self.sim.send_command(cmd)
                                    print(f"[DEBUG] load result: {result}")
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
                            response = {'status': 'error', 'message': 'Unknown command'}
                            await websocket.send(json.dumps(response))

                    except json.JSONDecodeError:
                        response = {'status': 'error', 'message': 'Invalid JSON'}
                        await websocket.send(json.dumps(response))
                    except Exception as e:
                        response = {'status': 'error', 'message': str(e)}
                        await websocket.send(json.dumps(response))
        except websockets.exceptions.ConnectionClosed:
            self.running = False
        finally:
            if not duration_recorded:
                duration_recorded = True
                duration = int(time.time() - start_time)
                if duration > 0:
                    stats.add_duration(duration)
                    stats.flush_all()
                    print(f"[STATS] Client disconnected. Duration: {duration}s. Current: {stats.get_stats()}")

    async def start(self):
        print(f"Initializing C++ simulator...")
        if not self.init_simulator():
            print("Warning: Failed to initialize simulator. Client must send 'load' command first.")

        async with websockets.serve(self.handle_client, "localhost", PORT):
            print(f"WebSocket server running on ws://localhost:{PORT}")
            print(f"Simulator executable: {self.sim.exe_path if self.sim else 'N/A'}")
            await asyncio.Future()


if __name__ == "__main__":
    server = WebSocketServer()
    try:
        asyncio.run(server.start())
    except KeyboardInterrupt:
        print("\nShutting down...")
        if server.sim:
            server.sim.stop()
