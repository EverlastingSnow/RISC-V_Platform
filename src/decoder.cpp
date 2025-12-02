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

constexpr s32 imm_j(u32 raw) {
    const u32 bits19_12 = (raw >> 12) & 0xFF;
    const u32 bit11 = (raw >> 20) & 0x1;
    const u32 bits10_1 = (raw >> 21) & 0x3FF;
    const u32 bit20 = (raw >> 31) & 0x1;
    const u32 value = (bit20 << 20) | (bits19_12 << 12) | (bit11 << 11) | (bits10_1 << 1);
    return sign_extend<s32>(static_cast<s32>(value), 21);
}

bool is_shift_imm_valid(u32 funct7) {
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
        case InstructionKind::LBU:
        case InstructionKind::LHU:
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
        case InstructionKind::LBU:
        case InstructionKind::LHU:
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
    return kind == InstructionKind::ECALL || kind == InstructionKind::EBREAK;
}

DecodedInstruction decode(u32 raw, u32 pc) {
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
                    if (inst.funct7 == 0b0000000) {
                        inst.kind = InstructionKind::SLLI;
                    }
                    break;
                case 0b101:
                    if (inst.funct7 == 0b0000000) {
                        inst.kind = InstructionKind::SRLI;
                    } else if (inst.funct7 == 0b0100000) {
                        inst.kind = InstructionKind::SRAI;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 0b0110011:  // Register ALU
            inst.format = InstructionFormat::R;
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
            if (is_m_extension(inst.funct7)) {
                inst.kind = InstructionKind::INVALID;  // M扩展未实现
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
            inst.imm = imm_i(raw);
            if (inst.funct3 == 0) {
                if ((raw >> 20) == 0) {
                    inst.kind = InstructionKind::ECALL;
                } else if ((raw >> 20) == 1) {
                    inst.kind = InstructionKind::EBREAK;
                }
            } else {
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
        case InstructionKind::LBU: return "LBU";
        case InstructionKind::LHU: return "LHU";
        case InstructionKind::SB: return "SB";
        case InstructionKind::SH: return "SH";
        case InstructionKind::SW: return "SW";
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
        case InstructionKind::FENCE_I: return "FENCE.I";
        case InstructionKind::ECALL: return "ECALL";
        case InstructionKind::EBREAK: return "EBREAK";
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

}  // namespace riscv


