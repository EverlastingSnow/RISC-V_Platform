"""
旧版 HTTP 状态/信号服务（前端 Mock 用）。

该服务返回的是**模拟**的 CPU 状态与流水线信号数据，
未与真实的 C++ 模拟器交互，仅用于前端在没有真机/真模拟器时调试界面。
如需真实仿真请使用 api_websocket_server.py。
"""

import http.server
import socketserver
import json
import subprocess
import os

import config

# 服务监听端口（来自 config 模块）
PORT = config.API_SERVER_PORT


class MyHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    """
    自定义 HTTP 请求处理器。

    除了提供父类的静态文件服务外，还实现了
    /api/state、/api/signals 两个 GET 接口，
    以及 /api/clock、/api/reset、/api/load 三个 POST 接口。
    """

    def do_GET(self):
        """
        处理 GET 请求。

        Returns:
            None: 响应直接通过 self.wfile 写回客户端。
        """
        if self.path == '/api/state':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            # 允许任意源跨域访问，便于本地前端调试
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()

            # 模拟状态响应：cycle、pc 与 32 个通用寄存器初值
            state = {
                "cycle": 0,
                "pc": "0x80000000",
                "registers": [
                    {"addr": i, "value": 0} for i in range(32)
                ]
            }
            self.wfile.write(json.dumps(state).encode())
        elif self.path == '/api/signals':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()

            # 模拟五级流水线各阶段的关键信号
            signals = {
                "fetch": {
                    "pc": "0x80000000",
                    "valid": True,
                    "target": "0x0",
                    "jump": False,
                    "PC_next": "0x80000004",
                    "allow_to_go": True
                },
                "decode": {
                    "pc": "0x80000000",
                    "inst": "0x0",
                    "src1_raddr": 0,
                    "src1_rdata": 0,
                    "src2_raddr": 0,
                    "src2_rdata": 0
                },
                "execute": {
                    "pc": "0x80000000",
                    "alu_result": 0,
                    "fu_type": "NONE"
                },
                "memory": {
                    "pc": "0x80000000",
                    "info_valid": False,
                    "info_reg_wen": False,
                    "info_reg_waddr": 0
                },
                "writeback": {
                    "pc": "0x80000000",
                    "debug_commit": False,
                    "debug_pc": "0x80000000",
                    "debug_wb_rf_wen": False,
                    "debug_wb_rf_waddr": 0,
                    "debug_wb_rf_wdata": 0
                },
                "regfile": {
                    "src1_raddr": 0,
                    "src1_rdata": 0,
                    "src2_raddr": 0,
                    "src2_rdata": 0,
                    "reg_wen": False,
                    "reg_waddr": 0,
                    "reg_wdata": 0
                },
                "datamem": {
                    "DataMEM_en": False,
                    "DataMEM_wen": False,
                    "DataMEM_addr": "0x0",
                    "DataMEM_rdata": 0,
                    "DataMEM_wdata": 0
                }
            }
            self.wfile.write(json.dumps(signals).encode())
        else:
            # 非 API 路径走父类静态文件服务（前端 HTML/JS 等）
            super().do_GET()

    def do_POST(self):
        """
        处理 POST 请求（模拟器控制类操作）。

        Returns:
            None: 响应直接通过 self.wfile 写回客户端。
        """
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length)

        if self.path == '/api/clock':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()

            # 模拟单步时钟推进
            response = {
                "cycle": 1,
                "pc": "0x80000004"
            }
            self.wfile.write(json.dumps(response).encode())
        elif self.path == '/api/reset':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()

            # 模拟复位到初始状态
            response = {
                "cycle": 0,
                "pc": "0x80000000"
            }
            self.wfile.write(json.dumps(response).encode())
        elif self.path == '/api/load':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()

            # 模拟加载程序（此处无实际动作，仅返回 OK 风格响应）
            response = {
                "cycle": 0,
                "pc": "0x80000000"
            }
            self.wfile.write(json.dumps(response).encode())
        else:
            # 未识别的接口返回 404
            self.send_response(404)
            self.end_headers()

    def do_OPTIONS(self):
        """处理 CORS 预检请求，统一放行 GET/POST/OPTIONS。"""
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()


if __name__ == "__main__":
    # 启动 TCP 服务并持续监听（单线程）
    with socketserver.TCPServer(("", PORT), MyHTTPRequestHandler) as httpd:
        print(f"Server running at http://localhost:{PORT}")
        httpd.serve_forever()
