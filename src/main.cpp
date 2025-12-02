#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "riscv/simulator.h"

namespace {

std::vector<riscv::u8> read_binary(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("无法打开文件: " + path);
    }
    return std::vector<riscv::u8>(std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>());
}

void dump_registers(const riscv::RegisterFile& regs) {
    const auto& raw = regs.raw();
    for (std::size_t i = 0; i < raw.size(); ++i) {
        std::cout << "x" << i << " (" << riscv::reg_name(static_cast<riscv::u32>(i)) << ") = 0x"
                  << std::hex << raw[i] << std::dec << "\n";
    }
}

constexpr riscv::u32 encode_addi(riscv::u32 rd, riscv::u32 rs1, riscv::s32 imm) {
    if (imm < -2048 || imm > 2047) {
        throw std::runtime_error("ADDI 立即数超出12位范围");
    }
    const riscv::u32 imm12 = static_cast<riscv::u32>(imm) & 0xFFFu;
    return (imm12 << 20) | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0b0010011;
}

constexpr riscv::u32 encode_lui(riscv::u32 rd, riscv::u32 imm20) {
    return (imm20 << 12) | (rd << 7) | 0b0110111;
}

constexpr riscv::u32 encode_auipc(riscv::u32 rd, riscv::u32 imm20) {
    return (imm20 << 12) | (rd << 7) | 0b0010111;
}

constexpr riscv::u32 encode_r(riscv::u32 rd, riscv::u32 rs1, riscv::u32 rs2,
                              riscv::u32 funct3, riscv::u32 funct7) {
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | 0b0110011;
}

constexpr riscv::u32 encode_jal(riscv::u32 rd, riscv::s32 imm) {
    // imm 必须是 4 对齐的字节偏移
    const riscv::u32 uimm = static_cast<riscv::u32>(imm);
    const riscv::u32 bit20 = (uimm >> 20) & 0x1;
    const riscv::u32 bits10_1 = (uimm >> 1) & 0x3FF;
    const riscv::u32 bit11 = (uimm >> 11) & 0x1;
    const riscv::u32 bits19_12 = (uimm >> 12) & 0xFF;
    const riscv::u32 encoded =
        (bit20 << 31) | (bits19_12 << 12) | (bit11 << 20) | (bits10_1 << 21);
    return encoded | (rd << 7) | 0b1101111;
}

constexpr riscv::u32 encode_jalr(riscv::u32 rd, riscv::u32 rs1, riscv::s32 imm) {
    const riscv::u32 imm12 = static_cast<riscv::u32>(imm) & 0xFFFu;
    return (imm12 << 20) | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0b1100111;
}

constexpr riscv::u32 encode_beq(riscv::u32 rs1, riscv::u32 rs2, riscv::s32 imm) {
    const riscv::u32 uimm = static_cast<riscv::u32>(imm);
    const riscv::u32 bit12 = (uimm >> 12) & 0x1;
    const riscv::u32 bit11 = (uimm >> 11) & 0x1;
    const riscv::u32 bits10_5 = (uimm >> 5) & 0x3F;
    const riscv::u32 bits4_1 = (uimm >> 1) & 0xF;
    const riscv::u32 encoded =
        (bit12 << 31) | (bits10_5 << 25) | (bits4_1 << 8) | (bit11 << 7);
    return encoded | (rs2 << 20) | (rs1 << 15) | (0b000 << 12) | 0b1100011;
}

constexpr riscv::u32 encode_sw(riscv::u32 rs2, riscv::u32 rs1, riscv::s32 imm) {
    const riscv::u32 uimm = static_cast<riscv::u32>(imm);
    const riscv::u32 imm11_5 = (uimm >> 5) & 0x7F;
    const riscv::u32 imm4_0 = uimm & 0x1F;
    return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) |
           (imm4_0 << 7) | 0b0100011;
}

