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

namespace {

std::atomic<bool> g_running{true};

struct DiffTestConfig {
    bool enabled{false};
    std::set<std::string> enabled_signals;
    std::map<std::string, bool> user_signals;
    std::string pending_signal;
    bool waiting_for_input{false};
    bool diff_detected{false};
    std::string diff_message;
    
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

void output_need_signal_input(const std::string& signal_name, const std::string& expected_value) {
    std::cout << "{\"type\":\"need_signal_input\",\"needInput\":{"
              << "\"signalName\":\"" << signal_name << "\","
              << "\"expectedValue\":\"" << expected_value << "\""
              << "}}";
    std::cout << std::endl;
    std::cout.flush();
}

void output_diff_detected() {
    std::cout << "{\"type\":\"diff_detected\",\"diffResult\":{"
              << "\"detected\":true,"
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
            g_difftest.diff_detected = false;
            g_difftest.waiting_for_input = false;
            g_difftest.user_signals.clear();
            std::cout << "{\"status\":\"ok\",\"message\":\"Loaded " << escape_json(filepath) << "\"}" << std::endl;

        } else if (cmd == "step") {
            if (!sim) {
                std::cout << "{\"status\":\"error\",\"message\":\"No program loaded\"}" << std::endl;
                continue;
            }
            if (sim->halted()) {
                std::cout << "{\"status\":\"error\",\"message\":\"Simulation halted\"}" << std::endl;
                continue;
            }
            if (g_difftest.enabled && !g_difftest.diff_detected && !g_difftest.waiting_for_input) {
                const auto& if_id = sim->if_id();
                if (if_id.valid) {
                    auto instr = riscv::decode(if_id.inst, if_id.pc);
                    for (const auto& signal : g_difftest.enabled_signals) {
                        bool expected = get_default_signal(instr, signal);
                        auto it = g_difftest.user_signals.find(signal);
                        if (it == g_difftest.user_signals.end()) {
                            g_difftest.waiting_for_input = true;
                            g_difftest.pending_signal = signal;
                            std::string expected_str = expected ? "1" : "0";
                            output_need_signal_input(signal, expected_str);
                            break;
                        }
                    }
                }
            }
            if (!g_difftest.waiting_for_input && !g_difftest.diff_detected) {
                sim->step();
            }
            if (!g_difftest.waiting_for_input) {
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
            std::string signals_str;
            std::getline(iss, signals_str);
            std::istringstream sig_iss(signals_str);
            std::string signal;
            g_difftest.enabled_signals.clear();
            while (sig_iss >> signal) {
                if (!signal.empty()) {
                    g_difftest.enabled_signals.insert(signal);
                }
            }
            g_difftest.enabled = true;
            g_difftest.user_signals.clear();
            std::cout << "{\"status\":\"ok\",\"message\":\"Difftest enabled with signals: " << escape_json(signals_str) << "\"}" << std::endl;

        } else if (cmd == "disable_difftest") {
            g_difftest.enabled = false;
            g_difftest.enabled_signals.clear();
            g_difftest.user_signals.clear();
            std::cout << "{\"status\":\"ok\",\"message\":\"Difftest disabled\"}" << std::endl;

        } else if (cmd == "set_user_signal") {
            std::string signal_name, value_str;
            iss >> signal_name >> value_str;
            bool value = (value_str == "true" || value_str == "1");
            g_difftest.user_signals[signal_name] = value;
            std::cout << "{\"status\":\"ok\",\"message\":\"User signal set: " << escape_json(signal_name) << "=" << (value ? "1" : "0") << "\"}" << std::endl;

        } else if (cmd == "continue") {
            g_difftest.diff_detected = false;
            g_difftest.waiting_for_input = false;
            std::cout << "{\"status\":\"ok\",\"message\":\"Continuing\"}" << std::endl;

        } else if (cmd == "quit") {
            break;

        } else {
            std::cout << "{\"status\":\"error\",\"message\":\"Unknown command: " << escape_json(cmd) << "\"}" << std::endl;
        }
    }

    return 0;
}
