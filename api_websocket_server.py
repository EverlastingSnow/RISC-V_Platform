import asyncio
import websockets
import json
import subprocess
import os
import signal
import time
from typing import Optional, Dict, Any

PORT = 8081
SIM_SERVER_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "riscv_sim_server.exe")


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
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1
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
            self.process.stdin.write(cmd + "\n")
            self.process.stdin.flush()
            
            for _ in range(20):
                line = self.process.stdout.readline()
                if not line:
                    time.sleep(0.05)
                    continue
                stripped = line.strip()
                print(f"[DEBUG] C++ stdout: {stripped[:100] if stripped else 'None'}")
                if stripped.startswith('{'):
                    return stripped
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
                self.process.stdin.write("quit\n")
                self.process.stdin.flush()
                self.process.terminate()
                self.process.wait(timeout=2)
            except:
                self.process.kill()
            self.process = None


class WebSocketServer:
    def __init__(self):
        self.sim: Optional[CppSimulator] = None
        self.run_task: Optional[asyncio.Task] = None
        self.running = False

    def init_simulator(self) -> bool:
        self.sim = CppSimulator()
        return self.sim.start()

    async def handle_client(self, websocket):
        try:
            async for message in websocket:
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

                        if self.sim.load(filepath):
                            signals = self.sim.get_signals()
                            response = {'status': 'ok', 'message': f'Loaded {filepath}', 'signals': signals}
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
                            response = {'status': 'ok', 'message': f'Signal {signal_name} set to {value}'}
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
