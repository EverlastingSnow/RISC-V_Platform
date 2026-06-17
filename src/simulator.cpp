#include "riscv/simulator.h"

#include <iostream>
#include <optional>

#include "riscv/csr.h"
#include "riscv/decoder.h"
#include "riscv/register_file.h"
#include "riscv/types.h"

#if defined(__SIZEOF_INT128__)
#define RISCV_HAVE_INT128 1
#else
#define RISCV_HAVE_INT128 0
#endif

namespace riscv {
namespace {

/**
 * @brief 判断指定指令是否使用 rs1 源寄存器。
 *
 * LUI、AUIPC、JAL、FENCE 系列、ECALL/EBREAK/MRET 以及 CSR 立即数形式不需要 rs1。
 *
 * @param kind RISC-V 指令类型枚举
 * @return true 表示该指令会读取 rs1；false 表示不使用 rs1
 */
bool uses_rs1(InstructionKind kind) {
    switch (kind) {
        case InstructionKind::LUI:
        case InstructionKind::AUIPC:
        case InstructionKind::JAL:
        case InstructionKind::FENCE:
        case InstructionKind::FENCE_I:
        case InstructionKind::ECALL:
        case InstructionKind::EBREAK:
        case InstructionKind::MRET:
        case InstructionKind::CSRRWI:
        case InstructionKind::CSRRSI:
        case InstructionKind::CSRRCI:
            return false;
        default:
            break;
    }
    return true;
}

/**
 * @brief 判断指定指令是否使用 rs2 源寄存器。
 *
 * R 型算术/逻辑/移位指令、Store 指令、分支指令、M 扩展乘除法指令均使用 rs2。
 *
 * @param kind RISC-V 指令类型枚举
 * @return true 表示该指令会读取 rs2；false 表示不使用 rs2
 */
bool uses_rs2(InstructionKind kind) {
    switch (kind) {
        case InstructionKind::SB:
        case InstructionKind::SH:
        case InstructionKind::SW:
        case InstructionKind::SD:
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

/**
 * @brief 判断指定指令是否使用立即数操作数。
 *
 * 包括 LUI/AUIPC/JAL/JALR、Load/Store、I 型算术/逻辑/移位指令、分支指令、FENCE 系列。
 *
 * @param kind RISC-V 指令类型枚举
 * @return true 表示该指令含立即数字段；false 表示无立即数
 */
bool uses_imm(InstructionKind kind) {
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
        case InstructionKind::SB:
        case InstructionKind::SH:
        case InstructionKind::SW:
        case InstructionKind::SD:
        case InstructionKind::ADDI:
        case InstructionKind::SLTI:
        case InstructionKind::SLTIU:
        case InstructionKind::XORI:
        case InstructionKind::ORI:
        case InstructionKind::ANDI:
        case InstructionKind::SLLI:
        case InstructionKind::SRLI:
        case InstructionKind::SRAI:
        case InstructionKind::ADDIW:
        case InstructionKind::SLLIW:
        case InstructionKind::SRLIW:
        case InstructionKind::SRAIW:
        case InstructionKind::BEQ:
        case InstructionKind::BNE:
        case InstructionKind::BLT:
        case InstructionKind::BGE:
        case InstructionKind::BLTU:
        case InstructionKind::BGEU:
        case InstructionKind::FENCE:
        case InstructionKind::FENCE_I:
            return true;
        default:
            return false;
    }
}

/**
 * @brief 根据指令类型与两个操作数判断条件分支是否成立。
 *
 * 严格按照 RISC-V 规范：BLT/BGE 使用有符号比较，BLTU/BGEU 使用无符号比较。
 *
 * @param instr 已解码的分支指令
 * @param lhs rs1 寄存器值
 * @param rhs rs2 寄存器值
 * @return true 表示分支条件成立，需要跳转；false 表示不跳转
 */
bool is_branch_taken(const DecodedInstruction& instr, u64 lhs, u64 rhs) {
    switch (instr.kind) {
        case InstructionKind::BEQ: return lhs == rhs;
        case InstructionKind::BNE: return lhs != rhs;
        case InstructionKind::BLT: return static_cast<s64>(lhs) < static_cast<s64>(rhs);
        case InstructionKind::BGE: return static_cast<s64>(lhs) >= static_cast<s64>(rhs);
        case InstructionKind::BLTU: return lhs < rhs;
        case InstructionKind::BGEU: return lhs >= rhs;
        default: return false;
    }
}

}  // namespace

/**
 * @brief 构造 RISC-V 模拟器，分配默认内存并复位到初始状态。
 *
 * 内存大小使用 DEFAULT_MEMORY_SIZE（256 MiB），基地址使用 RESET_VECTOR（0x80000000）。
 */
RISCVSimulator::RISCVSimulator() : memory_(static_cast<std::size_t>(DEFAULT_MEMORY_SIZE), RESET_VECTOR) {
    reset();
}

/**
 * @brief 加载程序二进制到内存并复位模拟器。
 *
 * 会先清空内存，再写入新程序，最后调用 reset() 将所有寄存器/CSR/流水线状态归零。
 *
 * @param binary 程序二进制字节流
 * @param offset 程序在虚拟地址空间中的偏移，相对 memory 基地址
 */
void RISCVSimulator::load_program(const std::vector<u8>& binary, u64 offset) {
    memory_.reset();
    memory_.load_program(binary, offset);
    reset();
}

/**
 * @brief 复位模拟器到初始状态。
 *
 * 清零通用寄存器、CSR、流水线寄存器、异常/中断标志，并将 PC 复位到内存基地址。
 */
void RISCVSimulator::reset() {
    regs_.reset();
    csr_.reset();
    if_id_ = {};
    id_ex_ = {};
    ex_mem_ = {};
    mem_wb_ = {};
    next_if_id_ = {};
    next_id_ex_ = {};
    next_ex_mem_ = {};
    next_mem_wb_ = {};
    pipeline_state_ = {};
    pc_ = memory_.base();
    next_pc_ = pc_;
    cycle_ = 0;
    halted_ = false;
    halt_reason_ = HaltReason::None;
    halt_pc_ = 0;
    halt_inst_ = 0;
    stall_fetch_ = false;
    redirect_ = false;
    redirect_target_ = 0;
    flush_decode_ = false;
    flush_execute_ = false;
    pending_ebreak_ = false;
    pending_ecall_exit_ = false;
    pending_ecall_halt_pc_ = 0;
    pending_ecall_halt_inst_ = 0;
    last_trap_cause_ = TrapCause::None;
}

/**
 * @brief 顺序执行指定周期数。
 *
 * 每周期调用一次 step()，遇到 halted_ 状态会提前停止。
 *
 * @param cycles 期望执行的最大周期数
 */
void RISCVSimulator::run(u32 cycles) {
    for (u32 i = 0; i < cycles && !halted_; ++i) {
        step();
    }
}

/**
 * @brief 单步执行一个时钟周期，依次推进 WB → MEM → EX → ID → IF 五个流水线阶段。
 *
 * 若 halted_ 或 waiting_for_input_ 为真则直接返回。每个周期结束时更新流水线寄存器与状态。
 */
void RISCVSimulator::step() {
    if (halted_) {
        return;
    }
    
    // If waiting for user input, pause the pipeline
    if (waiting_for_input_) {
        return;
    }

    stall_fetch_ = false;
    flush_decode_ = false;
    flush_execute_ = false;
    next_pc_ = pc_;


    stage_wb();
    stage_mem();
    stage_ex();
    stage_id();
    stage_if();

    update_pipeline_registers();
    update_pipeline_state();
    ++cycle_;
}

/**
 * @brief 流水线 IF（取指）阶段。
 *
 * 处理流程：
 *   1. 已停机/重定向：本阶段不取指
 *   2. 检查待处理中断，按 mstatus.MIE/特权级和 mtvec 模式计算入口地址
 *   3. 检查 PC 4 字节对齐，不对齐触发 Instruction address misaligned 异常
 *   4. 正常路径：从内存取 32 位指令写入 IF/ID 寄存器，next_pc += 4
 */
void RISCVSimulator::stage_if() {
    if (halted_) {
        next_if_id_ = {};
        return;
    }

    if (redirect_) {
        next_if_id_ = {};
        return;
    }

    if (!stall_fetch_ && csr_.has_pending_interrupt()) {
        u64 mstatus_val = csr_.read(CSR_MSTATUS);
        u64 cause = csr_.get_interrupt_cause();
        csr_.write(CSR_MEPC, pc_);
        csr_.write(CSR_MCAUSE, cause | (1ULL << 63));
        csr_.write(CSR_MTVAL, 0);

        constexpr u64 MSTATUS_MIE = 1ULL << 3;
        constexpr u64 MSTATUS_MPIE = 1ULL << 7;
        // MIE -> MPIE；MIE 清零（使用 if/else 避免位运算优先级歧义）
        if (mstatus_val & MSTATUS_MIE) {
            mstatus_val |= MSTATUS_MPIE;
        } else {
            mstatus_val &= ~MSTATUS_MPIE;
        }
        mstatus_val &= ~MSTATUS_MIE;
        csr_.write(CSR_MSTATUS, mstatus_val);

        u64 mtvec = csr_.read(CSR_MTVEC);
        u64 mtvec_mode = mtvec & 0x3;
        u64 mtvec_base = mtvec & ~0x3ULL;
        u64 target;

        if (mtvec_mode == 1) {
            target = mtvec_base + 4 * cause;
        } else {
            target = mtvec_base;
        }

        last_trap_cause_ = TrapCause::Interrupt;
        redirect_ = true;
        redirect_target_ = target;
        flush_decode_ = true;
        flush_execute_ = true;

        if_id_ = {};
        id_ex_ = {};
        ex_mem_ = {};
        mem_wb_ = {};
        next_if_id_ = {};
        next_id_ex_ = {};
        next_ex_mem_ = {};
        next_mem_wb_ = {};
        return;
    }

    if (stall_fetch_) {
        if (!halted_ && csr_.has_pending_interrupt()) {
            u64 mstatus_val = csr_.read(CSR_MSTATUS);
            u64 cause = csr_.get_interrupt_cause();
            csr_.write(CSR_MEPC, pc_);
            csr_.write(CSR_MCAUSE, cause | (1ULL << 63));
            csr_.write(CSR_MTVAL, 0);

            constexpr u64 MSTATUS_MIE = 1ULL << 3;
            constexpr u64 MSTATUS_MPIE = 1ULL << 7;
            if (mstatus_val & MSTATUS_MIE) {
                mstatus_val |= MSTATUS_MPIE;
            } else {
                mstatus_val &= ~MSTATUS_MPIE;
            }
            mstatus_val &= ~MSTATUS_MIE;
            csr_.write(CSR_MSTATUS, mstatus_val);

            u64 mtvec = csr_.read(CSR_MTVEC);
            u64 mtvec_mode = mtvec & 0x3;
            u64 mtvec_base = mtvec & ~0x3ULL;
            u64 target;

            if (mtvec_mode == 1) {
                target = mtvec_base + 4 * cause;
            } else {
                target = mtvec_base;
            }

            last_trap_cause_ = TrapCause::Interrupt;
            redirect_ = true;
            redirect_target_ = target;
            flush_decode_ = true;
            flush_execute_ = true;

            if_id_ = {};
            id_ex_ = {};
            ex_mem_ = {};
            mem_wb_ = {};
            next_if_id_ = {};
            next_id_ex_ = {};
            next_ex_mem_ = {};
            next_mem_wb_ = {};
            return;
        }
        next_if_id_ = if_id_;
        next_pc_ = pc_;
        return;
    }

    if (!memory_.contains(pc_)) {
        // PC 越界视为程序错误，设置停止原因并优雅停止
        halted_ = true;
        halt_reason_ = HaltReason::InvalidInstruction;
        halt_pc_ = pc_;
        halt_inst_ = 0;
        next_if_id_ = {};
        return;
    }

    // PC 必须 4 字节对齐；不满足时触发取指 misalign trap (mcause=0)
    // 对应 rv64mi-p-ma_fetch 测试。注意：trick 方式跳转（jalr 目标地址未对齐）
    // 是合法实现可检测的异常，不能直接 halt。
    if ((pc_ & 0x3ULL) != 0) {
        csr_.write(CSR_MEPC, pc_);
        csr_.write(CSR_MCAUSE, 0);  // Exception code 0 = Instruction address misaligned
        csr_.write(CSR_MTVAL, pc_);
        {
            constexpr u64 MSTATUS_MIE = 1ULL << 3;
            constexpr u64 MSTATUS_MPIE = 1ULL << 7;
            constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
            const u64 priv = static_cast<u64>(csr_.privilege_mode());
            u64 mstatus = csr_.read(CSR_MSTATUS);
            if (mstatus & MSTATUS_MIE) {
                mstatus |= MSTATUS_MPIE;
            } else {
                mstatus &= ~MSTATUS_MPIE;
            }
            mstatus &= ~MSTATUS_MIE;
            mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (priv << 11);
            csr_.write(CSR_MSTATUS, mstatus);
        }
        {
            u64 mtvec = csr_.read(CSR_MTVEC);
            redirect_ = true;
            redirect_target_ = mtvec & ~0x3ULL;
            flush_decode_ = true;
            flush_execute_ = true;
        }
        csr_.set_privilege_mode(PrivilegeMode::Machine);
        last_trap_cause_ = TrapCause::Exception;
        if_id_ = {};
        id_ex_ = {};
        ex_mem_ = {};
        mem_wb_ = {};
        next_if_id_ = {};
        next_id_ex_ = {};
        next_ex_mem_ = {};
        next_mem_wb_ = {};
        return;
    }

    next_if_id_ = {};
    next_if_id_.valid = true;
    next_if_id_.pc = pc_;
    next_if_id_.inst = memory_.read32(pc_);
    next_pc_ = pc_ + 4;
}


/**
 * @brief 流水线 ID（译码）阶段。
 *
 * 1. 调用 decode() 把 32 位指令字解码为 DecodedInstruction
 * 2. 检测 load-use 与 store-load 数据冒险，必要时置 stall_fetch_ 阻塞 IF
 * 3. 根据指令类型读取 rs1/rs2 寄存器值
 * 4. 透传用户控制信号（用于交互式教学）
 */
void RISCVSimulator::stage_id() {
    if (redirect_) {
        next_id_ex_ = {};
        return;
    }
    
    auto saved_user_signals = next_id_ex_.user_signals;
    next_id_ex_ = {};
    if (!if_id_.valid) {
        return;
    }

    auto instr = decode(if_id_.inst, if_id_.pc);

    const bool load_use_hazard = id_ex_.valid && id_ex_.instr.is_load() && id_ex_.instr.rd != 0 &&
                        ((uses_rs1(instr.kind) && instr.rs1 == id_ex_.instr.rd) ||
                         (uses_rs2(instr.kind) && instr.rs2 == id_ex_.instr.rd));

    if (load_use_hazard) {
        stall_fetch_ = true;
        next_id_ex_.valid = false;
        return;
    }

    const bool store_load_hazard_detected = [&]() {
        if (!id_ex_.valid || !id_ex_.instr.is_store() || !instr.is_load()) {
            return false;
        }
        if (id_ex_.instr.rs1 != instr.rs1) {
            return false;
        }
        u64 store_addr = id_ex_.rs1_value + static_cast<u64>(static_cast<s64>(id_ex_.instr.imm));
        u64 load_addr = regs_.read(instr.rs1) + static_cast<u64>(static_cast<s64>(instr.imm));
        return store_addr == load_addr;
    }();

    if (store_load_hazard_detected) {
        stall_fetch_ = true;
        next_id_ex_.valid = false;
        return;
    }

    next_id_ex_.valid = true;
    next_id_ex_.instr = instr;
    next_id_ex_.rs1_value = uses_rs1(instr.kind) ? regs_.read(instr.rs1) : 0;
    next_id_ex_.rs2_value = uses_rs2(instr.kind) ? regs_.read(instr.rs2) : 0;

    if (waiting_for_input_ && if_id_.pc == waiting_pc_) {
        next_id_ex_.user_signals = saved_user_signals;
    } else if (!waiting_for_input_ && !waiting_handled_ && if_id_.pc == waiting_pc_) {
        next_id_ex_.user_signals = saved_user_signals;
    } else if (!waiting_handled_) {
        next_id_ex_.user_signals.clear();
        waiting_handled_ = false;
    }
}

/**
 * @brief 流水线 EX（执行）阶段。
 *
 * 1. 通过 forward 逻辑优先使用 EX/MEM、MEM/WB 阶段最新的寄存器值
 * 2. 根据 InstructionKind 分发到具体实现：算术/逻辑/移位/比较/乘除/分支跳转/CSR
 * 3. 处理异常路径：misaligned、illegal instruction、ECALL/EBREAK、MRET/SRET 等
 * 4. 写回 ALU 结果、分支目标、CSR 写信号到 EX/MEM 寄存器
 */
void RISCVSimulator::stage_ex() {
    next_ex_mem_ = {};
    if (!id_ex_.valid) {
        return;
    }

    if (redirect_) {
        return;
    }

    // 重置本周期 EX 阶段异常标志
    exception_taken_ = false;

    const auto instr = id_ex_.instr;
    u64 rs1_val = id_ex_.rs1_value;
    u64 rs2_val = id_ex_.rs2_value;

    auto forward_from_ex_mem = [&](u32 reg) -> std::optional<u64> {
        if (!ex_mem_.valid || !ex_mem_.instr.writes_rd() || ex_mem_.instr.is_load()) {
            return std::nullopt;
        }
        if (reg != 0 && ex_mem_.instr.rd == reg) {
            return ex_mem_.alu_result;
        }
        return std::nullopt;
    };

    auto forward_from_mem_wb = [&](u32 reg) -> std::optional<u64> {
        if (!mem_wb_.valid || !mem_wb_.instr.writes_rd()) {
            return std::nullopt;
        }
        if (reg != 0 && mem_wb_.instr.rd == reg) {
            return mem_wb_.wb_value;
        }
        return std::nullopt;
    };

    if (uses_rs1(instr.kind)) {
        if (auto val = forward_from_ex_mem(instr.rs1)) {
            rs1_val = *val;
        } else if (auto val2 = forward_from_mem_wb(instr.rs1)) {
            rs1_val = *val2;
        }
    }

    if (uses_rs2(instr.kind)) {
        if (auto val = forward_from_ex_mem(instr.rs2)) {
            rs2_val = *val;
        } else if (auto val2 = forward_from_mem_wb(instr.rs2)) {
            rs2_val = *val2;
        }
    }

    u64 alu_result = 0;
    bool branch_taken = false;
    u64 branch_target = 0;
    u64 alu_src1 = rs1_val;
    u64 alu_src2 = rs2_val;

    if (id_ex_.valid) {
        bool uses_imm_for_alu = false;
        switch (instr.kind) {
            case InstructionKind::ADDI:
            case InstructionKind::ADDIW:
            case InstructionKind::ANDI:
            case InstructionKind::ORI:
            case InstructionKind::XORI:
            case InstructionKind::SLLI:
            case InstructionKind::SRLI:
            case InstructionKind::SRAI:
            case InstructionKind::SLTI:
            case InstructionKind::SLTIU:
            case InstructionKind::LB:
            case InstructionKind::LH:
            case InstructionKind::LW:
            case InstructionKind::LD:
            case InstructionKind::LBU:
            case InstructionKind::LHU:
            case InstructionKind::LWU:
            case InstructionKind::SB:
            case InstructionKind::SH:
            case InstructionKind::SW:
            case InstructionKind::SD:
            case InstructionKind::JALR:
                uses_imm_for_alu = true;
                break;
            default:
                uses_imm_for_alu = false;
                break;
        }
        if (id_ex_.user_signals.alu_src.has_value()) {
            if (id_ex_.user_signals.alu_src.value()) {
                alu_src2 = static_cast<u64>(id_ex_.instr.imm);
            }
            // 如果 alu_src = 0，则什么都不做，alu_src2 保持为 rs2_val (寄存器值)
        } else if (uses_imm_for_alu) {
            alu_src2 = static_cast<u64>(id_ex_.instr.imm);
        }
    }

    switch (instr.kind) {
        case InstructionKind::LUI:
            alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(instr.imm & 0xFFFFFFFFu)));
            break;
        case InstructionKind::AUIPC:
            alu_result = instr.pc + static_cast<u64>(instr.imm);
            break;
        case InstructionKind::JAL: {
            // JAL 目标地址必须 4 字节对齐；不满足时触发取指 misalign trap (mcause=0)
            // 对应 rv64mi-p-ma_fetch 测试中"jal 到 2 字节偏移地址"等场景
            const u64 jal_target = instr.pc + static_cast<u64>(instr.imm);
            if ((jal_target & 0x3ULL) != 0) {
                alu_result = 0;
                csr_.write(CSR_MEPC, instr.pc);
                csr_.write(CSR_MCAUSE, 0);  // Exception code 0 = Instruction address misaligned
                csr_.write(CSR_MTVAL, jal_target);
                {
                    constexpr u64 MSTATUS_MIE = 1ULL << 3;
                    constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                    constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
                    const u64 priv = static_cast<u64>(csr_.privilege_mode());
                    u64 mstatus = csr_.read(CSR_MSTATUS);
                    if (mstatus & MSTATUS_MIE) {
                        mstatus |= MSTATUS_MPIE;
                    } else {
                        mstatus &= ~MSTATUS_MPIE;
                    }
                    mstatus &= ~MSTATUS_MIE;
                    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (priv << 11);
                    csr_.write(CSR_MSTATUS, mstatus);
                }
                {
                    u64 mtvec = csr_.read(CSR_MTVEC);
                    branch_taken = true;
                    branch_target = mtvec & ~0x3ULL;
                    flush_decode_ = true;
                    flush_execute_ = true;
                }
                csr_.set_privilege_mode(PrivilegeMode::Machine);
                pending_ebreak_ = false;
                last_trap_cause_ = TrapCause::Exception;
                exception_taken_ = true;  // 抑制 trap 指令 commit
                break;
            }
            alu_result = instr.pc + 4;
            branch_taken = true;
            branch_target = jal_target;
            break;
        }
        case InstructionKind::JALR: {
            alu_result = instr.pc + 4;
            // JALR 目标地址必须 4 字节对齐；不满足时触发取指 misalign trap (mcause=0)
            // 对应 rv64mi-p-ma_fetch 测试中"jalr 到 2 字节偏移地址"等场景
            const u64 jalr_target = (rs1_val + static_cast<u64>(instr.imm)) & ~1ULL;
            if ((jalr_target & 0x3ULL) != 0) {
                // 触发 Instruction address misaligned 异常
                csr_.write(CSR_MEPC, instr.pc);
                csr_.write(CSR_MCAUSE, 0);  // Exception code 0 = Instruction address misaligned
                csr_.write(CSR_MTVAL, jalr_target);
                {
                    constexpr u64 MSTATUS_MIE = 1ULL << 3;
                    constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                    constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
                    const u64 priv = static_cast<u64>(csr_.privilege_mode());
                    u64 mstatus = csr_.read(CSR_MSTATUS);
                    if (mstatus & MSTATUS_MIE) {
                        mstatus |= MSTATUS_MPIE;
                    } else {
                        mstatus &= ~MSTATUS_MPIE;
                    }
                    mstatus &= ~MSTATUS_MIE;
                    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (priv << 11);
                    csr_.write(CSR_MSTATUS, mstatus);
                }
                {
                    u64 mtvec = csr_.read(CSR_MTVEC);
                    branch_taken = true;
                    branch_target = mtvec & ~0x3ULL;
                    flush_decode_ = true;
                    flush_execute_ = true;
                }
                csr_.set_privilege_mode(PrivilegeMode::Machine);
                pending_ebreak_ = false;
                last_trap_cause_ = TrapCause::Exception;
                exception_taken_ = true;  // 抑制 trap 指令 commit
            } else {
                branch_taken = true;
                branch_target = jalr_target;
            }
            break;
        }
        case InstructionKind::BEQ:
        case InstructionKind::BNE:
        case InstructionKind::BLT:
        case InstructionKind::BGE:
        case InstructionKind::BLTU:
        case InstructionKind::BGEU: {
            branch_taken = is_branch_taken(instr, rs1_val, rs2_val);
            const u64 br_target = instr.pc + static_cast<u64>(instr.imm);
            if (id_ex_.user_signals.branch.has_value()) {
                branch_taken = id_ex_.user_signals.branch.value();
            }
            // 分支目标地址必须 4 字节对齐；不满足时触发取指 misalign trap (mcause=0)
            // 对应 rv64mi-p-ma_fetch 测试中"条件分支到 2 字节偏移地址"等场景
            if (branch_taken && (br_target & 0x3ULL) != 0) {
                alu_result = 0;
                csr_.write(CSR_MEPC, instr.pc);
                csr_.write(CSR_MCAUSE, 0);  // Exception code 0 = Instruction address misaligned
                csr_.write(CSR_MTVAL, br_target);
                {
                    constexpr u64 MSTATUS_MIE = 1ULL << 3;
                    constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                    constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
                    const u64 priv = static_cast<u64>(csr_.privilege_mode());
                    u64 mstatus = csr_.read(CSR_MSTATUS);
                    if (mstatus & MSTATUS_MIE) {
                        mstatus |= MSTATUS_MPIE;
                    } else {
                        mstatus &= ~MSTATUS_MPIE;
                    }
                    mstatus &= ~MSTATUS_MIE;
                    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (priv << 11);
                    csr_.write(CSR_MSTATUS, mstatus);
                }
                {
                    u64 mtvec = csr_.read(CSR_MTVEC);
                    branch_taken = true;
                    branch_target = mtvec & ~0x3ULL;
                    flush_decode_ = true;
                    flush_execute_ = true;
                }
                csr_.set_privilege_mode(PrivilegeMode::Machine);
                pending_ebreak_ = false;
                last_trap_cause_ = TrapCause::Exception;
                exception_taken_ = true;  // 抑制 trap 指令 commit
                break;
            }
            branch_target = br_target;
            break;
        }
        case InstructionKind::LB:
        case InstructionKind::LH:
        case InstructionKind::LW:
        case InstructionKind::LD:
        case InstructionKind::LBU:
        case InstructionKind::LHU:
        case InstructionKind::LWU:
        case InstructionKind::SB:
        case InstructionKind::SH:
        case InstructionKind::SW:
        case InstructionKind::SD: {
            alu_result = rs1_val + alu_src2;
            // 内存访问对齐检查：RV64 规范要求半字/字/双字访问必须对齐
            // 否则触发 Load/Store address misaligned 异常
            // mcause: 4 = Load misaligned, 6 = Store/AMO misaligned
            const bool is_load = instr.is_load();
            const bool is_store = instr.is_store();
            u32 required_align = 1;
            switch (instr.kind) {
                case InstructionKind::LH: case InstructionKind::LHU:
                case InstructionKind::SH: required_align = 2; break;
                case InstructionKind::LW: case InstructionKind::LWU:
                case InstructionKind::SW: required_align = 4; break;
                case InstructionKind::LD:
                case InstructionKind::SD: required_align = 8; break;
                default: required_align = 1; break;
            }
            if (required_align > 1 && (alu_result % required_align) != 0) {
                // 触发 misaligned 异常
                csr_.write(CSR_MEPC, instr.pc);
                csr_.write(CSR_MCAUSE, is_load ? 4u : 6u);
                csr_.write(CSR_MTVAL, alu_result);
                {
                    constexpr u64 MSTATUS_MIE = 1ULL << 3;
                    constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                    constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
                    const u64 priv = static_cast<u64>(csr_.privilege_mode());
                    u64 mstatus = csr_.read(CSR_MSTATUS);
                    if (mstatus & MSTATUS_MIE) {
                        mstatus |= MSTATUS_MPIE;
                    } else {
                        mstatus &= ~MSTATUS_MPIE;
                    }
                    mstatus &= ~MSTATUS_MIE;
                    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (priv << 11);
                    csr_.write(CSR_MSTATUS, mstatus);
                }
                {
                    u64 mtvec = csr_.read(CSR_MTVEC);
                    branch_taken = true;
                    branch_target = mtvec & ~0x3ULL;
                    flush_decode_ = true;
                    flush_execute_ = true;
                }
                csr_.set_privilege_mode(PrivilegeMode::Machine);
                pending_ebreak_ = false;
                last_trap_cause_ = TrapCause::Exception;
                exception_taken_ = true;  // 抑制 trap 指令 commit
                (void)is_store;
            }
            break;
        }
        case InstructionKind::ADDI:
            alu_result = rs1_val + alu_src2;
            break;
        case InstructionKind::ADDIW: {
            const s32 r = static_cast<s32>(static_cast<s64>(rs1_val) + static_cast<s64>(alu_src2));
            alu_result = static_cast<u64>(static_cast<s64>(r));
            break;
        }
        case InstructionKind::SLTI:
            alu_result = static_cast<s64>(rs1_val) < static_cast<s64>(alu_src2) ? 1ULL : 0ULL;
            break;
        case InstructionKind::SLTIU:
            alu_result = rs1_val < alu_src2 ? 1ULL : 0ULL;
            break;
        case InstructionKind::XORI:
            alu_result = rs1_val ^ alu_src2;
            break;
        case InstructionKind::ORI:
            alu_result = rs1_val | alu_src2;
            break;
        case InstructionKind::ANDI:
            alu_result = rs1_val & alu_src2;
            break;
        case InstructionKind::SLLI:
            alu_result = rs1_val << (static_cast<u32>(instr.imm) & 0x3F);
            break;
        case InstructionKind::SRLI:
            alu_result = rs1_val >> (static_cast<u32>(instr.imm) & 0x3F);
            break;
        case InstructionKind::SRAI:
            alu_result = static_cast<u64>(static_cast<s64>(rs1_val) >> (static_cast<u32>(instr.imm) & 0x3F));
            break;
        case InstructionKind::SLLIW: {
            const u32 sh = static_cast<u32>(instr.imm) & 0x1F;
            const u32 r = static_cast<u32>(rs1_val) << sh;
            alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(r)));
            break;
        }
        case InstructionKind::SRLIW: {
            const u32 sh = static_cast<u32>(instr.imm) & 0x1F;
            const u32 r = static_cast<u32>(rs1_val) >> sh;
            alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(r)));
            break;
        }
        case InstructionKind::SRAIW: {
            const u32 sh = static_cast<u32>(instr.imm) & 0x1F;
            const s32 r = static_cast<s32>(static_cast<u32>(rs1_val));
            alu_result = static_cast<u64>(static_cast<s64>(r >> sh));
            break;
        }
            break;
        case InstructionKind::ADD:
            alu_result = rs1_val + alu_src2;
            break;
        case InstructionKind::SUB:
            alu_result = rs1_val - alu_src2;
            break;
        case InstructionKind::ADDW: {
            const s32 r = static_cast<s32>(static_cast<u32>(rs1_val) + static_cast<u32>(alu_src2));
            alu_result = static_cast<u64>(static_cast<s64>(r));
            break;
        }
        case InstructionKind::SUBW: {
            const s32 r = static_cast<s32>(static_cast<u32>(rs1_val) - static_cast<u32>(alu_src2));
            alu_result = static_cast<u64>(static_cast<s64>(r));
            break;
        }
        case InstructionKind::SLL:
            alu_result = rs1_val << (alu_src2 & 0x3F);
            break;
        case InstructionKind::SLLW: {
            const u32 sh = static_cast<u32>(alu_src2) & 0x1F;
            const u32 r = static_cast<u32>(rs1_val) << sh;
            alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(r)));
            break;
        }
        case InstructionKind::SLT:
            alu_result = static_cast<s64>(rs1_val) < static_cast<s64>(alu_src2) ? 1ULL : 0ULL;
            break;
        case InstructionKind::SLTU:
            alu_result = rs1_val < alu_src2 ? 1ULL : 0ULL;
            break;
        case InstructionKind::XOR:
            alu_result = rs1_val ^ alu_src2;
            break;
        case InstructionKind::SRL:
            alu_result = rs1_val >> (alu_src2 & 0x3F);
            break;
        case InstructionKind::SRLW: {
            const u32 sh = static_cast<u32>(alu_src2) & 0x1F;
            const u32 r = static_cast<u32>(rs1_val) >> sh;
            alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(r)));
            break;
        }
        case InstructionKind::SRA:
            alu_result = static_cast<u64>(static_cast<s64>(rs1_val) >> (alu_src2 & 0x3F));
            break;
        case InstructionKind::SRAW: {
            const u32 sh = static_cast<u32>(alu_src2) & 0x1F;
            const s32 r = static_cast<s32>(static_cast<u32>(rs1_val));
            alu_result = static_cast<u64>(static_cast<s64>(r >> sh));
            break;
        }
        case InstructionKind::OR:
            alu_result = rs1_val | alu_src2;
            break;
        case InstructionKind::AND:
            alu_result = rs1_val & alu_src2;
            break;
        case InstructionKind::MUL:
            alu_result = rs1_val * alu_src2;
            break;
