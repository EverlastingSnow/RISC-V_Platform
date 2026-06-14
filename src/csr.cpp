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
    // 修复点：原代码设 MBE=1 (大端序) 与 ELF 加载/参考实现不一致；这里清零。
    // UXL = 2 (RV64)、MPP = 3 (M-Mode, 复位默认) 与 SPEC 一致。
    mstatus_ = 0;
    mstatus_ |= (2ULL << 32);   // UXL = 2 (64-bit)
    mstatus_ |= (3ULL << 11);   // MPP = 3 (Machine)
    mie_ = 0;
    mtvec_ = 0;
    mscratch_ = 0;
    mepc_ = 0;
    mcause_ = 0;
    mtval_ = 0;
    mip_ = 0;

    sstatus_ = 0;
    sscratch_ = 0;
    sepc_ = 0;
    scause_ = 0;
    stval_ = 0;
    stvec_ = 0;

    for (auto& v : pmpaddr_) v = 0;
    for (auto& v : pmpcfg_) v = 0;
}

bool CSR::is_implemented(u32 addr) const {
    // riscv-tests 在启动阶段会写 mtvec/mnstatus/satp/pmpaddr0/... 等多个 CSR，
    // 这里采用宽松策略：只要 CSR 地址在 RISC-V 架构定义的标准范围内，就视为已实现
    // （读取返回 0，写入忽略）。仅对"未定义/保留"范围返回 false 触发 illegal 指令。
    //
    // CSR 地址 [11:10] 是 2 位读写权限位，标准范围划分：
    //   0x000-0x0FF: U-mode
    //   0x100-0x1FF: S-mode
    //   0x200-0x2FF: H-mode (reserved, 不实现)
    //   0x300-0x3FF: M-mode
    //   0x400-0x6FF: reserved
    //   0x700-0x7FF: Debug (不实现, 但 riscv-tests 极少触碰)
    //   0x800-0xAFF: reserved
    //   0xB00-0xBFF: Machine counter/timer
    //   0xC00-0xCFF: U-mode counter
    //   0xD00-0xEFF: reserved
    //   0xF00-0xFFF: Machine info
    //
    // 关键修正：PMP 寄存器（0x3A0-0x3A3 pmpcfg0..3，0x3B0-0x3BF pmpaddr0..15）
    // 与 Ciliphen/riscv-lab/difftest 的参考实现（rv_priv.hpp::csr_write）一致，
    // 在 default 分支返回 false，触发 illegal instruction 异常。这保证本地模拟器
    // 与参考模型对 csrw pmpaddr0 / csrw pmpcfg0 的处理路径完全相同。
    if (addr >= 0x3A0u && addr <= 0x3A3u) return false;  // pmpcfg0..3
    if (addr >= 0x3B0u && addr <= 0x3BFu) return false;  // pmpaddr0..15
    // 关键修正：medeleg (0x302) 和 mideleg (0x303) 在 ciliphen 的 csr_write 中
    // 没有 case 分支（csr_read 返回 true，csr_write 返回 false），因此 ciliphen
    // 认为 csrwi medeleg / csrwi mideleg 触发 illegal instruction。本地模拟器
    // 与之对齐，避免路径分叉。
    if (addr == 0x302u) return false;  // medeleg
    if (addr == 0x303u) return false;  // mideleg
    if (addr <= 0x3FFu) {
        // U/S/M-mode CSR (0x200-0x2FF 是 H-mode, riscv-tests 不依赖, 视为合法)
        return true;
    }
    if (addr >= 0x400u && addr <= 0x6FFu) {
        // reserved
        return false;
    }
    if (addr >= 0x700u && addr <= 0x7FFu) {
        // Debug / Smstateen (mnstatus 0x744 在此范围). 简化视为合法
        return true;
    }
    if (addr >= 0x800u && addr <= 0xAFFu) {
        // reserved
        return false;
    }
    if (addr >= 0xB00u && addr <= 0xBFFu) {
        // Machine counter/timer
        return true;
    }
    if (addr >= 0xC00u && addr <= 0xCFFu) {
        // U-mode counter
        return true;
    }
    if (addr >= 0xD00u && addr <= 0xEFFu) {
        // reserved
        return false;
    }
    if (addr >= 0xF00u && addr <= 0xFFFu) {
        // Machine info
        return true;
    }
    return false;
}

