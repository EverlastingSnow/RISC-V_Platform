#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riscv/types.h"

namespace riscv {

// ELF Constants
constexpr u32 SHN_UNDEF = 0;
constexpr u64 SHF_ALLOC = 0x2;
constexpr u32 SHT_PROGBITS = 1;
constexpr u32 SHT_NOBITS = 8;

// ELF Section Header (64-bit)
struct Elf64_Shdr {
    u32 sh_name;
    u32 sh_type;
    u64 sh_flags;
    u64 sh_addr;
    u64 sh_offset;
    u64 sh_size;
    u32 sh_link;
    u32 sh_info;
    u64 sh_addralign;
    u64 sh_entsize;
};

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