#if RISCV_HAVE_INT128
        case InstructionKind::MULH: {
            const __int128 p = static_cast<__int128>(static_cast<s64>(rs1_val)) *
                               static_cast<__int128>(static_cast<s64>(alu_src2));
            alu_result = static_cast<u64>(static_cast<__uint128_t>(p) >> 64);
            break;
        }
        case InstructionKind::MULHSU: {
            const __int128 p = static_cast<__int128>(static_cast<s64>(rs1_val)) *
                               static_cast<__uint128_t>(alu_src2);
            alu_result = static_cast<u64>(static_cast<__uint128_t>(p) >> 64);
            break;
        }
        case InstructionKind::MULHU: {
            const __uint128_t p = static_cast<__uint128_t>(rs1_val) * static_cast<__uint128_t>(alu_src2);
            alu_result = static_cast<u64>(p >> 64);
            break;
        }
#else
        case InstructionKind::MULH:
        case InstructionKind::MULHSU:
        case InstructionKind::MULHU: {
            // 无 128 位整数时的回退：64x64 低 64 位乘法，高 64 位用 32 位分段计算
            const u64 a_lo = rs1_val & 0xFFFFFFFFULL, a_hi = rs1_val >> 32;
            const u64 b_lo = rs2_val & 0xFFFFFFFFULL, b_hi = rs2_val >> 32;
            const u64 p0 = a_lo * b_lo;
            const u64 p1 = a_lo * b_hi;
            const u64 p2 = a_hi * b_lo;
            const u64 p3 = a_hi * b_hi;
            const u64 mid = p1 + p2;
            const u64 lo = p0 + (mid << 32);
            const u64 c = (lo < p0) ? 1ULL : 0ULL;
            u64 hi = p3 + (mid >> 32) + c;
            if (instr.kind == InstructionKind::MULH) {
                // 有符号积的高 64 位：从无符号积修正
                if (static_cast<s64>(rs1_val) < 0) hi -= rs2_val;
                if (static_cast<s64>(rs2_val) < 0) hi -= rs1_val;
            } else if (instr.kind == InstructionKind::MULHSU) {
                if (static_cast<s64>(rs1_val) < 0) hi -= rs2_val;
            }
            alu_result = hi;
            break;
        }
