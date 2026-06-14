#pragma once

#include <vector>
#include <optional>

#include "riscv/csr.h"
#include "riscv/memory.h"
#include "riscv/pipeline.h"
#include "riscv/register_file.h"

namespace riscv {

enum class HaltReason {
    None,
    Ecall,
    EcallExit,           // riscv-tests 退出语义 (a7=93)
    Ebreak,
    InvalidInstruction,
    MisalignedAccess,    // 取指 PC 未 4 字节对齐
};

class RISCVSimulator {
public:
    RISCVSimulator();

    void load_program(const std::vector<u8>& binary, u64 offset = 0);
    void reset();
    void step();
    void run(u32 cycles);

    // 设置 riscv-tests 的 tohost 内存地址（非零时启用停机检测）
    void set_tohost_address(u64 addr) { tohost_address_ = addr; }

    [[nodiscard]] const RegisterFile& registers() const { return regs_; }
    [[nodiscard]] const Memory& memory() const { return memory_; }
    [[nodiscard]] Memory& memory() { return memory_; }
    [[nodiscard]] const PipelineState& pipeline_state() const { return pipeline_state_; }
    [[nodiscard]] u64 cycle() const { return cycle_; }
    [[nodiscard]] u64 pc() const { return pc_; }
    [[nodiscard]] bool halted() const { return halted_; }
    [[nodiscard]] HaltReason halt_reason() const { return halt_reason_; }
    [[nodiscard]] bool halt_reason_ecall() const {
        return halt_reason_ == HaltReason::Ecall ||
               halt_reason_ == HaltReason::EcallExit;
    }
    [[nodiscard]] bool halt_reason_ebreak() const {
        return halt_reason_ == HaltReason::Ebreak;
    }
    [[nodiscard]] u64 halt_pc() const { return halt_pc_; }
    [[nodiscard]] u32 halt_inst() const { return halt_inst_; }

    [[nodiscard]] const IFID& if_id() const { return if_id_; }
    [[nodiscard]] const IDEX& id_ex() const { return id_ex_; }
    [[nodiscard]] const EXMEM& ex_mem() const { return ex_mem_; }
    [[nodiscard]] const MEMWB& mem_wb() const { return mem_wb_; }
    [[nodiscard]] bool stall_fetch() const { return stall_fetch_; }
    [[nodiscard]] bool redirect() const { return redirect_; }
    [[nodiscard]] u64 redirect_target() const { return redirect_target_; }
    [[nodiscard]] bool flush_decode() const { return flush_decode_; }
    [[nodiscard]] bool flush_execute() const { return flush_execute_; }
    [[nodiscard]] u64 next_pc() const { return next_pc_; }

    // Trap 原因枚举：用于教学演示区分中断与异常
    enum class TrapCause { None, Interrupt, Exception };

    // 触发软件中断入口：仅设置 CSR_MIP 的对应 bit，
    // MIE 需由程序显式配置（教学意义：区分 pending vs enable）
    void trigger_pending_interrupt(u64 bit);

    // Trap cause 访问与清零
    [[nodiscard]] TrapCause last_trap_cause() const { return last_trap_cause_; }
    void clear_trap_cause() { last_trap_cause_ = TrapCause::None; }

    // CSR 只读访问
    [[nodiscard]] const CSR& csr() const { return csr_; }
    
    // Pause control for difftest
    void set_waiting_for_input(bool waiting, u64 pc = 0);
    [[nodiscard]] bool is_waiting_for_input() const { return waiting_for_input_; }
    [[nodiscard]] u64 get_waiting_pc() const { return waiting_pc_; }
    void set_waiting_handled(bool handled) { waiting_handled_ = handled; }
    
    // Set user signal for ID stage (will flow through pipeline)
    void set_user_signal_for_id(const std::string& signal_name, bool value);
    void clear_user_signals_for_id();
    
    // WB result for comparison
    struct WBResult {
        bool valid{false};
        u64 pc{0};
        bool wb_en{false};           // Default wb enable (from instruction decode)
        u32 wb_raddr{0};
        u64 wb_rdata{0};
        std::optional<bool> user_reg_write{std::nullopt};  // User's input for RegWrite
        bool actual_wb_en{false};    // Actual wb enable (after applying user signal)
        bool has_user_signal{false}; // True if any user signal was set for this instruction
    };
    WBResult last_wb_result;

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
    CSR csr_;

    IFID if_id_{};
    IDEX id_ex_{};
    EXMEM ex_mem_{};
    MEMWB mem_wb_{};

    IFID next_if_id_{};
    IDEX next_id_ex_{};
    EXMEM next_ex_mem_{};
    MEMWB next_mem_wb_{};

    PipelineState pipeline_state_{};

    u64 pc_{RESET_VECTOR};
    u64 cycle_{0};
    bool halted_{false};
    HaltReason halt_reason_{HaltReason::None};
    u64 halt_pc_{0};
    u32 halt_inst_{0};
    bool stall_fetch_{false};
    bool redirect_{false};
    u64 redirect_target_{0};
    bool flush_decode_{false};
    bool flush_execute_{false};
    u64 next_pc_{RESET_VECTOR};
    
    // Pause control
    bool waiting_for_input_{false};
    u64 waiting_pc_{0};
    bool pending_ebreak_{false};
    bool waiting_handled_{false};

    // EcallExit 延迟提交：EX 阶段检测到 a7=93 时只置位，
    // 等到该 ECALL 走到 WB 阶段、流水线已排空前置指令后再真正停机，
    // 避免读取陈旧的寄存器值（与 forward/写回时机相关）
    bool pending_ecall_exit_{false};
    u64 pending_ecall_halt_pc_{0};
    u32 pending_ecall_halt_inst_{0};

    // EX 阶段异常标志：当异常在 EX 阶段触发时置位，
    // 用于阻止异常指令继续推进到 MEM/WB（避免覆盖正常写回）
    bool exception_taken_{false};

    // Trap cause：每次 trap 时由 stage_if / stage_ex 设置，
    // output_signals_body 输出后由 C++ 端调用 clear_trap_cause() 清零
    TrapCause last_trap_cause_{TrapCause::None};

    // riscv-tests 退出协议：tohost 内存地址。
    // 默认采用 riscv-tests env 链接脚本约定的 0x80001000，
    // 这样前端的汇编演示（无 ELF 符号表）也能通过写 tohost 正常停机；
    // run_riscv_tests / difftest_runner 若解析到 ELF 符号表会覆盖此值。
    u64 tohost_address_{0x80001000ULL};
};

}  // namespace riscv
