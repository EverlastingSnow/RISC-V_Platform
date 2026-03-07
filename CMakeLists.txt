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
add_executable(riscv_instr_tests test/instruction_tests.cpp src/elf_loader.cpp)
target_link_libraries(riscv_instr_tests PRIVATE riscv_core)

# 使用 riscv-tests 的 ELF 测试本模拟器
add_executable(run_riscv_tests src/run_riscv_tests.cpp src/elf_loader.cpp)
target_include_directories(run_riscv_tests PRIVATE include)
target_link_libraries(run_riscv_tests PRIVATE riscv_core)

# WebSocket服务端调用的模拟器
add_executable(riscv_sim_server src/riscv_sim_server.cpp src/elf_loader.cpp)
target_include_directories(riscv_sim_server PRIVATE include)
target_link_libraries(riscv_sim_server PRIVATE riscv_core)


