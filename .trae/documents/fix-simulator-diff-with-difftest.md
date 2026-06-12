# 后端模拟器 vs Ciliphen/riscv-lab difftest/src 差异修复方案

## 背景

将 `/opt/RISC-V_Platform/src/` + `/opt/RISC-V_Platform/include/riscv/`（本地 RISC-V 五级流水线后端模拟器）
与 `https://github.com/Ciliphen/riscv-lab/tree/main/difftest/src`（教学 RISC-V 差分测试参考实现 C++ REF）进行逐项对比。

目前现状：直接运行 `./build/run_riscv_tests --isa rv64ui` 时，**全部 54 个 rv64ui 测试均 TIMEOUT（500000 周期内未停机）**。
根本原因：本地模拟器对 riscv-tests 的退出机制不感知（ECALL 仅 trap 到 mtvec 而不 halt；TOHOST 写入也无特殊处理）。

## 用户确认的修复范围

- **优先级 1（本次必做）**：解除测试阻塞 + CSR / 异常 / 中断相关正确性
- **不做**：Sv39 分页、AMO 原子指令、C 扩展、Counter CSR（mcycle/minstret 等）
- **退出机制**：采用 `ECALL a7=93 → halt with exit code`（与参考实现一致）

## 一、本地代码现状要点

`include/riscv/simulator.h` 中 `RISCVSimulator` 已是 5 级流水线（IF/ID/EX/MEM/WB）。
`HaltReason` 目前有 `None / Ecall / Ebreak / InvalidInstruction`。
模拟器入口 `src/main.cpp` 用 `load_program + run(cycles)`；测试入口 `src/run_riscv_tests.cpp`
依赖 `sim.halted()` + `sim.halt_reason_ecall()` + `a0==0` 判定 PASS。

---

## 二、待修改文件清单

| 文件 | 主要改动 |
| --- | --- |
| `include/riscv/csr.h` | 新增 MISA / 异常原因枚举 / 中断 mask 常量；扩展 mstatus 字段 |
| `src/csr.cpp` | 修复 mstatus 初值、sstatus mask、mip/mie 掩码、中断优先级、SRET/MRET、MISA |
| `src/simulator.cpp` | ECALL 退出逻辑、ECALL 异常原因按特权级、MRET mpp 复位、SRET 完整实现、PC 对齐检查、misalign trap 入口 |
| `include/riscv/simulator.h` | 新增 `HaltReason::EcallExit`，新增 misalign trap 状态 |
| `src/decoder.cpp` | 若要支持 trap 路径，可能要区分 EBREAK vs ECALL（已区分，无需改） |
| `include/riscv/pipeline.h` | 视需要补充 `EXMEM` 中 `exception_taken` 字段（可选） |

> 暂不修改：`memory.cpp`（无分页需求）、`register_file.cpp`（行为已正确）、`decoder.cpp`（指令解码已完整）、
> `run_riscv_tests.cpp`（已能识别 Ecall halt）、`riscv_sim_server.cpp`（API 端无需变）。

---

## 三、详细修改点

### 3.1 【关键】ECALL 退出机制 —— `src/simulator.cpp`

**问题**：当前 ECALL 把 PC 重定向到 mtvec（=0），随后跳转出物理内存导致 InvalidInstruction 卡死；
riscv-tests 的 _start 是 `ecall`，需要识别"退出语义"。

**做法**（参考 `sim_mycpu.cpp` 第 711-735 行 + `rv_core.hpp` 第 808-833 行）：

在 `stage_ex()` 的 `case InstructionKind::ECALL` 分支中，先读 `a7 = regs_.read(17)`：
- 若 `a7 == 93`（POSIX exit）：
  - 读 `a0 = regs_.read(10)`
  - 写 `halt_reason_ = HaltReason::EcallExit`
  - 写 `halt_pc_ = instr.pc`，`halt_inst_ = instr.raw`
  - 设 `halted_ = true`
  - **不**走 trap 重定向
- 否则：保持现有 trap 到 mtvec 行为（mcause 改为按特权级计算，见 3.2）

```cpp
case InstructionKind::ECALL: {
    const u64 a7 = regs_.read(17);
    if (a7 == 93) {                       // 退出 syscall
        const u64 a0 = regs_.read(10);
        (void)a0;                          // 调用方在 run_riscv_tests 中读 sim.registers().raw()[10]
        halted_ = true;
        halt_reason_ = HaltReason::EcallExit;
        halt_pc_ = instr.pc;
        halt_inst_ = instr.raw;
        alu_result = 0;
        break;
    }
    // —— 以下保持原 trap 行为，但用 cur_priv 决定 mcause ——
    csr_.write(CSR_MEPC, instr.pc + 4);
    const u64 priv = static_cast<u64>(csr_.privilege_mode());
    csr_.write(CSR_MCAUSE, priv + 8);      // 8=U, 9=S, 11=M
    csr_.write(CSR_MTVAL, 0);
    // ... 现有 mstatus 更新 + redirect 到 mtvec ...
}
```

