#pragma once

#include <cstdint>
#include <optional>

#include "riscv/decoder.h"

namespace riscv {

struct UserControlSignals {
    std::optional<bool> reg_write{std::nullopt};
    std::optional<bool> alu_src{std::nullopt};
    std::optional<bool> mem_read{std::nullopt};
    std::optional<bool> mem_write{std::nullopt};
    std::optional<bool> branch{std::nullopt};
    
    bool has_any() const {
        return reg_write || alu_src || mem_read || mem_write || branch;
    }
    
    void clear() {
        reg_write = std::nullopt;
        alu_src = std::nullopt;
        mem_read = std::nullopt;
        mem_write = std::nullopt;
        branch = std::nullopt;
    }
};

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
    UserControlSignals user_signals{};
};

struct EXMEM {
    bool valid{false};
    DecodedInstruction instr{};
    u64 alu_result{0};
    u64 alu_src1{0};
    u64 alu_src2{0};
    u64 rs2_value{0};
    bool branch_taken{false};
    u64 branch_target{0};
    bool csr_write{false};
    u32 csr_addr{0};
    u64 csr_new_val{0};
    UserControlSignals user_signals{};
};

struct MEMWB {
    bool valid{false};
    DecodedInstruction instr{};
    u64 wb_value{0};
    u64 mem_addr{0};
    u64 store_data{0};
    bool csr_write{false};
    u32 csr_addr{0};
    u64 csr_new_val{0};
    UserControlSignals user_signals{};
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
