#pragma once

#include "riscv/types.h"

namespace riscv {

constexpr u32 CSR_MEPC = 0x341;
constexpr u32 CSR_MCAUSE = 0x342;
constexpr u32 CSR_MTVAL = 0x343;
constexpr u32 CSR_MTVEC = 0x305;
constexpr u32 CSR_MSTATUS = 0x300;
constexpr u32 CSR_MIE = 0x304;
constexpr u32 CSR_MIP = 0x344;
constexpr u32 CSR_MISA = 0x301;
constexpr u32 CSR_STP = 0x5B;

constexpr u32 CSR_SSTATUS = 0x100;
constexpr u32 CSR_SSCRATCH = 0x140;
constexpr u32 CSR_SEPC = 0x141;
constexpr u32 CSR_SCAUSE = 0x142;
constexpr u32 CSR_STVAL = 0x143;
constexpr u32 CSR_STVEC = 0x105;
constexpr u32 CSR_SIE = 0x104;
constexpr u32 CSR_SIP = 0x144;

// Machine-mode interrupt mask: bits 1 (SSI), 3 (MSI), 5 (STI), 7 (MTI), 9 (SEI), 11 (MEI).
// 对应参考实现 rv_common.hpp 中的 m_int_mask，限制 mie/mip 只能写入这些位。
constexpr u64 M_INT_MASK = (1ULL << 1) | (1ULL << 3) | (1ULL << 5) |
                           (1ULL << 7) | (1ULL << 9) | (1ULL << 11);

// Supervisor-mode interrupt mask: bits 1 (SSI), 5 (STI), 9 (SEI).
// 对应参考实现 rv_common.hpp 中的 s_int_mask，sie/sip 读取时只透传这些位。
constexpr u64 S_INT_MASK = (1ULL << 1) | (1ULL << 5) | (1ULL << 9);

// sstatus 字段在 mstatus 中的位掩码（来自 riscv-spec 2024 与参考 csr_sstatus_def）。
// bit 1 (SIE), 5 (SPIE), 6 (UBE), 8 (SPP), 9-10 (VS), 13-14 (FS), 15-16 (XS),
// 18 (SUM), 19 (MXR), 32-33 (UXL), 63 (SD)。
constexpr u64 SSTATUS_MASK = 0x80000003000DE762ULL;

enum class PrivilegeMode {
    User = 0,
    Supervisor = 1,
    Machine = 3
};

class CSR {
public:
    CSR();

    void reset();

    [[nodiscard]] u64 read(u32 addr) const;

    void write(u32 addr, u64 value);

    [[nodiscard]] bool is_implemented(u32 addr) const;
    
    [[nodiscard]] bool has_pending_interrupt() const;
    
    [[nodiscard]] u64 get_interrupt_cause() const;

    [[nodiscard]] PrivilegeMode privilege_mode() const { return priv_mode_; }
    void set_privilege_mode(PrivilegeMode mode) { priv_mode_ = mode; }

private:
    u64 mstatus_{0};
    u64 mie_{0};
    u64 mtvec_{0};
    u64 mscratch_{0};
    u64 mepc_{0};
    u64 mcause_{0};
    u64 mtval_{0};
    u64 mip_{0};

    u64 sstatus_{0};
    u64 sscratch_{0};
    u64 sepc_{0};
    u64 scause_{0};
    u64 stval_{0};
    u64 stvec_{0};

    // PMP 寄存器（PMPADDR 0..15 + PMPCFG 0..3）。riscv-tests 的 pmpaddr 用例
    // 需要能正确读回写入的值，因此这里提供完整的 16 个 PMPADDR 和 4 个
    // PMPCFG 的存储（采用 NAPOT/GRAIN 无关的统一 u64 表示，写入即存）。
    // PMP 的功能检查（地址匹配）暂不实现——这与 riscv-tests 的 pmpaddr
    // 子测试无关。
    static constexpr std::size_t PMP_N = 16;
    u64 pmpaddr_[PMP_N]{};
    u64 pmpcfg_[4]{};

    PrivilegeMode priv_mode_{PrivilegeMode::Machine};
};

}  // namespace riscv
