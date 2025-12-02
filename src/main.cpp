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

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string_view(argv[1]) == "--test-addi") {
        return run_addi_test();
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

        std::cout << "执行完成，周期数: " << sim.cycle() << "\n";
        dump_registers(sim.registers());
    } catch (const std::exception& ex) {
        std::cerr << "错误: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}