### 3.2 【关键】ECALL 异常原因按特权级

`src/simulator.cpp` 当前 `csr_.write(CSR_MCAUSE, 11)` 硬编码。
改为：`(CSR_MCAUSE, cur_priv + 8)`（参考 `rv_priv.hpp::ecall()` 行 622-628）。
`CSR` 类已有 `privilege_mode()`，直接调用。

### 3.3 【关键】`HaltReason` 增加 `EcallExit`

`include/riscv/simulator.h`：
```cpp
enum class HaltReason {
    None,
    Ecall,
    EcallExit,          // 新增：riscv-tests 退出语义
    Ebreak,
    InvalidInstruction,
    MisalignedAccess,   // 新增：可选
};
```

> 之所以单独加 `EcallExit` 而不是复用 `Ecall`，
> 是因为 `run_riscv_tests.cpp` 仍会读 `sim.halt_reason_ecall()` 判定 PASS。
> 为避免与"普通 ECALL trap"语义混淆，新增一个值；并把 `halt_reason_ecall()` 视为 `Ecall || EcallExit`。

### 3.4 【正确性】`mstatus` 复位值

`src/csr.cpp::CSR::reset()` 当前：
```cpp
mstatus_ = 0x200000000ULL;  // MBE = 1 (Machine Big-Endian)  ← 错
mstatus_ |= (2ULL << 32);   // UXL = 2  (RV64)
mstatus_ |= (3ULL << 11);   // MPP = 3  (M-Mode)
```

**问题**：MBE=1 表示机器是大端序，与 ELF 加载和参考实现（MBE=0）不一致。

**修复**：
```cpp
mstatus_ = 0;                         // MBE=0, SBE=0, WPRI=0
mstatus_ |= (2ULL << 32);              // UXL = 2 (RV64)
mstatus_ |= (3ULL << 11);              // MPP = 3 (M-Mode)  ← 符合 SPEC（reset 后 MPP=M）
```

### 3.5 【正确性】SSTATUS 掩码

`src/csr.cpp::CSR::read()` 中：
```cpp
case CsrAddr::SSTATUS:
    return mstatus_ & 0x800000030001DE00ULL;   // 错的 mask
```

**正确的 sstatus 子集**（来自参考 `rv_common.hpp` 的 `csr_sstatus_def`）：
- bit 1 (SIE), 5 (SPIE), 6 (UBE), 8 (SPP)
- 9-10 (VS), 13-14 (FS), 15-16 (XS)
- 18 (SUM), 19 (MXR)
- 32-33 (UXL)
- 63 (SD)

正确 mask = `0x8000_0003_000D_E762ULL`，并把 `write(SSTATUS, value)` 中的
`(mstatus_ & ~MASK) | (value & MASK)` 同步替换。

### 3.6 【正确性】MIP / MIE 写入掩码

`src/csr.cpp::CSR::write()`：
```cpp
case CsrAddr::MIE: mie_ = value;  break;        // 未掩码
case CsrAddr::MIP: mip_ = value;  break;        // 未掩码
```

**问题**：参考实现只允许写入 bits 1, 3, 5, 7, 9, 11（`m_int_mask`）。

**修复**：在 `csr.h` 增加
```cpp
constexpr u64 M_INT_MASK =
    (1ULL << 1) | (1ULL << 3) | (1ULL << 5) | (1ULL << 7) | (1ULL << 9) | (1ULL << 11);
```
写入时：`mie_ = value & M_INT_MASK; mip_ = value & M_INT_MASK;`
读取时原样返回 `mie_ / mip_`（与参考一致，未实现位保持 0）。

### 3.7 【正确性】CSR 权限检查

`src/simulator.cpp::stage_ex()` 的 `case InstructionKind::CSRRW/CSRRS/CSRRC/CSRRWI/CSRRSI/CSRRCI` 块
当前无任何特权级检查。riscv-tests 会触发 `csrr t0, sstatus` 之类，
若 privilege != M，应 raise illegal instruction。

**简化策略**（对齐参考 `csr_op_permission_check` 行 341-354）：
- 若 `csr_.privilege_mode() != M_MODE` 且 `csr_addr` 高 2 位 `(addr >> 10) & 3 == 3`：置非法指令
- 若 `csr_addr == CSR_MISA` 等只读寄存器被 CSRRS/CSRRC 写入：忽略写入（参考做法）
- 若 `csr_addr` 未在 `is_implemented()` 中：置非法指令

最小实现（够覆盖 riscv-tests）：
```cpp
if (csr_.privilege_mode() != PrivilegeMode::Machine &&
    ((csr_addr >> 10) & 0x3u) == 0x3u) {
    // 非法指令
    halted_ = true;
    halt_reason_ = HaltReason::InvalidInstruction;
    break;
}
```

