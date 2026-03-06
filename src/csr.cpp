#include "riscv/csr.h"
#include <iostream>

namespace riscv {

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

constexpr u32 SSTATUS = 0x100;
constexpr u32 SSCRATCH = 0x140;
constexpr u32 SEPC = 0x141;
constexpr u32 SCAUSE = 0x142;
constexpr u32 STVAL = 0x143;
constexpr u32 STVEC = 0x105;
}  // namespace CsrAddr

CSR::CSR() {
    reset();
}

void CSR::reset() {
    mstatus_ = 0x200000000ULL;  // MBE = 1 (Machine Big-Endian)
    mstatus_ |= (2ULL << 32);   // UXL = 2 (64-bit mode)
    mstatus_ |= (3ULL << 11);   // MPP = 3 (Machine mode)
    mie_ = 0;
    mtvec_ = 0;
    mscratch_ = 0;
    mepc_ = 0;
    mcause_ = 0;
    mip_ = 0;

    sstatus_ = 0;
    sscratch_ = 0;
    sepc_ = 0;
    scause_ = 0;
    stval_ = 0;
    stvec_ = 0;
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
            return true;
        case CsrAddr::MTVAL:
            return true;
        case CsrAddr::SSTATUS:
        case CsrAddr::SSCRATCH:
        case CsrAddr::SEPC:
        case CsrAddr::SCAUSE:
        case CsrAddr::STVAL:
        case CsrAddr::STVEC:
            return true;
        default:
            return false;
    }
}

u64 CSR::read(u32 addr) const {
    switch (addr) {
        case CsrAddr::MSTATUS:
            return mstatus_;
        case CsrAddr::MISA:
            return (2ULL << 62) | (0x41ULL << 0);
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
            return mtval_;
        case CsrAddr::MIP:
            return mip_;
        case CsrAddr::SSTATUS:
            return mstatus_ & 0x800000030001DE00ULL;  // SSTATUS 是 MSTATUS 的子集
        case CsrAddr::SSCRATCH:
            return sscratch_;
        case CsrAddr::SEPC:
            return sepc_;
        case CsrAddr::SCAUSE:
            return scause_;
        case CsrAddr::STVAL:
            return stval_;
        case CsrAddr::STVEC:
            return stvec_;
        default:
            return 0;
    }
}

void CSR::write(u32 addr, u64 value) {
    switch (addr) {
        case CsrAddr::MSTATUS:
            mstatus_ = (value & ~0x200000000ULL) | (mstatus_ & 0x200000000ULL);  // MBE is read-only
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
            mepc_ = value & ~1ULL;
            break;
        case CsrAddr::MCAUSE:
            mcause_ = value;
            break;
        case CsrAddr::MIP:
            mip_ = value;
            break;
        case CsrAddr::MISA:
            break;
        case CsrAddr::MTVAL:
            mtval_ = value;
            break;
        case CsrAddr::SSTATUS:
            sstatus_ = value;
            mstatus_ = (mstatus_ & ~0x800000030001DE00ULL) | (value & 0x800000030001DE00ULL);
            break;
        case CsrAddr::SSCRATCH:
            sscratch_ = value;
            break;
        case CsrAddr::SEPC:
            sepc_ = value & ~1ULL;
            break;
        case CsrAddr::SCAUSE:
            scause_ = value;
            break;
        case CsrAddr::STVAL:
            stval_ = value;
            break;
        case CsrAddr::STVEC:
            stvec_ = value;
            break;
        default:
            break;
    }
}

bool CSR::has_pending_interrupt() const {
    constexpr u64 MSTATUS_MIE = 1ULL << 3;
    bool mie_enabled = (mstatus_ & MSTATUS_MIE) != 0;
    if (!mie_enabled) return false;
    
    u64 pending = mip_ & mie_;
    return pending != 0;
}

u64 CSR::get_interrupt_cause() const {
    u64 pending = mip_ & mie_;
    if (pending == 0) return 0;
    
    for (int i = 0; i < 64; ++i) {
        if (pending & (1ULL << i)) {
            return i;
        }
    }
    return 0;
}

}  // namespace riscv
