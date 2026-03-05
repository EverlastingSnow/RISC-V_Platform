#pragma once

#include "riscv/types.h"

namespace riscv {

constexpr u32 CSR_MEPC = 0x341;
constexpr u32 CSR_STP = 0x5B;

constexpr u32 CSR_SSTATUS = 0x100;
constexpr u32 CSR_SSCRATCH = 0x140;
constexpr u32 CSR_SEPC = 0x141;
constexpr u32 CSR_SCAUSE = 0x142;
constexpr u32 CSR_STVAL = 0x143;
constexpr u32 CSR_STVEC = 0x105;

class CSR {
public:
    CSR();

    void reset();

    [[nodiscard]] u64 read(u32 addr) const;

    void write(u32 addr, u64 value);

    [[nodiscard]] bool is_implemented(u32 addr) const;

private:
    u64 mstatus_{0};
    u64 mie_{0};
    u64 mtvec_{0};
    u64 mscratch_{0};
    u64 mepc_{0};
    u64 mcause_{0};
    u64 mip_{0};

    u64 sstatus_{0};
    u64 sscratch_{0};
    u64 sepc_{0};
    u64 scause_{0};
    u64 stval_{0};
    u64 stvec_{0};
};

}  // namespace riscv