### 3.8 【正确性】MRET 后 mpp 复位

`src/simulator.cpp::stage_ex()` `case InstructionKind::MRET` 当前**没有**把 `mstatus.MPP` 置 0。
SPEC 要求 mret 后 mpp=U_MODE（=0）。

**修复**：在 MRET 分支末尾追加
```cpp
mstatus &= ~(3ULL << 11);   // MPP = 0 (U-Mode)
csr_.write(CSR_MSTATUS, mstatus);
```

（现有代码已设置 MPIE=1、MIE=MPIE_old，方向正确；仅缺最后一步清 mpp。）

> 顺带：当前 `if (mstatus & MSTATUS_MPIE) mstatus |= MSTATUS_MIE; else mstatus &= ~MSTATUS_MIE;`
> 等价于 `mstatus = (mstatus & ~MSTATUS_MIE) | (MPIE_old ? MSTATUS_MIE : 0)`，逻辑正确，保留。

### 3.9 【新增】SRET 完整实现

`include/riscv/decoder.h` 已有 `InstructionKind::SRET`，但 `simulator.cpp` `default` 分支未处理。
参考 `rv_priv.hpp::sret()` 行 651-666：

在 `stage_ex()` 增加：
```cpp
case InstructionKind::SRET: {
    if (csr_.privilege_mode() == PrivilegeMode::User) {
        halted_ = true;
        halt_reason_ = HaltReason::InvalidInstruction;
        break;
    }
    // 从 mstatus 的 S-mode 字段恢复
    u64 sstatus = csr_.read(CSR_SSTATUS);
    const u64 SPIE = 1ULL << 5;   // spie
    const u64 SIE  = 1ULL << 1;   // sie
    const u64 SPP_MASK = 1ULL << 8;
    u64 mstatus = csr_.read(CSR_MSTATUS);
    u64 new_sie = (sstatus & SPIE) ? SIE : 0;
    mstatus = (mstatus & ~SIE) | new_sie;
    // spec: mstatus.SPP = cur_priv, sstatus.SPIE = 1
    mstatus = (mstatus & ~SPP_MASK) | (static_cast<u64>(csr_.privilege_mode()) << 8);
    mstatus |= SPIE;  // 写入 SPIE=1
    csr_.write(CSR_MSTATUS, mstatus);
    u64 sepc = csr_.read(CSR_SEPC);
    branch_taken = true;
    branch_target = sepc;
    flush_decode_ = flush_execute_ = true;
    alu_result = 0;
    break;
}
```

> 注：当前为最小可用版本。完整的 SRET 还涉及 MPRV 清零、跳转后的 priv 切换，**测试集 rv64si 主要需要 SEPC/MTVEC 系列，
> SRET 在教学场景下出现频率低**，若测试中报错再补全。

### 3.10 【正确性】中断优先级

`src/csr.cpp::CSR::get_interrupt_cause()` 当前简单"找最低位"。
参考 `rv_priv.hpp::int2index()` 行 730-763 优先级：MEI(11) > MSI(3) > MTI(7) > SEI(9) > SSI(1) > STI(5)。

**修复**：按显式优先级顺序查找第一个 pending 位：
```cpp
u64 CSR::get_interrupt_cause() const {
    const u64 pending = mip_ & mie_;
    if (!pending) return 0;
    static constexpr int order[] = {11, 3, 7, 9, 1, 5};
    for (int b : order) {
        if (pending & (1ULL << b)) return b;
    }
    return 0;
}
```

### 3.11 【正确性】MISA 只读实现

`src/csr.cpp::CSR::read()` 当前：
```cpp
case CsrAddr::MISA: return (2ULL << 62) | (0x41ULL << 0);
```

`0x41 = i + a`（bit 0 = I, bit 6 = A），但我们**不实现 A 扩展**。应改为 `i + m`：
```cpp
case CsrAddr::MISA: return (2ULL << 62) | 0x1001ULL;   // MXL=2, I+M
```

### 3.12 【次要】PC 对齐与取指 misalign

参考实现 `rv_core.hpp` 行 150-154：
```cpp
if (pc % PC_ALIGN) {  // 取指 PC 必须 4 字节对齐
    priv.raise_trap(csr_cause_def(exc_instr_misalign), pc);
    goto exception;
}
```

`src/simulator.cpp::stage_if()` 当前仅在"PC 越界"时停机，未检查 4 字节对齐。
教学集 `rv64mi-p-ma_fetch` 依赖此 trap。

**修复**：在 `stage_if()` 末尾加
```cpp
if ((pc_ & 0x3ULL) != 0) {
    halted_ = true;
    halt_reason_ = HaltReason::MisalignedAccess;
    halt_pc_ = pc_;
    halt_inst_ = 0;
    next_if_id_ = {};
    return;
}
```

