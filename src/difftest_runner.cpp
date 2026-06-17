// difftest_runner.cpp
// 差分测试运行器：把 Ciliphen/riscv-lab 的 C++ 参考模型 (rv_core) 与本地后端模拟器
// (RISCVSimulator) 锁步运行，对比每条提交指令的 PC 与写回信息。
//
// 用法（与 Ciliphen/difftest 的 lab9 目标对齐）：
//   difftest_runner <test.bin|test.elf> [--max-cycles N] [--max-instr N] [--debug]
//   difftest_runner --elf <test.elf>
//   difftest_runner --bin <test.bin>
//
// 支持 ELF 和裸二进制（riscv-tests 的 .bin 文件，lab9 风格）。
//
// 退出码：
//   0  - 一致
//   1  - 写回信息不一致
//   2  - 超时

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <fstream>

#include "riscv/elf_loader.h"
#include "riscv/simulator.h"

// ciliphen 的代码。
// rv_systembus.hpp 会 #include <assert.h> 定义 assert(expr) 宏。
// 而 mmio_mem.hpp 使用了 2 参数的 assert(x, y)，
// 所以必须在包含完 rv_systembus 后、包含 mmio_mem 前重定义 assert。
#include "ciliphen/rv_systembus.hpp"

#ifdef assert
#undef assert
#endif
inline void assert(bool, const char* = "") {}
#include "ciliphen/mmio_mem.hpp"
#include "ciliphen/rv_core.hpp"

// 模拟器内存大小与 ciliphen 对齐
static constexpr uint64_t kMemSize = 128ULL * 1024 * 1024;
static constexpr uint64_t kMemBase = 0x80000000ULL;

// 5 级流水线的预热周期数
static constexpr int kPipelineWarmup = 4;

struct Args {
    std::string file_path;
    uint64_t max_cycles = 2000000ULL;
    uint64_t max_instr = 0;  // 0 = 不限
    bool force_raw = false;  // 强制按 .bin 处理
};

/**
 * @brief 判断文件是否以 ELF 魔数（0x7F 'E' 'L' 'F'）开头。
 *
 * 用于在按 ELF/裸二进制模式之间自动判定。
 *
 * @param path 文件路径
 * @return true 表示是 ELF 文件
 */
static bool starts_with_elf(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    unsigned char ident[4] = {0};
    f.read(reinterpret_cast<char*>(ident), 4);
    return f.gcount() == 4 && ident[0] == 0x7f && ident[1] == 'E' && ident[2] == 'L' && ident[3] == 'F';
}

static Args ParseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--max-cycles") == 0 && i + 1 < argc) {
            args.max_cycles = std::strtoull(argv[++i], nullptr, 0);
        } else if (std::strcmp(argv[i], "--max-instr") == 0 && i + 1 < argc) {
            args.max_instr = std::strtoull(argv[++i], nullptr, 0);
        } else if (std::strcmp(argv[i], "--bin") == 0 && i + 1 < argc) {
            args.file_path = argv[++i];
            args.force_raw = true;
        } else if (std::strcmp(argv[i], "--elf") == 0 && i + 1 < argc) {
            args.file_path = argv[++i];
            args.force_raw = false;
        } else if (argv[i][0] != '-') {
            args.file_path = argv[i];
        }
    }
    return args;
}

/**
 * @brief 差分测试运行器主入口。
 *
 * 流程：
 *   1. 解析参数，加载 ELF/裸二进制到本地后端模拟器
 *   2. 构造 ciliphen 参考模型 (rv_core)
 *   3. 后端预热 kPipelineWarmup 个周期，填满流水线
 *   4. 每周期：后端 run(1) → 同步推进 ref.step → 比对 PC/rd/wdata
 *
 * 退出码：0=一致, 1=写回不一致, 2=超时
 */
