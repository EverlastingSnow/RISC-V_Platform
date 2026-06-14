cmake_minimum_required(VERSION 3.20)
project(riscv_platform LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(RISCV_CORE_SOURCES
    src/memory.cpp
    src/register_file.cpp
    src/decoder.cpp
    src/csr.cpp
    src/simulator.cpp
)

add_library(riscv_core ${RISCV_CORE_SOURCES})
target_include_directories(riscv_core PUBLIC include)

add_executable(riscv_sim src/main.cpp)
target_link_libraries(riscv_sim PRIVATE riscv_core)

# 额外测试可执行程序（含 riscv-tests rv64ui ELF 运行能力）
# add_executable(riscv_instr_tests test/instruction_tests.cpp src/elf_loader.cpp)
# target_link_libraries(riscv_instr_tests PRIVATE riscv_core)

# 使用 riscv-tests 的 ELF 测试本模拟器
add_executable(run_riscv_tests src/run_riscv_tests.cpp src/elf_loader.cpp)
target_include_directories(run_riscv_tests PRIVATE include)
target_link_libraries(run_riscv_tests PRIVATE riscv_core)

# Debug runner: dump simulator state for a single ELF
add_executable(debug_test src/debug_test.cpp src/elf_loader.cpp)
target_include_directories(debug_test PRIVATE include)
target_link_libraries(debug_test PRIVATE riscv_core)

# WebSocket服务端调用的模拟器
add_executable(riscv_sim_server src/riscv_sim_server.cpp src/elf_loader.cpp)
target_include_directories(riscv_sim_server PRIVATE include)
target_link_libraries(riscv_sim_server PRIVATE riscv_core)

# === Difftest：与 Ciliphen/riscv-lab 的 C++ 参考模型锁步对比 ===
# 1) 把 ciliphen 的头文件集合编译为一个静态库
add_library(ciliphen_ref STATIC src/ciliphen_ref.cpp src/elf_loader.cpp)
# ciliphen 头文件之间的相对 include 需要 src/ciliphen 在 include 路径中
# 同时 elf_loader.cpp 需要 include/ 目录
target_include_directories(ciliphen_ref PUBLIC src include)
target_compile_options(ciliphen_ref PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable)

# 2) difftest_runner 可执行
add_executable(difftest_runner src/difftest_runner.cpp)
target_include_directories(difftest_runner PRIVATE include)
target_link_libraries(difftest_runner PRIVATE riscv_core ciliphen_ref)


