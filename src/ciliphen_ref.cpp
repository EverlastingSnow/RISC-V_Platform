// ciliphen_ref.cpp
//
// @file ciliphen_ref.cpp
// @brief ciliphen 参考模型的 header-only 聚合 wrapper。
//
// 把 ciliphen/riscv-lab/difftest/src 下的所有头文件聚合到一个 translation unit 里，
// 供 difftest_runner 链接为一个静态库。
//
// 这里直接 #include 所有 .hpp；由于它们是 header-only，且互相 #include，
// 单一 TU 内只会有一份定义。
//
// 注意：本文件不实现任何逻辑，仅做头文件聚合与全局符号占位。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <queue>
#include <vector>

// === 提前定义 ciliphen 代码需要的 extern 全局（必须在全局作用域） ===
// ciliphen 的若干 .hpp 中引用了这些 extern 变量，
// 这里在全局命名空间下提供一份定义（值与 ciliphen 运行时一致即可）。
bool run_riscv_test = true;
bool perf_counter = false;
bool only_modeM = false;
bool running = true;
long long total_instr = 0;
long long total_cycle = 0;

// === 屏蔽 ciliphen 的 assert 宏 ===
// ciliphen 用自定义的 void assert(bool, const char*)，与标准 assert(expr) 冲突。
// mmio_mem.hpp 是唯一用 2 参数 assert 的，必须在包含前把宏 unset 并声明一个兼容函数。
// 这里按 ciliphen sim_mycpu.cpp 的两段式处理：先包含不需要 2 参数 assert 的头文件，
// 然后 #undef assert + 声明函数，再包含 mmio_mem.hpp。

// === 第一阶段：先包含不需要 2 参数 assert 的 ciliphen 头文件 ===
// 这些头文件本身不依赖自定义 assert，可以直接包含。
#include "ciliphen/rv_common.hpp"
#include "ciliphen/rv_systembus.hpp"
#include "ciliphen/rv_sv39.hpp"
#include "ciliphen/mmio_dev.hpp"
#include "ciliphen/rv_priv.hpp"
#include "ciliphen/rv_clint.hpp"
#include "ciliphen/rv_plic.hpp"
#include "ciliphen/rv_core.hpp"
// 注：nscscc_sram*.hpp 依赖 verilated.h（Verilator），跳过

// === 第二阶段：把 assert 宏替换为兼容的 void 函数 ===
// 必须先 #undef 标准的 <cassert> 引入的 assert 宏，再提供一个签名兼容的空实现，
// 才能继续包含需要 2 参数 assert 的 mmio_mem.hpp。
#ifdef assert
#undef assert
#endif
inline void assert(bool, const char* = "") {}

// === 第三阶段：再包含用 2 参数 assert 的 mmio_mem.hpp ===
// 此时 assert 已被替换为上面的空实现函数，可以安全包含。
#include "ciliphen/mmio_mem.hpp"
