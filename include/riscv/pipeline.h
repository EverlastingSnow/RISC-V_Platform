#pragma once

#include <cstdint>

#include "riscv/decoder.h"

namespace riscv {

struct IFID {
    bool valid{false};
    u32 pc{0};
    u32 inst{0};
};

struct IDEX {
    bool valid{false};
    DecodedInstruction instr{};
    u32 rs1_value{0};
    u32 rs2_value{0};
};

struct EXMEM {
    bool valid{false};
    DecodedInstruction instr{};
    u32 alu_result{0};
    u32 rs2_value{0};
    bool branch_taken{false};
    u32 branch_target{0};
};

struct MEMWB {
    bool valid{false};
    DecodedInstruction instr{};
    u32 wb_value{0};
};

struct StageSignals {
    bool valid{false};
    u32 pc{0};
    u32 inst{0};
    u32 rd{0};
    u32 rs1{0};
    u32 rs2{0};
    u32 imm{0};
};

struct PipelineState {
    StageSignals fetch;
    StageSignals decode;
    StageSignals execute;
    StageSignals memory;
    StageSignals writeback;
    u64 cycle{0};
};

}  // namespace riscv


