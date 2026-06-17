#include "riscv/memory.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace riscv {

/**
 * @brief Memory 类构造函数。
 *
 * 分配 `size` 字节的存储并以 0 初始化，记录内存的基地址 `base`。
 *
 * @param size 内存大小（字节），默认 DEFAULT_MEMORY_SIZE (256 MiB)
 * @param base 内存的基地址，默认 RESET_VECTOR (0x80000000)
 */
Memory::Memory(std::size_t size, u64 base) : data_(size, 0), base_address_(base) {}

/**
 * @brief 将整段内存清零。
 *
 * 复位模拟器时调用，所有字节置为 0，保留 base_address_ 不变。
 */
void Memory::reset() {
    std::fill(data_.begin(), data_.end(), 0);
}

/**
 * @brief 将一段二进制程序加载到内存中。
 *
 * 加载规则：
 *   - vaddr_offset 是相对 `base_address_` 的偏移（即目标虚拟地址 - base）
 *   - 若 vaddr_offset 小于 base_address_，则按 vaddr_offset 直接作为 data_ 索引
 *     （兼容某些直接把 vaddr 当成偏移的旧用法）
 *   - 若加载范围超出 data_ 大小则抛出 std::out_of_range
 *
 * @param binary 待加载的二进制程序
 * @param vaddr_offset 目标地址相对 base 的偏移，默认为 0
 * @throws std::out_of_range 当程序超出可用内存时
 */
void Memory::load_program(const std::vector<u8>& binary, u64 vaddr_offset) {
    // vaddr_offset 是程序加载的虚拟地址偏移（相对于 base_address_）
    // 转换为 data_ 数组索引
    u64 index = (vaddr_offset >= base_address_) ? (vaddr_offset - base_address_) : vaddr_offset;
    if (index + binary.size() > data_.size()) {
        throw std::out_of_range("Program does not fit in memory");
    }
    std::copy(binary.begin(), binary.end(), data_.begin() + static_cast<std::size_t>(index));
}

/**
 * @brief 读取 1 字节（小端序，低位有效）。
 *
 * 地址在内存范围内时返回对应字节；越界则返回 0。
 *
 * @param address 目标地址
 * @return 读取到的字节值，越界时返回 0
 */
u8 Memory::read8(u64 address) const {
    if (contains(address)) {
        return data_.at(translate(address));
    }
    return 0;
}

/**
 * @brief 读取 2 字节（小端序）。
 *
 * 当地址按 2 字节对齐且完整范围都在内存中时走快速路径；
 * 否则回退到逐字节读取（read16_unaligned_slow）。
 *
 * @param address 目标地址
 * @return 读取到的 16 位值
 */
u16 Memory::read16(u64 address) const {
    if ((address & 0x1u) == 0) {
        const auto idx = translate(address);
        if (idx + 1 < data_.size()) {
            u16 value = data_.at(idx);
            value |= static_cast<u16>(data_.at(idx + 1)) << 8;
            return value;
        }
    }
    return read16_unaligned_slow(address);
}

/**
 * @brief 非对齐地址的 16 位读取回退实现。
 *
 * 逐字节读取，最多读取 2 字节；超出内存范围的字节按 0 处理。
 *
 * @param address 目标地址（可能非对齐）
 * @return 拼装后的 16 位值
 */
u16 Memory::read16_unaligned_slow(u64 address) const {
    u16 value = 0;
    for (int i = 0; i < 2; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            value |= static_cast<u16>(data_.at(translate(a))) << (i * 8);
        }
    }
    return value;
}

/**
 * @brief 读取 4 字节（小端序）。
 *
 * 当地址按 4 字节对齐且完整范围都在内存中时走快速路径；
 * 否则回退到 read32_unaligned_slow。
 *
 * @param address 目标地址
 * @return 读取到的 32 位值
 */
u32 Memory::read32(u64 address) const {
    if ((address & 0x3u) == 0 && contains(address) && contains(address + 3)) {
        const auto idx = translate(address);
        u32 value = data_.at(idx);
        value |= static_cast<u32>(data_.at(idx + 1)) << 8;
        value |= static_cast<u32>(data_.at(idx + 2)) << 16;
        value |= static_cast<u32>(data_.at(idx + 3)) << 24;
        return value;
    }
    return read32_unaligned_slow(address);
}

/**
 * @brief 非对齐地址的 32 位读取回退实现。
 *
 * 逐字节读取，最多 4 字节；越界字节按 0 处理。
 *
 * @param address 目标地址（可能非对齐）
 * @return 拼装后的 32 位值
 */
u32 Memory::read32_unaligned_slow(u64 address) const {
    u32 value = 0;
    for (int i = 0; i < 4; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            value |= static_cast<u32>(data_.at(translate(a))) << (i * 8);
        }
    }
    return value;
}

/**
 * @brief 读取 8 字节（小端序）。
 *
 * 当地址按 8 字节对齐且完整范围都在内存中时走快速路径；
 * 否则回退到 read64_unaligned_slow。
 *
 * @param address 目标地址
 * @return 读取到的 64 位值
 */
