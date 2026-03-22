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

RISCVSimulator::RISCVSimulator() : memory_(static_cast<std::size_t>(DEFAULT_MEMORY_SIZE), RESET_VECTOR) {
    reset();
}

void RISCVSimulator::load_program(const std::vector<u8>& binary, u64 offset) {
    memory_.reset();
    memory_.load_program(binary, offset);
    reset();
}

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
}

void RISCVSimulator::run(u32 cycles) {
    for (u32 i = 0; i < cycles && !halted_; ++i) {
        step();
    }
}

void RISCVSimulator::step() {
    if (halted_) {
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
        mstatus_val = (mstatus_val & ~MSTATUS_MIE) | ((mstatus_val & MSTATUS_MIE) ? MSTATUS_MPIE : 0);
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
            mstatus_val = (mstatus_val & ~MSTATUS_MIE) | ((mstatus_val & MSTATUS_MIE) ? MSTATUS_MPIE : 0);
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

    next_if_id_ = {};
    next_if_id_.valid = true;
    next_if_id_.pc = pc_;
    next_if_id_.inst = memory_.read32(pc_);
    next_pc_ = pc_ + 4;
}


void RISCVSimulator::stage_id() {
    if (redirect_) {
        next_id_ex_ = {};
        return;
    }
    
    next_id_ex_ = {};
    if (!if_id_.valid) {
        return;
    }

    auto instr = decode(if_id_.inst, if_id_.pc);

    // 特权级检查暂时禁用
    // bool needs_supervisor_or_machine = false;
    // switch (instr.kind) {
    //     case InstructionKind::SRET:
    //     case InstructionKind::SFENCE_VMA:
    //         needs_supervisor_or_machine = true;
    //         break;
    //     default:
    //         break;
    // }
    // 
    // if (needs_supervisor_or_machine) {
    //     u64 mstatus = csr_.read(CSR_MSTATUS);
    //     u64 current_priv = (mstatus >> 11) & 0x3;
    //     if (current_priv == 0) {
    //         instr.kind = InstructionKind::INVALID;
    //     }
    // }

    if (instr.rd == 3 && instr.writes_rd()) {
    }

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

}

void RISCVSimulator::stage_ex() {
    next_ex_mem_ = {};
    if (!id_ex_.valid) {
        return;
    }

    if (redirect_) {
        return;
    }

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

    switch (instr.kind) {
        case InstructionKind::LUI:
            alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(instr.imm & 0xFFFFFFFFu)));
            break;
        case InstructionKind::AUIPC:
            alu_result = instr.pc + static_cast<u64>(instr.imm);
            break;
        case InstructionKind::JAL:
            alu_result = instr.pc + 4;
            branch_taken = true;
            branch_target = instr.pc + static_cast<u64>(instr.imm);
            break;
        case InstructionKind::JALR:
            alu_result = instr.pc + 4;
            branch_taken = true;
            branch_target = (rs1_val + static_cast<u64>(instr.imm)) & ~1ULL;
            break;
        case InstructionKind::BEQ:
        case InstructionKind::BNE:
        case InstructionKind::BLT:
        case InstructionKind::BGE:
        case InstructionKind::BLTU:
        case InstructionKind::BGEU:
            branch_taken = is_branch_taken(instr, rs1_val, rs2_val);
            branch_target = instr.pc + static_cast<u64>(instr.imm);
            break;
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
            switch (instr.kind) {
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
                    alu_result = rs1_val + static_cast<u64>(instr.imm);
                    break;
                case InstructionKind::ADDI:
                    alu_result = rs1_val + static_cast<u64>(instr.imm);
                    break;
        case InstructionKind::ADDIW: {
            const s32 r = static_cast<s32>(static_cast<s64>(rs1_val) + static_cast<s64>(instr.imm));
            alu_result = static_cast<u64>(static_cast<s64>(r));
            break;
        }
                case InstructionKind::SLTI:
                    alu_result = static_cast<s64>(rs1_val) < instr.imm ? 1ULL : 0ULL;
                    break;
                case InstructionKind::SLTIU:
                    alu_result = rs1_val < static_cast<u64>(instr.imm) ? 1ULL : 0ULL;
                    break;
                case InstructionKind::XORI:
                    alu_result = rs1_val ^ static_cast<u64>(instr.imm);
                    break;
                case InstructionKind::ORI:
                    alu_result = rs1_val | static_cast<u64>(instr.imm);
                    break;
                case InstructionKind::ANDI:
                    alu_result = rs1_val & static_cast<u64>(instr.imm);
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
                default:
                    break;
            }
            break;
        case InstructionKind::ADD:
            alu_result = rs1_val + rs2_val;
            break;
        case InstructionKind::SUB:
            alu_result = rs1_val - rs2_val;
            break;
        case InstructionKind::ADDW: {
            const s32 r = static_cast<s32>(static_cast<u32>(rs1_val) + static_cast<u32>(rs2_val));
            alu_result = static_cast<u64>(static_cast<s64>(r));
            break;
        }
        case InstructionKind::SUBW: {
            const s32 r = static_cast<s32>(static_cast<u32>(rs1_val) - static_cast<u32>(rs2_val));
            alu_result = static_cast<u64>(static_cast<s64>(r));
            break;
        }
        case InstructionKind::SLL:
            alu_result = rs1_val << (rs2_val & 0x3F);
            break;
        case InstructionKind::SLLW: {
            const u32 sh = static_cast<u32>(rs2_val) & 0x1F;
            const u32 r = static_cast<u32>(rs1_val) << sh;
            alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(r)));
            break;
        }
        case InstructionKind::SLT:
            alu_result = static_cast<s64>(rs1_val) < static_cast<s64>(rs2_val) ? 1ULL : 0ULL;
            break;
        case InstructionKind::SLTU:
            alu_result = rs1_val < rs2_val ? 1ULL : 0ULL;
            break;
        case InstructionKind::XOR:
            alu_result = rs1_val ^ rs2_val;
            break;
        case InstructionKind::SRL:
            alu_result = rs1_val >> (rs2_val & 0x3F);
            break;
        case InstructionKind::SRLW: {
            const u32 sh = static_cast<u32>(rs2_val) & 0x1F;
            const u32 r = static_cast<u32>(rs1_val) >> sh;
            alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(r)));
            break;
        }
        case InstructionKind::SRA:
            alu_result = static_cast<u64>(static_cast<s64>(rs1_val) >> (rs2_val & 0x3F));
            break;
        case InstructionKind::SRAW: {
            const u32 sh = static_cast<u32>(rs2_val) & 0x1F;
            const s32 r = static_cast<s32>(static_cast<u32>(rs1_val));
            alu_result = static_cast<u64>(static_cast<s64>(r >> sh));
            break;
        }
        case InstructionKind::OR:
            alu_result = rs1_val | rs2_val;
            break;
        case InstructionKind::AND:
            alu_result = rs1_val & rs2_val;
            break;
        case InstructionKind::MUL:
            alu_result = rs1_val * rs2_val;
            break;
