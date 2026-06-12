# 修复后端模拟器与 Ciliphen/riscv-lab difftest/src 差异 — 实施计划

## 1. 背景

- 参考实现：教学 RISC-V 差分测试 C++ REF（rv_core.hpp / rv_common.hpp / rv_priv.hpp）
- 本地实现：当前 `/opt/RISC-V_Platform/src/` 五级流水线模拟器
- 症状：直接跑 `./build/run_riscv_tests --isa rv64ui` 全部 54 个测试 TIMEOUT
- 根因：本地 ECALL 仅 trap 到 mtvec，riscv-tests 的"a7=93"退出 syscall 无法识别；多处 CSR 行为与 RISC-V SPEC 不一致

## 2. 当前代码状态（与原计划对比）

| 位置 | 原计划要求 | 实际状态 | 待办 |
| --- | --- | --- | --- |
| `include/riscv/csr.h` | 加 `M_INT_MASK` / `SSTATUS_MASK` / `CSR_MISA` | ✅ 已完成 | 无 |
| `include/riscv/simulator.h` | 加 `HaltReason::EcallExit/MisalignedAccess`，扩展 `halt_reason_ecall()` | ✅ 已完成 | 无 |
| `src/csr.cpp::reset()` | mstatus 初值 MBE=0 | ✅ 已完成 | 无 |
| `src/csr.cpp::read(MISA)` | 返回 `MXL=2 + I + M` | ✅ 已完成 | 无 |
| `src/csr.cpp::read(SSTATUS)` | 用 `SSTATUS_MASK` 过滤 | ✅ 已完成 | 无 |
| `src/csr.cpp::read(SIE/SIP)` | 返回 `mie_/mip_` 过滤 supervisor 位 | ❌ **编译失败**：使用了未定义的 `S_INT_MASK_FROM_MIE` / `S_INT_MASK_FROM_MIP` | **必须修** |
| `src/csr.cpp::write(SSTATUS)` | 用 `SSTATUS_MASK` 过滤回写 | ❌ 仍用旧 `0x800000030001DE00ULL` | **必须修** |
| `src/csr.cpp::write(MIE/MIP)` | 写入前用 `M_INT_MASK` 掩码 | ❌ 直接赋值未掩码 | **必须修** |
| `src/csr.cpp::get_interrupt_cause()` | 按 `MEI>MSI>MTI>SEI>SSI>STI` 优先级 | ❌ 仍是"找最低位" | **必须修** |
| `src/simulator.cpp::stage_ex` ECALL | 识别 `a7=93 → EcallExit` | ❌ 硬编码 `mcause=11`、无退出 | **必须修** |
| `src/simulator.cpp::stage_ex` MRET | 末尾清 `mstatus.MPP=0` | ❌ 注释保留 `MPP 不变` | **必须修** |
| `src/simulator.cpp::stage_ex` SRET | 完整 SRET 实现 | ❌ 完全缺失 | **必须修** |
| `src/simulator.cpp::stage_if` 取指 | 检查 PC 4 字节对齐 | ❌ 仅检查内存越界 | **必须修** |
| `src/simulator.cpp::stage_ex` WFI | 改为 nop | ❌ 复杂且错误 | **必须修** |
| `src/simulator.cpp::stage_ex` CSR | 特权级检查 + 未实现检查 | ❌ 完全缺失 | **必须修**（不阻塞 rv64ui，但需要） |

> **关键风险**：`csr.cpp` 当前不能编译（line 112-114 引用未定义符号）。任何 `cmake --build build` 都会立即失败。
> 所有后续验证步骤都依赖先把 `csr.cpp` 修好。

## 3. 详细修改清单

### 3.1 `src/csr.cpp` 修复（必做，且使代码能编译）

**位置 1**：SIE / SIP 读取（line 111-114）
- 现状：使用 `S_INT_MASK_FROM_MIE` / `S_INT_MASK_FROM_MIP` 两个未定义符号 → 编译错误
- 修复：用 `mie_ & S_INT_MASK` / `mip_ & S_INT_MASK`（s_int_mask = bits 1, 5, 9）
- 新增常量到 `csr.h`：
  ```cpp
  constexpr u64 S_INT_MASK = (1ULL << 1) | (1ULL << 5) | (1ULL << 9);
  ```

