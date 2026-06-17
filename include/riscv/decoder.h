#pragma once

#include <cstdint>
#include <string>

#include "riscv/types.h"

namespace riscv {

/**
 * @brief RISC-V 指令编码格式分类。
 *
 * 决定立即数字段的拆分方式与寄存器字段的使用：
 *   R：寄存器-寄存器操作（funct3/funct7 区分具体运算）
 *   I：含 12 位符号扩展立即数（ADDI/LW/JALR 等）
 *   S：Store 类型（imm 拆 7+5）
 *   B：Branch 类型（imm 拆 12+1+10+1+5）
 *   U：20 位高位立即数（LUI/AUIPC）
 *   J：JAL（20 位立即数）
 */
enum class InstructionFormat {
    R,
    I,
    S,
    B,
    U,
    J,
    INVALID,
};

/**
 * @brief 解码后的 RISC-V 指令类型。
 *
 * 覆盖 RV64I + RV64M + 系统指令 + CSR 指令。
 * 命名遵循 RISC-V 规范指令助记符；
 *   - W 后缀：RV64I "W" 变种（结果截断为 32 位再符号扩展到 64 位）
 *   - H/HSU/HU 后缀：MUL 不同符号扩展
 *   - W 后缀：RV64M 乘除法的 32 位版本
 *   - CSRxx：CSR 读写指令族
 */
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
    NOP,
    INVALID,
};

/**
 * @brief 解码后的指令结构。
 *
 * 字段：
 *   kind   指令语义类型
 *   format 编码格式（R/I/S/B/U/J/INVALID）
 *   raw    原始 32 位指令字
 *   pc     该指令所在 PC（用于分支/异常时回填）
 *   opcode 原始 opcode 字段（位 6:0）
 *   rd/rs1/rs2 原始寄存器编号
 *   funct3/funct7 原始 funct3/funct7
 *   imm    已按 format 完成拆分与符号扩展的 64 位立即数
 *
 * 通过 is_valid()/writes_rd()/is_branch()/is_jump()/is_load()/
 *   is_store()/is_csr()/is_system() 等谓词可方便地查询指令类别。
 */
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

    /** @brief 是否会向 rd 寄存器写入结果。 */
    [[nodiscard]] bool writes_rd() const;
    /** @brief 是否为条件分支指令（BEQ/BNE/BLT/BGE/BLTU/BGEU）。 */
    [[nodiscard]] bool is_branch() const;
    /** @brief 是否为无条件跳转（JAL/JALR）。 */
    [[nodiscard]] bool is_jump() const;
    /** @brief 是否为 Load 指令。 */
    [[nodiscard]] bool is_load() const;
    /** @brief 是否为 Store 指令。 */
    [[nodiscard]] bool is_store() const;
    /** @brief 是否为 CSR 指令。 */
    [[nodiscard]] bool is_csr() const;
    /** @brief 是否为系统指令（ECALL/EBREAK/MRET/SRET/WFI/SFENCE.VMA）。 */
    [[nodiscard]] bool is_system() const;
    /** @brief 解码是否成功（kind != INVALID）。 */
    [[nodiscard]] bool is_valid() const { return kind != InstructionKind::INVALID; }
};

/**
 * @brief 把 32 位指令字解码为 DecodedInstruction。
 *
 * @param raw 32 位指令字
 * @param pc  该指令所在 PC（用于回填 DecodedInstruction::pc）
 * @return 解码结果（无法识别的指令返回 kind = INVALID）
 */
DecodedInstruction decode(u32 raw, u64 pc);

/**
 * @brief 把指令类型枚举翻译为助记符字符串。
 *
 * @param kind 指令类型
 * @return 助记符字符串（如 "ADDI"、"BEQ"）；非法值返回 "INVALID"
 */
std::string to_string(InstructionKind kind);

/**
 * @brief 把一条解码后的指令格式化为 `mnemonic rd, rs1, rs2, imm` 形式的字符串。
 *
 * 用于 difftest、调试输出与前端展示。
 *
 * @param instr 解码结果
 * @return 汇编字符串
 */
std::string to_asm_string(const DecodedInstruction& instr);

}  // namespace riscv