#if RISCV_HAVE_INT128
        case InstructionKind::MULH: {
            const __int128 p = static_cast<__int128>(static_cast<s64>(rs1_val)) *
                               static_cast<__int128>(static_cast<s64>(rs2_val));
            alu_result = static_cast<u64>(static_cast<__uint128_t>(p) >> 64);
            break;
        }
        case InstructionKind::MULHSU: {
            const __int128 p = static_cast<__int128>(static_cast<s64>(rs1_val)) *
                               static_cast<__uint128_t>(rs2_val);
            alu_result = static_cast<u64>(static_cast<__uint128_t>(p) >> 64);
            break;
        }
        case InstructionKind::MULHU: {
            const __uint128_t p = static_cast<__uint128_t>(rs1_val) * static_cast<__uint128_t>(rs2_val);
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
            const u64 b = rs2_val;
            alu_result = (b == 0) ? ~0ULL : (rs1_val / b);
            break;
        }
        case InstructionKind::REM: {
            const s64 a = static_cast<s64>(rs1_val);
            const s64 b = static_cast<s64>(rs2_val);
            if (b == 0) {
                alu_result = rs1_val;
            } else if (a == INT64_MIN && b == -1) {
                alu_result = 0;  // 溢出情况
            } else {
                alu_result = static_cast<u64>(a % b);
            }
            break;
        }
        case InstructionKind::REMU: {
            const u64 b = rs2_val;
            alu_result = (b == 0) ? rs1_val : (rs1_val % b);
            break;
        }
        case InstructionKind::MULW: {
            const s32 a = static_cast<s32>(static_cast<u32>(rs1_val));
            const s32 b = static_cast<s32>(static_cast<u32>(rs2_val));
            alu_result = static_cast<u64>(static_cast<s64>(static_cast<s32>(a * b)));
            break;
        }
        case InstructionKind::DIVW: {
            const s32 a = static_cast<s32>(static_cast<u32>(rs1_val));
            const s32 b = static_cast<s32>(static_cast<u32>(rs2_val));
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
            const u32 b = static_cast<u32>(rs2_val);
            alu_result = (b == 0) ? ~0ULL : static_cast<u64>(static_cast<s32>(a / b));
            break;
        }
        case InstructionKind::REMW: {
            const s32 a = static_cast<s32>(static_cast<u32>(rs1_val));
            const s32 b = static_cast<s32>(static_cast<u32>(rs2_val));
            
            if (b == 0) {
                alu_result = static_cast<u64>(static_cast<s64>(a));  // 符号扩展
            } else if (a == INT32_MIN && b == -1) {
                alu_result = 0;  // 溢出情况
            } else {
                s32 res32 = a % b;
                alu_result = static_cast<u64>(static_cast<s64>(res32));  // 符号扩展
            }
            break;
        }
        case InstructionKind::REMUW: {
            const u32 a = static_cast<u32>(rs1_val);
            const u32 b = static_cast<u32>(rs2_val);
            
            if (b == 0) {
                // 将a视为有符号32位数并符号扩展到64位
                alu_result = static_cast<u64>(static_cast<s32>(a));
            } else {
                u32 res32 = a % b;
                // 将结果视为有符号32位数并符号扩展到64位
                alu_result = static_cast<u64>(static_cast<s32>(res32));
            }
            break;
        }
        case InstructionKind::FENCE:
        case InstructionKind::FENCE_I:
        case InstructionKind::SFENCE_VMA:
            alu_result = 0;
            break;
        case InstructionKind::WFI: {
            if (csr_.has_pending_interrupt()) {
                u64 mstatus_wfi = csr_.read(CSR_MSTATUS);
                constexpr u64 MSTATUS_MIE = 1ULL << 3;
                constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                u64 old_mie = (mstatus_wfi & MSTATUS_MIE) ? 1 : 0;
                mstatus_wfi = (mstatus_wfi & ~MSTATUS_MIE) | (old_mie ? MSTATUS_MPIE : 0);
                csr_.write(CSR_MSTATUS, mstatus_wfi);
                csr_.write(0x344, 0);
                redirect_ = true;
                redirect_target_ = pc_ + 4;
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
            }
            stall_fetch_ = true;
            next_id_ex_.valid = false;
            alu_result = 0;
            break;
        }
        case InstructionKind::ECALL:
        case InstructionKind::EBREAK:
            // EBREAK 应该触发断点异常，而不是直接停机
            // 设置异常相关 CSR
            if (instr.kind == InstructionKind::EBREAK) {
                csr_.write(CSR_MEPC, instr.pc);  // mepc = ebreak 指令地址
                csr_.write(CSR_MCAUSE, 3);        // mcause = 3 (Breakpoint)
                csr_.write(CSR_MTVAL, 0);         // mtval = 0
                
                // 更新 mstatus: MPIE = MIE, MIE = 0
                u64 mstatus = csr_.read(CSR_MSTATUS);
                constexpr u64 MSTATUS_MIE = 1ULL << 3;
                constexpr u64 MSTATUS_MPIE = 1ULL << 7;
                u64 old_mie = (mstatus & MSTATUS_MIE) ? 1 : 0;
                mstatus = (mstatus & ~MSTATUS_MPIE) | (old_mie ? MSTATUS_MPIE : 0);
                mstatus &= ~MSTATUS_MIE;
                csr_.write(CSR_MSTATUS, mstatus);
                
                // 跳转到异常处理程序
                u64 mtvec = csr_.read(CSR_MTVEC);
                branch_taken = true;
                branch_target = mtvec & ~0x3ULL;
                flush_decode_ = true;
                flush_execute_ = true;
            }
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
            
            u64 mstatus = csr_.read(CSR_MSTATUS);
            constexpr u64 MSTATUS_MIE = 1ULL << 3;
            constexpr u64 MSTATUS_MPIE = 1ULL << 7;
            constexpr u64 MSTATUS_MPP = 0x1800;  // bits [12:11]
            u64 old_mpie = (mstatus & MSTATUS_MPIE) ? 1 : 0;
            u64 old_mpp = (mstatus >> 11) & 0x3;
            mstatus = (mstatus & ~MSTATUS_MIE) | (old_mpie ? MSTATUS_MIE : 0);
            mstatus |= MSTATUS_MPIE;
            mstatus = (mstatus & ~MSTATUS_MPP) | (old_mpp << 11);
            csr_.write(CSR_MSTATUS, mstatus);
            
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
            // 非法指令不在此处停机，传至 WB 再停机，保证前一条 ECALL/EBREAK 能先提交
            alu_result = 0;
            branch_taken = false;
            branch_target = 0;
            break;
    }

    next_ex_mem_.valid = id_ex_.valid;
    next_ex_mem_.instr = instr;
    next_ex_mem_.alu_result = alu_result;
    next_ex_mem_.rs2_value = rs2_val;
    next_ex_mem_.branch_taken = branch_taken;
    next_ex_mem_.branch_target = branch_target;

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

    if (redirect_) {
        return;
    }

    const auto instr = ex_mem_.instr;
    u64 value = ex_mem_.alu_result;
    const u64 addr = ex_mem_.alu_result;

    u64 store_data = 0;
    if (instr.is_store()) {
        store_data = ex_mem_.rs2_value;
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
    }

    if (instr.is_load()) {
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
}

