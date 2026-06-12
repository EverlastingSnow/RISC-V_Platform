const http = require('http');
const url = require('url');

const PORT = 8080;

// Mock state
let state = {
  cycle: 0,
  pc: '0x80000000',
  registers: Array.from({ length: 32 }, (_, i) => ({ addr: i, value: 0 }))
};

// Create server
const server = http.createServer((req, res) => {
  const parsedUrl = url.parse(req.url, true);
  const path = parsedUrl.pathname;
  
  // Handle CORS
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
  
  if (req.method === 'OPTIONS') {
    res.statusCode = 200;
    res.end();
    return;
  }
  
  if (req.method === 'GET') {
    if (path === '/api/state') {
      res.statusCode = 200;
      res.setHeader('Content-Type', 'application/json');
      res.end(JSON.stringify(state));
    } else if (path === '/api/signals') {
      res.statusCode = 200;
      res.setHeader('Content-Type', 'application/json');
      const signals = {
        fetch: {
          pc: state.pc,
          valid: true,
          target: '0x0',
          jump: false,
          PC_next: '0x80000004',
          allow_to_go: true
        },
        decode: {
          pc: '0x80000000',
          inst: '0x0',
          src1_raddr: 0,
          src1_rdata: 0,
          src2_raddr: 0,
          src2_rdata: 0
        },
        execute: {
          pc: '0x80000000',
          alu_result: 0,
          fu_type: 'NONE'
        },
        memory: {
          pc: '0x80000000',
          info_valid: false,
          info_reg_wen: false,
          info_reg_waddr: 0
        },
        writeback: {
          pc: '0x80000000',
          debug_commit: false,
          debug_pc: '0x80000000',
          debug_wb_rf_wen: false,
          debug_wb_rf_waddr: 0,
          debug_wb_rf_wdata: 0
        },
        regfile: {
          src1_raddr: 0,
          src1_rdata: 0,
          src2_raddr: 0,
          src2_rdata: 0,
          reg_wen: false,
          reg_waddr: 0,
          reg_wdata: 0
        },
        datamem: {
          DataMEM_en: false,
          DataMEM_wen: false,
          DataMEM_addr: '0x0',
          DataMEM_rdata: 0,
          DataMEM_wdata: 0
        }
      };
      res.end(JSON.stringify(signals));
    } else {
      res.statusCode = 404;
      res.end('Not Found');
    }
  } else if (req.method === 'POST') {
    let body = '';
    req.on('data', chunk => {
      body += chunk.toString();
    });
    req.on('end', () => {
      if (path === '/api/clock') {
        state.cycle++;
        state.pc = '0x' + (parseInt(state.pc, 16) + 4).toString(16).toUpperCase();
        res.statusCode = 200;
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify({ cycle: state.cycle, pc: state.pc }));
      } else if (path === '/api/reset') {
        state = {
          cycle: 0,
          pc: '0x80000000',
          registers: Array.from({ length: 32 }, (_, i) => ({ addr: i, value: 0 }))
        };
        res.statusCode = 200;
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify({ cycle: state.cycle, pc: state.pc }));
      } else if (path === '/api/load') {
        // Simulate program load
        res.statusCode = 200;
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify({ cycle: state.cycle, pc: state.pc }));
      } else {
        res.statusCode = 404;
        res.end('Not Found');
      }
    });
  } else {
    res.statusCode = 405;
    res.end('Method Not Allowed');
  }
});

// Start server
server.listen(PORT, () => {
  console.log(`Server running at http://localhost:${PORT}`);
});
