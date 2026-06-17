#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "riscv/simulator.h"

namespace {

/**
 * @brief 从指定路径以二进制方式读取整个文件到 vector<u8>。
 *
 * @param path 文件路径
 * @return 文件内容的字节数组
 * @throws std::runtime_error 当文件无法打开时
 */
std::vector<riscv::u8> read_binary(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("无法打开文件: " + path);
    }
    return std::vector<riscv::u8>(std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>());
}

/**
 * @brief 将寄存器文件中的所有寄存器以 `xN (alias) = 0x...` 的格式输出到 stdout。
 *
 * 用于程序执行结束后人工核对寄存器状态。
 *
 * @param regs 寄存器文件引用
 */
void dump_registers(const riscv::RegisterFile& regs) {
    const auto& raw = regs.raw();
    for (std::size_t i = 0; i < raw.size(); ++i) {
        std::cout << "x" << i << " (" << riscv::reg_name(static_cast<riscv::u32>(i)) << ") = 0x"
                  << std::hex << raw[i] << std::dec << "\n";
    }
}

}  // namespace

/**
 * @brief 简单 CLI 入口：加载裸二进制并运行指定周期数，最后输出寄存器。
 *
 * 用法：
 *   - riscv_sim <program.bin>           运行 10,000 周期
 *   - riscv_sim <program.bin> [cycles]  运行指定周期数
 *
 * 返回码：
 *   - 0：执行完成
 *   - 1：参数错误或文件读写错误
 *   - 2：执行过程中检测到非法指令
 */
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "用法: riscv_sim <program.bin> [cycles]\n";
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