u64 Memory::read64(u64 address) const {
    if ((address & 0x7u) == 0 && contains(address) && contains(address + 7)) {
        const auto idx = translate(address);
        u64 value = data_.at(idx);
        value |= static_cast<u64>(data_.at(idx + 1)) << 8;
        value |= static_cast<u64>(data_.at(idx + 2)) << 16;
        value |= static_cast<u64>(data_.at(idx + 3)) << 24;
        value |= static_cast<u64>(data_.at(idx + 4)) << 32;
        value |= static_cast<u64>(data_.at(idx + 5)) << 40;
        value |= static_cast<u64>(data_.at(idx + 6)) << 48;
        value |= static_cast<u64>(data_.at(idx + 7)) << 56;
        return value;
    }
    return read64_unaligned_slow(address);
}

/**
 * @brief 非对齐地址的 64 位读取回退实现。
 *
 * 逐字节读取，最多 8 字节；越界字节按 0 处理。
 *
 * @param address 目标地址（可能非对齐）
 * @return 拼装后的 64 位值
 */
u64 Memory::read64_unaligned_slow(u64 address) const {
    u64 value = 0;
    for (int i = 0; i < 8; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            value |= static_cast<u64>(data_.at(translate(a))) << (i * 8);
        }
    }
    return value;
}

/**
 * @brief 写入 1 字节。
 *
 * 地址越界时静默忽略（与读路径的越界返回 0 对称）。
 *
 * @param address 目标地址
 * @param value 要写入的字节值
 */
void Memory::write8(u64 address, u8 value) {
    if (contains(address)) {
        data_.at(translate(address)) = value;
    }
}

/**
 * @brief 写入 2 字节（小端序）。
 *
 * 地址按 2 字节对齐且范围在内存中时走快速路径，否则回退到逐字节写入。
 *
 * @param address 目标地址
 * @param value 要写入的 16 位值
 */
void Memory::write16(u64 address, u16 value) {
    if ((address & 0x1u) == 0 && contains(address) && contains(address + 1)) {
        const auto idx = translate(address);
        data_.at(idx) = static_cast<u8>(value & 0xFF);
        data_.at(idx + 1) = static_cast<u8>((value >> 8) & 0xFF);
        return;
    }
    write16_unaligned_slow(address, value);
}

/**
 * @brief 非对齐地址的 16 位写入回退实现。
 *
 * 逐字节写入，越界字节忽略。
 *
 * @param address 目标地址（可能非对齐）
 * @param value 要写入的 16 位值
 */
void Memory::write16_unaligned_slow(u64 address, u16 value) {
    for (int i = 0; i < 2; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            data_.at(translate(a)) = static_cast<u8>((value >> (i * 8)) & 0xFF);
        }
    }
}

/**
 * @brief 写入 4 字节（小端序）。
 *
 * 地址按 4 字节对齐且范围在内存中时走快速路径，否则回退到逐字节写入。
 *
 * @param address 目标地址
 * @param value 要写入的 32 位值
 */
void Memory::write32(u64 address, u32 value) {
    if ((address & 0x3u) == 0 && contains(address) && contains(address + 3)) {
        const auto idx = translate(address);
        for (int i = 0; i < 4; ++i) {
            data_.at(idx + i) = static_cast<u8>((value >> (i * 8)) & 0xFF);
        }
        return;
    }
    write32_unaligned_slow(address, value);
}

/**
 * @brief 非对齐地址的 32 位写入回退实现。
 *
 * 逐字节写入，越界字节忽略。
 *
 * @param address 目标地址（可能非对齐）
 * @param value 要写入的 32 位值
 */
void Memory::write32_unaligned_slow(u64 address, u32 value) {
    for (int i = 0; i < 4; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            data_.at(translate(a)) = static_cast<u8>((value >> (i * 8)) & 0xFF);
        }
    }
}

/**
 * @brief 写入 8 字节（小端序）。
 *
 * 地址按 8 字节对齐且范围在内存中时走快速路径，否则回退到逐字节写入。
 *
 * @param address 目标地址
 * @param value 要写入的 64 位值
 */
void Memory::write64(u64 address, u64 value) {
    if ((address & 0x7u) == 0 && contains(address) && contains(address + 7)) {
        const auto idx = translate(address);
        for (int i = 0; i < 8; ++i) {
            data_.at(idx + i) = static_cast<u8>((value >> (i * 8)) & 0xFF);
        }
        return;
    }
    write64_unaligned_slow(address, value);
}

/**
 * @brief 非对齐地址的 64 位写入回退实现。
 *
 * 逐字节写入，越界字节忽略。
 *
 * @param address 目标地址（可能非对齐）
 * @param value 要写入的 64 位值
 */
void Memory::write64_unaligned_slow(u64 address, u64 value) {
    for (int i = 0; i < 8; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            data_.at(translate(a)) = static_cast<u8>((value >> (i * 8)) & 0xFF);
        }
    }
}

/**
 * @brief 判断地址是否落在模拟器内存范围内。
 *
 * 区间为半开区间 [base_address_, base_address_ + data_.size())。
 *
 * @param address 目标地址
 * @return 落在范围内返回 true，否则 false
 */
bool Memory::contains(u64 address) const {
    const u64 start = base_address_;
    const u64 end = base_address_ + data_.size();
    return address >= start && address < end;
}

/**
 * @brief 将虚拟地址转换为 data_ 数组下标。
 *
 * @param address 虚拟地址（必须落在 [base_address_, base_address_ + size)）
 * @return 对应的 data_ 数组下标
 * @throws std::out_of_range 当地址不在内存范围内时
 */
std::size_t Memory::translate(u64 address) const {
    if (!contains(address)) {
        throw std::out_of_range("Address outside memory range");
    }
    return static_cast<std::size_t>(address - base_address_);
}

}  // namespace riscv
