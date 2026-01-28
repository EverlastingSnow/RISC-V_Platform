#include "riscv/csr.h"

namespace riscv {

// RISC-V 特权规范中的 M 模式 CSR 地址（12 位）
namespace CsrAddr {
constexpr u32 MSTATUS = 0x300;
constexpr u32 MISA = 0x301;
constexpr u32 MIE = 0x304;
constexpr u32 MTVEC = 0x305;
constexpr u32 MSCRATCH = 0x340;
constexpr u32 MEPC = 0x341;
constexpr u32 MCAUSE = 0x342;
constexpr u32 MTVAL = 0x343;
constexpr u32 MIP = 0x344;
}  // namespace CsrAddr

CSR::CSR() {
    reset();
}

void CSR::reset() {
    mstatus_ = 0;
    mie_ = 0;
    mtvec_ = 0;
    mscratch_ = 0;
    mepc_ = 0;
    mcause_ = 0;
    mip_ = 0;
}

bool CSR::is_implemented(u32 addr) const {
    switch (addr) {
        case CsrAddr::MSTATUS:
        case CsrAddr::MIE:
        case CsrAddr::MTVEC:
        case CsrAddr::MSCRATCH:
        case CsrAddr::MEPC:
        case CsrAddr::MCAUSE:
        case CsrAddr::MIP:
            return true;
        case CsrAddr::MISA:
            return true;  // 只读，返回 MISA 值
        case CsrAddr::MTVAL:
            return true;  // 可读可写，简单实现
        default:
            return false;
    }
}

u64 CSR::read(u32 addr) const {
    switch (addr) {
        case CsrAddr::MSTATUS:
            return mstatus_;
        case CsrAddr::MISA:
            // RV64IM: M=0x40 (M extension), I=0x1, 64->0x60 in MXL
            return (2ULL << 62) | (0x41ULL << 0);  // MXL=2 (64), M+I
        case CsrAddr::MIE:
            return mie_;
        case CsrAddr::MTVEC:
            return mtvec_;
        case CsrAddr::MSCRATCH:
            return mscratch_;
        case CsrAddr::MEPC:
            return mepc_;
        case CsrAddr::MCAUSE:
            return mcause_;
        case CsrAddr::MTVAL:
            return 0;  // 简化：不维护 mtval
        case CsrAddr::MIP:
            return mip_;
        default:
            return 0;
    }
}

void CSR::write(u32 addr, u64 value) {
    switch (addr) {
        case CsrAddr::MSTATUS:
            mstatus_ = value;
            break;
        case CsrAddr::MIE:
            mie_ = value;
            break;
        case CsrAddr::MTVEC:
            mtvec_ = value;
            break;
        case CsrAddr::MSCRATCH:
            mscratch_ = value;
            break;
        case CsrAddr::MEPC:
            mepc_ = value & ~1ULL;  // 最低位保留为 0（对齐）
            break;
        case CsrAddr::MCAUSE:
            mcause_ = value;
            break;
        case CsrAddr::MIP:
            mip_ = value;
            break;
        case CsrAddr::MISA:
        case CsrAddr::MTVAL:
            // MISA 只读；MTVAL 此处不维护
            break;
        default:
            break;
    }
}

}  // namespace riscv