#endif
        case InstructionKind::DIV: {
            const s64 a = static_cast<s64>(rs1_val);
            const s64 b = static_cast<s64>(rs2_val);
            if (b == 0) {
                alu_result = static_cast<u64>(-1);
            } else if (a == (static_cast<s64>(1ULL << 63)) && b == -1) {
                alu_result = static_cast<u64>(static_cast<s64>(1ULL << 63));
            } else {
                alu_result = static_cast<u64>(static_cast<s64>(a / b));
            }
            break;
        }
        case InstructionKind::DIVU: {
            const u64 b = alu_src2;
            alu_result = (b == 0) ? ~0ULL : (rs1_val / b);
            break;
        }
        case InstructionKind::REM: {
            const s64 a = static_cast<s64>(rs1_val);
            const s64 b = static_cast<s64>(alu_src2);
            if (b == 0) {
                alu_result = rs1_val;
            } else if (a == INT64_MIN && b == -1) {
                alu_result = 0;
            } else {
                alu_result = static_cast<u64>(a % b);
            }
            break;
        }
        case InstructionKind::REMU: {
            const u64 b = alu_src2;
            alu_result = (b == 0) ? rs1_val : (rs1_val % b);
            break;
        }
        case InstructionKind::MULW: {
            const s32 a = static_cast<s32>(static_cast<u32>(rs1_val));
            const s32 b = static_cast<s32>(static_cast<u32>(alu_src2));
            alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(a * b)));
            break;
        }
        case InstructionKind::DIVW: {
            const s32 a = static_cast<s32>(static_cast<u32>(rs1_val));
            const s32 b = static_cast<s32>(static_cast<u32>(alu_src2));
            if (b == 0) {
                alu_result = static_cast<u64>(-1);
            } else if (a == (static_cast<s32>(0x80000000u)) && b == -1) {
                alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(0x80000000u)));
            } else {
                alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(a / b)));
            }
            break;
        }
        case InstructionKind::DIVUW: {
            const u32 a = static_cast<u32>(rs1_val);
            const u32 b = static_cast<u32>(alu_src2);
            alu_result = (b == 0) ? ~0ULL : static_cast<u64>(static_cast<s32>(a / b));
            break;
        }
        case InstructionKind::REMW: {
            const s32 a = static_cast<s32>(static_cast<u32>(rs1_val));
            const s32 b = static_cast<s32>(static_cast<u32>(alu_src2));
            
            if (b == 0) {
                alu_result = static_cast<u64>(static_cast<s64>(a));
            } else if (a == INT32_MIN && b == -1) {
                alu_result = 0;
            } else {
                s32 res32 = a % b;
                alu_result = static_cast<u64>(static_cast<s64>(res32));
            }
            break;
        }
        case InstructionKind::REMUW: {
            const u32 a = static_cast<u32>(rs1_val);
            const u32 b = static_cast<u32>(alu_src2);
            
            if (b == 0) {
                alu_result = static_cast<u64>(static_cast<s32>(a));
            } else {
                u32 res32 = a % b;
                alu_result = static_cast<u64>(static_cast<s32>(res32));
            }
            break;
        }
        case InstructionKind::FENCE:
        case InstructionKind::FENCE_I:
            alu_result = 0;
            break;
        case InstructionKind::SFENCE_VMA: {
            // mstatus.TVM (Trap Virtual Memory) 在 S-mode 下置 1 时，
            // 执行 SFENCE.VMA 必须触发 Illegal instruction 异常 (mcause=2)，
            // 跳转到 mtvec。对应 riscv-tests rv64mi-p-illegal 测试 2。
            if (csr_.privilege_mode() == PrivilegeMode::Supervisor) {
                constexpr u64 MSTATUS_TVM = 1ULL << 20;
                if (csr_.read(CSR_MSTATUS) & MSTATUS_TVM) {
                    csr_.write(CSR_MEPC, instr.pc);
                    csr_.write(CSR_MCAUSE, 2);  // Illegal instruction
                    csr_.write(CSR_MTVAL, instr.raw);
                    {
                        constexpr u64 MSTATUS_MIE = 1ULL << 3;
                        constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                        constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
                        const u64 priv = static_cast<u64>(csr_.privilege_mode());
                        u64 mstatus = csr_.read(CSR_MSTATUS);
                        if (mstatus & MSTATUS_MIE) {
                            mstatus |= MSTATUS_MPIE;
                        } else {
                            mstatus &= ~MSTATUS_MPIE;
                        }
                        mstatus &= ~MSTATUS_MIE;
                        mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (priv << 11);
                        csr_.write(CSR_MSTATUS, mstatus);
                    }
                    {
                        u64 mtvec = csr_.read(CSR_MTVEC);
                        branch_taken = true;
                        branch_target = mtvec & ~0x3ULL;
                        flush_decode_ = true;
                        flush_execute_ = true;
                    }
                    csr_.set_privilege_mode(PrivilegeMode::Machine);
                    pending_ebreak_ = false;
                    last_trap_cause_ = TrapCause::Exception;
                    exception_taken_ = true;  // 抑制 trap 指令 commit
                    alu_result = 0;
                    break;
                }
            }
            alu_result = 0;
            break;
        }
        case InstructionKind::WFI:
            // 教学场景：WFI 视为 nop（与参考实现一致）
            alu_result = 0;
            break;
        case InstructionKind::ECALL: {
            // riscv-tests 退出语义：a7=93 表示 POSIX exit
            // 模拟器应停止运行，run_riscv_tests 读 a0 判定 PASS/FAIL
            // 注意：不能直接在 EX 阶段停机！否则读 a0 时流水线前置指令
            // (如 <pass> 中的 li a0, 0) 尚未写回寄存器文件，会读到陈旧值。
            // 这里仅置位 pending_ecall_exit_，真正的停机推迟到 WB 阶段。
            const u64 a7 = regs_.read(17);
            if (a7 == 93) {
                pending_ecall_exit_ = true;
                pending_ecall_halt_pc_ = instr.pc;
                pending_ecall_halt_inst_ = instr.raw;
                alu_result = 0;
                break;
            }
            // 普通 ECALL：trap 到 mtvec，mcause 按特权级区分
            // 8=U-mode, 9=S-mode, 11=M-mode
            // 重要：ECALL 的 mepc 应指向 ECALL 指令本身（不是 ECALL+4），
            // riscv-tests rv64mi-p-scall 的 mtvec_handler 会校验 mepc == ecall_pc
            csr_.write(CSR_MEPC, instr.pc);
            const u64 priv = static_cast<u64>(csr_.privilege_mode());
            csr_.write(CSR_MCAUSE, priv + 8);
            csr_.write(CSR_MTVAL, 0);
            {
                constexpr u64 MSTATUS_MIE = 1ULL << 3;
                constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
                u64 mstatus = csr_.read(CSR_MSTATUS);
                if (mstatus & MSTATUS_MIE) {
                    mstatus |= MSTATUS_MPIE;
                } else {
                    mstatus &= ~MSTATUS_MPIE;
                }
                mstatus &= ~MSTATUS_MIE;
                // SPEC: 异常/中断进入 M-mode 时 MPP = 旧特权级
                mstatus = (mstatus & ~MSTATUS_MPP_MASK) |
                          (priv << 11);
                csr_.write(CSR_MSTATUS, mstatus);
            }
            {
                u64 mtvec = csr_.read(CSR_MTVEC);
                branch_taken = true;
                branch_target = mtvec & ~0x3ULL;
                flush_decode_ = true;
                flush_execute_ = true;
            }
            // trap 处理始终在 M-mode 下执行
            csr_.set_privilege_mode(PrivilegeMode::Machine);
            pending_ebreak_ = false;
            last_trap_cause_ = TrapCause::Exception;
            exception_taken_ = true;  // 抑制 trap 指令 commit
            alu_result = 0;
            break;
        }
        case InstructionKind::EBREAK:
            // EBREAK 触发断点异常（mcause=3），与 ECALL 类似走 trap 到 mtvec
            // 而不是直接 halt——riscv-tests 中 sbreak 用例需要测试 trap 路径
            // 注意：EBREAK 的 mepc 应指向 EBREAK 本身（与 ECALL 不同）
            csr_.write(CSR_MEPC, instr.pc);
            csr_.write(CSR_MCAUSE, 3);  // Exception code 3 = Breakpoint
            csr_.write(CSR_MTVAL, instr.pc);
            {
                constexpr u64 MSTATUS_MIE = 1ULL << 3;
                constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
                const u64 priv = static_cast<u64>(csr_.privilege_mode());
                u64 mstatus = csr_.read(CSR_MSTATUS);
                if (mstatus & MSTATUS_MIE) {
                    mstatus |= MSTATUS_MPIE;
                } else {
                    mstatus &= ~MSTATUS_MPIE;
                }
                mstatus &= ~MSTATUS_MIE;
                mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (priv << 11);
                csr_.write(CSR_MSTATUS, mstatus);
            }
            {
                u64 mtvec = csr_.read(CSR_MTVEC);
                branch_taken = true;
                branch_target = mtvec & ~0x3ULL;
                flush_decode_ = true;
                flush_execute_ = true;
            }
            csr_.set_privilege_mode(PrivilegeMode::Machine);
            pending_ebreak_ = false;
            last_trap_cause_ = TrapCause::Exception;
            exception_taken_ = true;  // 抑制 trap 指令 commit
            alu_result = 0;
            break;
        case InstructionKind::MRET: {
            u64 mepc = csr_.read(CSR_MEPC);
            branch_taken = true;
            branch_target = mepc;
            flush_decode_ = true;
            flush_execute_ = true;
            next_if_id_ = {};
            stall_fetch_ = false;

            constexpr u64 MSTATUS_MIE = 1ULL << 3;
            constexpr u64 MSTATUS_MPIE = 1ULL << 7;
            constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
            u64 mstatus = csr_.read(CSR_MSTATUS);
            // MIE = MPIE（恢复中断使能）
            if (mstatus & MSTATUS_MPIE) {
                mstatus |= MSTATUS_MIE;
            } else {
                mstatus &= ~MSTATUS_MIE;
            }
            // MPIE 置 1（规范要求）
            mstatus |= MSTATUS_MPIE;
            // SPEC: mret 后 MPP = U-Mode(0)
            // 修复点：原代码直接比较 new_priv (mstatus & MPP_MASK) 与特权级枚举值
            // (0/1/3)，但掩码后的值是 0/0x800/0x1800，永远不会等于 1 或 3。
            // 需要先把 MPP 字段右移到低 2 位再比较。
            const u64 mpp = (mstatus & MSTATUS_MPP_MASK) >> 11;
            mstatus &= ~MSTATUS_MPP_MASK;
            csr_.write(CSR_MSTATUS, mstatus);
            // 更新特权级：MRET 之后切换到 MPP 指定的模式（U=0, S=1, M=3）
            if (mpp == static_cast<u64>(PrivilegeMode::Supervisor)) {
                csr_.set_privilege_mode(PrivilegeMode::Supervisor);
            } else if (mpp == static_cast<u64>(PrivilegeMode::Machine)) {
                csr_.set_privilege_mode(PrivilegeMode::Machine);
            } else {
                csr_.set_privilege_mode(PrivilegeMode::User);
            }

            alu_result = 0;
            break;
        }
        case InstructionKind::SRET: {
            // SRET 仅在 S-Mode 或 M-Mode 下合法
            if (csr_.privilege_mode() == PrivilegeMode::User) {
                halted_ = true;
                halt_reason_ = HaltReason::InvalidInstruction;
                halt_pc_ = instr.pc;
                halt_inst_ = instr.raw;
                alu_result = 0;
                break;
            }
            // mstatus.TSR (Trap SRET) 在 S-mode 下置 1 时，执行 SRET 必须触发
            // Illegal instruction 异常 (mcause=2)，对应 riscv-tests rv64mi-p-illegal
            // 测试 2 的 test_tsr 路径 (bad9)。
            if (csr_.privilege_mode() == PrivilegeMode::Supervisor) {
                constexpr u64 MSTATUS_TSR = 1ULL << 22;
                if (csr_.read(CSR_MSTATUS) & MSTATUS_TSR) {
                    csr_.write(CSR_MEPC, instr.pc);
                    csr_.write(CSR_MCAUSE, 2);  // Illegal instruction
                    csr_.write(CSR_MTVAL, instr.raw);
                    {
                        constexpr u64 MSTATUS_MIE = 1ULL << 3;
                        constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                        constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
                        const u64 priv = static_cast<u64>(csr_.privilege_mode());
                        u64 mstatus = csr_.read(CSR_MSTATUS);
                        if (mstatus & MSTATUS_MIE) {
                            mstatus |= MSTATUS_MPIE;
                        } else {
                            mstatus &= ~MSTATUS_MPIE;
                        }
                        mstatus &= ~MSTATUS_MIE;
                        mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (priv << 11);
                        csr_.write(CSR_MSTATUS, mstatus);
                    }
                    {
                        u64 mtvec = csr_.read(CSR_MTVEC);
                        branch_taken = true;
                        branch_target = mtvec & ~0x3ULL;
                        flush_decode_ = true;
                        flush_execute_ = true;
                    }
                    csr_.set_privilege_mode(PrivilegeMode::Machine);
                    pending_ebreak_ = false;
                    last_trap_cause_ = TrapCause::Exception;
                    exception_taken_ = true;  // 抑制 trap 指令 commit
                    alu_result = 0;
                    break;
                }
            }
            constexpr u64 SIE = 1ULL << 1;       // sstatus.SIE
            constexpr u64 SPIE = 1ULL << 5;      // sstatus.SPIE
            constexpr u64 SPP_MASK = 1ULL << 8;  // sstatus.SPP
            u64 mstatus = csr_.read(CSR_MSTATUS);
            // SIE = SPIE
            u64 new_sie = (mstatus & SPIE) ? SIE : 0;
            mstatus = (mstatus & ~SIE) | new_sie;
            // 先记录 SPP（切换前），再清零
            const u64 new_priv = (mstatus & SPP_MASK) ? 1ULL : 0ULL;
            mstatus = (mstatus & ~SPP_MASK) |
                      (static_cast<u64>(csr_.privilege_mode()) << 8);
            // SPIE 置 1
            mstatus |= SPIE;
            csr_.write(CSR_MSTATUS, mstatus);
            // 跳转到 SEPC
            u64 sepc = csr_.read(CSR_SEPC);
            branch_taken = true;
            branch_target = sepc;
            flush_decode_ = true;
            flush_execute_ = true;
            // SRET 之后切换到 SPP 指定的模式
            csr_.set_privilege_mode(new_priv == 0 ? PrivilegeMode::User : PrivilegeMode::Supervisor);
            alu_result = 0;
            break;
        }
        case InstructionKind::CSRRW:
        case InstructionKind::CSRRS:
        case InstructionKind::CSRRC:
        case InstructionKind::CSRRWI:
        case InstructionKind::CSRRSI:
        case InstructionKind::CSRRCI: {
            const u32 csr_addr = static_cast<u32>(instr.imm) & 0xFFFu;

            // 特权级检查：低特权模式访问高特权 CSR → 触发 Illegal instruction 异常 (mcause=2)
            // CSR 地址布局（0xC00-0xCFF 计数器可由 U 访问；其它高位地址需要对应特权）：
            //   0x000-0x0FF: U-mode
            //   0x100-0x1FF: S-mode
            //   0x200-0x2FF: H-mode (reserved)
            //   0x300-0x3FF: M-mode
            //   0x400-0x6FF: reserved
            //   0x700-0x7FF: Debug/Mnstatus
            //   0x800-0xAFF: reserved
            //   0xB00-0xBFF: M-mode counters
            //   0xC00-0xCFF: U-mode counters
            //   0xD00-0xEFF: reserved
            //   0xF00-0xFFF: Machine info
            const auto priv = csr_.privilege_mode();
            const bool is_u_counter = (csr_addr >= 0xC00u && csr_addr <= 0xCFFu);
            const bool requires_m = (csr_addr >= 0x300u && csr_addr <= 0xBFFu) ||
                                    (csr_addr >= 0xF00u);
            const bool requires_s_or_m = (csr_addr >= 0x100u && csr_addr <= 0x1FFu) ||
                                         requires_m;
            auto trigger_illegal = [&]() {
                csr_.write(CSR_MEPC, instr.pc);
                csr_.write(CSR_MCAUSE, 2);  // Exception code 2 = Illegal instruction
                csr_.write(CSR_MTVAL, instr.raw);
                {
                    constexpr u64 MSTATUS_MIE = 1ULL << 3;
                    constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                    constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
                    const u64 cur_priv = static_cast<u64>(csr_.privilege_mode());
                    u64 mstatus = csr_.read(CSR_MSTATUS);
                    if (mstatus & MSTATUS_MIE) {
                        mstatus |= MSTATUS_MPIE;
                    } else {
                        mstatus &= ~MSTATUS_MPIE;
                    }
                    mstatus &= ~MSTATUS_MIE;
                    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (cur_priv << 11);
                    csr_.write(CSR_MSTATUS, mstatus);
                }
                {
                    u64 mtvec = csr_.read(CSR_MTVEC);
                    branch_taken = true;
                    branch_target = mtvec & ~0x3ULL;
                    flush_decode_ = true;
                    flush_execute_ = true;
                }
                csr_.set_privilege_mode(PrivilegeMode::Machine);
                pending_ebreak_ = false;
                last_trap_cause_ = TrapCause::Exception;
                exception_taken_ = true;  // 抑制 trap 指令 commit
                alu_result = 0;
            };
            if (priv == PrivilegeMode::User && requires_s_or_m && !is_u_counter) {
                trigger_illegal();
                break;
            }
            if (priv == PrivilegeMode::Supervisor && requires_m && !is_u_counter) {
                trigger_illegal();
                break;
            }
            // mstatus.TVM 在 S-mode 下置 1 时，访问 SATP 必须触发 Illegal instruction 异常
            // (mcause=2)。对应 riscv-tests rv64mi-p-illegal 测试 2 (bad7 路径)。
            if (priv == PrivilegeMode::Supervisor && csr_addr == 0x180u) {
                constexpr u64 MSTATUS_TVM = 1ULL << 20;
                if (csr_.read(CSR_MSTATUS) & MSTATUS_TVM) {
                    trigger_illegal();
                    break;
                }
            }
            // CSR 写权限检查 (与 ciliphen rv_priv.hpp::csr_op_permission_check 对齐)：
            //   对 0xC00-0xFFF 范围的 CSR，**写**操作必须触发 Illegal instruction 异常。
            //   这些 CSR (cycle/instret 计数器、machine info) 在 RISC-V 规范中是只读的。
            //   对应 riscv-tests rv64mi-p-csr 中的 `csrrw a0, cycle, zero` 测试。
            if (instr.kind == InstructionKind::CSRRW || instr.kind == InstructionKind::CSRRWI ||
                instr.kind == InstructionKind::CSRRS || instr.kind == InstructionKind::CSRRSI ||
                instr.kind == InstructionKind::CSRRC || instr.kind == InstructionKind::CSRRCI) {
                // CSRRS/CSRRC/CSRRSI/CSRRCI 写条件：rs1 != 0
                bool is_write = false;
                if (instr.kind == InstructionKind::CSRRW || instr.kind == InstructionKind::CSRRWI) {
                    is_write = true;
                } else {
                    const u32 rs1 = static_cast<u32>(instr.rs1);
                    if (instr.kind == InstructionKind::CSRRSI || instr.kind == InstructionKind::CSRRCI) {
                        is_write = (rs1 != 0);
                    } else {
                        is_write = (rs1_val != 0);  // CSRRS/CSRRC: rs1_val != 0 才写
                    }
                }
                if (is_write && csr_addr >= 0xC00u && csr_addr <= 0xFFFu) {
                    trigger_illegal();
                    break;
                }
            }
            // 未实现 CSR：触发非法指令异常
            if (!csr_.is_implemented(csr_addr)) {
                trigger_illegal();
                break;
            }

            const u64 old_val = csr_.read(csr_addr);
            u64 new_val = old_val;
            
            if (instr.kind == InstructionKind::CSRRW || instr.kind == InstructionKind::CSRRWI) {
                new_val = (instr.kind == InstructionKind::CSRRWI)
                              ? static_cast<u64>(instr.rs1)
                              : rs1_val;
                csr_.write(csr_addr, new_val);
            } else if (instr.kind == InstructionKind::CSRRS || instr.kind == InstructionKind::CSRRSI) {
                const u64 src = (instr.kind == InstructionKind::CSRRSI)
                                    ? static_cast<u64>(instr.rs1)
                                    : rs1_val;
                new_val = old_val | src;
                if (src != 0) {
                    csr_.write(csr_addr, new_val);
                }
            } else {
                const u64 mask = (instr.kind == InstructionKind::CSRRCI)
                                    ? static_cast<u64>(instr.rs1)
                                    : rs1_val;
                new_val = old_val & ~mask;
                if (mask != 0) csr_.write(csr_addr, new_val);
            }
            alu_result = old_val;
            break;
        }
        default:
            // 非法指令触发 Illegal instruction 异常（mcause=2），与 ECALL/EBREAK 类似
            // 走 trap 到 mtvec，而不是直接 halt（riscv-tests 中 illegal 用例需要测试 trap 路径）
            if (instr.kind == InstructionKind::INVALID || !instr.is_valid()) {
                csr_.write(CSR_MEPC, instr.pc);
                csr_.write(CSR_MCAUSE, 2);  // Exception code 2 = Illegal instruction
                csr_.write(CSR_MTVAL, instr.raw);
                {
                    constexpr u64 MSTATUS_MIE = 1ULL << 3;
                    constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                    constexpr u64 MSTATUS_MPP_MASK = 3ULL << 11;
                    const u64 priv = static_cast<u64>(csr_.privilege_mode());
                    u64 mstatus = csr_.read(CSR_MSTATUS);
                    if (mstatus & MSTATUS_MIE) {
                        mstatus |= MSTATUS_MPIE;
                    } else {
                        mstatus &= ~MSTATUS_MPIE;
                    }
                    mstatus &= ~MSTATUS_MIE;
                    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (priv << 11);
                    csr_.write(CSR_MSTATUS, mstatus);
                }
                {
                    u64 mtvec = csr_.read(CSR_MTVEC);
                    branch_taken = true;
                    branch_target = mtvec & ~0x3ULL;
                    flush_decode_ = true;
                    flush_execute_ = true;
                }
                csr_.set_privilege_mode(PrivilegeMode::Machine);
                pending_ebreak_ = false;
                last_trap_cause_ = TrapCause::Exception;
                exception_taken_ = true;  // 抑制 trap 指令 commit
            } else {
                // 其他未识别的非非法指令：保持默认（不跳转）
                alu_result = 0;
                branch_taken = false;
                branch_target = 0;
            }
            alu_result = 0;
            break;
    }

    // 异常/中断路径：仍推进当前指令的 ex_mem.valid（让 trap 指令 commit 一次），
    // 但强制 rd=0, alu_result=0，确保不会写 GPR。
    // ciliphen rv_core::exec 的语义：trap 指令会进入 exception label 并执行
    // raise_trap/set_GPR(rd=0)（因为 rd=0 时 set_GPR 是 no-op），但 debug_pc 仍然
    // 是 trap 指令的 PC。所以本地模拟器要 commit trap（rd=0, wdata=0）才能对齐。
    next_ex_mem_.valid = id_ex_.valid;
    if (exception_taken_) {
        DecodedInstruction trap_instr = instr;  // 拷贝以便修改 rd
        trap_instr.rd = 0;
        next_ex_mem_.instr = trap_instr;
        alu_result = 0;
        next_ex_mem_.trap_taken = true;  // 通知 MEM 阶段跳过内存访问
    } else {
        next_ex_mem_.instr = instr;
        next_ex_mem_.trap_taken = false;
    }
    next_ex_mem_.alu_result = alu_result;
    next_ex_mem_.alu_src1 = alu_src1;
    next_ex_mem_.alu_src2 = alu_src2;
    next_ex_mem_.rs2_value = rs2_val;
    next_ex_mem_.branch_taken = branch_taken;
    next_ex_mem_.branch_target = branch_target;
    
    // Propagate user signals from ID/EX to EX/MEM
    next_ex_mem_.user_signals = id_ex_.user_signals;

    if (instr.is_csr()) {
        next_ex_mem_.csr_write = true;
        next_ex_mem_.csr_addr = static_cast<u32>(instr.imm) & 0xFFFu;
    }

    if (branch_taken) {
        next_if_id_ = {};
        redirect_ = true;
        redirect_target_ = branch_target;
        flush_decode_ = true;
        flush_execute_ = true;
    }
}

