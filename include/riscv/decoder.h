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
    LD,
    LBU,
    LHU,
    LWU,
    SB,
    SH,
    SW,
    SD,
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
    MRET,  // 特权返回（riscv-tests 启动代码需要）
    SRET,  // 超级监视器返回
    WFI,   // 等待中断
    SFENCE_VMA,  // 虚拟内存屏障

    // RV64I "W" 指令（OP-IMM-32 / OP-32）：结果截断为 32 位再符号扩展到 64 位
    ADDIW,
    SLLIW,
    SRLIW,
    SRAIW,
    ADDW,
    SUBW,
    SLLW,
    SRLW,
    SRAW,

    // RV64M 乘除法扩展
    MUL,
    MULH,
    MULHSU,
    MULHU,
    DIV,
    DIVU,
    REM,
    REMU,
    MULW,
    DIVW,
    DIVUW,
    REMW,
    REMUW,

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
    u64 pc{0};
    u32 opcode{0};
    u32 rd{0};
    u32 rs1{0};
    u32 rs2{0};
    u32 funct3{0};
    u32 funct7{0};
    s64 imm{0};

    [[nodiscard]] bool writes_rd() const;
    [[nodiscard]] bool is_branch() const;
    [[nodiscard]] bool is_jump() const;
    [[nodiscard]] bool is_load() const;
    [[nodiscard]] bool is_store() const;
    [[nodiscard]] bool is_csr() const;
    [[nodiscard]] bool is_system() const;
    [[nodiscard]] bool is_valid() const { return kind != InstructionKind::INVALID; }
};

DecodedInstruction decode(u32 raw, u64 pc);
std::string to_string(InstructionKind kind);

}  // namespace riscv


