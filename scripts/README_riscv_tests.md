# 使用 riscv-tests 跑本模拟器

## 前置条件

1. 已安装 RISC-V 工具链（如 riscv-gnu-toolchain），并设置 `RISCV` 环境变量。
2. 已构建 riscv-tests 的 **64 位** ISA 测试（本模拟器为 RV64，见主 README「使用 riscv-tests 测试本模拟器」）。

## 在本机示例

假设工程在 `E:\RISC-V_Platform`，riscv-tests 在 `E:\riscv-tests`，且已在 `riscv-tests\isa` 下执行过 `make XLEN=64`：

```powershell
cd E:\RISC-V_Platform\build
cmake --build . --target run_riscv_tests
.\run_riscv_tests.exe --dir E:\riscv-tests\isa
```

若只跑 RV64I 基础整数测试，可只挑 `rv64ui-p-*`：

```powershell
.\run_riscv_tests.exe E:\riscv-tests\isa\rv64ui-p-add.elf E:\riscv-tests\isa\rv64ui-p-sub.elf
```

注意：本模拟器为 RV64，当前仅支持 RV64I 中除 CSR 外的指令，未实现 M 扩展等，故 `rv64um-p-*`、`rv64si-*` 等出现 FAIL 或 TIMEOUT 属正常。ELF32（rv32*）也会被自动识别并加载。