/**
 * @brief 流水线 MEM（访存）阶段。
 *
 * 1. Store 指令：按 SB/SH/SW/SD 写入内存；riscv-tests 约定写 tohost 非零即停机
 * 2. Load 指令：优先使用前向 store 的数据（EX/MEM、MEM/WB），否则从内存读取
 * 3. trap 指令或 redirect_ 触发的指令不进行访存
 * 4. 将结果透传到 MEM/WB 寄存器
 */
void RISCVSimulator::stage_mem() {
    next_mem_wb_ = {};
    if (!ex_mem_.valid) {
        return;
    }

    // 即使 redirect_ 为 true，也要完成当前指令的 MEM 阶段
    // 这样 JAL/JALR 的返回地址才能正确写入寄存器

    next_mem_wb_.valid = ex_mem_.valid;
    next_mem_wb_.instr = ex_mem_.instr;
    next_mem_wb_.wb_value = ex_mem_.alu_result;
    next_mem_wb_.mem_addr = ex_mem_.alu_result;
    next_mem_wb_.store_data = ex_mem_.rs2_value;
    next_mem_wb_.csr_write = ex_mem_.csr_write;
    next_mem_wb_.csr_addr = ex_mem_.csr_addr;
    next_mem_wb_.csr_new_val = ex_mem_.csr_new_val;

    // trap 指令（异常/中断触发的指令）不需要访存
    if (ex_mem_.trap_taken) {
        return;
    }

    if (redirect_) {
        return;
    }

    const auto instr = ex_mem_.instr;
    u64 value = ex_mem_.alu_result;
    const u64 addr = ex_mem_.alu_result;

    u64 store_data = 0;
    if (instr.is_store()) {
        store_data = ex_mem_.rs2_value;
        bool should_store = true;
        if (ex_mem_.user_signals.mem_write.has_value()) {
            should_store = ex_mem_.user_signals.mem_write.value();
        }
        if (should_store) {
            switch (instr.kind) {
                case InstructionKind::SB:
                    memory_.write8(addr, static_cast<u8>(store_data & 0xFF));
                    break;
                case InstructionKind::SH:
                    memory_.write16(addr, static_cast<u16>(store_data & 0xFFFF));
                    break;
                case InstructionKind::SW:
                    memory_.write32(addr, static_cast<u32>(store_data & 0xFFFFFFFFu));
                    break;
                case InstructionKind::SD:
                    memory_.write64(addr, store_data);
                    break;
                default:
                    break;
            }
            // riscv-tests 约定：向 tohost (0x80001000) 写入非零值即代表测试结束。
            // 模拟器需停机，run_riscv_tests 读 a0 判定 PASS/FAIL。
            if (tohost_address_ != 0 && addr == tohost_address_ && store_data != 0) {
                halted_ = true;
                halt_reason_ = HaltReason::EcallExit;
                halt_pc_ = instr.pc;
                halt_inst_ = instr.raw;
            }
        }
    }

    if (instr.is_load()) {
        bool should_load = true;
        if (ex_mem_.user_signals.mem_read.has_value()) {
            should_load = ex_mem_.user_signals.mem_read.value();
        }
        
        if (should_load) {
            bool forwarded = false;
            u64 store_val = 0;

            // 检查 EX/MEM 阶段的 store 指令 - 直接使用 rs2_value
            if (ex_mem_.valid && ex_mem_.instr.is_store() && ex_mem_.alu_result == addr) {
                store_val = ex_mem_.rs2_value;
                forwarded = true;
            }

            // 检查 MEM/WB 阶段的 store 指令
            if (!forwarded && mem_wb_.valid && mem_wb_.instr.is_store() && mem_wb_.mem_addr == addr) {
                store_val = mem_wb_.store_data;
                forwarded = true;
            }

            if (forwarded) {
                switch (instr.kind) {
                    case InstructionKind::LB:
                        value = static_cast<u64>(static_cast<s64>(static_cast<s8>(store_val & 0xFF)));
                        break;
                    case InstructionKind::LH:
                        value = static_cast<u64>(static_cast<s64>(static_cast<s16>(store_val & 0xFFFF)));
                        break;
                    case InstructionKind::LW:
                        value = static_cast<u64>(sign_extend<s64>(static_cast<u32>(store_val & 0xFFFFFFFFu), 32));
                        break;
                    case InstructionKind::LD:
                        value = store_val;
                        break;
                    case InstructionKind::LBU:
                        value = store_val & 0xFF;
                        break;
                    case InstructionKind::LHU:
                        value = store_val & 0xFFFF;
                        break;
                    case InstructionKind::LWU:
                        value = store_val & 0xFFFFFFFFu;
                        break;
                    default:
                        forwarded = false;
                        break;
                }
            }

            if (!forwarded) {
                switch (instr.kind) {
                    case InstructionKind::LB:
                        value = static_cast<u64>(static_cast<s64>(static_cast<s8>(memory_.read8(addr))));
                        break;
                    case InstructionKind::LH:
                        value = static_cast<u64>(static_cast<s64>(static_cast<s16>(memory_.read16(addr))));
                        break;
                    case InstructionKind::LW:
                        value = static_cast<u64>(sign_extend<s64>(memory_.read32(addr), 32));
                        break;
                    case InstructionKind::LD:
                        value = memory_.read64(addr);
                        break;
                    case InstructionKind::LBU:
                        value = memory_.read8(addr);
                        break;
                    case InstructionKind::LHU:
                        value = memory_.read16(addr);
                        break;
                    case InstructionKind::LWU:
                        value = static_cast<u64>(memory_.read32(addr));
                        break;
                    default:
                        break;
                }
            }
        }
    }

    next_mem_wb_.valid = ex_mem_.valid;
    next_mem_wb_.instr = instr;
    next_mem_wb_.wb_value = value;
    if (instr.is_store() || instr.is_load()) {
        next_mem_wb_.mem_addr = addr;
        if (instr.is_store()) {
            next_mem_wb_.store_data = store_data;
        } else {
            next_mem_wb_.store_data = 0;
        }
    } else {
        next_mem_wb_.mem_addr = 0;
        next_mem_wb_.store_data = 0;
    }
    next_mem_wb_.csr_write = ex_mem_.csr_write;
    next_mem_wb_.csr_addr = ex_mem_.csr_addr;
    next_mem_wb_.csr_new_val = ex_mem_.csr_new_val;
    
    // Propagate user signals from EX/MEM to MEM/WB
    next_mem_wb_.user_signals = ex_mem_.user_signals;
}

