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
    src/simulator.cpp
)

add_library(riscv_core ${RISCV_CORE_SOURCES})
target_include_directories(riscv_core PUBLIC include)

add_executable(riscv_sim src/main.cpp)
target_link_libraries(riscv_sim PRIVATE riscv_core)

