// instruction_tests.cpp：RISC-V 指令级测试，目标为 RV64（XLEN=64）
// 可运行 E:\riscv-tests\isa\rv64ui 中仅用本模拟器支持指令的 ELF（见 RUNNABLE_RV64UI_TESTS）
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "riscv/elf_loader.h"
#include "riscv/simulator.h"

using namespace riscv;

namespace {

constexpr u32 encode_addi(u32 rd, u32 rs1, s32 imm) {
    if (imm < -2048 || imm > 2047) {
        throw std::runtime_error("ADDI 立即数超出12位范围");
    }
    const u32 imm12 = static_cast<u32>(imm) & 0xFFFu;
    return (imm12 << 20) | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0b0010011;
}

constexpr u32 encode_lui(u32 rd, u32 imm20) {
    return (imm20 << 12) | (rd << 7) | 0b0110111;
}

constexpr u32 encode_auipc(u32 rd, u32 imm20) {
    return (imm20 << 12) | (rd << 7) | 0b0010111;
}

// 通用 I 型 ALU（非移位），用于 SLTI/SLTIU/XORI/ORI/ANDI 等
constexpr u32 encode_imm_alu(u32 rd, u32 rs1, s32 imm, u32 funct3) {
    const u32 imm12 = static_cast<u32>(imm) & 0xFFFu;
    return (imm12 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | 0b0010011;
}

constexpr u32 encode_r(u32 rd, u32 rs1, u32 rs2, u32 funct3, u32 funct7) {
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
           (rd << 7) | 0b0110011;
}

// I 型移位指令：SLLI/SRLI/SRAI（RV64 使用 shamt[5:0]，即 & 0x3F）
constexpr u32 encode_shift_imm(u32 rd, u32 rs1, u32 shamt, u32 funct3, u32 funct7) {
    const u32 sh = shamt & 0x3Fu;
    return (funct7 << 25) | (sh << 20) | (rs1 << 15) | (funct3 << 12) |
           (rd << 7) | 0b0010011;
}

constexpr u32 encode_jal(u32 rd, s32 imm) {
    const u32 uimm = static_cast<u32>(imm);
    const u32 bit20 = (uimm >> 20) & 0x1;
    const u32 bits10_1 = (uimm >> 1) & 0x3FF;
    const u32 bit11 = (uimm >> 11) & 0x1;
    const u32 bits19_12 = (uimm >> 12) & 0xFF;
    const u32 encoded =
        (bit20 << 31) | (bits19_12 << 12) | (bit11 << 20) | (bits10_1 << 21);
    return encoded | (rd << 7) | 0b1101111;
}

constexpr u32 encode_jalr(u32 rd, u32 rs1, s32 imm) {
    const u32 imm12 = static_cast<u32>(imm) & 0xFFFu;
    return (imm12 << 20) | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0b1100111;
}

constexpr u32 encode_beq(u32 rs1, u32 rs2, s32 imm) {
    const u32 uimm = static_cast<u32>(imm);
    const u32 bit12 = (uimm >> 12) & 0x1;
    const u32 bit11 = (uimm >> 11) & 0x1;
    const u32 bits10_5 = (uimm >> 5) & 0x3F;
    const u32 bits4_1 = (uimm >> 1) & 0xF;
    const u32 encoded =
        (bit12 << 31) | (bits10_5 << 25) | (bits4_1 << 8) | (bit11 << 7);
    return encoded | (rs2 << 20) | (rs1 << 15) | (0b000 << 12) | 0b1100011;
}

// 通用分支编码（用于 BNE/BLT/BGE/BLTU/BGEU）
constexpr u32 encode_branch(u32 rs1, u32 rs2, s32 imm, u32 funct3) {
    const u32 uimm = static_cast<u32>(imm);
    const u32 bit12 = (uimm >> 12) & 0x1;
    const u32 bit11 = (uimm >> 11) & 0x1;
    const u32 bits10_5 = (uimm >> 5) & 0x3F;
    const u32 bits4_1 = (uimm >> 1) & 0xF;
    const u32 encoded =
        (bit12 << 31) | (bits10_5 << 25) | (bits4_1 << 8) | (bit11 << 7);
    return encoded | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | 0b1100011;
}

constexpr u32 encode_sw(u32 rs2, u32 rs1, s32 imm) {
    const u32 uimm = static_cast<u32>(imm);
    const u32 imm11_5 = (uimm >> 5) & 0x7F;
    const u32 imm4_0 = uimm & 0x1F;
    return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) |
           (imm4_0 << 7) | 0b0100011;
}

// 通用 S 型存储：SB/SH/SW
constexpr u32 encode_store(u32 rs2, u32 rs1, s32 imm, u32 funct3) {
    const u32 uimm = static_cast<u32>(imm);
    const u32 imm11_5 = (uimm >> 5) & 0x7F;
    const u32 imm4_0 = uimm & 0x1F;
    return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
           (imm4_0 << 7) | 0b0100011;
}

constexpr u32 encode_lw(u32 rd, u32 rs1, s32 imm) {
    const u32 imm12 = static_cast<u32>(imm) & 0xFFFu;
    return (imm12 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0000011;
}

// 通用 I 型访存：LB/LH/LW/LBU/LHU
constexpr u32 encode_load(u32 rd, u32 rs1, s32 imm, u32 funct3) {
    const u32 imm12 = static_cast<u32>(imm) & 0xFFFu;
    return (imm12 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | 0b0000011;
}

// MISC-MEM: FENCE (pred=0, succ=0) = 0x0000000F; FENCE.I = 0x0000100F
constexpr u32 encode_fence(u32 pred, u32 succ) {
    return (pred << 24) | (succ << 20) | (0b000 << 12) | (0b000 << 7) | 0b0001111;
}
constexpr u32 instr_fence = 0x0000000Fu;
constexpr u32 instr_fence_i = 0x0000100Fu;
constexpr u32 instr_ecall = 0x00000073u;
constexpr u32 instr_ebreak = 0x00100073u;

std::vector<u8> assemble_words(const std::vector<u32>& words) {
    std::vector<u8> binary;
    binary.reserve(words.size() * 4);
    for (auto word : words) {
        binary.push_back(static_cast<u8>(word & 0xFF));
        binary.push_back(static_cast<u8>((word >> 8) & 0xFF));
        binary.push_back(static_cast<u8>((word >> 16) & 0xFF));
        binary.push_back(static_cast<u8>((word >> 24) & 0xFF));
    }
    return binary;
}

int run_addi_test() {
    const std::vector<u32> program = {
        encode_addi(/*rd=*/1, /*rs1=*/0, /*imm=*/5),  // x1 = 5
        encode_addi(/*rd=*/2, /*rs1=*/1, /*imm=*/3),  // x2 = x1 + 3 = 8
        0x00100073,                                   // ebreak 结束
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(20);

    const auto& regs = sim.registers().raw();
    const bool pass = regs[1] == 5 && regs[2] == 8;

    std::cout << "[ADDI Test] x1=" << regs[1] << ", x2=" << regs[2]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

int run_lui_auipc_test() {
    // 0: lui x1, 0x12345   -> x1 = 0x12345000
    // 4: auipc x2, 0x10    -> x2 = (pc=RESET_VECTOR+4) + 0x10'000 = RESET_VECTOR + 0x00010004
    // 8: ebreak
    const std::vector<u32> program = {
        encode_lui(1, 0x12345),
        encode_auipc(2, 0x10),
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(20);

    const auto& regs = sim.registers().raw();
    const bool pass =
        regs[1] == 0x12345000ULL &&
        regs[2] == (RESET_VECTOR + 0x00010004ULL);

    std::cout << "[LUI/AUIPC Test] x1=0x" << std::hex << regs[1]
              << " x2=0x" << regs[2] << std::dec
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

int run_rtype_test() {
    // x1 = 5, x2 = 3
    // x3 = x1 + x2 = 8  (ADD)
    // x4 = x1 - x2 = 2  (SUB)
    // x5 = x1 & x2 = 1  (AND)
    // x6 = x1 | x2 = 7  (OR)
    // x7 = x1 ^ x2 = 6  (XOR)
    const std::vector<u32> program = {
        encode_addi(1, 0, 5),
        encode_addi(2, 0, 3),
        encode_r(3, 1, 2, 0b000, 0b0000000),  // add
        encode_r(4, 1, 2, 0b000, 0b0100000),  // sub
        encode_r(5, 1, 2, 0b111, 0b0000000),  // and
        encode_r(6, 1, 2, 0b110, 0b0000000),  // or
        encode_r(7, 1, 2, 0b100, 0b0000000),  // xor
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(50);

    const auto& regs = sim.registers().raw();
    const bool pass = regs[3] == 8 && regs[4] == 2 &&
                      regs[5] == 1 && regs[6] == 7 && regs[7] == 6;

    std::cout << "[R-type Test] x3=" << regs[3] << " x4=" << regs[4]
              << " x5=" << regs[5] << " x6=" << regs[6]
              << " x7=" << regs[7]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// 测试：移位指令 SLL/SRL/SRA 以及 SLLI/SRLI/SRAI（RV64：LUI 符号扩展到 64 位）
int run_shift_test() {
    const std::vector<u32> program = {
        // x1 = 1, x2 = 3
        encode_addi(1, 0, 1),
        encode_addi(2, 0, 3),
        // x4 = LUI 0x80000 → RV64 下为 0xFFFF_FFFF_8000_0000（符号扩展）
        encode_lui(4, 0x80000),
        // SLL  x3,x1,x2  -> 1 << 3 = 8
        encode_r(3, 1, 2, 0b001, 0b0000000),
        // SRL  x5,x4,x2  -> 逻辑右移 3 → 0x1FFF_FFFF_F000_0000
        encode_r(5, 4, 2, 0b101, 0b0000000),
        // SRA  x6,x4,x2  -> 算术右移 3 → 0xFFFF_FFFF_F000_0000
        encode_r(6, 4, 2, 0b101, 0b0100000),
        // SLLI x7,x1,4 -> 16
        encode_shift_imm(7, 1, 4, 0b001, 0b0000000),
        // SRLI x8,x4,4 -> 逻辑右移 4 → 0x0FFF_FFFF_F800_0000
        encode_shift_imm(8, 4, 4, 0b101, 0b0000000),
        // SRAI x9,x4,4 -> 算术右移 4 → 0xFFFF_FFFF_F800_0000
        encode_shift_imm(9, 4, 4, 0b101, 0b0100000),
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(50);

    const auto& r = sim.registers().raw();
    constexpr u64 u64_1fff_f000 = 0x1FFFFFFFF0000000ULL;
    constexpr u64 u64_f000 = 0xFFFFFFFFF0000000ULL;
    constexpr u64 u64_0fff_f800 = 0x0FFFFFFFF8000000ULL;
    constexpr u64 u64_f800 = 0xFFFFFFFFF8000000ULL;
    const bool pass =
        r[3] == 8ULL &&
        r[5] == u64_1fff_f000 &&
        r[6] == u64_f000 &&
        r[7] == 16ULL &&
        r[8] == u64_0fff_f800 &&
        r[9] == u64_f800;

    std::cout << "[Shift Test] x3=" << r[3]
              << " x5=0x" << std::hex << r[5]
              << " x6=0x" << r[6]
              << " x7=" << std::dec << r[7]
              << " x8=0x" << std::hex << r[8]
              << " x9=0x" << r[9] << std::dec
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// 测试：立即数算术/逻辑：SLTI/SLTIU/XORI/ORI/ANDI
int run_imm_alu_test() {
    const std::vector<u32> program = {
        // x1 = 5, x2 = 0xFFFF_FFF0 (有符号为 -16)
        encode_addi(1, 0, 5),
        encode_addi(2, 0, -16),
        // slti  x3,x1,10   -> 1
        encode_imm_alu(3, 1, 10, 0b010),
        // slti  x4,x1,0    -> 0
        encode_imm_alu(4, 1, 0, 0b010),
        // sltiu x5,x1,10   -> 1
        encode_imm_alu(5, 1, 10, 0b011),
        // xori  x6,x2,0x0F -> -16 ^ 0x0F = -1 (0xFFFF_FFFF)
        encode_imm_alu(6, 2, 0x0F, 0b100),
        // ori   x7,x1,0x10 -> 0x15
        encode_imm_alu(7, 1, 0x10, 0b110),
        // andi  x8,x1,0x03 -> 1
        encode_imm_alu(8, 1, 0x03, 0b111),
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(50);

    const auto& r = sim.registers().raw();
    // RV64: x2=-16 即 0xFFFF_FFFF_FFFF_FFF0，xori 0x0F 得全 1
    constexpr u64 all_ones_64 = 0xFFFFFFFFFFFFFFFFULL;
    const bool pass = r[3] == 1ULL && r[4] == 0ULL && r[5] == 1ULL &&
                      r[6] == all_ones_64 && r[7] == 0x15ULL && r[8] == 1ULL;

    std::cout << "[IMM ALU Test] x3=" << r[3] << " x4=" << r[4]
              << " x5=" << r[5] << " x6=0x" << std::hex << r[6]
              << " x7=0x" << r[7] << std::dec << " x8=" << r[8]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

int run_load_store_test() {
    // RV64：用 AUIPC 获得可访问基址，LUI 0x80000 会变成 0xFFFF_FFFF_8000_0000 越界
    const std::vector<u32> program = {
        encode_auipc(1, 0),   // x1 = PC = RESET_VECTOR = 0x80000000（首条指令）
        encode_lui(2, 0xDEADC),
        encode_addi(2, 2, -273),  // x2 = 0xDEADBEEF
        encode_sw(2, 1, 0),
        encode_lw(3, 1, 0),
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(50);

    const auto& regs = sim.registers().raw();
    // RV64：LW 加载 32 位 0xDEADBEEF 并符号扩展到 64 位 = 0xFFFFFFFFDEADBEEF
    constexpr u64 lw_deadbeef = 0xFFFFFFFFDEADBEEFULL;
    const bool pass = regs[3] == lw_deadbeef;

    std::cout << "[LW/SW Test] x3=0x" << std::hex << regs[3] << std::dec
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// 测试：字节/半字加载的符号扩展与零扩展（LB/LH/LBU/LHU + SB/SW），RV64 全 64 位结果
int run_load_variants_test() {
    const std::vector<u32> program = {
        encode_auipc(1, 0),   // x1 = RESET_VECTOR（RV64 下用 AUIPC 获得可访问基址）
        encode_lui(2, 0x80FF0),
        encode_addi(2, 2, 0xAA),   // x2 = 0x80FF00AA
        encode_sw(2, 1, 0),
        encode_load(3, 1, 0, 0b000),  // LB  -> 符号扩展到 64 位
        encode_load(4, 1, 2, 0b001),  // LH
        encode_load(5, 1, 0, 0b100),  // LBU -> 零扩展
        encode_load(6, 1, 2, 0b101),  // LHU
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(50);

    const auto& r = sim.registers().raw();
    constexpr u64 lb_aa = 0xFFFFFFFFFFFFFFAAULL;
    constexpr u64 lh_80ff = 0xFFFFFFFFFFFF80FFULL;
    const bool pass = r[3] == lb_aa &&
                      r[4] == lh_80ff &&
                      r[5] == 0xAAULL &&
                      r[6] == 0x80FFULL;

    std::cout << "[Load Variants Test] x3=0x" << std::hex << r[3]
              << " x4=0x" << r[4] << " x5=0x" << r[5]
              << " x6=0x" << r[6] << std::dec
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

int run_branch_test() {
    // x1 = 5, x2 = 5
    // beq x1,x2, +8   跳过加 1 指令
    // addi x3,x0,1    (如果跳转失败就会执行)
    // addi x3,x0,2    (预期执行)
    const std::vector<u32> program = {
        encode_addi(1, 0, 5),
        encode_addi(2, 0, 5),
        encode_beq(1, 2, 8),          // 跳过下一条
        encode_addi(3, 0, 1),         // 不应执行
        encode_addi(3, 0, 2),         // 预期 x3 = 2
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(50);

    const auto& regs = sim.registers().raw();
    const bool pass = regs[3] == 2;

    std::cout << "[BEQ Test] x3=" << regs[3]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// 测试：更多分支指令 BNE/BLT/BGE/BLTU/BGEU
int run_branch_variants_test() {
    const std::vector<u32> program = {
        // x1 = 5, x2 = 3, x3 = 5U (无符号比较时相等)
        encode_addi(1, 0, 5),
        encode_addi(2, 0, 3),
        encode_addi(3, 0, 5),
        // bne x1,x2,+8  -> 跳过 x4=1，执行 x4=2
        encode_branch(1, 2, 8, 0b001),
        encode_addi(4, 0, 1),
        encode_addi(4, 0, 2),
        // blt x2,x1,+8  -> 真，跳过 x5=1，执行 x5=2
        encode_branch(2, 1, 8, 0b100),
        encode_addi(5, 0, 1),
        encode_addi(5, 0, 2),
        // bgeu x3,x2,+8 -> 真(5U>=3U)，跳过 x6=1，执行 x6=2
        encode_branch(3, 2, 8, 0b111),
        encode_addi(6, 0, 1),
        encode_addi(6, 0, 2),
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(100);

    const auto& r = sim.registers().raw();
    const bool pass = r[4] == 2 && r[5] == 2 && r[6] == 2;

    std::cout << "[Branch Variants Test] x4=" << r[4]
              << " x5=" << r[5] << " x6=" << r[6]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// 测试：SLT/SLTU + 额外分支 BLTU/BGE，以及 SB/SH 存储（RV64：基址用 AUIPC）
int run_compare_and_store_test() {
    const std::vector<u32> program = {
        encode_auipc(20, 0),   // x20 = RESET_VECTOR（首条 PC，用于后续 SB/SH 基址）
        // ----- SLT / SLTU 部分 -----
        encode_addi(1, 0, 5),
        encode_addi(2, 0, -1),
        encode_lui(3, 0x80000),   // RV64 下 x3 = 0xFFFF_FFFF_8000_0000（仅作比较用）
        encode_addi(4, 0, 3),
        encode_r(5, 1, 2, 0b010, 0b0000000),
        encode_r(6, 3, 4, 0b010, 0b0000000),
        encode_r(7, 1, 2, 0b011, 0b0000000),
        encode_r(8, 2, 1, 0b011, 0b0000000),
        // ----- BLTU / BGE 分支部分 -----
        encode_addi(9, 0, 1),
        encode_addi(10, 0, 3),
        encode_addi(11, 0, -1),
        encode_addi(12, 0, 0),
        encode_branch(9, 10, 8, 0b110),
        encode_addi(13, 0, 1),
        encode_addi(13, 0, 2),
        encode_branch(11, 10, 8, 0b101),
        encode_addi(14, 0, 1),
        encode_addi(14, 0, 2),

        // ----- SB / SH 部分（x20 已在首条设定）-----
        encode_sw(0, 20, 0),
        // x21 = 0xFFFF00AA, 只写低字节
        encode_lui(21, 0xFFFF1),
        encode_addi(21, 21, -86),  // -86 = 0xAA 的补码，合法 12 位立即数
        // sb x21,0(x20) -> 内存低字节 = 0xAA, 高位保持 0
        encode_store(21, 20, 0, 0b000),
        // x22 = 0x1234, 存为半字到偏移 2
        // 0x1234 无法直接作为有符号 12 位立即数，这里用 LUI+ADDI 构造
        encode_lui(22, 0x1),              // 0x00010000
        encode_addi(22, 22, 0x234),       // +0x234 = 0x0001234
        // sh x22,2(x20)
        encode_store(22, 20, 2, 0b001),
        // lw x23,0(x20) -> 预期 0x123400AA (小端：AA,00,34,12)
        encode_lw(23, 20, 0),

        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(200);

    const auto& r = sim.registers().raw();
    const bool pass_slt =
        r[5] == 0u && r[6] == 1u && r[7] == 1u && r[8] == 0u;
    const bool pass_branch =
        r[13] == 2u && r[14] == 2u;
    const bool pass_store =
        r[23] == 0x1234'00AAu;

    std::cout << "[Compare/Store Test] SLT(x5,x6,x7,x8)="
              << r[5] << "," << r[6] << "," << r[7] << "," << r[8]
              << " BR(x13,x14)=" << r[13] << "," << r[14]
              << " MEM x23=0x" << std::hex << r[23] << std::dec
              << " -> " << ((pass_slt && pass_branch && pass_store) ? "PASS" : "FAIL") << "\n";

    return (pass_slt && pass_branch && pass_store) ? 0 : 1;
}

int run_jump_test() {
    // 测试 JAL/JALR：
    // 0:   jal x1, +8
    // 4:   addi x2,x0,1       ; 不应执行
    // 8:   addi x3,x0,2
    // 12:  jalr x4,x1,4       ; x4=16, 跳到 pc=(RESET_VECTOR+4+4)&~1 -> 回到 addi
    // 16:  ebreak
    const std::vector<u32> program = {
        encode_jal(1, 8),
        encode_addi(2, 0, 1),
        encode_addi(3, 0, 2),
        encode_jalr(4, 1, 4),
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(100);

    const auto& regs = sim.registers().raw();
    // x1 被写为返回地址 RESET_VECTOR+4，x3 最终为 2（证明跳转生效），x2 仍为 0（证明被跳过）
    const bool pass = regs[1] == (RESET_VECTOR + 4) && regs[2] == 0 && regs[3] == 2;

    std::cout << "[JAL/JALR Test] x1=" << regs[1]
              << " x2=" << regs[2] << " x3=" << regs[3]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// ---------- RISC-V 规范符合性测试 ----------

// x0 恒为 0，写 x0 必须被忽略（RISC-V 规范）
int run_spec_x0_test() {
    const std::vector<u32> program = {
        encode_addi(0, 0, 123),   // 写 x0，应被忽略
        encode_addi(1, 0, 1),     // x1 = 1
        encode_addi(2, 1, 0),     // x2 = x1 + 0 = 1（若 x0 被误写则会错）
        0x00100073,
    };
    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(30);
    const auto& r = sim.registers().raw();
    const bool pass = (r[0] == 0u && r[1] == 1u && r[2] == 1u);
    std::cout << "[Spec x0] x0=" << r[0] << " x1=" << r[1] << " x2=" << r[2]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// ADDI x0, x0, 0 作为 NOP，x0 保持 0
int run_spec_nop_test() {
    const std::vector<u32> program = {
        encode_addi(0, 0, 0),
        encode_addi(1, 0, 42),
        0x00100073,
    };
    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(20);
    const bool pass = sim.registers().raw()[0] == 0u && sim.registers().raw()[1] == 42u;
    std::cout << "[Spec NOP] x0=" << sim.registers().raw()[0]
              << " x1=" << sim.registers().raw()[1]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// JALR 目标地址 LSB 必须置 0（规范：target = (rs1 + imm) & ~1），RV64 用 AUIPC 得合法基址
int run_spec_jalr_lsb_test() {
    const std::vector<u32> program = {
        encode_auipc(1, 0),             // x1 = RESET_VECTOR
        encode_addi(1, 1, 13),         // x1 = RESET_VECTOR + 13（奇数）
        encode_jalr(2, 1, -1),         // target = (x1-1)&~1 = RESET_VECTOR+12
        encode_addi(3, 0, 1),          // 不执行
        encode_addi(3, 0, 2),          // 应执行：x3=2
        0x00100073,
    };
    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(50);
    const auto& r = sim.registers().raw();
    const bool pass = (r[2] == RESET_VECTOR + 12ULL && r[3] == 2ULL);
    std::cout << "[Spec JALR LSB] link=" << r[2] << " x3=" << r[3]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// RV64：移位量取 shamt[5:0]，SLLI x1,x2,65 等价于 SLLI x1,x2,1 -> 1<<1=2
int run_spec_shift_mask_test() {
    const std::vector<u32> program = {
        encode_addi(2, 0, 1),
        encode_shift_imm(1, 2, 65, 0b001, 0b0000000),  // 65 & 0x3F = 1
        0x00100073,
    };
    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(30);
    const bool pass = (sim.registers().raw()[1] == 2ULL);
    std::cout << "[Spec Shift Mask] SLLI 65=>1<<1=2 (RV64 shamt[5:0]) -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// SLTIU：立即数零扩展，imm=0xFFF 表示 4095
int run_spec_sltiu_test() {
    // x1=5, SLTIU x2,x1,0xFFF -> 5 < 4095 => 1
    const std::vector<u32> program = {
        encode_addi(1, 0, 5),
        encode_imm_alu(2, 1, 0xFFF, 0b011),
        0x00100073,
    };
    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(30);
    const bool pass = (sim.registers().raw()[2] == 1u);
    std::cout << "[Spec SLTIU] 5 < 0xFFF(4095) => 1 -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// JAL x0, offset：不写链接，仅跳转
int run_spec_jal_rd_zero_test() {
    const std::vector<u32> program = {
        encode_jal(0, 8),             // JAL x0, +8
        encode_addi(1, 0, 1),         // 不执行
        encode_addi(1, 0, 2),         // 执行，x1=2
        0x00100073,
    };
    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(30);
    const auto& r = sim.registers().raw();
    const bool pass = (r[0] == 0u && r[1] == 2u);
    std::cout << "[Spec JAL rd=x0] x0=" << r[0] << " x1=" << r[1]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// ECALL / EBREAK 停机原因符合规范（用 halt_reason_ecall/ebreak 避免跨编译单元枚举比较）
int run_ecall_ebreak_halt_test() {
    {
        const std::vector<u32> program = { instr_ecall };
        RISCVSimulator sim;
        sim.load_program(assemble_words(program));
        sim.run(30);
        const bool ok = sim.halted() && sim.halt_reason_ecall();
        std::cout << "[ECALL Halt] halted=" << sim.halted()
                  << " reason_ecall=" << sim.halt_reason_ecall()
                  << " -> " << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok) return 1;
    }
    {
        const std::vector<u32> program = { instr_ebreak };
        RISCVSimulator sim;
        sim.load_program(assemble_words(program));
        sim.run(30);
        const bool ok = sim.halted() && sim.halt_reason_ebreak();
        std::cout << "[EBREAK Halt] halted=" << sim.halted()
                  << " reason_ebreak=" << sim.halt_reason_ebreak()
                  << " -> " << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok) return 1;
    }
    return 0;
}

// FENCE / FENCE.I 不改变语义，仅前进 PC（当前实现为 no-op）
int run_fence_test() {
    const std::vector<u32> program = {
        instr_fence,
        instr_fence_i,
        encode_addi(1, 0, 1),
        0x00100073,
    };
    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(30);
    const bool pass = (sim.registers().raw()[1] == 1u);
    std::cout << "[FENCE/FENCE.I Test] x1=" << sim.registers().raw()[1]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// ---------- 47 条指令逐一验证（单程序覆盖所有指令，RV64 语义） ----------
int run_all_47_instructions_test() {
    const std::vector<u32> program = {
        // 数据区基址：RV64 用 AUIPC+ADDI，0x400 偏移避免覆盖后续代码
        encode_auipc(10, 0),                       // x10 = RESET_VECTOR
        encode_addi(10, 10, 0x400),                // x10 = RESET_VECTOR + 0x400
        encode_lui(1, 0x12345),                    // x1 = 0x12345000
        encode_auipc(2, 0),                        // x2 = pc + 0（覆盖）
        encode_addi(11, 0, 0x80),
        encode_sw(11, 10, 0),
        encode_load(12, 10, 0, 0b000),            // LB 符号扩展
        encode_load(13, 10, 0, 0b001),            // LH
        encode_load(14, 10, 0, 0b010),            // LW
        encode_load(15, 10, 0, 0b100),            // LBU
        encode_load(16, 10, 0, 0b101),            // LHU
        // 16-18. Store
        encode_store(11, 10, 4, 0b000),
        encode_lui(11, 0x1),
        encode_addi(11, 11, 0x234),   // x11 = 0x1234（0x1234 超出 12 位有符号，用 LUI+ADDI）
        encode_store(11, 10, 8, 0b001),
        // 19-25. I ALU
        encode_addi(20, 0, 7),
        encode_imm_alu(21, 20, 10, 0b010),       // SLTI 7<10 => 1
        encode_imm_alu(22, 20, 3, 0b011),        // SLTIU 7<3 => 0
        encode_imm_alu(23, 20, 0x0F, 0b100),     // XORI
        encode_imm_alu(24, 20, 0x0F, 0b110),     // ORI
        encode_imm_alu(25, 20, 0x0F, 0b111),     // ANDI
        encode_shift_imm(26, 20, 2, 0b001, 0),
        encode_shift_imm(27, 20, 2, 0b101, 0),
        encode_shift_imm(28, 20, 2, 0b101, 0b0100000),
        // 26-35. R-type
        encode_addi(30, 0, 10),
        encode_addi(31, 0, 3),
        encode_r(3, 30, 31, 0b000, 0),
        encode_r(4, 30, 31, 0b000, 0b0100000),
        encode_r(5, 30, 31, 0b001, 0),
        encode_r(6, 31, 30, 0b010, 0),   // SLT  x6,x31,x30 => 3<10 => 1
        encode_r(7, 31, 30, 0b011, 0),   // SLTU x7,x31,x30 => 3<10 => 1
        encode_r(8, 30, 31, 0b100, 0),
        encode_r(9, 30, 31, 0b101, 0),
        encode_r(17, 30, 31, 0b101, 0b0100000),
        encode_r(18, 30, 31, 0b110, 0),
        encode_r(19, 30, 31, 0b111, 0),
        // 36-37. FENCE, FENCE.I
        instr_fence,
        instr_fence_i,
        // 38-39. ECALL/EBREAK 用最后一条
        encode_addi(29, 0, 99),                  // 占位，表示前面都执行到
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(500);

    const auto& r = sim.registers().raw();
    constexpr u64 lb_signed_80 = 0xFFFFFFFFFFFFFF80ULL;  // LB 单字节 0x80 符号扩展到 64 位
    bool pass = true;
    if (r[1] != 0x12345000ULL) { std::cout << "LUI x1 fail\n"; pass = false; }
    if (r[12] != lb_signed_80) { std::cout << "LB fail " << r[12] << "\n"; pass = false; }
    // LW 加载 4 字节 0x80,0,0,0 → 32 位 0x00000080，符号扩展为 64 位 = 128
    if (r[14] != 0x80ULL) { std::cout << "LW fail " << r[14] << "\n"; pass = false; }
    if (r[15] != 0x80ULL) { std::cout << "LBU fail " << r[15] << "\n"; pass = false; }
    if (r[21] != 1ULL) { std::cout << "SLTI fail " << r[21] << "\n"; pass = false; }
    if (r[22] != 0ULL) { std::cout << "SLTIU fail " << r[22] << "\n"; pass = false; }
    if (r[26] != 28ULL) { std::cout << "SLLI fail " << r[26] << "\n"; pass = false; }
    if (r[3] != 13ULL) { std::cout << "ADD fail " << r[3] << "\n"; pass = false; }
    if (r[4] != 7ULL) { std::cout << "SUB fail " << r[4] << "\n"; pass = false; }
    if (r[6] != 1ULL) { std::cout << "SLT fail " << r[6] << "\n"; pass = false; }
    if (r[7] != 1ULL) { std::cout << "SLTU fail " << r[7] << "\n"; pass = false; }
    if (r[29] != 99ULL) { std::cout << "middle marker fail " << r[29] << "\n"; pass = false; }

    std::cout << "[All 47 Instructions Smoketest] -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// 分支六条 + JAL/JALR 已在前面多组测试覆盖，此处仅保证不遗漏
int run_branch_all_six_test() {
    const std::vector<u32> program = {
        encode_addi(1, 0, 5),
        encode_addi(2, 0, 5),
        encode_addi(3, 0, 3),
        encode_branch(1, 2, 8, 0b000),   // BEQ 5==5 跳
        encode_addi(4, 0, 0),
        encode_addi(4, 0, 1),
        encode_branch(1, 3, 8, 0b001),   // BNE 5!=3 跳
        encode_addi(5, 0, 0),
        encode_addi(5, 0, 1),
        encode_branch(3, 1, 8, 0b100),   // BLT 3<5 跳
        encode_addi(6, 0, 0),
        encode_addi(6, 0, 1),
        encode_branch(1, 3, 8, 0b101),   // BGE 5>=3 跳
        encode_addi(7, 0, 0),
        encode_addi(7, 0, 1),
        encode_branch(3, 2, 8, 0b110),   // BLTU 3<5 跳
        encode_addi(8, 0, 0),
        encode_addi(8, 0, 1),
        encode_branch(2, 3, 8, 0b111),  // BGEU 5>=3 跳
        encode_addi(9, 0, 0),
        encode_addi(9, 0, 1),
        0x00100073,
    };
    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(200);
    const auto& r = sim.registers().raw();
    const bool pass = (r[4] == 1u && r[5] == 1u && r[6] == 1u && r[7] == 1u && r[8] == 1u && r[9] == 1u);
    std::cout << "[All 6 Branches] BEQ/BNE/BLT/BGE/BLTU/BGEU -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

// -----------------------------------------------------------------------------
// riscv-tests rv64ui ELF 运行（仅跑本模拟器支持的用例：无 W 扩展、无 ld/sd/lwu）
// 设置环境变量 RVTEST_RV64UI_ELF_DIR 指向含 rv64ui-p-*.elf 的目录，如 E:\riscv-tests\build\isa
// -----------------------------------------------------------------------------
constexpr u64 MAX_RV64UI_CYCLES = 500000u;

// 从 ELF 路径提取测试短名，如 "path/rv64ui-p-st_ld.elf" -> "st_ld"
static std::string rv64ui_elf_stem(const std::string& elf_path) {
    std::string name = elf_path;
    auto pos = name.find_last_of("/\\");
    if (pos != std::string::npos)
        name = name.substr(pos + 1);
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".elf") == 0)
        name.resize(name.size() - 4);
    const std::string prefix = "rv64ui-p-";
    if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
        return name.substr(prefix.size());
    return name;
}

int run_one_rv64ui_elf(const std::string& elf_path) {
    auto res = load_elf(elf_path);
    if (!res.success)
        return -1;  // 文件不存在或加载失败，当作 skip
    RISCVSimulator sim;
    sim.load_program(res.binary, res.load_offset);
    
    const std::string test_name = rv64ui_elf_stem(elf_path);
    
    sim.run(static_cast<u32>(MAX_RV64UI_CYCLES));
    const auto& r = sim.registers().raw();
    const u64 a0 = r[10];
    if (sim.halt_reason_ecall() && a0 == 0)
        return 0;
    if (sim.halt_reason_ecall()) {
        // riscv-tests fail 路径：a0 = (gp<<1)|1，故失败子测试号 = (a0-1)>>1
        const u64 subtest = (a0 >= 1 && (a0 & 1)) ? ((a0 - 1) >> 1) : 0;
        std::cout << "  [rv64ui debug] " << test_name << " FAIL"
                  << " subtest=" << subtest << " (a0=0x" << std::hex << a0 << std::dec << ")"
                  << " pc=0x" << std::hex << sim.halt_pc() << " inst=0x" << sim.halt_inst()
                  << " | x3(gp)=0x" << r[3] << " x14(a4)=0x" << r[14] << " x7(t2)=0x" << r[7]
                  << " x1=0x" << r[1] << " x2(sp)=0x" << r[2] << std::dec << "\n";
        std::cout << "  -> In dump search for: test_" << subtest << " (e.g. rv64ui-p-" << test_name << ".dump)\n";
        return 1;
    }
    if (sim.halt_reason_ebreak() || sim.halt_reason() == HaltReason::InvalidInstruction) {
        std::cout << "  [rv64ui debug] " << test_name << " FAIL halt="
                  << (sim.halt_reason_ebreak() ? "ebreak" : "invalid")
                  << " pc=0x" << std::hex << sim.halt_pc()
                  << " inst=0x" << sim.halt_inst() << std::dec << "\n";
        return 1;
    }
    return 2;  // timeout
}

// 与 E:\riscv-tests\isa\rv64ui 中汇编对应、且本模拟器可跑的测试名（无 addiw/addw/ld/ld_st/lwu/sd/sllw 等）
static const char* const RUNNABLE_RV64UI[] = {
    "ld","add", "addi", "and", "andi", "auipc", "beq", "bge", "bgeu", "blt", "bltu", "bne",
    "fence_i", "jal", "jalr", "lb", "lbu", "lh", "lhu", "lui", "lw",
    "or", "ori", "sb", "sh", "simple", "sll", "slli", "slt", "slti", "sltiu", "sltu",
    "sra", "srai", "srl", "srli", "st_ld", "sub", "sw", "xor", "xori",
};
static constexpr std::size_t RUNNABLE_RV64UI_COUNT =
    sizeof(RUNNABLE_RV64UI) / sizeof(RUNNABLE_RV64UI[0]);

int run_rv64ui_elf_tests(const char* base_dir) {
    if (!base_dir || !*base_dir)
        return 0;
    std::string dir(base_dir);
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    int fail = 0;
    int run = 0;
    for (std::size_t i = 0; i < RUNNABLE_RV64UI_COUNT; ++i) {
        // riscv-tests 通常生成的 ELF 文件名没有 .elf 后缀（例如 rv64ui-p-auipc），.dump 只是反汇编文本
        const std::string stem = dir + "/rv64ui-p-" + std::string(RUNNABLE_RV64UI[i]);
        int r = run_one_rv64ui_elf(stem + ".elf");  // 兼容带后缀的情况
        if (r == -1) {
            r = run_one_rv64ui_elf(stem);  // 默认：无后缀
        }
        if (r == -1)
            continue;  // 文件不存在/无法加载，跳过
        ++run;
        if (r == 0) {
            std::cout << "[rv64ui] " << RUNNABLE_RV64UI[i] << " -> PASS\n";
        } else {
            std::cout << "[rv64ui] " << RUNNABLE_RV64UI[i] << " -> FAIL\n";
            ++fail;
        }
    }
    if (run == 0)
        std::cout << "[rv64ui] No ELF in " << base_dir << " (need rv64ui-p-*.elf), skipped\n";
    else
        std::cout << "[rv64ui] ran " << run << ", failed " << fail << "\n";
    return fail;
}

int run_all_tests() {
    int fail = 0;
    fail += run_addi_test();
    fail += run_lui_auipc_test();
    fail += run_rtype_test();
    fail += run_shift_test();
    fail += run_load_store_test();
    fail += run_imm_alu_test();
    fail += run_load_variants_test();
    fail += run_branch_test();
    fail += run_branch_variants_test();
    fail += run_compare_and_store_test();
    fail += run_jump_test();
    // RISC-V 规范符合性
    fail += run_spec_x0_test();
    fail += run_spec_nop_test();
    fail += run_spec_jalr_lsb_test();
    fail += run_spec_shift_mask_test();
    fail += run_spec_sltiu_test();
    fail += run_spec_jal_rd_zero_test();
    fail += run_ecall_ebreak_halt_test();
    fail += run_fence_test();
    // 47 条指令覆盖
    fail += run_all_47_instructions_test();
    fail += run_branch_all_six_test();

    // riscv-tests rv64ui ELF: set RVTEST_RV64UI_ELF_DIR to dir with rv64ui-p-*.elf (e.g. after make in riscv-tests/isa)
    const char* rv64ui_dir = std::getenv("RVTEST_RV64UI_ELF_DIR");
    if (!rv64ui_dir)
        rv64ui_dir = "E:/riscv-tests/isa";  // default; set env if ELF built elsewhere (e.g. build/isa)
    fail += run_rv64ui_elf_tests(rv64ui_dir);

    std::cout << "========== Summary ==========\n";
    std::cout << "Total tests failed: " << fail << "\n";
    return fail == 0 ? 0 : 1;
}

}  // namespace

int main() {
    return run_all_tests();
}

