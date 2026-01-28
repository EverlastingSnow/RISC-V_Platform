#pragma once

#include "riscv/types.h"

namespace riscv {

// M 模式 CSR 地址（供 MRET 等使用）
constexpr u32 CSR_MEPC = 0x341;

// Zicsr: 控制与状态寄存器。实现 M 模式常用 CSR，供 CSR 指令与 MRET 使用。
class CSR {
public:
    CSR();

    void reset();

    // 读 CSR：addr 为 12 位 CSR 地址。未实现或只写寄存器返回 0。
    [[nodiscard]] u64 read(u32 addr) const;

    // 写 CSR：addr 为 12 位地址，value 为写入值。只读或未实现则忽略。
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
    // 可选：mcycle/minstret 等，此处不实现
};

}  // namespace riscv
