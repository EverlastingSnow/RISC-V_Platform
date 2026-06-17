#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <csignal>
#include <atomic>
#include <memory>
#include <map>
#include <set>
#include <cstdint>

#include "riscv/types.h"
#include "riscv/simulator.h"
#include "riscv/elf_loader.h"
#include "riscv/decoder.h"
#include "riscv/teaching_tests.h"
#include "riscv/teaching_elf_config.h"

namespace {

std::atomic<bool> g_running{true};
std::string g_current_elf_path;
std::vector<riscv::u8> g_last_binary;
riscv::u64 g_last_load_offset{0};

struct DiffTestConfig {
    bool enabled{false};
    bool shadow_mode{false};
    std::set<std::string> enabled_signals;
    std::map<std::string, bool> user_signals;

    // Waiting state
    bool waiting_for_input{false};
    std::uint64_t waiting_pc{0};
    bool signals_just_submitted{false};

    // Diff detection state
    bool diff_detected{false};
    std::string diff_message;

    // Shadow simulator for user input mode
    std::unique_ptr<riscv::RISCVSimulator> shadow_sim;

    struct Result {
        bool valid{false};
        std::uint64_t pc{0};
        bool reg_write{false};
        std::uint32_t waddr{0};
        std::uint64_t wdata{0};
        std::uint64_t alu_result{0};
        bool branch_taken{false};
        std::uint64_t branch_target{0};
    };

    Result golden_result;
    Result user_result;
};

DiffTestConfig g_difftest;

void output_diff_detected();
void output_signals_body(riscv::RISCVSimulator& sim);

class ShadowSimulator {
public:
    using u8 = std::uint8_t;
    using u64 = std::uint64_t;
    
    ShadowSimulator() : sim_(std::make_unique<riscv::RISCVSimulator>()) {}
    
    void load_program(const std::vector<u8>& binary, u64 offset) {
        sim_->load_program(binary, offset);
    }
    
    void reset() {
        sim_->reset();
    }
    
    void step() {
        sim_->step();
    }
    
    bool halted() const {
        return sim_->halted();
    }
    
    const riscv::RegisterFile& registers() const {
        return sim_->registers();
    }
    
    const riscv::Memory& memory() const {
        return sim_->memory();
    }
    
    const auto& mem_wb() const {
        return sim_->mem_wb();
    }
    
    const auto& ex_mem() const {
        return sim_->ex_mem();
    }
    