/**
 * @brief 流水线 WB（写回）阶段。
 *
 * 1. 检查 pending EBREAK，命中则停机
 * 2. 记录 WBResult（含 PC、目标寄存器、写回数据、用户信号）供 difftest 使用
 * 3. 应用 user_signals.reg_write 决定是否真正写寄存器
 * 4. 处理 pending ECALL 退出（延迟到本阶段以保证寄存器已排空写回）
 */
void RISCVSimulator::stage_wb() {
    if (!mem_wb_.valid) {
        last_wb_result.valid = false;
        return;
    }

    if (pending_ebreak_ && mem_wb_.instr.kind == InstructionKind::EBREAK) {
        halted_ = true;
        pending_ebreak_ = false;
    }

    // Record WB result before applying user signals
    last_wb_result.valid = mem_wb_.valid;
    last_wb_result.pc = mem_wb_.instr.pc;
    last_wb_result.wb_en = mem_wb_.instr.writes_rd();
    last_wb_result.wb_raddr = mem_wb_.instr.rd;
    last_wb_result.wb_rdata = mem_wb_.wb_value;
    
    // Use user signals from pipeline register (flowed from ID stage)
    bool should_write = mem_wb_.instr.writes_rd();
    if (mem_wb_.user_signals.reg_write.has_value()) {
        should_write = mem_wb_.user_signals.reg_write.value();
        last_wb_result.user_reg_write = mem_wb_.user_signals.reg_write;
    }
    
    // Set flag if any user signal was set for this instruction
    last_wb_result.has_user_signal = mem_wb_.user_signals.has_any();
    
    if (should_write) {
        u64 val = mem_wb_.wb_value;
        regs_.write(mem_wb_.instr.rd, val);
    }

    last_wb_result.actual_wb_en = should_write;

    // ★ ECALL 退出延迟提交：EX 阶段检测到 a7=93 时只置位，
    // 到 WB 阶段（此时前置指令的写回已完成）才真正停机。
    // 这样 run_riscv_tests 读取的 a0/寄存器状态是流水线排空后的最终值。
    if (pending_ecall_exit_) {
        halted_ = true;
        halt_reason_ = HaltReason::EcallExit;
        halt_pc_ = pending_ecall_halt_pc_;
        halt_inst_ = pending_ecall_halt_inst_;
        pending_ecall_exit_ = false;
    }

    // ★ ECALL 不再 halt：EX 阶段已触发 trap 重定向，WB 阶段直接通过
    // EBREAK 仍 halt（在 stage_wb 开头的 pending_ebreak_ 分支处理）
}

