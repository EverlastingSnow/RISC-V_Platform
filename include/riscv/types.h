#pragma once

#include <cstdint>

namespace riscv {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

constexpr u32 XLEN = 32;
constexpr u32 REGISTER_COUNT = 32;
constexpr u32 DEFAULT_MEMORY_SIZE = 4 * 1024 * 1024;  // 4 MiB
constexpr u32 RESET_VECTOR = 0x80000000;

inline constexpr u32 mask(u32 bits) {
    return (bits == 32) ? 0xFFFF'FFFFu : ((1u << bits) - 1u);
}

template <typename T>
constexpr T sign_extend(T value, u32 from_bits) {
    const T sign_bit = static_cast<T>(1) << (from_bits - 1);
    return static_cast<T>((value ^ sign_bit) - sign_bit);
}

}  // namespace riscv