    void apply_user_signals(const std::map<std::string, bool>& signals,
                           const std::set<std::string>& enabled_signals,
                           const riscv::DecodedInstruction& instr,
                           riscv::u64 src1_value,
                           riscv::u64 src2_value,
                           riscv::u64 imm_value) {
    }
    
private:
    std::unique_ptr<riscv::RISCVSimulator> sim_;
};

void signal_handler(int) {
    g_running = false;
}

bool uses_rs1(riscv::InstructionKind kind) {
    switch (kind) {
        case riscv::InstructionKind::LUI:
        case riscv::InstructionKind::AUIPC:
        case riscv::InstructionKind::JAL:
        case riscv::InstructionKind::FENCE:
        case riscv::InstructionKind::FENCE_I:
        case riscv::InstructionKind::ECALL:
        case riscv::InstructionKind::EBREAK:
        case riscv::InstructionKind::MRET:
        case riscv::InstructionKind::CSRRWI:
        case riscv::InstructionKind::CSRRSI:
        case riscv::InstructionKind::CSRRCI:
            return false;
        default:
            break;
    }
    return true;
}

bool uses_rs2(riscv::InstructionKind kind) {
    switch (kind) {
        case riscv::InstructionKind::SB:
        case riscv::InstructionKind::SH:
        case riscv::InstructionKind::SW:
        case riscv::InstructionKind::SD:
        case riscv::InstructionKind::ADD:
        case riscv::InstructionKind::SUB:
        case riscv::InstructionKind::SLL:
        case riscv::InstructionKind::SLT:
        case riscv::InstructionKind::SLTU:
        case riscv::InstructionKind::XOR:
        case riscv::InstructionKind::SRL:
        case riscv::InstructionKind::SRA:
        case riscv::InstructionKind::OR:
        case riscv::InstructionKind::AND:
        case riscv::InstructionKind::ADDW:
        case riscv::InstructionKind::SUBW:
        case riscv::InstructionKind::SLLW:
        case riscv::InstructionKind::SRLW:
        case riscv::InstructionKind::SRAW:
        case riscv::InstructionKind::MUL:
        case riscv::InstructionKind::MULH:
        case riscv::InstructionKind::MULHSU:
        case riscv::InstructionKind::MULHU:
        case riscv::InstructionKind::DIV:
        case riscv::InstructionKind::DIVU:
        case riscv::InstructionKind::REM:
        case riscv::InstructionKind::REMU:
        case riscv::InstructionKind::MULW:
        case riscv::InstructionKind::DIVW:
        case riscv::InstructionKind::DIVUW:
        case riscv::InstructionKind::REMW:
        case riscv::InstructionKind::REMUW:
        case riscv::InstructionKind::BEQ:
        case riscv::InstructionKind::BNE:
        case riscv::InstructionKind::BLT:
        case riscv::InstructionKind::BGE:
        case riscv::InstructionKind::BLTU:
        case riscv::InstructionKind::BGEU:
            return true;
        default:
            return false;
    }
}

/**
 * @brief JSON 字符串转义，处理双引号、反斜杠与控制字符。
 *
 * @param s 原始字符串
 * @return 转义后可直接嵌入 JSON 字符串字段
 */
std::string escape_json(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

bool get_default_signal(const riscv::DecodedInstruction& instr, const std::string& signal_name) {
    if (signal_name == "RegWrite") {
        return instr.writes_rd();
    } else if (signal_name == "ALUSrc") {
        return instr.format == riscv::InstructionFormat::I || 
               instr.format == riscv::InstructionFormat::S ||
               instr.format == riscv::InstructionFormat::U;
    } else if (signal_name == "MemRead") {
        return instr.is_load();
    } else if (signal_name == "MemWrite") {
        return instr.is_store();
    } else if (signal_name == "Branch") {
        return instr.is_branch();
    }
    return false;
}

/**
 * @brief 判断该信号在当前指令下是否需要用户交互输入。
 *
 * ECALL/EBREAK/INVALID/NOP 跳过所有信号；写 x0 的指令不需要 RegWrite；
 * 分支/跳转不需要 ALUSrc；其余指令需要用户作答。
 *
 * @param instr 已解码的指令
 * @param signal_name 信号名
 * @return true 表示需要用户输入
 */
bool is_signal_relevant(const riscv::DecodedInstruction& instr, const std::string& signal_name) {
    // ECALL and EBREAK are special - they halt the processor, don't require signal input
    if (instr.kind == riscv::InstructionKind::ECALL || instr.kind == riscv::InstructionKind::EBREAK) {
        return false;
    }

    // INVALID and NOP instructions don't require signal input
    if (instr.kind == riscv::InstructionKind::INVALID || instr.kind == riscv::InstructionKind::NOP) {
        return false;
    }

    if (signal_name == "RegWrite") {
        // Skip jal x0, offset (rd=0, writing to x0 is meaningless)
        if (instr.writes_rd() && instr.rd == 0) {
            return false;
        }
        // All other instructions need input (answer could be 0 or 1)
        return true;
    } else if (signal_name == "ALUSrc") {
        // Skip branches and jumps (they don't use standard ALU path)
        if (instr.is_branch() || instr.kind == riscv::InstructionKind::JAL || instr.kind == riscv::InstructionKind::JALR) {
            return false;
        }
        // All ALU-using instructions need input
        // R-type: ALUSrc=0, I/S/U-type: ALUSrc=1
        return true;
    } else if (signal_name == "MemRead") {
        // All instructions need input (answer: 1 for load, 0 for others)
        return true;
    } else if (signal_name == "MemWrite") {
        // All instructions need input (answer: 1 for store, 0 for others)
        return true;
    } else if (signal_name == "Branch") {
        // All instructions need input (answer: 1 for branch, 0 for others)
        return true;
    }
    return false;
}

bool needs_user_input(const riscv::DecodedInstruction& instr, const std::set<std::string>& enabled_signals) {
    for (const auto& signal : enabled_signals) {
        if (is_signal_relevant(instr, signal)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 对比 Golden 模拟器与 Shadow（用户控制）模拟器的 WB 阶段结果。
 *
 * 仅在两者都 valid 且用户提交了控制信号时进行比对。
 * 差异会写入 g_difftest 并调用 output_diff_detected() 输出 JSON 通知前端。
 *
 * @param sim Golden 模拟器指针
 */
void check_wb_diff(riscv::RISCVSimulator* sim) {
    if (!g_difftest.shadow_sim || !sim) return;

    const auto& golden_wb = sim->last_wb_result;
    const auto& user_wb = g_difftest.shadow_sim->last_wb_result;
    
    // Only compare if both have valid WB results
    if (!golden_wb.valid || !user_wb.valid) return;
    // Compare if shadow (user) received any user signals
    if (!user_wb.has_user_signal) return;  // No user input for this instruction
    
    // Compare actual WB results
    if (golden_wb.actual_wb_en != user_wb.actual_wb_en ||
        (golden_wb.actual_wb_en && (golden_wb.wb_raddr != user_wb.wb_raddr ||
                                     golden_wb.wb_rdata != user_wb.wb_rdata))) {
        
        g_difftest.diff_detected = true;
        g_difftest.golden_result.pc = golden_wb.pc;
        g_difftest.golden_result.reg_write = golden_wb.actual_wb_en;
        g_difftest.golden_result.waddr = golden_wb.wb_raddr;
        g_difftest.golden_result.wdata = golden_wb.wb_rdata;
        
        g_difftest.user_result.pc = user_wb.pc;
        g_difftest.user_result.reg_write = user_wb.actual_wb_en;
        g_difftest.user_result.waddr = user_wb.wb_raddr;
        g_difftest.user_result.wdata = user_wb.wb_rdata;
        
        g_difftest.diff_message = "WB阶段差异: ";
        if (golden_wb.actual_wb_en != user_wb.actual_wb_en) {
            g_difftest.diff_message += "写使能不匹配 (Golden: " + 
                std::string(golden_wb.actual_wb_en ? "1" : "0") + 
                ", User: " + std::string(user_wb.actual_wb_en ? "1" : "0") + ")";
        } else if (golden_wb.wb_raddr != user_wb.wb_raddr) {
            g_difftest.diff_message += "写地址不匹配 (Golden: x" + 
                std::to_string(golden_wb.wb_raddr) + 
                ", User: x" + std::to_string(user_wb.wb_raddr) + ")";
        } else {
            g_difftest.diff_message += "写数据不匹配 (Golden: 0x" + 
                std::to_string(golden_wb.wb_rdata) + 
                ", User: 0x" + std::to_string(user_wb.wb_rdata) + ")";
        }
        
        output_diff_detected();
    }
}

bool get_user_signal(const std::string& signal_name, const std::map<std::string, bool>& user_signals) {
    auto it = user_signals.find(signal_name);
    if (it != user_signals.end()) {
        return it->second;
    }
    return get_default_signal(riscv::DecodedInstruction{}, signal_name);
}

bool compare_ex_results(const DiffTestConfig::Result& golden, const DiffTestConfig::Result& user, std::string& message) {
    if (golden.alu_result != user.alu_result) {
        message = "EX阶段差异: ALU结果不匹配 (Golden: 0x" + 
                  std::to_string(golden.alu_result) + 
                  ", User: 0x" + std::to_string(user.alu_result) + ")";
        return false;
    }
    if (golden.branch_taken != user.branch_taken) {
        message = "EX阶段差异: 分支Taken不匹配 (Golden: " + 
                  std::string(golden.branch_taken ? "1" : "0") + 
                  ", User: " + std::string(user.branch_taken ? "1" : "0") + ")";
        return false;
    }
    if (golden.branch_target != user.branch_target) {
        message = "EX阶段差异: 分支目标不匹配 (Golden: 0x" + 
                  std::to_string(golden.branch_target) + 
                  ", User: 0x" + std::to_string(user.branch_target) + ")";
        return false;
    }
    return true;
}

/**
 * @brief 对比 WB 阶段的寄存器写使能、写地址与写数据。
 *
 * @param golden Golden 模拟器结果
 * @param user 用户控制模拟器结果
 * @param message 不一致时的详细描述（中文）
 * @return true 表示完全一致
 */
bool compare_wb_results(const DiffTestConfig::Result& golden, const DiffTestConfig::Result& user, std::string& message) {
    if (golden.reg_write != user.reg_write) {
        message = "WB阶段差异: 寄存器写使能不匹配 (Golden: " + 
                  std::string(golden.reg_write ? "1" : "0") + 
                  ", User: " + std::string(user.reg_write ? "1" : "0") + ")";
        return false;
    }
    if (golden.waddr != user.waddr) {
        message = "WB阶段差异: 写地址不匹配 (Golden: x" + 
                  std::to_string(golden.waddr) + 
                  ", User: x" + std::to_string(user.waddr) + ")";
        return false;
    }
    if (golden.wdata != user.wdata) {
        message = "WB阶段差异: 写数据不匹配 (Golden: 0x" + 
                  std::to_string(golden.wdata) + 
                  ", User: 0x" + std::to_string(user.wdata) + ")";
        return false;
    }
    return true;
}

void output_need_signal_input(const std::string& signal_name, const std::string& expected_value, const std::string& instruction_name) {
    std::cout << "{\"type\":\"need_signal_input\",\"needInput\":{"
              << "\"signalName\":\"" << signal_name << "\","
              << "\"expectedValue\":\"" << expected_value << "\","
              << "\"instruction\":\"" << instruction_name << "\""
              << "}}";
    std::cout << std::endl;
    std::cout.flush();
}

void output_need_all_signals_input(const riscv::DecodedInstruction& instr, const std::string& instruction_name) {
    std::vector<std::string> relevant_signals;
    std::vector<std::string> auto_signals;
    
    for (const auto& signal : g_difftest.enabled_signals) {
        if (is_signal_relevant(instr, signal)) {
            relevant_signals.push_back(signal);
        } else {
            auto_signals.push_back(signal);
            bool default_value = get_default_signal(instr, signal);
            g_difftest.user_signals[signal] = default_value;
        }
    }
    
    if (relevant_signals.empty()) {
        g_difftest.waiting_for_input = false;
        return;
    }
    
    std::cout << "{\"type\":\"need_signal_input\",\"needInput\":{"
              << "\"pc\":\"0x" << std::hex << instr.pc << std::dec << "\","
              << "\"signals\":[";
    
    bool first = true;
    for (const auto& signal : relevant_signals) {
        if (!first) {
            std::cout << ",";
        }
        bool expected = get_default_signal(instr, signal);
        std::string expected_str = expected ? "1" : "0";
        std::cout << "{\"name\":\"" << signal << "\",\"expectedValue\":\"" << expected_str << "\"}";
        first = false;
    }
    
    std::cout << "],"
              << "\"instruction\":\"" << instruction_name << "\""
              << "}}";
    std::cout << std::endl;
    std::cout.flush();
}

/**
 * @brief 向 stdout 输出 WB 阶段差异检测结果的 JSON。
 */
void output_diff_detected() {
    std::cout << "{\"type\":\"diff_detected\",\"diffResult\":{\"detected\":true,\"stage\":\"" << (g_difftest.diff_message.find("EX") != std::string::npos ? "EX" : "WB") << "\",\"goldenPC\":\"0x" << std::hex << g_difftest.golden_result.pc << std::dec << "\",\"userPC\":\"0x" << std::hex << g_difftest.user_result.pc << std::dec << "\",\"goldenResult\":{\"regWrite\":" << (g_difftest.golden_result.reg_write ? "true" : "false") << ",\"waddr\":" << g_difftest.golden_result.waddr << ",\"wdata\":\"0x" << std::hex << g_difftest.golden_result.wdata << std::dec << "\"},\"userResult\":{\"regWrite\":" << (g_difftest.user_result.reg_write ? "true" : "false") << ",\"waddr\":" << g_difftest.user_result.waddr << ",\"wdata\":\"0x" << std::hex << g_difftest.user_result.wdata << std::dec << "\"},\"message\":\"" << escape_json(g_difftest.diff_message) << "\"}}" << std::endl;
    std::cout.flush();
}

void output_ex_diff_detected() {
    std::cout << "{\"type\":\"diff_detected\",\"diffResult\":{"
              << "\"detected\":true,"
              << "\"stage\":\"EX\","
              << "\"goldenPC\":\"0x" << std::hex << g_difftest.golden_result.pc << std::dec << "\","
              << "\"userPC\":\"0x" << std::hex << g_difftest.user_result.pc << std::dec << "\","
              << "\"goldenResult\":{"
              << "\"alu_result\":\"0x" << std::hex << g_difftest.golden_result.alu_result << std::dec << "\","
              << "\"branch_taken\":" << (g_difftest.golden_result.branch_taken ? "true" : "false") << ","
              << "\"branch_target\":\"0x" << std::hex << g_difftest.golden_result.branch_target << std::dec << "\""
              << "},"
              << "\"userResult\":{"
              << "\"alu_result\":\"0x" << std::hex << g_difftest.user_result.alu_result << std::dec << "\","
              << "\"branch_taken\":" << (g_difftest.user_result.branch_taken ? "true" : "false") << ","
              << "\"branch_target\":\"0x" << std::hex << g_difftest.user_result.branch_target << std::dec << "\""
              << "},"
              << "\"message\":\"" << escape_json(g_difftest.diff_message) << "\""
              << "}}";
    std::cout << std::endl;
    std::cout.flush();
}

void output_signals_body(riscv::RISCVSimulator& sim) {
    const auto& if_id = sim.if_id();
    const auto& id_ex = sim.id_ex();
    const auto& ex_mem = sim.ex_mem();
    const auto& mem_wb = sim.mem_wb();

    std::cout << "\"cycle\":" << sim.cycle()
              << ",\"pc\":\"0x" << std::hex << sim.pc() << std::dec << "\"";

    auto if_id_instr = if_id.valid ? riscv::decode(if_id.inst, if_id.pc) : riscv::DecodedInstruction{};
    std::string if_id_asm = if_id.valid ? riscv::to_asm_string(if_id_instr) : "NOP";

    std::cout << ",\"if_id\":{"
              << "\"pc\":\"0x" << std::hex << if_id.pc << std::dec << "\","
              << "\"valid\":" << (if_id.valid ? "true" : "false") << ","
              << "\"inst\":" << if_id.inst << ","
              << "\"instruction\":\"" << (if_id.valid ? riscv::to_string(if_id_instr.kind) : "NONE") << "\","
              << "\"asm\":\"" << if_id_asm << "\","
              << "\"target\":\"0x" << std::hex << sim.redirect_target() << std::dec << "\","
              << "\"taken\":" << (sim.redirect() ? "true" : "false") << ","
              << "\"PC_next\":\"0x" << std::hex << sim.next_pc() << std::dec << "\","
              << "\"allow_to_go\":" << ((!sim.stall_fetch() && !sim.halted()) ? "true" : "false")
              << "}";

    auto id_ex_instr = id_ex.valid ? riscv::decode(id_ex.instr.raw, id_ex.instr.pc) : riscv::DecodedInstruction{};
    std::string id_ex_asm = id_ex.valid ? riscv::to_asm_string(id_ex_instr) : "NOP";
    riscv::u32 id_ex_src1_raddr = id_ex.valid && uses_rs1(id_ex_instr.kind) ? id_ex_instr.rs1 : 0;
    riscv::u32 id_ex_src2_raddr = id_ex.valid && uses_rs2(id_ex_instr.kind) ? id_ex_instr.rs2 : 0;
    riscv::u32 id_ex_rd_addr = id_ex.valid && id_ex_instr.writes_rd() ? id_ex_instr.rd : 0;

    std::cout << ",\"id_ex\":{"
              << "\"pc\":\"0x" << std::hex << (id_ex.valid ? id_ex.instr.pc : 0) << std::dec << "\","
              << "\"inst\":" << (id_ex.valid ? id_ex.instr.raw : 0) << ","
              << "\"src1_raddr\":" << id_ex_src1_raddr << ","
              << "\"src1_rdata\":\"0x" << std::hex << (id_ex.valid ? id_ex.rs1_value : 0) << std::dec << "\","
              << "\"src2_raddr\":" << id_ex_src2_raddr << ","
              << "\"src2_rdata\":\"0x" << std::hex << (id_ex.valid ? id_ex.rs2_value : 0) << std::dec << "\","
              << "\"rd_addr\":" << id_ex_rd_addr << ","
            //   << "\"rd_data\":\"0x" << std::hex << (id_ex.valid ? id_ex.instr.imm : 0) << std::dec << "\","
              << "\"imm\":\"0x" << std::hex << (id_ex.valid ? id_ex.instr.imm : 0) << std::dec << "\","
              << "\"instruction\":\"" << (id_ex.valid ? riscv::to_string(id_ex_instr.kind) : "NONE") << "\","
              << "\"asm\":\"" << id_ex_asm << "\""
              << "}";

    auto ex_mem_instr = ex_mem.valid ? riscv::decode(ex_mem.instr.raw, ex_mem.instr.pc) : riscv::DecodedInstruction{};
    std::string ex_mem_asm = ex_mem.valid ? riscv::to_asm_string(ex_mem_instr) : "NOP";

    riscv::u64 alu_src2_value = id_ex.valid ? id_ex.rs2_value : 0;
    if (id_ex.valid) {
        if (id_ex_instr.format == riscv::InstructionFormat::I ||
            id_ex_instr.format == riscv::InstructionFormat::S ||
            id_ex_instr.format == riscv::InstructionFormat::U) {
            alu_src2_value = static_cast<riscv::u64>(id_ex.instr.imm);
        }
    }

    std::cout << ",\"execute\":{"
              << "\"pc\":\"0x" << std::hex << (id_ex.valid ? id_ex.instr.pc : 0) << std::dec << "\","
              << "\"valid\":" << (id_ex.valid ? "true" : "false") << ","
              << "\"alu_src1\":\"0x" << std::hex << (id_ex.valid ? id_ex.rs1_value : 0) << std::dec << "\","
              << "\"alu_src2\":\"0x" << std::hex << alu_src2_value << std::dec << "\","
              << "\"alu_result\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.alu_result : 0) << std::dec << "\","
              << "\"fu_type\":\"" << (id_ex.valid ? riscv::to_string(id_ex_instr.kind) : "NONE") << "\","
              << "\"asm\":\"" << id_ex_asm << "\""
              << "}";

    std::cout << ",\"ex_mem\":{"
              << "\"pc\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.instr.pc : 0) << std::dec << "\","
              << "\"valid\":" << (ex_mem.valid ? "true" : "false") << ","
              << "\"inst\":" << (ex_mem.valid ? ex_mem.instr.raw : 0) << ","
              << "\"instruction\":\"" << (ex_mem.valid ? riscv::to_string(ex_mem_instr.kind) : "NONE") << "\","
              << "\"asm\":\"" << ex_mem_asm << "\","
              << "\"alu_src1\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.alu_src1 : 0) << std::dec << "\","
              << "\"alu_src2\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.alu_src2 : 0) << std::dec << "\","
              << "\"alu_result\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.alu_result : 0) << std::dec << "\","
              << "\"branch_taken\":" << (ex_mem.valid ? (ex_mem.branch_taken ? "true" : "false") : "false") << ","
              << "\"branch_target\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.branch_target : 0) << std::dec << "\","
              << "\"mem_addr\":\"0x" << std::hex << (ex_mem.valid && (ex_mem.instr.is_load() || ex_mem.instr.is_store()) ? ex_mem.alu_result : 0) << std::dec << "\","
              << "\"mem_wen\":" << (ex_mem.valid && ex_mem.instr.is_store() ? "true" : "false") << ","
              << "\"mem_ren\":" << (ex_mem.valid && ex_mem.instr.is_load() ? "true" : "false") << ","
              << "\"mem_wdata\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.rs2_value : 0) << std::dec << "\","
              << "\"info\":\"" << (ex_mem.valid ? riscv::to_string(ex_mem.instr.kind) : "NONE") << "\""
              << "}";

    auto mem_wb_instr = mem_wb.valid ? riscv::decode(mem_wb.instr.raw, mem_wb.instr.pc) : riscv::DecodedInstruction{};
    std::string mem_wb_asm = mem_wb.valid ? riscv::to_asm_string(mem_wb_instr) : "NOP";

    std::cout << ",\"mem_wb\":{"
              << "\"pc\":\"0x" << std::hex << (mem_wb.valid ? mem_wb.instr.pc : 0) << std::dec << "\","
              << "\"valid\":" << (mem_wb.valid ? "true" : "false") << ","
              << "\"inst\":" << (mem_wb.valid ? mem_wb.instr.raw : 0) << ","
              << "\"instruction\":\"" << (mem_wb.valid ? riscv::to_string(mem_wb_instr.kind) : "NONE") << "\","
              << "\"asm\":\"" << mem_wb_asm << "\","
              << "\"wb_value\":\"0x" << std::hex << (mem_wb.valid ? mem_wb.wb_value : 0) << std::dec << "\","
              << "\"rf_wen\":" << (mem_wb.valid && mem_wb.instr.writes_rd() ? "true" : "false") << ","
              << "\"rf_waddr\":" << (mem_wb.valid ? mem_wb.instr.rd : 0) << ","
              << "\"info\":\"" << (mem_wb.valid ? riscv::to_string(mem_wb_instr.kind) : "NONE") << "\""
              << "}";

    std::cout << ",\"writeback\":{"
              << "\"pc\":\"0x" << std::hex << (mem_wb.valid ? mem_wb.instr.pc : 0) << std::dec << "\","
              << "\"valid\":" << (mem_wb.valid ? "true" : "false") << ","
              << "\"instruction\":\"" << (mem_wb.valid ? riscv::to_string(mem_wb_instr.kind) : "NONE") << "\","
              << "\"asm\":\"" << mem_wb_asm << "\","
              << "\"debug_commit\":" << (mem_wb.valid ? "true" : "false") << ","
              << "\"debug_pc\":\"0x" << std::hex << (mem_wb.valid ? mem_wb.instr.pc : 0) << std::dec << "\","
              << "\"debug_wb_rf_wen\":" << (mem_wb.valid && mem_wb.instr.writes_rd() ? "true" : "false") << ","
              << "\"debug_wb_rf_waddr\":" << (mem_wb.valid ? mem_wb.instr.rd : 0) << ","
              << "\"debug_wb_rf_wdata\":\"0x" << std::hex << (mem_wb.valid ? mem_wb.wb_value : 0) << std::dec << "\""
              << "}";

    std::cout << ",\"regfile\":{"
              << "\"src1_raddr\":" << id_ex_src1_raddr << ","
              << "\"src1_rdata\":\"0x" << std::hex << (id_ex.valid ? id_ex.rs1_value : 0) << std::dec << "\","
              << "\"src2_raddr\":" << id_ex_src2_raddr << ","
              << "\"src2_rdata\":\"0x" << std::hex << (id_ex.valid ? id_ex.rs2_value : 0) << std::dec << "\","
              << "\"reg_wen\":" << (mem_wb.valid && mem_wb.instr.writes_rd() ? "true" : "false") << ","
              << "\"reg_waddr\":" << (mem_wb.valid ? mem_wb.instr.rd : 0) << ","
              << "\"reg_wdata\":\"0x" << std::hex << (mem_wb.valid ? mem_wb.wb_value : 0) << std::dec << "\""
              << "}";

    bool datamem_en = ex_mem.valid && (ex_mem.instr.is_load() || ex_mem.instr.is_store());
    std::cout << ",\"datamem\":{"
              << "\"DataMEM_en\":" << (datamem_en ? "true" : "false") << ","
              << "\"DataMEM_wen\":" << (ex_mem.valid && ex_mem.instr.is_store() ? "true" : "false") << ","
              << "\"DataMEM_addr\":\"0x" << std::hex << (datamem_en ? ex_mem.alu_result : 0) << std::dec << "\","
              << "\"DataMEM_rdata\":0,"
              << "\"DataMEM_wdata\":\"0x" << std::hex << (ex_mem.valid && ex_mem.instr.is_store() ? ex_mem.rs2_value : 0) << std::dec << "\""
              << "}";

    std::cout << ",\"halted\":" << (sim.halted() ? "true" : "false");
    if (sim.halted()) {
        if (sim.halt_reason_ecall()) {
            std::cout << ",\"halt_reason\":\"ecall\"";
        } else if (sim.halt_reason_ebreak()) {
            std::cout << ",\"halt_reason\":\"ebreak\"";
        } else {
            std::cout << ",\"halt_reason\":\"other\"";
        }
    }

    // ★ 新增：trap 标志位（区分异常与中断）
    std::cout << ",\"trap_taken\":" << (sim.last_trap_cause() == riscv::RISCVSimulator::TrapCause::Exception ? "true" : "false");
    std::cout << ",\"interrupt_taken\":" << (sim.last_trap_cause() == riscv::RISCVSimulator::TrapCause::Interrupt ? "true" : "false");

    // ★ 新增：CSR 状态输出（教学演示 trap 处理流程）
    std::cout << ",\"csr\":{"
              << "\"mtvec\":\"0x" << std::hex << sim.csr().read(0x305) << std::dec << "\","
              << "\"mepc\":\"0x" << std::hex << sim.csr().read(0x341) << std::dec << "\","
              << "\"mcause\":\"0x" << std::hex << sim.csr().read(0x342) << std::dec << "\","
              << "\"mtval\":\"0x" << std::hex << sim.csr().read(0x343) << std::dec << "\","
              << "\"mstatus\":\"0x" << std::hex << sim.csr().read(0x300) << std::dec << "\","
              << "\"mie\":\"0x" << std::hex << sim.csr().read(0x304) << std::dec << "\","
              << "\"mip\":\"0x" << std::hex << sim.csr().read(0x344) << std::dec << "\""
              << "}";

    // ★ 每次输出后立即清零 trap_cause，
    // 保证前端每个 cycle 看到的 trap_taken 只反映本 cycle 发生的 trap
    sim.clear_trap_cause();
}

/**
 * @brief 输出 JSON 包裹的信号体（{} + body + 换行）。
 *
 * @param sim 目标模拟器
 * @param use_shadow true 时使用 shadow_sim（用户控制流），否则使用真实模拟器
 */
void output_signals(riscv::RISCVSimulator& sim, bool use_shadow = false) {
    std::cout << "{";
    if (use_shadow && g_difftest.shadow_sim) {
        output_signals_body(*g_difftest.shadow_sim);
    } else {
        output_signals_body(sim);
    }
    std::cout << "}" << std::endl;
    std::cout.flush();
}

/**
 * @brief 输出 32 个通用寄存器的当前值（JSON 数组）。
 *
 * @param sim 目标模拟器
 * @param use_shadow true 时使用 shadow_sim
 */
void output_registers(riscv::RISCVSimulator& sim, bool use_shadow = false) {
    const auto& regs = (use_shadow && g_difftest.shadow_sim) 
                        ? g_difftest.shadow_sim->registers().raw() 
                        : sim.registers().raw();
    std::cout << "{\"registers\":[";
    for (int i = 0; i < 32; ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "{\"addr\":" << i << ",\"value\":" << regs[i] << "}";
    }
    std::cout << "]}";
    std::cout << std::endl;
    std::cout.flush();
}

}  // namespace

/**
 * @brief RISC-V 教学模拟器服务器入口（基于 stdcin/stdout 的 JSON 行协议）。
 *
 * 支持的指令：load、load_test、load_elf_test、list_tests、list_elf_tests、step、
 *           set_signals、reset、get_registers、get_pipeline_state、quit 等。
 * 每条指令读取一行 stdin，输出 JSON 行到 stdout。
 */
int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string line;
    std::unique_ptr<riscv::RISCVSimulator> sim;

    while (g_running && std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "load") {
            std::string filepath;
            iss >> filepath;

            auto result = riscv::load_elf(filepath);
            if (!result.success) {
                std::cout << "{\"status\":\"error\",\"message\":\"Failed to load ELF: " << escape_json(result.error) << "\"}" << std::endl;
                continue;
            }

            sim = std::make_unique<riscv::RISCVSimulator>();
            sim->load_program(result.binary, result.load_offset);
            g_current_elf_path = filepath;
            g_last_binary = result.binary;
            g_last_load_offset = result.load_offset;
            
            if (g_difftest.enabled && !g_difftest.enabled_signals.empty()) {
                g_difftest.shadow_sim = std::make_unique<riscv::RISCVSimulator>();
                g_difftest.shadow_sim->load_program(result.binary, result.load_offset);
            }
            
            g_difftest.diff_detected = false;
            g_difftest.waiting_for_input = false;
            g_difftest.user_signals.clear();
            
            // Output signals with status - output as complete JSON on one line
            std::cout << "{\"status\":\"ok\",\"message\":\"Loaded " << escape_json(filepath) << "\",";
            if (g_difftest.enabled && g_difftest.shadow_sim) {
                output_signals_body(*g_difftest.shadow_sim);
            } else {
                output_signals_body(*sim);
            }
            std::cout << "}" << std::endl;
            std::cout.flush();

        } else if (cmd == "load_test") {
            std::string test_name;
            iss >> test_name;

            const auto* test = riscv::TeachingTests::get_test(test_name);
            if (!test) {
                std::cout << "{\"status\":\"error\",\"message\":\"Teaching test not found: " << escape_json(test_name) << "\"}" << std::endl;
                continue;
            }

            std::vector<riscv::u8> binary;
            binary.reserve(test->instructions.size() * 4);
            for (auto word : test->instructions) {
                binary.push_back(static_cast<riscv::u8>(word & 0xFF));
                binary.push_back(static_cast<riscv::u8>((word >> 8) & 0xFF));
                binary.push_back(static_cast<riscv::u8>((word >> 16) & 0xFF));
                binary.push_back(static_cast<riscv::u8>((word >> 24) & 0xFF));
            }

            sim = std::make_unique<riscv::RISCVSimulator>();
            sim->load_program(binary, 0x80000000ULL);

            if (g_difftest.shadow_mode) {
                g_difftest.shadow_sim = std::make_unique<riscv::RISCVSimulator>();
                g_difftest.shadow_sim->load_program(binary, 0x80000000ULL);
            }

            g_difftest.diff_detected = false;
            g_difftest.waiting_for_input = false;
            g_difftest.user_signals.clear();
            std::cout << "{\"status\":\"ok\",\"message\":\"Loaded teaching test: " << escape_json(test_name)
                      << "\",\"testInfo\":{\"name\":\"" << escape_json(test->name)
                      << "\",\"description\":\"" << escape_json(test->description)
                      << "\",\"scenario\":\"" << escape_json(test->scenario) << "\"}}" << std::endl;

        } else if (cmd == "list_tests") {
            std::string scenario_filter;
            std::getline(iss, scenario_filter);
            while (!scenario_filter.empty() && scenario_filter[0] == ' ') scenario_filter.erase(scenario_filter.begin());

            std::cout << "{\"status\":\"ok\",\"tests\":[";
            bool first = true;
            for (const auto& test : riscv::TeachingTests::get_all_tests()) {
                if (!scenario_filter.empty() && test.scenario != scenario_filter && test.scenario != "all") {
                    continue;
                }
                if (!first) std::cout << ",";
                std::cout << "{\"name\":\"" << escape_json(test.name)
                          << "\",\"description\":\"" << escape_json(test.description)
                          << "\",\"scenario\":\"" << escape_json(test.scenario) << "\"}";
                first = false;
            }
            std::cout << "]}";

        } else if (cmd == "load_elf_test") {
            std::string test_name;
            iss >> test_name;

            bool found = false;
            for (const auto& test : riscv::TeachingElfConfig::get_all_elf_tests()) {
                if (test.name == test_name) {
                    auto result = riscv::load_elf(test.elf_path);
                    if (!result.success) {
                        std::cout << "{\"status\":\"error\",\"message\":\"Failed to load ELF: " << escape_json(result.error) << "\"}" << std::endl;
                        break;
                    }

                    sim = std::make_unique<riscv::RISCVSimulator>();
                    sim->load_program(result.binary, result.load_offset);
                    g_current_elf_path = test.elf_path;
                    g_last_binary = result.binary;
                    g_last_load_offset = result.load_offset;

                    if (g_difftest.enabled && !g_difftest.enabled_signals.empty()) {
                        g_difftest.shadow_sim = std::make_unique<riscv::RISCVSimulator>();
                        g_difftest.shadow_sim->load_program(result.binary, result.load_offset);
                    }

                    g_difftest.diff_detected = false;
                    g_difftest.waiting_for_input = false;
                    g_difftest.user_signals.clear();

                    std::cout << "{\"status\":\"ok\",\"message\":\"Loaded ELF test: " << escape_json(test_name)
                              << "\",\"testInfo\":{\"name\":\"" << escape_json(test.display_name)
                              << "\",\"description\":\"" << escape_json(test.description)
                              << "\",\"scenario\":\"" << escape_json(test.scenario)
                              << "\",\"elfPath\":\"" << escape_json(test.elf_path) << "\"}}" << std::endl;
                    found = true;
                    break;
                }
            }

            if (!found) {
                std::cout << "{\"status\":\"error\",\"message\":\"ELF test not found: " << escape_json(test_name) << "\"}" << std::endl;
            }

        } else if (cmd == "list_elf_tests") {
            std::string scenario_filter;
            std::getline(iss, scenario_filter);
            while (!scenario_filter.empty() && scenario_filter[0] == ' ') scenario_filter.erase(scenario_filter.begin());

            std::cout << "{\"status\":\"ok\",\"elfTests\":[";
            bool first = true;
            for (const auto& test : riscv::TeachingElfConfig::get_all_elf_tests()) {
                if (!scenario_filter.empty() && test.scenario != scenario_filter && test.scenario != "all") {
                    continue;
                }
                if (!first) std::cout << ",";
                std::cout << "{\"name\":\"" << escape_json(test.name)
                          << "\",\"displayName\":\"" << escape_json(test.display_name)
                          << "\",\"description\":\"" << escape_json(test.description)
                          << "\",\"scenario\":\"" << escape_json(test.scenario) << "\"}";
                first = false;
            }
            std::cout << "]}";

        } else if (cmd == "step") {
            if (!sim) {
                std::cout << "{\"status\":\"error\",\"message\":\"No program loaded\"}" << std::endl;
                continue;
            }
            if (sim->halted()) {
                std::cout << "{\"status\":\"error\",\"message\":\"Simulation halted\"}" << std::endl;
                continue;
            }
            
            // If waiting for input, don't step
            if (g_difftest.waiting_for_input) {
                std::cout << "{\"status\":\"error\",\"message\":\"Waiting for signal input\"}" << std::endl;
                continue;
            }
            
            // If diff detected, user needs to reset
            if (g_difftest.diff_detected) {
                std::cout << "{\"status\":\"error\",\"message\":\"Diff detected, please reset\"}" << std::endl;
                continue;
            }
            
            if (g_difftest.enabled) {
                if (g_difftest.signals_just_submitted) {
                    g_difftest.signals_just_submitted = false;
                    sim->step();
                    if (g_difftest.shadow_sim) {
                        g_difftest.shadow_sim->step();
                    }
                    check_wb_diff(sim.get());
                    if (g_difftest.diff_detected) {
                        continue;
                    }
                    output_signals(*sim, true);
                } else {
                    const auto& if_id = sim->if_id();
                    bool needs_input = false;
                    riscv::DecodedInstruction id_instr{};

                    if (if_id.valid) {
                        id_instr = riscv::decode(if_id.inst, if_id.pc);
                        needs_input = needs_user_input(id_instr, g_difftest.enabled_signals);
                    }

                    if (needs_input) {
                        g_difftest.waiting_for_input = true;
                        g_difftest.waiting_pc = if_id.pc;
                        g_difftest.user_signals.clear();

                        sim->set_waiting_for_input(true, if_id.pc);
                        if (g_difftest.shadow_sim) {
                            g_difftest.shadow_sim->set_waiting_for_input(true, if_id.pc);
                        }

                        std::string instr_name = riscv::to_string(id_instr.kind);
                        output_need_all_signals_input(id_instr, instr_name);
                    } else {
                        sim->step();
                        if (g_difftest.shadow_sim) {
                            g_difftest.shadow_sim->step();
                        }

                        check_wb_diff(sim.get());
                        if (g_difftest.diff_detected) {
                            continue;
                        }
                        output_signals(*sim, true);
                    }
                }
            } else {
                // Normal mode without difftest
                sim->step();
                output_signals(*sim);
            }

        } else if (cmd == "run") {
            if (!sim) {
                std::cout << "{\"status\":\"error\",\"message\":\"No program loaded\"}" << std::endl;
                continue;
            }
            
            if (g_difftest.enabled) {
                while (g_running && !sim->halted()) {
                    sim->step();
                    if (g_difftest.shadow_sim) {
                        g_difftest.shadow_sim->step();
                    }
                    check_wb_diff(sim.get());
                    if (g_difftest.diff_detected) {
                        break;
                    }
                    output_signals(*sim, true);
                }
            } else {
                while (g_running && !sim->halted()) {
                    sim->step();
                    output_signals(*sim, false);
                }
            }

        } else if (cmd == "reset") {
            if (sim) {
                sim->reset();
                sim->set_waiting_for_input(false);
            }
            if (g_difftest.shadow_sim) {
                g_difftest.shadow_sim->reset();
                g_difftest.shadow_sim->set_waiting_for_input(false);
            }
            g_difftest.diff_detected = false;
            g_difftest.diff_message.clear();
            g_difftest.waiting_for_input = false;
            g_difftest.signals_just_submitted = false;
            g_difftest.waiting_pc = 0;
            g_difftest.user_signals.clear();
            g_difftest.golden_result = DiffTestConfig::Result();
            g_difftest.user_result = DiffTestConfig::Result();

            std::cout << "{\"status\":\"ok\",\"message\":\"Reset\",\"difftest\":{\"enabled\":" 
                      << (g_difftest.enabled ? "true" : "false") 
                      << ",\"shadow_mode\":" << (g_difftest.shadow_mode ? "true" : "false")
                      << ",\"signals\":[";
            bool first_signal = true;
            for (const auto& sig : g_difftest.enabled_signals) {
                if (!first_signal) std::cout << ",";
                std::cout << "\"" << sig << "\"";
                first_signal = false;
            }
            std::cout << "]}}" << std::endl;

        } else if (cmd == "signals") {
            if (!sim) {
                std::cout << "{\"status\":\"error\",\"message\":\"No program loaded\"}" << std::endl;
                continue;
            }
            output_signals(*sim, g_difftest.enabled && g_difftest.shadow_sim);

        } else if (cmd == "registers") {
            if (!sim) {
                std::cout << "{\"status\":\"error\",\"message\":\"No program loaded\"}" << std::endl;
                continue;
            }
            output_registers(*sim, g_difftest.enabled && g_difftest.shadow_sim);

        } else if (cmd == "enable_difftest") {
            std::string signals_str;
            std::getline(iss, signals_str);
            std::istringstream sig_iss(signals_str);
            std::string signal;
            g_difftest.enabled_signals.clear();
            bool has_shadow_flag = false;

            while (sig_iss >> signal) {
                if (signal == "--shadow") {
                    has_shadow_flag = true;
                } else if (!signal.empty()) {
                    g_difftest.enabled_signals.insert(signal);
                }
            }

            g_difftest.shadow_mode = has_shadow_flag;
            g_difftest.enabled = !g_difftest.enabled_signals.empty();
            g_difftest.waiting_for_input = false;
            g_difftest.waiting_pc = 0;
            g_difftest.diff_detected = false;
            g_difftest.user_signals.clear();

            if (g_difftest.enabled) {
                if (!sim) {
                    std::cout << "{\"status\":\"error\",\"message\":\"No program loaded\"}" << std::endl;
                } else if (!g_last_binary.empty()) {
                    g_difftest.shadow_sim = std::make_unique<riscv::RISCVSimulator>();
                    g_difftest.shadow_sim->load_program(g_last_binary, g_last_load_offset);
                } else {
                    std::cout << "{\"status\":\"error\",\"message\":\"No binary data available for shadow simulator\"}" << std::endl;
                }
            }

            std::string mode_str = g_difftest.shadow_mode ? "shadow mode" : "user input mode";
            std::cout << "{\"status\":\"ok\",\"message\":\"Difftest enabled (" << mode_str << ") with signals: " << escape_json(signals_str) << "\"}" << std::endl;
            std::cout.flush();

        } else if (cmd == "disable_difftest") {
            g_difftest.enabled = false;
            g_difftest.shadow_mode = false;
            g_difftest.diff_detected = false;
            g_difftest.diff_message.clear();
            g_difftest.waiting_for_input = false;
            g_difftest.signals_just_submitted = false;
            g_difftest.waiting_pc = 0;
            g_difftest.enabled_signals.clear();
            g_difftest.user_signals.clear();
            g_difftest.golden_result = DiffTestConfig::Result();
            g_difftest.user_result = DiffTestConfig::Result();
            g_difftest.shadow_sim.reset();
            std::cout << "{\"status\":\"ok\",\"message\":\"Difftest disabled\"}" << std::endl;

        } else if (cmd == "set_user_signal") {
            std::string signal_name, value_str;
            iss >> signal_name >> value_str;
            bool value = (value_str == "true" || value_str == "1");
            
            g_difftest.user_signals[signal_name] = value;
            
            // Check if all signals received
            if (g_difftest.waiting_for_input) {
                bool all_signals_received = true;
                for (const auto& signal : g_difftest.enabled_signals) {
                    if (g_difftest.user_signals.find(signal) == g_difftest.user_signals.end()) {
                        all_signals_received = false;
                        break;
                    }
                }
                
                if (all_signals_received) {
                    if (g_difftest.shadow_sim) {
                        for (const auto& [sig_name, sig_value] : g_difftest.user_signals) {
                            g_difftest.shadow_sim->set_user_signal_for_id(sig_name, sig_value);
                        }
                        g_difftest.shadow_sim->set_waiting_handled(false);
                    }

                    g_difftest.waiting_for_input = false;
                    g_difftest.signals_just_submitted = true;
                    sim->set_waiting_for_input(false);
                    if (g_difftest.shadow_sim) {
                        g_difftest.shadow_sim->set_waiting_for_input(false);
                    }

                    g_difftest.user_signals.clear();

                    output_signals(*sim, true);
                    std::cout.flush();
                    continue;
                }
            }
            
            std::cout << "{\"status\":\"ok\",\"message\":\"User signal set: " << escape_json(signal_name) << "=" << (value ? "1" : "0") << "\"}" << std::endl;

        } else if (cmd == "skip_signal_input") {
            g_difftest.waiting_for_input = false;
            g_difftest.user_signals.clear();
            std::cout << "{\"status\":\"ok\",\"message\":\"Signal input skipped\"}" << std::endl;

        } else if (cmd == "trigger_interrupt") {
            // 教学演示：触发软件中断（仅设置 MIP，不自动设置 MIE）
            riscv::u64 bit = 0;
            iss >> bit;
            if (sim) {
                sim->trigger_pending_interrupt(bit);
                std::cout << "{\"status\":\"ok\",\"message\":\"Triggered interrupt bit " << bit << "\"}" << std::endl;
            } else {
                std::cout << "{\"status\":\"error\",\"message\":\"Simulator not initialized\"}" << std::endl;
            }

        } else if (cmd == "continue") {
            g_difftest.diff_detected = false;
            g_difftest.waiting_for_input = false;
            std::cout << "{\"status\":\"ok\",\"message\":\"Continuing\"}" << std::endl;

        } else if (cmd == "load_elf_binary") {
            std::string hex_str;
            std::getline(iss, hex_str);
            while (!hex_str.empty() && hex_str[0] == ' ') hex_str.erase(hex_str.begin());

            std::vector<riscv::u8> binary;
            try {
                for (size_t i = 0; i < hex_str.length(); i += 2) {
                    while (i < hex_str.length() && hex_str[i] == ' ') i++;
                    if (i + 2 <= hex_str.length()) {
                        unsigned int byte;
                        std::stringstream ss;
                        ss << std::hex << hex_str.substr(i, 2);
                        ss >> byte;
                        binary.push_back(static_cast<riscv::u8>(byte));
                    }
                }
            } catch (...) {
                std::cout << "{\"status\":\"error\",\"message\":\"Failed to parse binary data\"}" << std::endl;
                continue;
            }

            if (binary.empty()) {
                std::cout << "{\"status\":\"error\",\"message\":\"Empty binary data\"}" << std::endl;
                continue;
            }

            sim = std::make_unique<riscv::RISCVSimulator>();
            sim->load_program(binary, 0x80000000ULL);

            if (g_difftest.enabled && !g_difftest.enabled_signals.empty()) {
                g_difftest.shadow_sim = std::make_unique<riscv::RISCVSimulator>();
                g_difftest.shadow_sim->load_program(binary, 0x80000000ULL);
            }

            g_current_elf_path = "memory_binary";
            g_last_binary = binary;
            g_last_load_offset = 0x80000000ULL;

            g_difftest.diff_detected = false;
            g_difftest.waiting_for_input = false;
            g_difftest.user_signals.clear();

            std::cout << "{\"status\":\"ok\",\"message\":\"Loaded ELF binary\",\"binary_size\":" << binary.size() << ",";
            if (g_difftest.enabled && g_difftest.shadow_sim) {
                output_signals_body(*g_difftest.shadow_sim);
            } else {
                output_signals_body(*sim);
            }
            std::cout << "}" << std::endl;
            std::cout.flush();

        } else if (cmd == "quit") {
            break;

        } else if (cmd == "ping") {
            std::cout << "{\"status\":\"ok\",\"message\":\"pong\"}" << std::endl;

        } else {
            std::cout << "{\"status\":\"error\",\"message\":\"Unknown command: " << escape_json(cmd) << "\"}" << std::endl;
        }
    }

    return 0;
}
