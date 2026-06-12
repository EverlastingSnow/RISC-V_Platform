// 使用 riscv-tests 的 rv32ui-p-* 等 ELF 测试本模拟器。
// 用法: run_riscv_tests <elf1> [elf2 ...]  或  run_riscv_tests --dir <path>
// 默认加载 isa 目录下的 rv64ui、rv64um、rv64mi、rv64si 测试
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

#include "riscv/elf_loader.h"
#include "riscv/simulator.h"
#include "riscv/types.h"

namespace fs = std::filesystem;

namespace {

constexpr riscv::u64 MAX_CYCLES_PER_TEST = 500'000;

bool find_isa_dir(std::string& out_dir) {
    std::vector<std::string> candidates = {"isa", "../isa", "../../isa"};
    for (const auto& cand : candidates) {
        try {
            if (fs::exists(cand) && fs::is_directory(cand)) {
                out_dir = cand;
                return true;
            }
        } catch (...) {}
    }
    return false;
}

struct TestGroup {
    const char* name;
    const char* prefix;
};

const std::vector<TestGroup> TEST_GROUPS = {
    {"rv64ui", "rv64ui-p-"},
    {"rv64um", "rv64um-p-"},
    {"rv64mi", "rv64mi-p-"},
    {"rv64si", "rv64si-p-"},
};

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
            bool is_elf = false;
            if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".elf") == 0)
                is_elf = true;
            else if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".dump") == 0)
                continue;
            else {
                for (const auto& group : TEST_GROUPS) {
                    const std::string& prefix = group.prefix;
                    if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
                        is_elf = true;
                        break;
                    }
                }
            }
            if (is_elf)
                out.push_back(p);
        }
    } catch (const std::exception& ex) {
        std::cerr << "遍历目录失败: " << ex.what() << "\n";
        return {};
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> collect_elfs_from_isa_dir(const std::string& isa_dir) {
    std::vector<std::string> out;
    try {
        for (const auto& e : fs::directory_iterator(isa_dir)) {
            if (!e.is_regular_file())
                continue;
            std::string name = e.path().filename().string();
            if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".dump") == 0)
                continue;
            bool matched = false;
            for (const auto& group : TEST_GROUPS) {
                const std::string& prefix = group.prefix;
                if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
                    matched = true;
                    break;
                }
            }
            if (matched)
                out.push_back(e.path().string());
        }
    } catch (const std::exception& ex) {
        std::cerr << "遍历目录失败: " << ex.what() << "\n";
        return {};
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> collect_elfs_by_groups(const std::string& isa_dir, const std::vector<std::string>& groups) {
    std::vector<std::string> out;
    try {
        for (const auto& e : fs::directory_iterator(isa_dir)) {
            if (!e.is_regular_file())
                continue;
            std::string name = e.path().filename().string();
            if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".dump") == 0)
                continue;
            
            for (const auto& group : groups) {
                std::string prefix = group + "-p-";
                if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
                    out.push_back(e.path().string());
                    break;
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "遍历目录失败: " << ex.what() << "\n";
        return {};
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool is_valid_group(const std::string& group_name) {
    for (const auto& g : TEST_GROUPS) {
        if (g.name == group_name)
            return true;
    }
    return false;
}

std::vector<std::string> parse_groups(const std::string& str) {
    std::vector<std::string> result;
    std::string current;
    for (char c : str) {
        if (c == ',') {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty())
        result.push_back(current);
    return result;
}

void print_usage(const char* prog_name) {
    std::cerr << "用法:\n";
    std::cerr << "  " << prog_name << " --isa [group1[,group2...]]\n";
    std::cerr << "  " << prog_name << " --dir <目录路径>\n";
    std::cerr << "  " << prog_name << " <file1> [file2.elf ...]\n";
    std::cerr << "\n可用测试组:\n";
    for (const auto& g : TEST_GROUPS) {
        std::cerr << "  " << g.name << "\n";
    }
    std::cerr << "\n示例:\n";
    std::cerr << "  " << prog_name << " --isa rv64ui\n";
    std::cerr << "  " << prog_name << " --isa rv64ui,rv64um\n";
    std::cerr << "  " << prog_name << " --isa (所有组)\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> elfs;
    
    if (argc < 2) {
        print_usage(argv[0] ? argv[0] : "run_riscv_tests");
        return 1;
    }

    std::string arg1 = argv[1];
    if (arg1 == "--isa") {
        std::vector<std::string> groups;
        if (argc >= 3) {
            groups = parse_groups(argv[2]);
            for (const auto& g : groups) {
                if (!is_valid_group(g)) {
                    std::cerr << "未知测试组: " << g << "\n";
                    print_usage(argv[0] ? argv[0] : "run_riscv_tests");
                    return 1;
                }
            }
        } else {
            for (const auto& g : TEST_GROUPS)
                groups.push_back(g.name);
        }
        
        std::string isa_dir;
        if (!find_isa_dir(isa_dir)) {
            std::cerr << "无法找到 isa 目录。\n";
            return 1;
        }
        
        elfs = collect_elfs_by_groups(isa_dir, groups);
        if (elfs.empty()) {
            std::cerr << isa_dir << " 目录下未找到匹配的测试文件。\n";
            return 1;
        }
        std::cout << "从 " << isa_dir << " 目录加载测试: ";
        for (size_t i = 0; i < groups.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << groups[i];
        }
        std::cout << "\n";
    } else if (arg1 == "--dir") {
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
