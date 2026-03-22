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

#include "riscv/types.h"
#include "riscv/simulator.h"
#include "riscv/elf_loader.h"
#include "riscv/decoder.h"
#include "riscv/teaching_tests.h"
#include "riscv/teaching_elf_config.h"

namespace {

std::atomic<bool> g_running{true};
std::string g_current_elf_path;

struct DiffTestConfig {
    bool enabled{false};
    bool shadow_mode{false};
    std::set<std::string> enabled_signals;
    std::map<std::string, bool> user_signals;
    std::string pending_signal;
    size_t current_signal_index{0};
    bool waiting_for_input{false};
    bool diff_detected{false};
    bool signal_input_processed{false};
    std::string diff_message;
    
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

bool is_signal_relevant(const riscv::DecodedInstruction& instr, const std::string& signal_name) {
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

void output_diff_detected() {
    std::cout << "{\"type\":\"diff_detected\",\"diffResult\":{"
              << "\"detected\":true,"
              << "\"stage\":\"" << (g_difftest.diff_message.find("EX") != std::string::npos ? "EX" : "WB") << "\","
              << "\"goldenPC\":\"0x" << std::hex << g_difftest.golden_result.pc << std::dec << "\","
              << "\"userPC\":\"0x" << std::hex << g_difftest.user_result.pc << std::dec << "\","
              << "\"goldenResult\":{"
              << "\"regWrite\":" << (g_difftest.golden_result.reg_write ? "true" : "false") << ","
              << "\"waddr\":" << g_difftest.golden_result.waddr << ","
              << "\"wdata\":\"0x" << std::hex << g_difftest.golden_result.wdata << std::dec << "\""
              << "},"
              << "\"userResult\":{"
              << "\"regWrite\":" << (g_difftest.user_result.reg_write ? "true" : "false") << ","
              << "\"waddr\":" << g_difftest.user_result.waddr << ","
              << "\"wdata\":\"0x" << std::hex << g_difftest.user_result.wdata << std::dec << "\""
              << "},"
              << "\"message\":\"" << escape_json(g_difftest.diff_message) << "\""
              << "}}";
    std::cout << std::endl;
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

void output_signals(riscv::RISCVSimulator& sim) {
    const auto& if_id = sim.if_id();
    const auto& id_ex = sim.id_ex();
    const auto& ex_mem = sim.ex_mem();
    const auto& mem_wb = sim.mem_wb();

    std::cout << "{\"cycle\":" << sim.cycle()
              << ",\"pc\":\"0x" << std::hex << sim.pc() << std::dec << "\"";

    std::cout << ",\"fetch\":{"
              << "\"pc\":\"0x" << std::hex << sim.pc() << std::dec << "\","
              << "\"valid\":" << (if_id.valid ? "true" : "false") << ","
              << "\"target\":\"0x" << std::hex << sim.redirect_target() << std::dec << "\","
              << "\"taken\":" << (sim.redirect() ? "true" : "false") << ","
              << "\"PC_next\":\"0x" << std::hex << sim.next_pc() << std::dec << "\","
              << "\"allow_to_go\":" << ((!sim.stall_fetch() && !sim.halted()) ? "true" : "false")
              << "}";

    auto decode_instr = if_id.valid ? riscv::decode(if_id.inst, if_id.pc) : riscv::DecodedInstruction{};
    riscv::u32 src1_raddr = if_id.valid && uses_rs1(decode_instr.kind) ? decode_instr.rs1 : 0;
    riscv::u32 src2_raddr = if_id.valid && uses_rs2(decode_instr.kind) ? decode_instr.rs2 : 0;

    std::cout << ",\"decode\":{"
              << "\"pc\":\"0x" << std::hex << (if_id.valid ? if_id.pc : 0) << std::dec << "\","
              << "\"inst\":" << (if_id.valid ? if_id.inst : 0) << ","
              << "\"src1_raddr\":" << src1_raddr << ","
              << "\"src1_rdata\":\"0x" << std::hex << sim.registers().read(src1_raddr) << std::dec << "\","
              << "\"src2_raddr\":" << src2_raddr << ","
              << "\"src2_rdata\":\"0x" << std::hex << sim.registers().read(src2_raddr) << std::dec << "\","
              << "\"decodeInfo\":{\"src1_ren\":" << (if_id.valid && uses_rs1(decode_instr.kind) ? "true" : "false")
              << ",\"src2_ren\":" << (if_id.valid && uses_rs2(decode_instr.kind) ? "true" : "false")
              << ",\"src1_raddr\":" << src1_raddr
              << ",\"src2_raddr\":" << src2_raddr << "}}";

    std::cout << ",\"execute\":{"
              << "\"pc\":\"0x" << std::hex << (id_ex.valid ? id_ex.instr.pc : 0) << std::dec << "\","
              << "\"valid\":" << (id_ex.valid ? "true" : "false") << ","
              << "\"alu_result\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.alu_result : 0) << std::dec << "\","
              << "\"fu_type\":\"" << (id_ex.valid ? riscv::to_string(id_ex.instr.kind) : "NONE") << "\","
              << "\"branch_taken\":" << (ex_mem.valid && ex_mem.branch_taken ? "true" : "false") << ","
              << "\"branch_target\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.branch_target : 0) << std::dec << "\""
              << "}";

    bool mem_valid = ex_mem.valid && (ex_mem.instr.is_load() || ex_mem.instr.is_store());
    std::cout << ",\"memory\":{"
              << "\"pc\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.instr.pc : 0) << std::dec << "\","
              << "\"valid\":" << (ex_mem.valid ? "true" : "false") << ","
              << "\"mem_addr\":\"0x" << std::hex << (mem_valid ? ex_mem.alu_result : 0) << std::dec << "\","
              << "\"mem_wen\":" << (ex_mem.valid && ex_mem.instr.is_store() ? "true" : "false") << ","
              << "\"mem_ren\":" << (ex_mem.valid && ex_mem.instr.is_load() ? "true" : "false") << ","
              << "\"info\":\"" << (ex_mem.valid ? riscv::to_string(ex_mem.instr.kind) : "NONE") << "\""
              << "}";

    std::cout << ",\"writeback\":{"
              << "\"pc\":\"0x" << std::hex << (mem_wb.valid ? mem_wb.instr.pc : 0) << std::dec << "\","
              << "\"valid\":" << (mem_wb.valid ? "true" : "false") << ","
              << "\"debug_commit\":" << (mem_wb.valid ? "true" : "false") << ","
              << "\"debug_pc\":\"0x" << std::hex << (mem_wb.valid ? mem_wb.instr.pc : 0) << std::dec << "\","
              << "\"debug_wb_rf_wen\":" << (mem_wb.valid && mem_wb.instr.writes_rd() ? "true" : "false") << ","
              << "\"debug_wb_rf_waddr\":" << (mem_wb.valid ? mem_wb.instr.rd : 0) << ","
              << "\"debug_wb_rf_wdata\":\"0x" << std::hex << (mem_wb.valid ? mem_wb.wb_value : 0) << std::dec << "\""
              << "}";

    riscv::u32 wb_waddr = mem_wb.valid && mem_wb.instr.writes_rd() ? mem_wb.instr.rd : 0;
    riscv::u64 wb_wdata = mem_wb.valid ? mem_wb.wb_value : 0;
    std::cout << ",\"regfile\":{"
              << "\"src1_raddr\":" << src1_raddr << ","
              << "\"src1_rdata\":\"0x" << std::hex << sim.registers().read(src1_raddr) << std::dec << "\","
              << "\"src2_raddr\":" << src2_raddr << ","
              << "\"src2_rdata\":\"0x" << std::hex << sim.registers().read(src2_raddr) << std::dec << "\","
              << "\"reg_wen\":" << (mem_wb.valid && mem_wb.instr.writes_rd() ? "true" : "false") << ","
              << "\"reg_waddr\":" << wb_waddr << ","
              << "\"reg_wdata\":\"0x" << std::hex << wb_wdata << std::dec << "\""
              << "}";

    bool datamem_en = ex_mem.valid && (ex_mem.instr.is_load() || ex_mem.instr.is_store());
    std::cout << ",\"datamem\":{"
              << "\"DataMEM_en\":" << (datamem_en ? "true" : "false") << ","
              << "\"DataMEM_wen\":" << (ex_mem.valid && ex_mem.instr.is_store() ? "true" : "false") << ","
              << "\"DataMEM_addr\":\"0x" << std::hex << (datamem_en ? ex_mem.alu_result : 0) << std::dec << "\","
              << "\"DataMEM_rdata\":0,"
              << "\"DataMEM_wdata\":\"0x" << std::hex << (ex_mem.valid ? ex_mem.rs2_value : 0) << std::dec << "\""
              << "}";

    std::cout << ",\"halted\":" << (sim.halted() ? "true" : "false") << "}";
    std::cout << std::endl;
    std::cout.flush();
}

void output_registers(riscv::RISCVSimulator& sim) {
    const auto& regs = sim.registers().raw();
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
            
            if (g_difftest.shadow_mode) {
                g_difftest.shadow_sim = std::make_unique<riscv::RISCVSimulator>();
                g_difftest.shadow_sim->load_program(result.binary, result.load_offset);
            }
            
            g_difftest.diff_detected = false;
            g_difftest.waiting_for_input = false;
            g_difftest.user_signals.clear();
            std::cout << "{\"status\":\"ok\",\"message\":\"Loaded " << escape_json(filepath) << "\"}" << std::endl;

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

                    if (g_difftest.shadow_mode) {
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
            
            std::cerr << "[DEBUG step] difftest enabled: " << g_difftest.enabled 
                      << ", shadow_mode: " << g_difftest.shadow_mode 
                      << ", waiting: " << g_difftest.waiting_for_input
                      << ", diff: " << g_difftest.diff_detected << std::endl;
            
            if (g_difftest.enabled && g_difftest.shadow_mode && g_difftest.shadow_sim) {
                // Clear diff_detected flag to allow continuing
                if (g_difftest.diff_detected) {
                    g_difftest.diff_detected = false;
                    g_difftest.waiting_for_input = false;
                    g_difftest.user_signals.clear();
                }
                
                if (!g_difftest.waiting_for_input) {
                    sim->step();
                    g_difftest.shadow_sim->step();
                    
                    const auto& ex_mem = sim->ex_mem();
                    const auto& shadow_ex_mem = g_difftest.shadow_sim->ex_mem();
                    
                    if (ex_mem.valid && shadow_ex_mem.valid) {
                        g_difftest.golden_result.valid = true;
                        g_difftest.golden_result.pc = ex_mem.instr.pc;
                        g_difftest.golden_result.alu_result = ex_mem.alu_result;
                        g_difftest.golden_result.branch_taken = ex_mem.branch_taken;
                        g_difftest.golden_result.branch_target = ex_mem.branch_target;
                        
                        g_difftest.user_result.valid = true;
                        g_difftest.user_result.pc = shadow_ex_mem.instr.pc;
                        g_difftest.user_result.alu_result = shadow_ex_mem.alu_result;
                        g_difftest.user_result.branch_taken = shadow_ex_mem.branch_taken;
                        g_difftest.user_result.branch_target = shadow_ex_mem.branch_target;
                        
                        if (!compare_ex_results(g_difftest.golden_result, g_difftest.user_result, g_difftest.diff_message)) {
                            g_difftest.diff_detected = true;
                            output_ex_diff_detected();
                            continue;
                        }
                    }
                    
                    const auto& mem_wb = sim->mem_wb();
                    const auto& shadow_mem_wb = g_difftest.shadow_sim->mem_wb();
                    
                    if (mem_wb.valid && shadow_mem_wb.valid) {
                        g_difftest.golden_result.valid = true;
                        g_difftest.golden_result.pc = mem_wb.instr.pc;
                        g_difftest.golden_result.reg_write = mem_wb.instr.writes_rd();
                        g_difftest.golden_result.waddr = mem_wb.instr.rd;
                        g_difftest.golden_result.wdata = mem_wb.wb_value;
                        
                        g_difftest.user_result.valid = true;
                        g_difftest.user_result.pc = shadow_mem_wb.instr.pc;
                        g_difftest.user_result.reg_write = shadow_mem_wb.instr.writes_rd();
                        g_difftest.user_result.waddr = shadow_mem_wb.instr.rd;
                        g_difftest.user_result.wdata = shadow_mem_wb.wb_value;
                        
                        if (!compare_wb_results(g_difftest.golden_result, g_difftest.user_result, g_difftest.diff_message)) {
                            g_difftest.diff_detected = true;
                            output_diff_detected();
                            continue;
                        }
                    }
                }
                output_signals(*sim);
            }
            else if (g_difftest.enabled) {
                // Clear diff_detected flag to allow continuing
                if (g_difftest.diff_detected) {
                    g_difftest.diff_detected = false;
                    g_difftest.waiting_for_input = false;
                    g_difftest.user_signals.clear();
                }
                
                // Skip if signal input was already processed by set_user_signal
                if (g_difftest.signal_input_processed) {
                    g_difftest.signal_input_processed = false;
                    output_signals(*sim);
                } else if (g_difftest.waiting_for_input) {
                    bool all_signals_received = true;
                    for (const auto& signal : g_difftest.enabled_signals) {
                        if (g_difftest.user_signals.find(signal) == g_difftest.user_signals.end()) {
                            all_signals_received = false;
                            break;
                        }
                    }
                    
                    if (all_signals_received) {
                        std::cerr << "[DEBUG INPUT] All signals received, user_signals: ";
                        for (const auto& [k, v] : g_difftest.user_signals) {
                            std::cerr << k << "=" << v << " ";
                        }
                        std::cerr << std::endl;
                        
                        // Set external control signals on shadow_sim based on user input
                        g_difftest.shadow_sim->external_signals.clear();
                        for (const auto& [signal, value] : g_difftest.user_signals) {
                            if (signal == "RegWrite") {
                                g_difftest.shadow_sim->external_signals.reg_write = value;
                                std::cerr << "[DEBUG INPUT] Setting RegWrite=" << value << std::endl;
                            } else if (signal == "ALUSrc") {
                                g_difftest.shadow_sim->external_signals.alu_src = value;
                            } else if (signal == "MemRead") {
                                g_difftest.shadow_sim->external_signals.mem_read = value;
                            } else if (signal == "MemWrite") {
                                g_difftest.shadow_sim->external_signals.mem_write = value;
                            } else if (signal == "Branch") {
                                g_difftest.shadow_sim->external_signals.branch = value;
                            }
                        }
                        
                        // Step both simulators
                        sim->step();
                        g_difftest.shadow_sim->step();
                        
                        // Clear external signals after step to prevent pollution
                        g_difftest.shadow_sim->external_signals.clear();
                        
                        // Compare WB results
                        const auto& golden_wb = sim->last_wb_result;
                        const auto& user_wb = g_difftest.shadow_sim->last_wb_result;
                        
                        std::cerr << "[DEBUG WB] golden: pc=0x" << std::hex << golden_wb.pc
                                  << ", wb_en=" << golden_wb.wb_en
                                  << ", rd=x" << std::dec << golden_wb.wb_raddr
                                  << ", data=0x" << std::hex << golden_wb.wb_rdata << std::dec << std::endl;
                        std::cerr << "[DEBUG WB] user: pc=0x" << std::hex << user_wb.pc
                                  << ", wb_en=" << user_wb.wb_en
                                  << ", rd=x" << std::dec << user_wb.wb_raddr
                                  << ", data=0x" << std::hex << user_wb.wb_rdata << std::dec << std::endl;
                        
                        if (golden_wb.valid && user_wb.valid) {
                            if (golden_wb.wb_en != user_wb.wb_en ||
                                (golden_wb.wb_en && (golden_wb.wb_raddr != user_wb.wb_raddr ||
                                                     golden_wb.wb_rdata != user_wb.wb_rdata))) {
                                
                                g_difftest.diff_detected = true;
                                g_difftest.golden_result.pc = golden_wb.pc;
                                g_difftest.golden_result.reg_write = golden_wb.wb_en;
                                g_difftest.golden_result.waddr = golden_wb.wb_raddr;
                                g_difftest.golden_result.wdata = golden_wb.wb_rdata;
                                
                                g_difftest.user_result.pc = user_wb.pc;
                                g_difftest.user_result.reg_write = user_wb.wb_en;
                                g_difftest.user_result.waddr = user_wb.wb_raddr;
                                g_difftest.user_result.wdata = user_wb.wb_rdata;
                                
                                g_difftest.diff_message = "WB阶段差异: ";
                                if (golden_wb.wb_en != user_wb.wb_en) {
                                    g_difftest.diff_message += "写使能不匹配 (Golden: " + 
                                        std::string(golden_wb.wb_en ? "1" : "0") + 
                                        ", User: " + std::string(user_wb.wb_en ? "1" : "0") + ")";
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
                                continue;
                            }
                        }
                        
                        g_difftest.waiting_for_input = false;
                        g_difftest.user_signals.clear();
                        output_signals(*sim);
                    } else {
                        output_signals(*sim);
                    }
                } else {
                    const auto& if_id = sim->if_id();
                    if (if_id.valid) {
                        auto instr = riscv::decode(if_id.inst, if_id.pc);
                        std::cerr << "[DEBUG INPUT] Requesting signal input for instr at PC 0x" << std::hex << if_id.pc
                                  << ", instr=" << riscv::to_string(instr.kind) << std::dec << std::endl;
                        g_difftest.waiting_for_input = true;
                        g_difftest.user_signals.clear();
                        std::string instr_name = riscv::to_string(instr.kind);
                        output_need_all_signals_input(instr, instr_name);
                    }
                    
                    if (!g_difftest.waiting_for_input) {
                        // No user input needed, step both simulators together
                        sim->step();
                        if (g_difftest.shadow_sim) {
                            g_difftest.shadow_sim->step();
                        }
                        output_signals(*sim);
                    }
                }
            }
            else {
                sim->step();
                output_signals(*sim);
            }

        } else if (cmd == "run") {
            if (!sim) {
                std::cout << "{\"status\":\"error\",\"message\":\"No program loaded\"}" << std::endl;
                continue;
            }
            while (g_running && !sim->halted()) {
                sim->step();
                output_signals(*sim);
            }

        } else if (cmd == "reset") {
            if (sim) {
                sim->reset();
            }
            g_difftest.diff_detected = false;
            g_difftest.waiting_for_input = false;
            g_difftest.current_signal_index = 0;
            g_difftest.user_signals.clear();
            std::cout << "{\"status\":\"ok\",\"message\":\"Reset\"}" << std::endl;

        } else if (cmd == "signals") {
            if (!sim) {
                std::cout << "{\"status\":\"error\",\"message\":\"No program loaded\"}" << std::endl;
                continue;
            }
            output_signals(*sim);

        } else if (cmd == "registers") {
            if (!sim) {
                std::cout << "{\"status\":\"error\",\"message\":\"No program loaded\"}" << std::endl;
                continue;
            }
            output_registers(*sim);

        } else if (cmd == "enable_difftest") {
            std::cerr << "[DEBUG] enable_difftest command received" << std::endl;
            std::string signals_str;
            std::getline(iss, signals_str);
            std::cerr << "[DEBUG] signals_str: " << signals_str << std::endl;
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

            if (g_difftest.enabled) {
                if (!sim) {
                    std::cout << "{\"status\":\"error\",\"message\":\"No program loaded\"}" << std::endl;
                } else {
                    std::cerr << "[DEBUG] Creating shadow_sim, g_current_elf_path: " << g_current_elf_path << std::endl;
                    g_difftest.shadow_sim = std::make_unique<riscv::RISCVSimulator>();
                    auto load_result = riscv::load_elf(g_current_elf_path);
                    std::cerr << "[DEBUG] load_elf success: " << load_result.success 
                              << ", binary size: " << load_result.binary.size()
                              << ", load_offset: " << load_result.load_offset << std::endl;
                    if (load_result.success) {
                        g_difftest.shadow_sim->load_program(load_result.binary, load_result.load_offset);
                        std::cerr << "[DEBUG] shadow_sim PC after load: 0x" << std::hex << g_difftest.shadow_sim->pc() << std::dec << std::endl;
                    } else {
                        std::cerr << "[DEBUG] load_elf error: " << load_result.error << std::endl;
                    }
                }
            }

            g_difftest.user_signals.clear();
            std::string mode_str = g_difftest.shadow_mode ? "shadow mode" : "user input mode";
            std::cerr << "[DEBUG] difftest enabled: " << g_difftest.enabled << ", shadow_mode: " << g_difftest.shadow_mode << ", signals: " << g_difftest.enabled_signals.size() << std::endl;
            std::cout << "{\"status\":\"ok\",\"message\":\"Difftest enabled (" << mode_str << ") with signals: " << escape_json(signals_str) << "\"}" << std::endl;
            std::cout.flush();

        } else if (cmd == "disable_difftest") {
            g_difftest.enabled = false;
            g_difftest.shadow_mode = false;
            g_difftest.shadow_sim.reset();
            g_difftest.enabled_signals.clear();
            g_difftest.user_signals.clear();
            std::cout << "{\"status\":\"ok\",\"message\":\"Difftest disabled\"}" << std::endl;

        } else if (cmd == "set_user_signal") {
            std::string signal_name, value_str;
            iss >> signal_name >> value_str;
            bool value = (value_str == "true" || value_str == "1");
            g_difftest.user_signals[signal_name] = value;
            
            // Check if we need to process immediately in user input mode
            if (g_difftest.enabled && !g_difftest.shadow_mode && g_difftest.waiting_for_input) {
                bool all_signals_received = true;
                for (const auto& signal : g_difftest.enabled_signals) {
                    if (g_difftest.user_signals.find(signal) == g_difftest.user_signals.end()) {
                        all_signals_received = false;
                        break;
                    }
                }
                
                if (all_signals_received && g_difftest.shadow_sim) {
                    // Immediately process the step
                    g_difftest.shadow_sim->external_signals.clear();
                    for (const auto& [signal, sig_value] : g_difftest.user_signals) {
                        if (signal == "RegWrite") {
                            g_difftest.shadow_sim->external_signals.reg_write = sig_value;
                        } else if (signal == "ALUSrc") {
                            g_difftest.shadow_sim->external_signals.alu_src = sig_value;
                        } else if (signal == "MemRead") {
                            g_difftest.shadow_sim->external_signals.mem_read = sig_value;
                        } else if (signal == "MemWrite") {
                            g_difftest.shadow_sim->external_signals.mem_write = sig_value;
                        } else if (signal == "Branch") {
                            g_difftest.shadow_sim->external_signals.branch = sig_value;
                        }
                    }
                    
                    sim->step();
                    g_difftest.shadow_sim->step();
                    g_difftest.shadow_sim->external_signals.clear();
                    
                    const auto& golden_wb = sim->last_wb_result;
                    const auto& user_wb = g_difftest.shadow_sim->last_wb_result;
                    
                    if (golden_wb.valid && user_wb.valid) {
                        if (golden_wb.wb_en != user_wb.wb_en ||
                            (golden_wb.wb_en && (golden_wb.wb_raddr != user_wb.wb_raddr ||
                                                 golden_wb.wb_rdata != user_wb.wb_rdata))) {
                            
                            g_difftest.diff_detected = true;
                            g_difftest.golden_result.pc = golden_wb.pc;
                            g_difftest.golden_result.reg_write = golden_wb.wb_en;
                            g_difftest.golden_result.waddr = golden_wb.wb_raddr;
                            g_difftest.golden_result.wdata = golden_wb.wb_rdata;
                            
                            g_difftest.user_result.pc = user_wb.pc;
                            g_difftest.user_result.reg_write = user_wb.wb_en;
                            g_difftest.user_result.waddr = user_wb.wb_raddr;
                            g_difftest.user_result.wdata = user_wb.wb_rdata;
                            
                            g_difftest.diff_message = "WB阶段差异: ";
                            if (golden_wb.wb_en != user_wb.wb_en) {
                                g_difftest.diff_message += "写使能不匹配 (Golden: " +
                                    std::string(golden_wb.wb_en ? "1" : "0") +
                                    ", User: " + std::string(user_wb.wb_en ? "1" : "0") + ")";
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
                            g_difftest.user_signals.clear();
                            g_difftest.signal_input_processed = true;
                            std::cout.flush();
                            continue;
                        }
                    }

                    g_difftest.waiting_for_input = false;
                    g_difftest.user_signals.clear();
                    g_difftest.signal_input_processed = true;
                    output_signals(*sim);
                    std::cout.flush();
                    continue;
                }
            }
            
            std::cout << "{\"status\":\"ok\",\"message\":\"User signal set: " << escape_json(signal_name) << "=" << (value ? "1" : "0") << "\"}" << std::endl;

        } else if (cmd == "skip_signal_input") {
            g_difftest.waiting_for_input = false;
            g_difftest.current_signal_index = 0;
            g_difftest.user_signals.clear();
            std::cout << "{\"status\":\"ok\",\"message\":\"Signal input skipped\"}" << std::endl;

        } else if (cmd == "continue") {
            g_difftest.diff_detected = false;
            g_difftest.waiting_for_input = false;
            std::cout << "{\"status\":\"ok\",\"message\":\"Continuing\"}" << std::endl;

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
