#pragma once

#include <cstdint>

#include "riscv/decoder.h"

namespace riscv {

struct IFID {
    bool valid{false};
    u64 pc{0};
    u32 inst{0};
};

struct IDEX {
    bool valid{false};
    DecodedInstruction instr{};
    u64 rs1_value{0};
    u64 rs2_value{0};
};

struct EXMEM {
    bool valid{false};
    DecodedInstruction instr{};
    u64 alu_result{0};
    u64 rs2_value{0};
    bool branch_taken{false};
    u64 branch_target{0};
    bool csr_write{false};
    u32 csr_addr{0};
    u64 csr_new_val{0};
};

struct MEMWB {
    bool valid{false};
    DecodedInstruction instr{};
    u64 wb_value{0};
    u64 mem_addr{0};  // 保存 store/load 的地址，用于 store-forwarding
    u64 store_data{0};  // 保存 store 的数据，用于 store-forwarding
    bool csr_write{false};
    u32 csr_addr{0};
    u64 csr_new_val{0};
};

struct StageSignals {
    bool valid{false};
    u64 pc{0};
    u32 inst{0};
    u32 rd{0};
    u32 rs1{0};
    u32 rs2{0};
    u64 imm{0};
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