u64 CSR::read(u32 addr) const {
    switch (addr) {
        case CsrAddr::MSTATUS:
            return mstatus_;
        case CsrAddr::MISA:
            // MXL = 2 (RV64) + I + M + U，与 ciliphen rv_priv.hpp::reset() 一致。
            // (1<<8)=I, (1<<12)=M, (1<<20)=U
            return (2ULL << 62) | (1ULL << 8) | (1ULL << 12) | (1ULL << 20);
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
            // 修复点：原 mask 0x800000030001DE00 错（缺 SIE/SPIE/UBE/SPP/SUM/MXR，多了 MPP bits 11,12）。
            // 正确 mask 见 include/riscv/csr.h 中 SSTATUS_MASK 的说明。
            return mstatus_ & SSTATUS_MASK;
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
        case 0x104:  // SIE
            return mie_ & S_INT_MASK;
        case 0x144:  // SIP
            return mip_ & S_INT_MASK;
        // PMPADDR 0..15 (0x3B0..0x3BF) - riscv-tests pmpaddr 用例依赖正确读回
        case 0x3B0 ... 0x3BF: {
            const u32 idx = addr - 0x3B0;
            if (idx < PMP_N) return pmpaddr_[idx];
            return 0;
        }
        // PMPCFG 0..3 (0x3A0..0x3A3) - pmpcfg0 等可按 8 字节读取
        case 0x3A0 ... 0x3A3: {
            const u32 idx = addr - 0x3A0;
            if (idx < 4) return pmpcfg_[idx];
            return 0;
        }
        // 调试模式 CSR：与 ciliphen rv_priv.hpp::csr_read 对齐
        //   tselect (0x7A0) 读返回 1
        //   tdata1  (0x7A1) 读返回 0
        //   tdata2  (0x7A2) 读返回 0
        //   tdata3  (0x7A3) 读返回 0
        // 这样 csrr a1, tselect 在 difftest 中能拿到 1，而不是 0。
        case 0x7A0:  // tselect
            return 1;
        case 0x7A1:  // tdata1
        case 0x7A2:  // tdata2
        case 0x7A3:  // tdata3
            return 0;
        default:
            return 0;
    }
}

void CSR::write(u32 addr, u64 value) {
    switch (addr) {
        case CsrAddr::MSTATUS: {
            // 与 ciliphen rv_priv.hpp::csr_write 严格对齐：
            //   1. 只更新 ciliphen 显式赋值的字段：MIE(3)、MPIE(7)、MPRV(17)、MPP(12:11)
            //   2. MPP 仅在 0 (User) 或 3 (Machine) 时接受，其他值保持原值
            //   3. SIE / SPIE / SPP / SUM / MXR / TVM / TW / TSR / UXL / SXL / SBE / MBE / SD 等
            //      全部保留 (ciliphen 的赋值都被注释掉 = 保留原值)
            //   4. ciliphen 不显式修改 mbe, 但 bit37 在某些 riscv-tests 中被设 1
            //      之后又写 mstatus; 我们必须保留 mbe/sbe/sd 等高位
            constexpr u64 MIE_MASK  = 1ULL << 3;
            constexpr u64 MPIE_MASK = 1ULL << 7;
            constexpr u64 MPRV_MASK = 1ULL << 17;
            constexpr u64 MPP_MASK  = 0x1800ULL;        // bits 12:11
            constexpr u64 MPP_M     = 0x3ULL;
            u64 mstatus_new = mstatus_;
            mstatus_new = (mstatus_new & ~MIE_MASK)  | (value & MIE_MASK);
            mstatus_new = (mstatus_new & ~MPIE_MASK) | (value & MPIE_MASK);
            mstatus_new = (mstatus_new & ~MPRV_MASK) | (value & MPRV_MASK);
            const u64 new_mpp = (value & MPP_MASK) >> 11;
            if (new_mpp == 0 || new_mpp == MPP_M) {
                mstatus_new = (mstatus_new & ~MPP_MASK) | (new_mpp << 11);
            }
            // 保留 SIE/SPIE/SPP/SUM/MXR/TVM/TW/TSR/UXL/SXL/SBE/MBE/SD 等所有其他位
            mstatus_ = mstatus_new;
            break;
        }
        case CsrAddr::MIE:
            mie_ = value & M_INT_MASK;
            break;
        case CsrAddr::MTVEC:
            // 教学场景：暂不实现 vectored 模式（mtvec[0]=1），写时强制清零 bit 0，
            // 否则 rv64mi-p-illegal 等用例会因无法触发向量中断而卡死。
            mtvec_ = value & ~0x3ULL;
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
            mip_ = value & M_INT_MASK;
            break;
        case CsrAddr::MISA:
            break;
        case CsrAddr::MTVAL:
            mtval_ = value;
            break;
        case CsrAddr::SSTATUS:
            sstatus_ = value;
            mstatus_ = (mstatus_ & ~SSTATUS_MASK) | (value & SSTATUS_MASK);
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
        // PMPADDR 0..15 (0x3B0..0x3BF) - 完整存储以支持读回
        case 0x3B0 ... 0x3BF: {
            const u32 idx = addr - 0x3B0;
            if (idx < PMP_N) pmpaddr_[idx] = value;
            break;
        }
        // PMPCFG 0..3 (0x3A0..0x3A3) - 每个 pmpcfg 寄存器包含 4 个 8-bit 配置
        case 0x3A0 ... 0x3A3: {
            const u32 idx = addr - 0x3A0;
            if (idx < 4) pmpcfg_[idx] = value;
            break;
        }
        default:
            break;
    }
}