**位置 2**：MIE 写入（line 125-127）
- 现状：`mie_ = value;` 未掩码
- 修复：`mie_ = value & M_INT_MASK;`

**位置 3**：MIP 写入（line 140-141）
- 现状：`mip_ = value;` 未掩码
- 修复：`mip_ = value & M_INT_MASK;`

**位置 4**：SSTATUS 写入（line 148-150）
- 现状：mask 仍是错的 `0x800000030001DE00ULL`
- 修复：`mstatus_ = (mstatus_ & ~SSTATUS_MASK) | (value & SSTATUS_MASK);` 同步 `sstatus_ = value;`

**位置 5**：中断优先级（line 181-191）
- 现状：找 `pending` 最低位
- 修复：按显式优先级 `MEI(11) > MSI(3) > MTI(7) > SEI(9) > SSI(1) > STI(5)` 顺序查找第一个 pending

### 3.2 `include/riscv/csr.h` 补充

新增 `S_INT_MASK`：
```cpp
// Supervisor-mode interrupt mask: bits 1 (SSI), 5 (STI), 9 (SEI).
constexpr u64 S_INT_MASK = (1ULL << 1) | (1ULL << 5) | (1ULL << 9);
```

### 3.3 `src/simulator.cpp` 修复

**位置 A**：`stage_ex` `case ECALL`（line 808-837）

完整替换为：
```cpp
case InstructionKind::ECALL: {
    // riscv-tests 退出语义：a7=93 表示 POSIX exit
    const u64 a7 = regs_.read(17);
    if (a7 == 93) {
        halted_ = true;
        halt_reason_ = HaltReason::EcallExit;
        halt_pc_ = instr.pc;
        halt_inst_ = instr.raw;
        alu_result = 0;
        break;
    }
    // 普通 ECALL：trap 到 mtvec，mcause 按特权级区分
    csr_.write(CSR_MEPC, instr.pc + 4);
    const u64 priv = static_cast<u64>(csr_.privilege_mode());
    csr_.write(CSR_MCAUSE, priv + 8);     // 8=U, 9=S, 11=M
    csr_.write(CSR_MTVAL, 0);
    {
        constexpr u64 MIE = 1ULL << 3, MPIE = 1ULL << 7;
        u64 mstatus = csr_.read(CSR_MSTATUS);
        if (mstatus & MIE) mstatus |= MPIE; else mstatus &= ~MPIE;
        mstatus &= ~MIE;
        csr_.write(CSR_MSTATUS, mstatus);
    }
    {
        u64 mtvec = csr_.read(CSR_MTVEC);
        branch_taken = true;
        branch_target = mtvec & ~0x3ULL;
        flush_decode_ = flush_execute_ = true;
    }
    last_trap_cause_ = TrapCause::Exception;
    alu_result = 0;
    break;
}
```

**位置 B**：`stage_ex` `case MRET`（line 846-871）

末尾追加（现有逻辑保留）：
```cpp
// SPEC: mret 后 MPP = U_MODE(0)
mstatus &= ~(3ULL << 11);
csr_.write(CSR_MSTATUS, mstatus);
```

**位置 C**：新增 `case SRET`

插入在 `case MRET` 之后：
```cpp
case InstructionKind::SRET: {
    if (csr_.privilege_mode() == PrivilegeMode::User) {
        halted_ = true;
        halt_reason_ = HaltReason::InvalidInstruction;
        break;
    }
    constexpr u64 SIE = 1ULL << 1, SPIE = 1ULL << 5, SPP_MASK = 1ULL << 8;
    u64 mstatus = csr_.read(CSR_MSTATUS);
    u64 new_sie = (mstatus & SPIE) ? SIE : 0;
    mstatus = (mstatus & ~SIE) | new_sie;
    mstatus = (mstatus & ~SPP_MASK) |
              (static_cast<u64>(csr_.privilege_mode()) << 8);
    mstatus |= SPIE;                       // SPIE=1
    csr_.write(CSR_MSTATUS, mstatus);
    u64 sepc = csr_.read(CSR_SEPC);
    branch_taken = true;
    branch_target = sepc;
    flush_decode_ = flush_execute_ = true;
    alu_result = 0;
    break;
}
```