int main(int argc, char** argv) {
    Args args = ParseArgs(argc, argv);
    if (args.file_path.empty()) {
        std::fprintf(stderr, "Usage: %s <test.bin|test.elf> [--max-cycles N] [--max-instr N]\n", argv[0]);
        std::fprintf(stderr, "       %s --bin <test.bin>\n", argv[0]);
        std::fprintf(stderr, "       %s --elf <test.elf>\n", argv[0]);
        return 2;
    }

    // === 加载文件（自动按 ELF 魔数判断） ===
    riscv::ElfLoadResult elf;
    if (args.force_raw) {
        elf = riscv::load_raw_binary(args.file_path);
    } else {
        if (starts_with_elf(args.file_path)) {
            elf = riscv::load_elf(args.file_path);
        } else {
            elf = riscv::load_raw_binary(args.file_path);
        }
    }
    if (!elf.success) {
        std::fprintf(stderr, "Failed to load file: %s\n", elf.error.c_str());
        return 2;
    }
    // 仅 ELF 文件才有 tohost 符号；.bin 直接是 0
    const uint64_t tohost = args.force_raw ? 0 : riscv::find_tohost_address(args.file_path);

    // === 构造 ciliphen 参考 ===
    rv_systembus ref_bus;
    mmio_mem* ref_mem = new mmio_mem(kMemSize, args.file_path.c_str());
    // ciliphen 的 mmio_mem 从地址 0 开始；add_dev(0x80000000, 0x80000000) 表示设备内部地址 0 = 系统地址 0x80000000
    ref_bus.add_dev(kMemBase, kMemBase, ref_mem);
    rv_core ref(ref_bus);
    ref.jump(kMemBase);  // 与后端模拟器一致

    // === 构造本端后端 ===
    riscv::RISCVSimulator sim;
    sim.load_program(elf.binary, elf.load_offset);
    if (tohost != 0) sim.set_tohost_address(tohost);

    // === 预热：让流水线填满（参考的 step() 同样会消耗指令，但参考不需要预热） ===
    // 为保证对比严格对齐"第 N 条提交指令"，我们对齐方式：
    //   1) 后端先预热 kPipelineWarmup 个周期（不比较）
    //   2) 之后每周期：后端先 run(1) 再 ref.step()，每次对比一次
    for (int i = 0; i < kPipelineWarmup; ++i) {
        sim.run(1);
    }

    uint64_t total_instr = 0;
    uint64_t total_cycle = 0;
    uint64_t commit_cycle = 0;  // 参考模型已提交的指令数
    bool match = true;
    std::string fail_reason;

    while (total_cycle < args.max_cycles) {
        // 本地后端推进一周期（此周期会有一条指令完成 WB，或流水线 bubble）
        sim.run(1);
        ++total_cycle;

        // 跳过尚未完成 WB 的周期（流水线 bubble / 冲刷）
        // 这些周期本地没有提交，不应推进参考模型
        if (!sim.last_wb_result.valid) continue;

        // 同步推进参考（ref 也是 1 条/步）
        ref.step(false, false, false, false);
        ++commit_cycle;

        // Debug logging
        if (getenv("DFT_DEBUG")) {
            std::fprintf(stderr, "[c=%lu commit=%lu] sim.valid=%d sim.pc=0x%lx ref.pc=0x%lx ref.rd=0x%lx ref.wd=0x%lx\n",
                         total_cycle, commit_cycle,
                         (int)sim.last_wb_result.valid,
                         sim.last_wb_result.pc,
                         ref.debug_pc, ref.debug_reg_num, ref.debug_reg_wdata);
        }

        if (sim.halted()) {
            // 后端因 ECALL/EBREAK 停机；ref 也应该已经看到 ECALL/EBREAK
            // 我们至少做一次最终比较，然后退出
            const auto& wb = sim.last_wb_result;
            bool pc_match = (ref.debug_pc == wb.pc);
            bool reg_match = (ref.debug_reg_num == wb.wb_raddr) &&
                             (ref.debug_reg_wdata == wb.wb_rdata);
            if (!pc_match || !reg_match) {
                match = false;
                std::fprintf(stderr,
                             "\n[FAIL] halt mismatch at instr %lu (cycle %lu):\n"
                             "  ref:    pc=0x%016lx wnum=0x%02lx wdata=0x%016lx\n"
                             "  local:  pc=0x%016lx wnum=0x%02x wdata=0x%016lx\n",
                             commit_cycle, total_cycle,
                             ref.debug_pc, ref.debug_reg_num, ref.debug_reg_wdata,
                             wb.pc, wb.wb_raddr, wb.wb_rdata);
                fail_reason = "halt mismatch";
            }
            break;
        }

        ++total_instr;
        const auto& wb = sim.last_wb_result;

        // 与 ciliphen sim_mycpu.cpp 的策略一致：
        //   只有当参考模型 debug_reg_num != 0（即真正写回非 x0 寄存器）时才比对 wnum/wdata。
        //   x0 的写回是 no-op，比对无意义。
        const bool ref_writes = (ref.debug_reg_num != 0);
        const bool pc_match = (ref.debug_pc == wb.pc);
        const bool reg_match = !ref_writes ||
                               ((wb.wb_raddr == ref.debug_reg_num) &&
                                (wb.wb_rdata == ref.debug_reg_wdata));

        if (!pc_match || !reg_match) {
            match = false;
            std::fprintf(stderr,
                         "\n[FAIL] mismatch at commit %lu (cycle %lu):\n"
                         "  ref:    pc=0x%016lx wnum=0x%02lx wdata=0x%016lx\n"
                         "  local:  pc=0x%016lx wnum=0x%02x wdata=0x%016lx (wb_en=%d)\n",
                         commit_cycle, total_cycle,
                         ref.debug_pc, ref.debug_reg_num, ref.debug_reg_wdata,
                         wb.pc, wb.wb_raddr, wb.wb_rdata, (int)wb.wb_en);
            fail_reason = "writeback mismatch";
            break;
        }

        if (args.max_instr != 0 && total_instr >= args.max_instr) {
            break;
        }
    }

    if (total_cycle >= args.max_cycles) {
        std::fprintf(stderr, "\n[FAIL] timeout after %lu cycles, %lu instrs\n",
                     total_cycle, total_instr);
        delete ref_mem;
        return 2;
    }

    if (match) {
        std::printf("[PASS] %lu instrs / %lu cycles, all writeback info matches\n",
                    total_instr, total_cycle);
    } else {
        std::fprintf(stderr, "Reason: %s\n", fail_reason.c_str());
    }

    delete ref_mem;
    return match ? 0 : 1;
}