void RISCVSimulator::stage_wb() {
    if (!mem_wb_.valid) {
        return;
    }
    
    // 非法指令：触发异常而不是直接停机
    if (!mem_wb_.instr.is_valid()) {
        // 从内存中读取实际指令编码用于 mtval
        u32 illegal_inst = memory_.read32(mem_wb_.instr.pc);
        
        // 设置异常相关 CSR
        csr_.write(0x341, mem_wb_.instr.pc);  // mepc = 非法指令地址
        csr_.write(0x342, 2);                  // mcause = 2 (Illegal instruction)
        csr_.write(0x343, illegal_inst);       // mtval = 非法指令编码
        
        // 更新 mstatus: MPIE = MIE, MIE = 0
        u64 mstatus = csr_.read(CSR_MSTATUS);
        constexpr u64 MSTATUS_MIE = 1ULL << 3;
        constexpr u64 MSTATUS_MPIE = 1ULL << 7;
        u64 old_mie = (mstatus & MSTATUS_MIE) ? 1 : 0;
        mstatus = (mstatus & ~MSTATUS_MPIE) | (old_mie ? MSTATUS_MPIE : 0);
        mstatus &= ~MSTATUS_MIE;
        csr_.write(CSR_MSTATUS, mstatus);
        
        // 跳转到异常处理程序（支持向量模式）
        u64 mtvec = csr_.read(0x305);
        u64 mtvec_mode = mtvec & 0x3;
        u64 mtvec_base = mtvec & ~0x3ULL;
        u64 cause = 2;  // Illegal instruction
        u64 target;
        
        if (mtvec_mode == 1) {
            target = mtvec_base + 4 * cause;
        } else {
            target = mtvec_base;
        }
        
        redirect_ = true;
        redirect_target_ = target;
        flush_decode_ = true;
        flush_execute_ = true;
        
        // 清空所有流水线寄存器
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
    
    // Record WB result before applying external signals
    last_wb_result.valid = mem_wb_.valid;
    last_wb_result.pc = mem_wb_.instr.pc;
    last_wb_result.wb_en = mem_wb_.instr.writes_rd();
    last_wb_result.wb_raddr = mem_wb_.instr.rd;
    last_wb_result.wb_rdata = mem_wb_.wb_value;
    
    // Apply external control signals for RegWrite
    bool should_write = mem_wb_.instr.writes_rd();
    if (external_signals.reg_write.has_value()) {
        should_write = external_signals.reg_write.value();
    }
    
    if (should_write) {
        u64 val = mem_wb_.wb_value;
        regs_.write(mem_wb_.instr.rd, val);
    }
    
    // Update last_wb_result with actual write decision
    last_wb_result.wb_en = should_write;

    if (mem_wb_.instr.kind == InstructionKind::ECALL) {
        halted_ = true;
        halt_reason_ = HaltReason::Ecall;
        halt_pc_ = mem_wb_.instr.pc;
        halt_inst_ = mem_wb_.instr.raw;
    }
}

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

void RISCVSimulator::update_pipeline_state() {
    pipeline_state_.cycle = cycle_;
    pipeline_state_.fetch = {if_id_.valid, if_id_.pc, if_id_.inst, 0, 0, 0, static_cast<u64>(0)};
    pipeline_state_.decode = {id_ex_.valid, id_ex_.instr.pc, id_ex_.instr.raw, id_ex_.instr.rd,
                              id_ex_.instr.rs1, id_ex_.instr.rs2, static_cast<u64>(id_ex_.instr.imm)};
    pipeline_state_.execute = {ex_mem_.valid, ex_mem_.instr.pc, ex_mem_.instr.raw, ex_mem_.instr.rd,
                               ex_mem_.instr.rs1, ex_mem_.instr.rs2,
                               static_cast<u64>(ex_mem_.instr.imm)};
    pipeline_state_.memory = {mem_wb_.valid, ex_mem_.instr.pc, ex_mem_.instr.raw, ex_mem_.instr.rd,
                              ex_mem_.instr.rs1, ex_mem_.instr.rs2,
                              static_cast<u64>(ex_mem_.instr.imm)};
    pipeline_state_.writeback = {mem_wb_.valid, mem_wb_.instr.pc, mem_wb_.instr.raw,
                                 mem_wb_.instr.rd, mem_wb_.instr.rs1, mem_wb_.instr.rs2,
                                 static_cast<u64>(mem_wb_.instr.imm)};
}

}  // namespace riscv

