#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "riscv/memory.h"
#include "riscv/pipeline.h"
#include "riscv/register_file.h"

namespace riscv {

class RISCVSimulator {
public:
    RISCVSimulator();

    void load_program(const std::vector<u8>& binary, u32 offset = 0);
    void reset();
    void step();
    void run(u32 cycles);

    [[nodiscard]] const RegisterFile& registers() const { return regs_; }
    [[nodiscard]] const Memory& memory() const { return memory_; }
    [[nodiscard]] Memory& memory() { return memory_; }
    [[nodiscard]] const PipelineState& pipeline_state() const { return pipeline_state_; }
    [[nodiscard]] u64 cycle() const { return cycle_; }
    [[nodiscard]] u32 pc() const { return pc_; }

private:
    void stage_if();
    void stage_id();
    void stage_ex();
    void stage_mem();
    void stage_wb();
    void update_pipeline_registers();
    void update_pipeline_state();

    Memory memory_;
    RegisterFile regs_;

    IFID if_id_{};
    IDEX id_ex_{};
    EXMEM ex_mem_{};
    MEMWB mem_wb_{};

    IFID next_if_id_{};
    IDEX next_id_ex_{};
    EXMEM next_ex_mem_{};
    MEMWB next_mem_wb_{};

    PipelineState pipeline_state_{};

    u32 pc_{RESET_VECTOR};
    u64 cycle_{0};
    bool halted_{false};
    bool stall_fetch_{false};
    bool redirect_{false};
    u32 redirect_target_{0};
    bool flush_decode_{false};
    bool flush_execute_{false};
    u32 next_pc_{RESET_VECTOR};
};

}  // namespace riscv