**位置 D**：`stage_ex` `case WFI`（line 781-807）

整个分支替换为：
```cpp
case InstructionKind::WFI:
    alu_result = 0;
    break;
```

**位置 E**：`stage_ex` CSR 指令块（line 872-904）

在分支起始处加入权限检查：
```cpp
const u32 csr_addr = static_cast<u32>(instr.imm) & 0xFFFu;
// 特权级检查：非 M-mode 访问 M-mode CSR（高 2 位 == 11b）→ 非法指令
if (csr_.privilege_mode() != PrivilegeMode::Machine &&
    ((csr_addr >> 10) & 0x3u) == 0x3u) {
    csr_.write(CSR_MEPC, instr.pc + 4);
    csr_.write(CSR_MCAUSE, 2);   // illegal instruction
    csr_.write(CSR_MTVAL, csr_addr);
    /* mstatus 翻转 + redirect 到 mtvec，与 ECALL 同套 */
    halted_ = true;
    halt_reason_ = HaltReason::InvalidInstruction;
    halt_pc_ = instr.pc;
    halt_inst_ = instr.raw;
    alu_result = 0;
    break;
}
// 未实现 CSR：若 is_implemented()==false 也按非法指令处理
if (!csr_.is_implemented(csr_addr)) {
    csr_.write(CSR_MEPC, instr.pc + 4);
    csr_.write(CSR_MCAUSE, 2);
    csr_.write(CSR_MTVAL, csr_addr);
    halted_ = true;
    halt_reason_ = HaltReason::InvalidInstruction;
    halt_pc_ = instr.pc;
    halt_inst_ = instr.raw;
    alu_result = 0;
    break;
}
```

**位置 F**：`stage_if` 取指阶段

在 `if (!memory_.contains(pc_))` 之前加入 4 字节对齐检查：
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

## 4. 修改顺序

1. `include/riscv/csr.h` — 加 `S_INT_MASK`
2. `src/csr.cpp` — 5 处修复（编译 + 正确性）
3. `src/simulator.cpp` — A/B/C/D/E/F 六处修改
4. `cmake --build build -j`
5. `./build/run_riscv_tests --isa rv64ui` 验证

## 5. 验证步骤

```bash
cd /opt/RISC-V_Platform
cmake --build build -j
./build/run_riscv_tests --isa rv64ui
./build/run_riscv_tests --isa rv64um
./build/run_riscv_tests --isa rv64mi
./build/run_riscv_tests --isa rv64si
```

**判 PASS 标准**（run_riscv_tests 现状）：
- `halt_reason_ecall() == true` 且 `a0 == 0` → PASS
- 否则 FAIL / TIMEOUT

**预期结果**：
- rv64ui：~50/54 PASS（仅 `fence_i` / `ma_data` 失败）
- rv64um：~14/14 PASS
- rv64mi：~10/16 PASS（ma_fetch/sbreak/scall/illegal/csrd）
- rv64si：~5/7 PASS

## 6. 风险与回退

- 风险：若 SRET 翻转 SPP 字段导致某些 riscv-tests 异常，可临时把 SRET 退回到 "halt(InvalidInstruction)" 占位
- 风险：若 PC 对齐检查导致合法跳转因 LSB=1 误判，可考虑只检查 `if_id_` 入口（取指入口）而非每条跳转的 target（当前计划只检查取指入口，安全）
- 回退：`git checkout -- src/csr.cpp src/simulator.cpp include/riscv/csr.h` 即可

## 7. 不做

- Sv39 分页 / AMO 原子 / C 扩展 / 加载存储 misalign trap / 计数 CSR
- 不修改 `run_riscv_tests.cpp`（其已能识别 `EcallExit`，因 `halt_reason_ecall()` 已扩展为 Ecall || EcallExit）
- 不修改 CMakeLists.txt