/**
 * @brief 在每个时钟周期结束时更新所有流水线寄存器（IF/ID、ID/EX、EX/MEM、MEM/WB）。
 *
 * 1. 若本周期发生重定向：刷新 IF/ID 与 ID/EX，将 PC 设为跳转目标
 * 2. 否则：将所有 next_* 寄存器的值赋给当前 * 寄存器
 * 3. flush_decode_ / flush_execute_ 用于在 trap/分支冲刷时清空对应 next 寄存器
 */
void RISCVSimulator::update_pipeline_registers() {
    if (redirect_) {
        pc_ = redirect_target_;
        if_id_ = {};
        next_if_id_ = {};
        id_ex_ = {};
        next_id_ex_ = {};
        // 不清空 next_ex_mem_ 和 next_mem_wb_，让 JAL/JALR 的返回地址能正确写入
        // 但仍然需要更新 ex_mem_ 和 mem_wb_ 寄存器
        ex_mem_ = next_ex_mem_;
        mem_wb_ = next_mem_wb_;
        
        redirect_ = false;
        redirect_target_ = 0;
        return;
    }
    
    pc_ = next_pc_;

    if (flush_decode_) {
        next_if_id_ = {};
        next_id_ex_ = {};
    } else if (flush_execute_) {
        next_id_ex_ = {};
    }

    mem_wb_ = next_mem_wb_;
    ex_mem_ = next_ex_mem_;
    id_ex_ = next_id_ex_;
    if_id_ = next_if_id_;
}

