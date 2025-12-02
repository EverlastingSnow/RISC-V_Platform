#include "riscv/simulator.h"

#include <optional>

#include "riscv/decoder.h"
#include "riscv/register_file.h"
#include "riscv/types.h"

namespace riscv {
namespace {

bool uses_rs1(InstructionKind kind) {
    switch (kind) {
        case InstructionKind::LUI:
        case InstructionKind::AUIPC:
        case InstructionKind::JAL:
        case InstructionKind::FENCE:
        case InstructionKind::FENCE_I:
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
        case InstructionKind::LBU:
        case InstructionKind::LHU:
        case InstructionKind::SB:
        case InstructionKind::SH:
        case InstructionKind::SW:
        case InstructionKind::ADDI:
        case InstructionKind::SLTI:
        case InstructionKind::SLTIU:
        case InstructionKind::XORI:
        case InstructionKind::ORI:
        case InstructionKind::ANDI:
        case InstructionKind::SLLI:
        case InstructionKind::SRLI:
        case InstructionKind::SRAI:
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

bool is_branch_taken(const DecodedInstruction& instr, u32 lhs, u32 rhs) {
    switch (instr.kind) {
        case InstructionKind::BEQ: return lhs == rhs;
        case InstructionKind::BNE: return lhs != rhs;
        case InstructionKind::BLT: return static_cast<s32>(lhs) < static_cast<s32>(rhs);
        case InstructionKind::BGE: return static_cast<s32>(lhs) >= static_cast<s32>(rhs);
        case InstructionKind::BLTU: return lhs < rhs;
        case InstructionKind::BGEU: return lhs >= rhs;
        default: return false;
    }
}

}  // namespace

RISCVSimulator::RISCVSimulator() : memory_(DEFAULT_MEMORY_SIZE, RESET_VECTOR) {
    reset();
}

void RISCVSimulator::load_program(const std::vector<u8>& binary, u32 offset) {
    memory_.reset();
    memory_.load_program(binary, offset);
    reset();
}

void RISCVSimulator::reset() {
    regs_.reset();
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
    redirect_ = false;
    redirect_target_ = 0;
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
    next_if_id_ = if_id_;
    if (halted_) {
        next_if_id_.valid = false;
        return;
    }

    if (stall_fetch_) {
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
        next_if_id_.valid = false;
        return;
    }

    next_if_id_.valid = true;
    next_if_id_.pc = pc_;
    next_if_id_.inst = memory_.read32(pc_);
    next_pc_ = pc_ + 4;
}

void RISCVSimulator::stage_id() {
    next_id_ex_ = {};
    if (!if_id_.valid) {
        return;
    }

    auto instr = decode(if_id_.inst, if_id_.pc);

    const bool hazard = id_ex_.valid && id_ex_.instr.is_load() && id_ex_.instr.rd != 0 &&
                        ((uses_rs1(instr.kind) && instr.rs1 == id_ex_.instr.rd) ||
                         (uses_rs2(instr.kind) && instr.rs2 == id_ex_.instr.rd));

    if (hazard) {
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

    const auto instr = id_ex_.instr;
    u32 rs1_val = id_ex_.rs1_value;
    u32 rs2_val = id_ex_.rs2_value;

    auto forward_from_ex_mem = [&](u32 reg) -> std::optional<u32> {
        if (!ex_mem_.valid || !ex_mem_.instr.writes_rd() || ex_mem_.instr.is_load()) {
            return std::nullopt;
        }
        if (reg != 0 && ex_mem_.instr.rd == reg) {
            return ex_mem_.alu_result;
        }
        return std::nullopt;
    };

    auto forward_from_mem_wb = [&](u32 reg) -> std::optional<u32> {
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

    u32 alu_result = 0;
    bool branch_taken = false;
    u32 branch_target = 0;

    switch (instr.kind) {
        case InstructionKind::LUI:
            alu_result = instr.imm;
            break;
        case InstructionKind::AUIPC:
            alu_result = instr.pc + instr.imm;
            break;
        case InstructionKind::JAL:
            alu_result = instr.pc + 4;
            branch_taken = true;
            branch_target = instr.pc + instr.imm;
            break;
        case InstructionKind::JALR:
            alu_result = instr.pc + 4;
            branch_taken = true;
            branch_target = (rs1_val + instr.imm) & ~1u;
            break;
        case InstructionKind::BEQ:
        case InstructionKind::BNE:
        case InstructionKind::BLT:
        case InstructionKind::BGE:
        case InstructionKind::BLTU:
        case InstructionKind::BGEU:
            branch_taken = is_branch_taken(instr, rs1_val, rs2_val);
            branch_target = instr.pc + instr.imm;
            break;
        case InstructionKind::LB:
        case InstructionKind::LH:
        case InstructionKind::LW:
        case InstructionKind::LBU:
        case InstructionKind::LHU:
        case InstructionKind::SB:
        case InstructionKind::SH:
        case InstructionKind::SW:
        case InstructionKind::ADDI:
        case InstructionKind::SLTI:
        case InstructionKind::SLTIU:
        case InstructionKind::XORI:
        case InstructionKind::ORI:
        case InstructionKind::ANDI:
        case InstructionKind::SLLI:
        case InstructionKind::SRLI:
        case InstructionKind::SRAI:
            switch (instr.kind) {
                case InstructionKind::LB:
                case InstructionKind::LH:
                case InstructionKind::LW:
                case InstructionKind::LBU:
                case InstructionKind::LHU:
                case InstructionKind::SB:
                case InstructionKind::SH:
                case InstructionKind::SW:
                    alu_result = rs1_val + instr.imm;
                    break;
                case InstructionKind::ADDI:
                    alu_result = rs1_val + instr.imm;
                    break;
                case InstructionKind::SLTI:
                    alu_result = static_cast<s32>(rs1_val) < instr.imm ? 1u : 0u;
                    break;
                case InstructionKind::SLTIU:
                    alu_result = rs1_val < static_cast<u32>(instr.imm) ? 1u : 0u;
                    break;
                case InstructionKind::XORI:
                    alu_result = rs1_val ^ instr.imm;
                    break;
                case InstructionKind::ORI:
                    alu_result = rs1_val | instr.imm;
                    break;
                case InstructionKind::ANDI:
                    alu_result = rs1_val & instr.imm;
                    break;
                case InstructionKind::SLLI:
                    alu_result = rs1_val << (instr.imm & 0x1F);
                    break;
                case InstructionKind::SRLI:
                    alu_result = rs1_val >> (instr.imm & 0x1F);
                    break;
                case InstructionKind::SRAI:
                    alu_result = static_cast<u32>(static_cast<s32>(rs1_val) >> (instr.imm & 0x1F));
                    break;
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
        case InstructionKind::SLL:
            alu_result = rs1_val << (rs2_val & 0x1F);
            break;
        case InstructionKind::SLT:
            alu_result = static_cast<s32>(rs1_val) < static_cast<s32>(rs2_val) ? 1u : 0u;
            break;
        case InstructionKind::SLTU:
            alu_result = rs1_val < rs2_val ? 1u : 0u;
            break;
        case InstructionKind::XOR:
            alu_result = rs1_val ^ rs2_val;
            break;
        case InstructionKind::SRL:
            alu_result = rs1_val >> (rs2_val & 0x1F);
            break;
        case InstructionKind::SRA:
            alu_result = static_cast<u32>(static_cast<s32>(rs1_val) >> (rs2_val & 0x1F));
            break;
        case InstructionKind::OR:
            alu_result = rs1_val | rs2_val;
            break;
        case InstructionKind::AND:
            alu_result = rs1_val & rs2_val;
            break;
        case InstructionKind::FENCE:
        case InstructionKind::FENCE_I:
            alu_result = 0;
            break;
        case InstructionKind::ECALL:
        case InstructionKind::EBREAK:
            // 在EX阶段不立刻停止，只标记结果，让指令顺利流到WB阶段再停机
            alu_result = 0;
            break;
        default:
            // 理论上不应该到这里：将其视为非法指令
            halted_ = true;
            halt_reason_ = HaltReason::InvalidInstruction;
            halt_pc_ = instr.pc;
            halt_inst_ = instr.raw;
            return;
    }

    next_ex_mem_.valid = id_ex_.valid;
    next_ex_mem_.instr = instr;
    next_ex_mem_.alu_result = alu_result;
    next_ex_mem_.rs2_value = rs2_val;
    next_ex_mem_.branch_taken = branch_taken;
    next_ex_mem_.branch_target = branch_target;

    if (branch_taken) {
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

    const auto instr = ex_mem_.instr;
    u32 value = ex_mem_.alu_result;

    if (instr.is_load()) {
        switch (instr.kind) {
            case InstructionKind::LB:
                value = static_cast<u32>(sign_extend<s32>(memory_.read8(ex_mem_.alu_result), 8));
                break;
            case InstructionKind::LH:
                value = static_cast<u32>(sign_extend<s32>(memory_.read16(ex_mem_.alu_result), 16));
                break;
            case InstructionKind::LW:
                value = memory_.read32(ex_mem_.alu_result);
                break;
            case InstructionKind::LBU:
                value = memory_.read8(ex_mem_.alu_result);
                break;
            case InstructionKind::LHU:
                value = memory_.read16(ex_mem_.alu_result);
                break;
            default:
                break;
        }
    } else if (instr.is_store()) {
        switch (instr.kind) {
            case InstructionKind::SB:
                memory_.write8(ex_mem_.alu_result, static_cast<u8>(ex_mem_.rs2_value & 0xFF));
                break;
            case InstructionKind::SH:
                memory_.write16(ex_mem_.alu_result, static_cast<u16>(ex_mem_.rs2_value & 0xFFFF));
                break;
            case InstructionKind::SW:
                memory_.write32(ex_mem_.alu_result, ex_mem_.rs2_value);
                break;
            default:
                break;
        }
    }

    next_mem_wb_.valid = ex_mem_.valid;
    next_mem_wb_.instr = instr;
    next_mem_wb_.wb_value = value;
}

void RISCVSimulator::stage_wb() {
    if (!mem_wb_.valid) {
        return;
    }
    if (mem_wb_.instr.writes_rd()) {
        regs_.write(mem_wb_.instr.rd, mem_wb_.wb_value);
    }

    // 在WB阶段“提交”ECALL/EBREAK：等前面的指令都写回后再停机
    if (mem_wb_.instr.is_system()) {
        halted_ = true;
        if (mem_wb_.instr.kind == InstructionKind::ECALL) {
            halt_reason_ = HaltReason::Ecall;
        } else if (mem_wb_.instr.kind == InstructionKind::EBREAK) {
            halt_reason_ = HaltReason::Ebreak;
        }
        halt_pc_ = mem_wb_.instr.pc;
        halt_inst_ = mem_wb_.instr.raw;
    }
}

void RISCVSimulator::update_pipeline_registers() {
    if (redirect_) {
        pc_ = redirect_target_;
        next_if_id_ = {};
        next_id_ex_ = {};
    } else {
        pc_ = next_pc_;
    }

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
    pipeline_state_.fetch = {if_id_.valid, if_id_.pc, if_id_.inst, 0, 0, 0, 0};
    pipeline_state_.decode = {id_ex_.valid, id_ex_.instr.pc, id_ex_.instr.raw, id_ex_.instr.rd,
                              id_ex_.instr.rs1, id_ex_.instr.rs2, static_cast<u32>(id_ex_.instr.imm)};
    pipeline_state_.execute = {ex_mem_.valid, ex_mem_.instr.pc, ex_mem_.instr.raw, ex_mem_.instr.rd,
                               ex_mem_.instr.rs1, ex_mem_.instr.rs2,
                               static_cast<u32>(ex_mem_.instr.imm)};
    pipeline_state_.memory = {mem_wb_.valid, ex_mem_.instr.pc, ex_mem_.instr.raw, ex_mem_.instr.rd,
                              ex_mem_.instr.rs1, ex_mem_.instr.rs2,
                              static_cast<u32>(ex_mem_.instr.imm)};
    pipeline_state_.writeback = {mem_wb_.valid, mem_wb_.instr.pc, mem_wb_.instr.raw,
                                 mem_wb_.instr.rd, mem_wb_.instr.rs1, mem_wb_.instr.rs2,
                                 static_cast<u32>(mem_wb_.instr.imm)};
}

}  // namespace riscv

