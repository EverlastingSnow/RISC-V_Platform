// 使用 riscv-tests 的 rv32ui-p-* 等 ELF 测试本模拟器。
// 用法: run_riscv_tests <elf1> [elf2 ...]  或  run_riscv_tests --dir <path>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "riscv/elf_loader.h"
#include "riscv/simulator.h"
#include "riscv/types.h"

namespace fs = std::filesystem;

namespace {

constexpr riscv::u64 MAX_CYCLES_PER_TEST = 500'000;

// 从路径提取测试短名，如 "path/rv64ui-p-st_ld" -> "st_ld"
static std::string test_stem(const std::string& path) {
    std::string name = path;
    const std::size_t sep = name.find_last_of("/\\");
    if (sep != std::string::npos)
        name = name.substr(sep + 1);
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".elf") == 0)
        name.resize(name.size() - 4);
    const std::string prefix = "rv64ui-p-";
    if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
        return name.substr(prefix.size());
    return name;
}

// riscv-tests 通过/失败：若因 ECALL 停机，则 a0==0 为通过；非法指令或 EBREAK 通常表示失败或需进一步判断
int run_one(const std::string& elf_path) {
    auto result = riscv::load_elf(elf_path);
    if (!result.success) {
        std::cerr << "ELF 加载失败: " << result.error << "\n";
        return -1;
    }

    riscv::RISCVSimulator sim;
    sim.load_program(result.binary, result.load_offset);
    sim.run(MAX_CYCLES_PER_TEST);

    const auto& r = sim.registers().raw();
    const riscv::u64 a0 = r[10];
    const std::string stem = test_stem(elf_path);

    if (sim.halt_reason() == riscv::HaltReason::InvalidInstruction) {
        std::cout << "FAIL (非法指令) " << elf_path << "\n";
        std::cout << "  [debug] pc=0x" << std::hex << sim.halt_pc() << " inst=0x" << sim.halt_inst() << std::dec << "\n";
        return 1;
    }
    if (sim.halt_reason_ecall()) {
        if (a0 == 0) {
            std::cout << "PASS " << elf_path << "\n";
            return 0;
        }
        std::cout << "FAIL (a0=0x" << std::hex << a0 << std::dec << ") " << elf_path << "\n";
        // riscv-tests fail 路径：a0 = (gp<<1)|1，失败子测试号 = (a0-1)>>1
        if (a0 >= 1 && (a0 & 1)) {
            const riscv::u64 subtest = (a0 - 1) >> 1;
            std::cout << "  [debug] 失败子测试: test_" << subtest
                      << " | pc=0x" << std::hex << sim.halt_pc() << " inst=0x" << sim.halt_inst()
                      << " x3(gp)=0x" << r[3] << " x14=0x" << r[14] << " x7=0x" << r[7]
                      << " x1=0x" << r[1] << " x2(sp)=0x" << r[2] << std::dec << "\n";
            std::cout << "  -> In dump locate: rv64ui-p-" << stem << ".dump search <test_" << subtest << ">\n";
        }
        return 1;
    }
    if (sim.halt_reason_ebreak()) {
        std::cout << "FAIL (EBREAK) " << elf_path << "\n";
        std::cout << "  [debug] pc=0x" << std::hex << sim.halt_pc() << " inst=0x" << sim.halt_inst() << std::dec << "\n";
        return 1;
    }
    if (!sim.halted()) {
        std::cout << "TIMEOUT " << elf_path << " (未在 " << MAX_CYCLES_PER_TEST << " 周期内停机)\n";
        return 2;
    }
    std::cout << "UNKNOWN " << elf_path << " (停机原因未归类)\n";
    return 2;
}

std::vector<std::string> collect_elfs(const std::string& dir) {
    std::vector<std::string> out;
    try {
        for (const auto& e : fs::directory_iterator(dir)) {
            if (!e.is_regular_file())
                continue;
            std::string p = e.path().string();
            std::string name = e.path().filename().string();
            if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".elf") == 0)
                out.push_back(p);
        }
    } catch (const std::exception& ex) {
        std::cerr << "遍历目录失败: " << ex.what() << "\n";
        return {};
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> elfs;
    if (argc < 2) {
        std::cerr << "用法:\n"
                  << "  " << (argv[0] ? argv[0] : "run_riscv_tests")
                  << " <file.elf> [file2.elf ...]\n"
                  << "  " << (argv[0] ? argv[0] : "run_riscv_tests")
                  << " --dir <目录路径>\n"
                  << "例如（需先按 riscv-tests 文档用 XLEN=32 编译）:\n"
                  << "  " << (argv[0] ? argv[0] : "run_riscv_tests")
                  << " --dir /path/to/riscv-tests/isa\n";
        return 1;
    }

    if (std::string(argv[1]) == "--dir") {
        if (argc < 3) {
            std::cerr << "请给出 --dir 后的目录路径。\n";
            return 1;
        }
        elfs = collect_elfs(argv[2]);
        if (elfs.empty()) {
            std::cerr << "该目录下未找到 .elf 文件。\n";
            return 1;
        }
    } else {
        for (int i = 1; i < argc; ++i)
            elfs.push_back(argv[i]);
    }

    int pass = 0, fail = 0, other = 0;
    for (const auto& p : elfs) {
        int r = run_one(p);
        if (r == 0)
            ++pass;
        else if (r == 1)
            ++fail;
        else
            ++other;
    }

    std::cout << "-----------\nPASS: " << pass << "  FAIL: " << fail << "  OTHER: " << other << "\n";
    return fail > 0 ? 1 : 0;
}