> 加载/存储 misalign 暂不动（需要新增 `MisalignedAccess` halt 路径与流水线 bubble，
> 工作量大；rv64mi-p-ld-misaligned 等测试本次**允许 FAIL**，见下"已知遗留"）。

### 3.13 【次要】WFI / SFENCE.VMA 占位

`src/simulator.cpp` `case WFI` 当前有较复杂逻辑（修改 mstatus/mip），**不正确**。
改为 nop（与参考一致）：
```cpp
case InstructionKind::WFI:
    alu_result = 0;
    break;
```

`SFENCE_VMA` 当前直接 `alu_result = 0`，可保留（无 TLB 时为 nop）。

---

## 四、`run_riscv_tests.cpp` 兼容性确认

`src/run_riscv_tests.cpp` 行 82：
```cpp
if (sim.halt_reason_ecall()) {        // 当前实现为 Ecall 才返回 true
    if (a0 == 0) { ... PASS ... }
}
```

修复后：把 `halt_reason_ecall()` 扩展为 `Ecall || EcallExit`：
```cpp
// in simulator.h
[[nodiscard]] bool halt_reason_ecall() const {
    return halt_reason_ == HaltReason::Ecall ||
           halt_reason_ == HaltReason::EcallExit;
}
```

并把 `HaltReason::EcallExit` 计入 PASS 路径（run_riscv_tests 现有逻辑已覆盖）。

---

## 五、修改顺序

1. **`include/riscv/csr.h`** —— 加 `M_INT_MASK`、`PrivilegeMode` 已存在
2. **`include/riscv/simulator.h`** —— 加 `HaltReason::EcallExit`、`MisalignedAccess`，
   扩展 `halt_reason_ecall()`
3. **`src/csr.cpp`** —— mstatus 复位、sstatus mask、mip/mie mask、interrupt 优先级、MISA
4. **`src/simulator.cpp`** —— ECALL 退出分支、ECALL 异常原因按特权级、MRET mpp 复位、
   SRET、PC 对齐检查、WFI nop
5. 重新编译 `cmake --build build`
6. 运行 `./build/run_riscv_tests --isa rv64ui`，统计 PASS/FAIL/TIMEOUT
7. 跑 `--isa rv64mi` / `rv64si` 评估 ma_fetch / sbreak / csr 系列

---

## 六、验证步骤

```bash
cd /opt/RISC-V_Platform
cmake --build build -j
./build/run_riscv_tests --isa rv64ui   # 期望：add/addi/.../jal/lui 等基础指令 PASS
./build/run_riscv_tests --isa rv64um   # 期望：MUL/DIV/REM 系列 PASS
./build/run_riscv_tests --isa rv64mi   # 部分 PASS（含 breakpoint/csr/zicntr）
./build/run_riscv_tests --isa rv64si   # 大部分 PASS
```

**判 PASS 标准**（run_riscv_tests 现状）：
- `halt_reason_ecall() == true` 且 `a0 == 0` → PASS
- 否则 FAIL / TIMEOUT

**预期结果**：
- rv64ui：~50/54 PASS（仅 `fence_i` / `ma_data` 失败，前者依赖 icache，后者依赖 misalign trap）
- rv64um：~14/14 PASS
- rv64mi：~10/16 PASS（ma_fetch/sbreak/scall/illegal/csrd 等）
- rv64si：~5/7 PASS

---

## 七、已知遗留（本次不做）

| 项 | 原因 | 影响 |
| --- | --- | --- |
| Sv39 分页（satp / TLB / PTW） | 工作量大；本平台不宣称支持 MMU | `rv64mi-p-mcsr` 涉及 satp 测试 FAIL |
| AMO 原子指令 | rv64ui-p-ld_st 等已能通过；不实现不影响 PASS | AMO 测试 FAIL |
| C 扩展 | 16 位压缩指令解码 | `fence_i` 等可能 FAIL |
| 加载/存储 misalign trap | 需要新增 EX 阶段 trap 写入 | `rv64mi-p-*-misaligned` 系列 FAIL |
| 计数 CSR mcycle/minstret | 参考实现支持，riscv-tests 不强依赖 | `rv64mi-p-zicntr` 部分 FAIL |
| mprv / SUM / MXR | 仅 S-mode 才需要 | S-mode 高级测试 FAIL |

> 这些项目保持当前行为不变，避免本次改动面失控。
> 如后续需要扩展，可分独立 PR 处理。

---

## 八、回退预案

- 单一文件级回退：`git checkout -- src/csr.cpp` 即可
- 全量回退：`git stash`（所有修改在 `git status` 中可见）
- 不修改 `CMakeLists.txt`、不修改 riscv-tests 源、不改 web 前端
