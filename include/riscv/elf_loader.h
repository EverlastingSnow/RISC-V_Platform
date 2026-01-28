#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riscv/types.h"

namespace riscv {

// 从 ELF 文件中加载 PT_LOAD 段，得到可在 load_program(binary, offset) 中使用的
// 连续二进制与相对 base 的偏移。
struct ElfLoadResult {
    std::vector<u8> binary;
    u64 load_offset{0};  // 相对 memory.base() 的偏移，binary 应通过 load_program(binary, load_offset) 加载
    bool success{false};
    std::string error;
};

ElfLoadResult load_elf32(const std::string& path, u64 memory_base = RESET_VECTOR);
ElfLoadResult load_elf64(const std::string& path, u64 memory_base = RESET_VECTOR);

// 按 ELF 魔数自动选择 32/64 位加载
ElfLoadResult load_elf(const std::string& path, u64 memory_base = RESET_VECTOR);

}  // namespace riscv