constexpr riscv::u32 encode_lw(riscv::u32 rd, riscv::u32 rs1, riscv::s32 imm) {
    const riscv::u32 imm12 = static_cast<riscv::u32>(imm) & 0xFFFu;
    return (imm12 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0000011;
}

std::vector<riscv::u8> assemble_words(const std::vector<riscv::u32>& words) {
    std::vector<riscv::u8> binary;
    binary.reserve(words.size() * 4);
    for (auto word : words) {
        binary.push_back(static_cast<riscv::u8>(word & 0xFF));
        binary.push_back(static_cast<riscv::u8>((word >> 8) & 0xFF));
        binary.push_back(static_cast<riscv::u8>((word >> 16) & 0xFF));
        binary.push_back(static_cast<riscv::u8>((word >> 24) & 0xFF));
    }
    return binary;
}

int run_addi_test() {
    using namespace riscv;
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
    using namespace riscv;
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
        regs[1] == 0x12345000u &&
        regs[2] == (riscv::RESET_VECTOR + 0x00010004u);

    std::cout << "[LUI/AUIPC Test] x1=0x" << std::hex << regs[1]
              << " x2=0x" << regs[2] << std::dec
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

int run_rtype_test() {
    using namespace riscv;
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

int run_load_store_test() {
    using namespace riscv;
    // x1 = RESET_VECTOR (与指令同一基地址的内存区域)
    // x2 = 0xdeadbeef
    // sw x2, 0(x1)
    // lw x3, 0(x1)  -> 0xdeadbeef
    const std::vector<u32> program = {
        // x1 = RESET_VECTOR = 0x80000000
        encode_lui(1, 0x80000),
        // 构造 0xDEADBEEF：选择 imm12 与 imm20 满足：LUI(imm20) + imm12 = 0xDEADBEEF
        // const = 0xDEADBEEF, imm12 = sign_extend(0xEEF) = -273, imm20 = (const - imm12) >> 12 = 0xDEADC
        encode_lui(2, 0xDEADC),
        encode_addi(2, 2, -273),
        encode_sw(2, 1, 0),
        encode_lw(3, 1, 0),
        0x00100073,
    };

    RISCVSimulator sim;
    sim.load_program(assemble_words(program));
    sim.run(50);

    const auto& regs = sim.registers().raw();
    const bool pass = regs[3] == 0xDEADBEEFu;

    std::cout << "[LW/SW Test] x3=0x" << std::hex << regs[3] << std::dec
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

int run_branch_test() {
    using namespace riscv;
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

int run_jump_test() {
    using namespace riscv;
    // 测试 JAL/JALR：
    // 0:   jal x1, +8         ; x1 = 4, 跳到 pc=8
    // 4:   addi x2,x0,1       ; 不应执行
    // 8:   addi x3,x0,2
    // 12:  jalr x4,x1,4       ; x4=16, 跳到 pc=(4+4)=8(对齐后) -> 形成小循环，再次执行 addi x3,x0,2
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
    // 这里只检查：x1 被写为返回地址 RESET_VECTOR+4，x3 最终为 2（证明跳转生效），x2 仍为 0（证明被跳过）
    const bool pass = regs[1] == (riscv::RESET_VECTOR + 4) && regs[2] == 0 && regs[3] == 2;

    std::cout << "[JAL/JALR Test] x1=" << regs[1]
              << " x2=" << regs[2] << " x3=" << regs[3]
              << " -> " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}

int run_all_tests() {
    int fail = 0;
    fail += run_addi_test();
    fail += run_lui_auipc_test();
    fail += run_rtype_test();
    fail += run_load_store_test();
    fail += run_branch_test();
    fail += run_jump_test();

    std::cout << "========== Summary ==========\n";
    std::cout << "Total tests failed: " << fail << "\n";
    return fail == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2) {
        const std::string_view arg1(argv[1]);
        if (arg1 == "--test-addi") {
            return run_addi_test();
        }
        if (arg1 == "--run-tests") {
            return run_all_tests();
        }
    }

    if (argc < 2) {
        std::cerr << "用法: riscv_sim <program.bin> [cycles]\n";
        std::cerr << "或:   riscv_sim --test-addi  # 运行ADDI单元测试\n";
        return 1;
    }

    try {
        const std::string program_path = argv[1];
        const std::vector<riscv::u8> binary = read_binary(program_path);
        const std::uint32_t cycles = (argc >= 3) ? std::stoul(argv[2]) : 10'000;

        riscv::RISCVSimulator sim;
        sim.load_program(binary);
        sim.run(cycles);

        if (sim.halt_reason() == riscv::HaltReason::InvalidInstruction) {
            std::cerr << "执行过程中检测到非法指令，PC=0x" << std::hex << sim.halt_pc()
                      << " 指令=0x" << sim.halt_inst() << std::dec << "\n";
            std::cerr << "请检查/重新导入程序文件。\n";
            return 2;  // 让前端/脚本可以根据返回码识别非法指令
        }

        std::cout << "执行完成，周期数: " << sim.cycle() << "\n";
        dump_registers(sim.registers());
    } catch (const std::exception& ex) {
        std::cerr << "错误: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}

