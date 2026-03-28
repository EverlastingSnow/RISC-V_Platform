import http.server
import socketserver
import json
import subprocess
import os

import config

PORT = config.API_SERVER_PORT

class MyHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/api/state':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            
            # Simulate state response
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
            
            # Simulate signals response
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
            super().do_GET()
    
    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length)
        
        if self.path == '/api/clock':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            
            # Simulate clock response
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
            
            # Simulate reset response
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
            
            # Simulate load response
            response = {
                "cycle": 0,
                "pc": "0x80000000"
            }
            self.wfile.write(json.dumps(response).encode())
        else:
            self.send_response(404)
            self.end_headers()
    
    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

if __name__ == "__main__":
    with socketserver.TCPServer(("", PORT), MyHTTPRequestHandler) as httpd:
        print(f"Server running at http://localhost:{PORT}")
        httpd.serve_forever()