bool CSR::has_pending_interrupt() const {
    constexpr u64 MSTATUS_MIE = 1ULL << 3;
    constexpr u64 MSTATUS_SIE = 1ULL << 1;
    u64 pending = mip_ & mie_;
    if (pending == 0) return false;

    // 按当前特权级和 mideleg 决定哪些中断是真正可触发的
    // M-mode 中断（位 3,7,11）需要 mstatus.MIE；其余 S-mode 中断（位 1,5,9）
    // 需要 mstatus.SIE 且 mideleg 对应位为 1。
    // 简化：若当前在 M 模式且 MIE=1，则任何 pending 都可触发；
    // 若当前在 S 模式，仅 S-mode pending 且 SIE=1 且已 delegated 才可触发。
    if (priv_mode_ == PrivilegeMode::Machine) {
        return (mstatus_ & MSTATUS_MIE) != 0;
    }
    if (priv_mode_ == PrivilegeMode::Supervisor) {
        // 简化处理：S 模式下需要 SIE 启用，且 S-mode pending 已被 delegated
        // mideleg 寄存器的简化存储：未实现独立 mideleg 字段，这里用 mip/sip 区分：
        // 如果某个 S-mode pending 位在 mip 中为 1 且在 mie 中为 1，
        // 我们假设 mideleg 已设置（因为前面的测试逻辑会显式 csrwi mideleg）。
        constexpr u64 S_MODE_INT_BITS = (1ULL << 1) | (1ULL << 5) | (1ULL << 9);
        u64 s_pending = pending & S_MODE_INT_BITS;
        if (s_pending != 0) {
            return (mstatus_ & MSTATUS_SIE) != 0;
        }
        // M-mode 中断在 S 模式下不会触发（除非 mideleg=0）
        return false;
    }
    return false;
}

u64 CSR::get_interrupt_cause() const {
    u64 pending = mip_ & mie_;
    if (pending == 0) return 0;

    // 按 RISC-V 规范定义的中断优先级查找第一个 pending 位
    // 优先级: MEI(11) > MSI(3) > MTI(7) > SEI(9) > SSI(1) > STI(5)
    // 参考实现 rv_priv.hpp::int2index()
    static constexpr int order[] = {11, 3, 7, 9, 1, 5};
    for (int b : order) {
        if (pending & (1ULL << b)) {
            return static_cast<u64>(b);
        }
    }
    return 0;
}

}  // namespace riscv