/**
 * @brief 收集当前周期的五个流水线阶段状态，供前端/教学展示。
 *
 * 把 IF/ID、ID/EX、EX/MEM、MEM/WB 寄存器中指令的 valid/PC/rd/rs1/rs2/imm 提取到 PipelineState。
 */
void RISCVSimulator::update_pipeline_state() {
    pipeline_state_.cycle = cycle_;
    pipeline_state_.fetch = {if_id_.valid, if_id_.pc, if_id_.inst, 0, 0, 0, static_cast<u64>(0)};
    pipeline_state_.decode = {id_ex_.valid, id_ex_.instr.pc, id_ex_.instr.raw, id_ex_.instr.rd,
                              id_ex_.instr.rs1, id_ex_.instr.rs2, static_cast<u64>(id_ex_.instr.imm)};
    pipeline_state_.execute = {ex_mem_.valid, ex_mem_.instr.pc, ex_mem_.instr.raw, ex_mem_.instr.rd,
                               ex_mem_.instr.rs1, ex_mem_.instr.rs2,
                               static_cast<u64>(ex_mem_.instr.imm)};
    pipeline_state_.memory = {mem_wb_.valid, mem_wb_.instr.pc, mem_wb_.instr.raw, mem_wb_.instr.rd,
                              mem_wb_.instr.rs1, mem_wb_.instr.rs2,
                              static_cast<u64>(mem_wb_.instr.imm)};
    pipeline_state_.writeback = {mem_wb_.valid, mem_wb_.instr.pc, mem_wb_.instr.raw,
                                 mem_wb_.instr.rd, mem_wb_.instr.rs1, mem_wb_.instr.rs2,
                                 static_cast<u64>(mem_wb_.instr.imm)};
}

