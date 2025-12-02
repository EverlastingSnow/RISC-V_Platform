# RISC-V_Platform
RISC-V五级流水线模拟平台后端需求文档
1. 项目概述
本项目旨在实现一个周期精确的RISC-V五级流水线模拟器后端，支持RV32I基础指令集，并为特权指令、冒险处理和异常处理预留完整接口。后端通过HTTP API提供每个时钟周期流水线各阶段的完整信号状态，供前端可视化流水线动态流动过程。

2. 功能特性
完整的RV32I指令集：实现56条基础整数指令

周期精确的五级流水线模拟：IF、ID、EX、MEM、WB阶段

完整的数据通路与控制信号：与架构图完全一致的所有信号输出

模块化预留接口：

特权指令执行单元（CSR、陷阱处理等）

完整的数据旁路与冒险检测单元

异常与中断处理流水线控制

标准二进制程序加载：支持.bin文件格式

RESTful API：提供单步执行、连续执行、重置等控制接口

详细状态反馈：每个周期以JSON格式返回流水线全信号状态

3. 系统架构
text
                +-----------------------+
                |     前端Web页面        |
                | (React/Vue/HTML+JS)   |
                +-----------+-----------+
                            | HTTP/WebSocket
                            | JSON
                +-----------+-----------+
                |    HTTP API服务器     |
                |   (C++后端: 主控制器)  |
                +-----------+-----------+
                            |
                +-----------+-----------+
                |  流水线模拟核心引擎     |
                | (Pipeline, Stages)    |
                +-----------+-----------+
                            |
                +-----------+-----------+
                |    组件模块层          |
                | (ALU, RegFile, Memory)|
                +-----------+-----------+
4. 接口定义（核心JSON格式）
后端每个周期应返回以下结构的JSON数据，包含架构图中所有信号，详见架构图

5. API设计
后端作为HTTP服务器，提供以下RESTful接口：

端点	方法	参数	描述
/api/initialize	POST	program (二进制文件)	加载程序并重置模拟器
/api/reset	POST	无	重置CPU状态（PC=0x80000000）
/api/step	POST	无	执行一个时钟周期
/api/run	POST	cycles (可选)	连续执行指定周期数（默认到程序结束）
/api/pause	POST	无	暂停连续执行
/api/status	GET	无	获取当前完整状态（同上JSON）
/api/memory	GET	address, length	读取内存区域
/api/register	GET	name	读取特定寄存器值
/api/breakpoint	POST	address, action	设置/清除断点
6. 实现指南
6.1 核心模块设计
text
class RISCVSimulator {
public:
    // 主控制接口
    void load_program(const std::vector<uint8_t>& binary);
    void reset();
    void step();  // 执行一个周期
    
    // 状态获取
    const PipelineState& get_state() const;
    
private:
    // 流水线阶段寄存器
    struct IF_ID_Register { /* IF到ID的所有信号 */ };
    struct ID_EX_Register { /* ID到EX的所有信号 */ };
    struct EX_MEM_Register { /* EX到MEM的所有信号 */ };
    struct MEM_WB_Register { /* MEM到WB的所有信号 */ };
    
    // 组件实例
    Memory imem, dmem;
    RegisterFile regfile;
    ALU alu;
    
    // 预留接口实例
    HazardDetectionUnit hazard_unit;      // 冒险检测（占位）
    ForwardingUnit forwarding_unit;       // 数据旁路（占位）
    ExceptionHandler exception_handler;    // 异常处理（占位）
    PrivilegedUnit privileged_unit;       // 特权单元（占位）
    
    // 流水线控制方法
    void stage_if();
    void stage_id();
    void stage_ex();
    void stage_mem();
    void stage_wb();
    void update_pipeline_registers();
};
6.2 接口预留规范
每个预留模块需满足以下接口：

cpp
// 冒险检测单元（占位实现）
class HazardDetectionUnit {
public:
    struct HazardSignals {
        bool stall_if = false;
        bool stall_id = false;
        bool stall_ex = false;
        bool flush_if = false;
        bool flush_id = false;
        // ... 所有冒险相关信号
    };
    
    HazardSignals detect(const PipelineRegisters& regs) {
        // TODO: 未来实现完整冒险检测
        HazardSignals signals;
        // 当前返回空信号（无冒险）
        return signals;
    }
};

// 异常处理单元（占位实现）
class ExceptionHandler {
public:
    struct ExceptionSignals {
        bool exception_occurred = false;
        uint32_t cause = 0;
        uint32_t handler_pc = 0;
        bool pipeline_flush = false;
    };
    
    ExceptionSignals check(const PipelineRegisters& regs) {
        // TODO: 未来实现异常检测
        ExceptionSignals signals;
        return signals;
    }
    
    void handle_trap(PipelineRegisters& regs, CSRRegisters& csr) {
        // TODO: 未来实现陷阱处理
    }
};
6.3 指令集扩展方法
在stage_id()中解析指令opcode

实现execute_r_type(), execute_i_type()等模板方法

特权指令在PrivilegedUnit中预留处理函数：

cpp
class PrivilegedUnit {
public:
    void execute_csr_instruction(uint32_t instr, CSRRegisters& csr, RegisterFile& regs) {
        // TODO: 实现CSR指令
        // 当前只解析不执行
    }
    
    void handle_ecall(CSRRegisters& csr, PipelineRegisters& regs) {
        // TODO: 实现系统调用
    }
};
7. 前端技术选型建议
鉴于部署在学校服务器，建议：

方案	技术栈	优点	缺点
推荐	React + TypeScript	组件化好，生态丰富，适合复杂可视化	学习曲线稍陡
备选1	Vue 3 + TypeScript	渐进式，文档友好，易于上手	大型项目生态略逊于React
备选2	SvelteKit	编译时优化，代码简洁	相对较新，生态在成长
简单方案	纯HTML/JS + D3.js	无构建步骤，直接部署	代码组织复杂时难以维护
可视化库推荐：

流水线图：react-flow (React) / vue-flow (Vue) 或自定义Canvas

信号波形：wave.js 或自定义SVG

寄存器/内存视图：自定义表格组件

8. 构建与运行
bash
# 1. 构建C++后端
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j4

# 2. 安装依赖 (示例使用cpp-httplib和jsonlib)
# 假设使用vcpkg或conan管理依赖

# 3. 运行服务器
./riscv-simulator-server --port 8080 --binary program.bin

# 4. 前端构建 (以React为例)
cd frontend
npm install
npm run build
cp -r dist/* /var/www/html/

# 5. 通过Nginx代理 (推荐)
# nginx配置将/api请求代理到localhost:8080
9. 测试策略
单元测试：使用Google Test测试每个指令执行

集成测试：运行RV32I测试套（riscv-tests）

API测试：使用Postman或自动化脚本测试HTTP接口

前端集成测试：手动验证信号显示正确性

10. 后续开发路线图
Phase 1：实现RV32I指令，完成基础流水线

Phase 2：实现数据旁路与冒险检测

Phase 3：实现基础异常处理（ecall/ebreak）

Phase 4：实现CSR指令和完整特权模式

Phase 5：添加缓存模拟、分支预测等高级功能