#pragma once

#include <cstdint>
#include <string>

#include "riscv/types.h"

namespace riscv {

enum class InstructionFormat {
    R,
    I,
    S,
    B,
    U,
    J,
    INVALID,
};

enum class InstructionKind {
    LUI,
    AUIPC,
    JAL,
    JALR,
    BEQ,
    BNE,
    BLT,
    BGE,
    BLTU,
    BGEU,
    LB,
    LH,
    LW,
    LBU,
    LHU,
    SB,
    SH,
    SW,
    ADDI,
    SLTI,
    SLTIU,
    XORI,
    ORI,
    ANDI,
    SLLI,
    SRLI,
    SRAI,
    ADD,
    SUB,
    SLL,
    SLT,
    SLTU,
    XOR,
    SRL,
    SRA,
    OR,
    AND,
    FENCE,
    FENCE_I,
    ECALL,
    EBREAK,
    CSRRW,
    CSRRS,
    CSRRC,
    CSRRWI,
    CSRRSI,
    CSRRCI,
    INVALID,
};

struct DecodedInstruction {
    InstructionKind kind{InstructionKind::INVALID};
    InstructionFormat format{InstructionFormat::INVALID};
    u32 raw{0};
    u32 pc{0};
    u32 opcode{0};
    u32 rd{0};
    u32 rs1{0};
    u32 rs2{0};
    u32 funct3{0};
    u32 funct7{0};
    s32 imm{0};

    [[nodiscard]] bool writes_rd() const;
    [[nodiscard]] bool is_branch() const;
    [[nodiscard]] bool is_jump() const;
    [[nodiscard]] bool is_load() const;
    [[nodiscard]] bool is_store() const;
    [[nodiscard]] bool is_csr() const;
    [[nodiscard]] bool is_system() const;
    [[nodiscard]] bool is_valid() const { return kind != InstructionKind::INVALID; }
};

DecodedInstruction decode(u32 raw, u32 pc);
std::string to_string(InstructionKind kind);

}  // namespace riscv


