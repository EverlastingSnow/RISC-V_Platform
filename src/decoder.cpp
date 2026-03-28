#include "riscv/decoder.h"

#include <array>
#include <stdexcept>

namespace riscv {
namespace {

constexpr s32 imm_i(u32 raw) {
    return sign_extend<s32>(static_cast<s32>((raw >> 20) & 0xFFF), 12);
}

constexpr s32 imm_s(u32 raw) {
    const u32 low = (raw >> 7) & 0x1F;
    const u32 high = (raw >> 25) & 0x7F;
    return sign_extend<s32>(static_cast<s32>((high << 5) | low), 12);
}

constexpr s32 imm_b(u32 raw) {
    const u32 bit11 = (raw >> 7) & 0x1;
    const u32 bits4_1 = (raw >> 8) & 0xF;
    const u32 bits10_5 = (raw >> 25) & 0x3F;
    const u32 bit12 = (raw >> 31) & 0x1;
    const u32 value = (bit12 << 12) | (bit11 << 11) | (bits10_5 << 5) | (bits4_1 << 1);
    return sign_extend<s32>(static_cast<s32>(value), 13);
}

constexpr s32 imm_u(u32 raw) {
    return static_cast<s32>(raw & 0xFFFFF000);
}

constexpr u32 imm_csr(u32 raw) {
    return (raw >> 20) & 0xFFFu;  // CSR 地址是 12 位无符号，不做符号扩展
}

constexpr s32 imm_j(u32 raw) {
    const u32 bits19_12 = (raw >> 12) & 0xFF;
    const u32 bit11 = (raw >> 20) & 0x1;
    const u32 bits10_1 = (raw >> 21) & 0x3FF;
    const u32 bit20 = (raw >> 31) & 0x1;
    const u32 value = (bit20 << 20) | (bits19_12 << 12) | (bit11 << 11) | (bits10_1 << 1);
    return sign_extend<s32>(static_cast<s32>(value), 21);
}

bool is_shift_imm_valid(u32 funct7) {
    // 注意：RV64 的 I 型移位使用 shamt[5:0]，bit25 是 shamt[5]，不能简单用 bits[31:25] 当 funct7 判断。
    // 该函数仅保留给 RV32 风格检查用（funct7=0 或 0x20），RV64 解码处会用 funct6(bits[31:26]) 判断。
    return funct7 == 0b0000000 || funct7 == 0b0100000;
}

bool is_m_extension(u32 funct7) {
    return funct7 == 0b0000001;
}

}  // namespace

bool DecodedInstruction::writes_rd() const {
    switch (kind) {
        case InstructionKind::LUI:
        case InstructionKind::AUIPC:
        case InstructionKind::JAL:
        case InstructionKind::JALR:
        case InstructionKind::LB:
        case InstructionKind::LH:
        case InstructionKind::LW:
        case InstructionKind::LD:
        case InstructionKind::LBU:
        case InstructionKind::LHU:
        case InstructionKind::LWU:
        case InstructionKind::ADDI:
        case InstructionKind::SLTI:
        case InstructionKind::SLTIU:
        case InstructionKind::XORI:
        case InstructionKind::ORI:
        case InstructionKind::ANDI:
        case InstructionKind::SLLI:
        case InstructionKind::SRLI:
        case InstructionKind::SRAI:
        case InstructionKind::ADD:
        case InstructionKind::SUB:
        case InstructionKind::SLL:
        case InstructionKind::SLT:
        case InstructionKind::SLTU:
        case InstructionKind::XOR:
        case InstructionKind::SRL:
        case InstructionKind::SRA:
        case InstructionKind::OR:
        case InstructionKind::AND:
        case InstructionKind::ADDIW:
        case InstructionKind::SLLIW:
        case InstructionKind::SRLIW:
        case InstructionKind::SRAIW:
        case InstructionKind::ADDW:
        case InstructionKind::SUBW:
        case InstructionKind::SLLW:
        case InstructionKind::SRLW:
        case InstructionKind::SRAW:
        case InstructionKind::MUL:
        case InstructionKind::MULH:
        case InstructionKind::MULHSU:
        case InstructionKind::MULHU:
        case InstructionKind::DIV:
        case InstructionKind::DIVU:
        case InstructionKind::REM:
        case InstructionKind::REMU:
        case InstructionKind::MULW:
        case InstructionKind::DIVW:
        case InstructionKind::DIVUW:
        case InstructionKind::REMW:
        case InstructionKind::REMUW:
        case InstructionKind::CSRRW:
        case InstructionKind::CSRRS:
        case InstructionKind::CSRRC:
        case InstructionKind::CSRRWI:
        case InstructionKind::CSRRSI:
        case InstructionKind::CSRRCI:
            return true;
        default:
            return false;
    }
}

bool DecodedInstruction::is_branch() const {
    switch (kind) {
        case InstructionKind::BEQ:
        case InstructionKind::BNE:
        case InstructionKind::BLT:
        case InstructionKind::BGE:
        case InstructionKind::BLTU:
        case InstructionKind::BGEU:
            return true;
        default:
            return false;
    }
}

bool DecodedInstruction::is_jump() const {
    return kind == InstructionKind::JAL || kind == InstructionKind::JALR;
}

bool DecodedInstruction::is_load() const {
    switch (kind) {
        case InstructionKind::LB:
        case InstructionKind::LH:
        case InstructionKind::LW:
        case InstructionKind::LD:
        case InstructionKind::LBU:
        case InstructionKind::LHU:
        case InstructionKind::LWU:
            return true;
        default:
            return false;
    }
}

bool DecodedInstruction::is_store() const {
    switch (kind) {
        case InstructionKind::SB:
        case InstructionKind::SH:
        case InstructionKind::SW:
        case InstructionKind::SD:
            return true;
        default:
            return false;
    }
}

bool DecodedInstruction::is_csr() const {
    switch (kind) {
        case InstructionKind::CSRRW:
        case InstructionKind::CSRRS:
        case InstructionKind::CSRRC:
        case InstructionKind::CSRRWI:
        case InstructionKind::CSRRSI:
        case InstructionKind::CSRRCI:
            return true;
        default:
            return false;
    }
}

bool DecodedInstruction::is_system() const {
    return kind == InstructionKind::ECALL || kind == InstructionKind::EBREAK || kind == InstructionKind::MRET;
}

DecodedInstruction decode(u32 raw, u64 pc) {
    DecodedInstruction inst{};
    inst.raw = raw;
    inst.pc = pc;
    inst.opcode = raw & 0x7F;
    inst.rd = (raw >> 7) & 0x1F;
    inst.funct3 = (raw >> 12) & 0x7;
    inst.rs1 = (raw >> 15) & 0x1F;
    inst.rs2 = (raw >> 20) & 0x1F;
    inst.funct7 = (raw >> 25) & 0x7F;

    switch (inst.opcode) {
        case 0b0110111:  // LUI
            inst.kind = InstructionKind::LUI;
            inst.format = InstructionFormat::U;
            inst.imm = imm_u(raw);
            break;
        case 0b0010111:  // AUIPC
            inst.kind = InstructionKind::AUIPC;
            inst.format = InstructionFormat::U;
            inst.imm = imm_u(raw);
            break;
        case 0b1101111:  // JAL
            inst.kind = InstructionKind::JAL;
            inst.format = InstructionFormat::J;
            inst.imm = imm_j(raw);
            break;
        case 0b1100111:  // JALR
            if (inst.funct3 == 0b000) {
                inst.kind = InstructionKind::JALR;
                inst.format = InstructionFormat::I;
                inst.imm = imm_i(raw);
            }
            break;
        case 0b1100011:  // Branches
            inst.format = InstructionFormat::B;
            inst.imm = imm_b(raw);
            switch (inst.funct3) {
                case 0b000: inst.kind = InstructionKind::BEQ; break;
                case 0b001: inst.kind = InstructionKind::BNE; break;
                case 0b100: inst.kind = InstructionKind::BLT; break;
                case 0b101: inst.kind = InstructionKind::BGE; break;
                case 0b110: inst.kind = InstructionKind::BLTU; break;
                case 0b111: inst.kind = InstructionKind::BGEU; break;
                default: break;
            }
            break;
        case 0b0000011:  // Loads
            inst.format = InstructionFormat::I;
            inst.imm = imm_i(raw);
            switch (inst.funct3) {
                case 0b000: inst.kind = InstructionKind::LB; break;
                case 0b001: inst.kind = InstructionKind::LH; break;
                case 0b010: inst.kind = InstructionKind::LW; break;
                case 0b100: inst.kind = InstructionKind::LBU; break;
                case 0b101: inst.kind = InstructionKind::LHU; break;
                case 0b011: inst.kind = InstructionKind::LD; break;
                case 0b110: inst.kind = InstructionKind::LWU; break;
                default: break;
            }
            break;
        case 0b0100011:  // Stores
            inst.format = InstructionFormat::S;
            inst.imm = imm_s(raw);
            switch (inst.funct3) {
                case 0b000: inst.kind = InstructionKind::SB; break;
                case 0b001: inst.kind = InstructionKind::SH; break;
                case 0b010: inst.kind = InstructionKind::SW; break;
                case 0b011: inst.kind = InstructionKind::SD; break;
                default: break;
            }
            break;
        case 0b0010011:  // Immediate ALU
            inst.format = InstructionFormat::I;
            inst.imm = imm_i(raw);
            switch (inst.funct3) {
                case 0b000: inst.kind = InstructionKind::ADDI; break;
                case 0b010: inst.kind = InstructionKind::SLTI; break;
                case 0b011: inst.kind = InstructionKind::SLTIU; break;
                case 0b100: inst.kind = InstructionKind::XORI; break;
                case 0b110: inst.kind = InstructionKind::ORI; break;
                case 0b111: inst.kind = InstructionKind::ANDI; break;
                case 0b001:
                    // RV64: SLLI 的判断应看 funct6(bits[31:26])==0；bit25 是 shamt[5]
                    if (((raw >> 26) & 0x3Fu) == 0b000000) {
                        inst.kind = InstructionKind::SLLI;
                    }
                    break;
                case 0b101:
                    // RV64: SRLI/SRAI 也使用 funct6(bits[31:26]) 区分：0=SRLI, 0x10= SRAI
                    if (((raw >> 26) & 0x3Fu) == 0b000000) {
                        inst.kind = InstructionKind::SRLI;
                    } else if (((raw >> 26) & 0x3Fu) == 0b010000) {
                        inst.kind = InstructionKind::SRAI;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 0b0011011:  // OP-IMM-32 (RV64 "W" immediate ops)
            inst.format = InstructionFormat::I;
            inst.imm = imm_i(raw);
            switch (inst.funct3) {
                case 0b000: inst.kind = InstructionKind::ADDIW; break;
                case 0b001:
                    if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::SLLIW;
                    break;
                case 0b101:
                    if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::SRLIW;
                    else if (inst.funct7 == 0b0100000) inst.kind = InstructionKind::SRAIW;
                    break;
                default:
                    break;
            }
            break;
        case 0b0110011:  // Register ALU / M 扩展
            inst.format = InstructionFormat::R;
            if (is_m_extension(inst.funct7)) {
                switch (inst.funct3) {
                    case 0b000: inst.kind = InstructionKind::MUL; break;
                    case 0b001: inst.kind = InstructionKind::MULH; break;
                    case 0b010: inst.kind = InstructionKind::MULHSU; break;
                    case 0b011: inst.kind = InstructionKind::MULHU; break;
                    case 0b100: inst.kind = InstructionKind::DIV; break;
                    case 0b101: inst.kind = InstructionKind::DIVU; break;
                    case 0b110: inst.kind = InstructionKind::REM; break;
                    case 0b111: inst.kind = InstructionKind::REMU; break;
                    default: break;
                }
            } else {
                switch (inst.funct3) {
                    case 0b000:
                        if (inst.funct7 == 0b0000000) {
                            inst.kind = InstructionKind::ADD;
                        } else if (inst.funct7 == 0b0100000) {
                            inst.kind = InstructionKind::SUB;
                        }
                        break;
                    case 0b001:
                        if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::SLL;
                        break;
                    case 0b010:
                        if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::SLT;
                        break;
                    case 0b011:
                        if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::SLTU;
                        break;
                    case 0b100:
                        if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::XOR;
                        break;
                    case 0b101:
                        if (inst.funct7 == 0b0000000) {
                            inst.kind = InstructionKind::SRL;
                        } else if (inst.funct7 == 0b0100000) {
                            inst.kind = InstructionKind::SRA;
                        }
                        break;
                    case 0b110:
                        if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::OR;
                        break;
                    case 0b111:
                        if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::AND;
                        break;
                    default:
                        break;
                }
            }
            break;
        case 0b0111011:  // OP-32 (RV64 "W" register ops) / M 扩展 W
            inst.format = InstructionFormat::R;
            if (is_m_extension(inst.funct7)) {
                switch (inst.funct3) {
                    case 0b000: inst.kind = InstructionKind::MULW; break;
                    case 0b100: inst.kind = InstructionKind::DIVW; break;
                    case 0b101: inst.kind = InstructionKind::DIVUW; break;
                    case 0b110: inst.kind = InstructionKind::REMW; break;
                    case 0b111: inst.kind = InstructionKind::REMUW; break;
                    default: break;
                }
            } else {
                switch (inst.funct3) {
                    case 0b000:
                        if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::ADDW;
                        else if (inst.funct7 == 0b0100000) inst.kind = InstructionKind::SUBW;
                        break;
                    case 0b001:
                        if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::SLLW;
                        break;
                    case 0b101:
                        if (inst.funct7 == 0b0000000) inst.kind = InstructionKind::SRLW;
                        else if (inst.funct7 == 0b0100000) inst.kind = InstructionKind::SRAW;
                        break;
                    default:
                        break;
                }
            }
            break;
        case 0b0001111:  // MISC-MEM
            inst.format = InstructionFormat::I;
            inst.imm = imm_i(raw);
            if (inst.funct3 == 0b000) {
                inst.kind = InstructionKind::FENCE;
            } else if (inst.funct3 == 0b001) {
                inst.kind = InstructionKind::FENCE_I;
            }
            break;
        case 0b1110011:  // SYSTEM/CSR
            inst.format = InstructionFormat::I;
            if (inst.funct3 == 0) {
                inst.imm = imm_i(raw);
                const u32 imm12 = (raw >> 20) & 0xFFF;
                if (imm12 == 0) {
                    inst.kind = InstructionKind::ECALL;
                } else if (imm12 == 1) {
                    inst.kind = InstructionKind::EBREAK;
                } else if (imm12 == 0x302) {  // MRET: imm12=0x302 (mret = 0x30200073)
                    inst.kind = InstructionKind::MRET;
                } else if (imm12 == 0x102) {  // SRET: imm12=0x102
                    inst.kind = InstructionKind::SRET;
                } else if (imm12 == 0x105) {  // WFI: imm12=0x105
                    inst.kind = InstructionKind::WFI;
                } else if (imm12 == 0x120 && inst.funct3 == 0) {  // SFENCE.VMA
                    inst.kind = InstructionKind::SFENCE_VMA;
                }
            } else {
                inst.imm = static_cast<s32>(imm_csr(raw));  // CSR 地址 0..4095
                switch (inst.funct3) {
                    case 0b001: inst.kind = InstructionKind::CSRRW; break;
                    case 0b010: inst.kind = InstructionKind::CSRRS; break;
                    case 0b011: inst.kind = InstructionKind::CSRRC; break;
                    case 0b101: inst.kind = InstructionKind::CSRRWI; break;
                    case 0b110: inst.kind = InstructionKind::CSRRSI; break;
                    case 0b111: inst.kind = InstructionKind::CSRRCI; break;
                    default: break;
                }
            }
            break;
        default:
            break;
    }

    if (!inst.is_valid()) {
        inst.kind = InstructionKind::INVALID;
        inst.format = InstructionFormat::INVALID;
    }
    return inst;
}

std::string to_string(InstructionKind kind) {
    switch (kind) {
        case InstructionKind::LUI: return "LUI";
        case InstructionKind::AUIPC: return "AUIPC";
        case InstructionKind::JAL: return "JAL";
        case InstructionKind::JALR: return "JALR";
        case InstructionKind::BEQ: return "BEQ";
        case InstructionKind::BNE: return "BNE";
        case InstructionKind::BLT: return "BLT";
        case InstructionKind::BGE: return "BGE";
        case InstructionKind::BLTU: return "BLTU";
        case InstructionKind::BGEU: return "BGEU";
        case InstructionKind::LB: return "LB";
        case InstructionKind::LH: return "LH";
        case InstructionKind::LW: return "LW";
        case InstructionKind::LD: return "LD";
        case InstructionKind::LBU: return "LBU";
        case InstructionKind::LHU: return "LHU";
        case InstructionKind::LWU: return "LWU";
        case InstructionKind::SB: return "SB";
        case InstructionKind::SH: return "SH";
        case InstructionKind::SW: return "SW";
        case InstructionKind::SD: return "SD";
        case InstructionKind::ADDI: return "ADDI";
        case InstructionKind::SLTI: return "SLTI";
        case InstructionKind::SLTIU: return "SLTIU";
        case InstructionKind::XORI: return "XORI";
        case InstructionKind::ORI: return "ORI";
        case InstructionKind::ANDI: return "ANDI";
        case InstructionKind::SLLI: return "SLLI";
        case InstructionKind::SRLI: return "SRLI";
        case InstructionKind::SRAI: return "SRAI";
        case InstructionKind::ADD: return "ADD";
        case InstructionKind::SUB: return "SUB";
        case InstructionKind::SLL: return "SLL";
        case InstructionKind::SLT: return "SLT";
        case InstructionKind::SLTU: return "SLTU";
        case InstructionKind::XOR: return "XOR";
        case InstructionKind::SRL: return "SRL";
        case InstructionKind::SRA: return "SRA";
        case InstructionKind::OR: return "OR";
        case InstructionKind::AND: return "AND";
        case InstructionKind::FENCE: return "FENCE";
        case InstructionKind::FENCE_I: return "FENCE_I";
        case InstructionKind::ECALL: return "ECALL";
        case InstructionKind::EBREAK: return "EBREAK";
        case InstructionKind::MRET: return "MRET";
        case InstructionKind::SRET: return "SRET";
        case InstructionKind::WFI: return "WFI";
        case InstructionKind::SFENCE_VMA: return "SFENCE_VMA";
        case InstructionKind::ADDIW: return "ADDIW";
        case InstructionKind::SLLIW: return "SLLIW";
        case InstructionKind::SRLIW: return "SRLIW";
        case InstructionKind::SRAIW: return "SRAIW";
        case InstructionKind::ADDW: return "ADDW";
        case InstructionKind::SUBW: return "SUBW";
        case InstructionKind::SLLW: return "SLLW";
        case InstructionKind::SRLW: return "SRLW";
        case InstructionKind::SRAW: return "SRAW";
        case InstructionKind::MUL: return "MUL";
        case InstructionKind::MULH: return "MULH";
        case InstructionKind::MULHSU: return "MULHSU";
        case InstructionKind::MULHU: return "MULHU";
        case InstructionKind::DIV: return "DIV";
        case InstructionKind::DIVU: return "DIVU";
        case InstructionKind::REM: return "REM";
        case InstructionKind::REMU: return "REMU";
        case InstructionKind::MULW: return "MULW";
        case InstructionKind::DIVW: return "DIVW";
        case InstructionKind::DIVUW: return "DIVUW";
        case InstructionKind::REMW: return "REMW";
        case InstructionKind::REMUW: return "REMUW";
        case InstructionKind::CSRRW: return "CSRRW";
        case InstructionKind::CSRRS: return "CSRRS";
        case InstructionKind::CSRRC: return "CSRRC";
        case InstructionKind::CSRRWI: return "CSRRWI";
        case InstructionKind::CSRRSI: return "CSRRSI";
        case InstructionKind::CSRRCI: return "CSRRCI";
        case InstructionKind::INVALID: return "INVALID";
        default: return "UNKNOWN";
    }
}

std::string to_asm_string(const DecodedInstruction& instr) {
    if (!instr.is_valid()) {
        return "INVALID";
    }

    std::string name = to_string(instr.kind);
    char buf[128];

    switch (instr.format) {
        case InstructionFormat::R:
            if (instr.rs2 != 0) {
                snprintf(buf, sizeof(buf), "%s x%d, x%d, x%d", name.c_str(), instr.rd, instr.rs1, instr.rs2);
            } else {
                snprintf(buf, sizeof(buf), "%s x%d, x%d, x%d", name.c_str(), instr.rd, instr.rs1, instr.rs2);
            }
            break;
        case InstructionFormat::I:
            if (instr.is_load() || instr.is_jump() || instr.kind == InstructionKind::JALR) {
                snprintf(buf, sizeof(buf), "%s x%d, %lld(x%d)", name.c_str(), instr.rd, (long long)instr.imm, instr.rs1);
            } else if (instr.is_csr()) {
                snprintf(buf, sizeof(buf), "%s x%d, %d, x%d", name.c_str(), instr.rd, (int)instr.imm, instr.rs1);
            } else {
                snprintf(buf, sizeof(buf), "%s x%d, x%d, %lld", name.c_str(), instr.rd, instr.rs1, (long long)instr.imm);
            }
            break;
        case InstructionFormat::S:
            snprintf(buf, sizeof(buf), "%s x%d, %lld(x%d)", name.c_str(), instr.rs2, (long long)instr.imm, instr.rs1);
            break;
        case InstructionFormat::B:
            snprintf(buf, sizeof(buf), "%s x%d, x%d, %lld", name.c_str(), instr.rs1, instr.rs2, (long long)instr.imm);
            break;
        case InstructionFormat::U:
            snprintf(buf, sizeof(buf), "%s x%d, 0x%llx", name.c_str(), instr.rd, (unsigned long long)((instr.imm >> 12) & 0xFFFFF));
            break;
        case InstructionFormat::J:
            snprintf(buf, sizeof(buf), "%s x%d, %lld", name.c_str(), instr.rd, (long long)instr.imm);
            break;
        default:
            snprintf(buf, sizeof(buf), "%s", name.c_str());
            break;
    }

    return std::string(buf);
}

}  // namespace riscv