/**
 * @brief 设置模拟器暂停标志，用于 difftest 在用户输入模式下暂停流水线。
 *
 * 调用后 step() 会立即返回，等待用户的控制信号输入。
 *
 * @param waiting true 表示进入暂停状态
 * @param pc 触发暂停的指令 PC（用于在 ID 阶段恢复时透传 user_signals）
 */
void RISCVSimulator::set_waiting_for_input(bool waiting, u64 pc) {
    waiting_for_input_ = waiting;
    if (waiting || pc != 0) {
        waiting_pc_ = pc;
    }
}

/**
 * @brief 为 ID 阶段设置用户控制信号（用于交互式教学/difftest）。
 *
 * 支持 RegWrite、ALUSrc、MemRead、MemWrite、Branch 五种信号。
 * 仅在 waiting_for_input_ 状态下有效，会同时置位 waiting_handled_。
 *
 * @param signal_name 信号名称（"RegWrite" / "ALUSrc" / "MemRead" / "MemWrite" / "Branch"）
 * @param value 信号值
 */
void RISCVSimulator::set_user_signal_for_id(const std::string& signal_name, bool value) {
    if (waiting_for_input_) {
        if (signal_name == "RegWrite") {
            next_id_ex_.user_signals.reg_write = value;
        } else if (signal_name == "ALUSrc") {
            next_id_ex_.user_signals.alu_src = value;
        } else if (signal_name == "MemRead") {
            next_id_ex_.user_signals.mem_read = value;
        } else if (signal_name == "MemWrite") {
            next_id_ex_.user_signals.mem_write = value;
        } else if (signal_name == "Branch") {
            next_id_ex_.user_signals.branch = value;
        }
        waiting_handled_ = true;
    }
}

/**
 * @brief 清除 ID 阶段暂存的所有用户控制信号。
 */
void RISCVSimulator::clear_user_signals_for_id() {
    next_id_ex_.user_signals.clear();
}

/**
 * @brief 触发指定位号的软件中断（设置 CSR_MIP 对应位）。
 *
 * 教学演示：只设置 pending 位，不自动开启对应 MIE。
 * 学生需显式执行 `csrw mie, t0` 才能让中断真正被处理器响应。
 *
 * @param bit 中断位号（0..63），超过 64 的值直接忽略
 */
void RISCVSimulator::trigger_pending_interrupt(u64 bit) {
    if (bit >= 64) return;
    // 教学演示：只设置 MIP[bit]，不自动设置 MIE。
    // 让学生程序显式 `csrw mie, t0` 打开对应中断使能位，
    // 体现"中断 pending"与"中断使能"是两个独立概念。
    u64 mip = csr_.read(CSR_MIP);
    csr_.write(CSR_MIP, mip | (1ULL << bit));
}

}  // namespace riscv

