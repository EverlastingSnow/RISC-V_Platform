#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riscv/types.h"

namespace riscv {

/**
 * @brief 模拟器内存模型。
 *
 * 简单的字节数组实现，所有读写都是小端序；
 * 地址按 2/4/8 字节对齐时走快速路径，非对齐地址回退到逐字节处理。
 *
 * 地址区间为 [base(), base() + size())；越界读返回 0，越界写静默忽略。
 */
class Memory {
public:
    /**
     * @brief 构造一个内存对象。
     * @param size 内存大小（字节），默认 DEFAULT_MEMORY_SIZE (256 MiB)
     * @param base 内存基地址，默认 RESET_VECTOR (0x80000000)
     */
    explicit Memory(std::size_t size = static_cast<std::size_t>(DEFAULT_MEMORY_SIZE), u64 base = RESET_VECTOR);

    /** @brief 把整段内存清零。 */
    void reset();

    /**
     * @brief 加载一段二进制程序到内存。
     * @param binary 待加载的字节序列
     * @param offset 相对 base 的偏移，默认 0
     * @throws std::out_of_range 当加载范围越界时
     */
    void load_program(const std::vector<u8>& binary, u64 offset = 0);

    /** @brief 读取 1 字节；越界返回 0。 */
    u8 read8(u64 address) const;
    /** @brief 读取 2 字节（小端序）；非对齐自动回退。 */
    u16 read16(u64 address) const;
    /** @brief 读取 4 字节（小端序）；非对齐自动回退。 */
    u32 read32(u64 address) const;
    /** @brief 读取 8 字节（小端序）；非对齐自动回退。 */
    u64 read64(u64 address) const;

    /** @brief 写入 1 字节；越界静默忽略。 */
    void write8(u64 address, u8 value);
    /** @brief 写入 2 字节（小端序）；非对齐自动回退。 */
    void write16(u64 address, u16 value);
    /** @brief 写入 4 字节（小端序）；非对齐自动回退。 */
    void write32(u64 address, u32 value);
    /** @brief 写入 8 字节（小端序）；非对齐自动回退。 */
    void write64(u64 address, u64 value);

    /** @brief 判断地址是否落在本内存区间内。 */
    [[nodiscard]] bool contains(u64 address) const;
    /** @brief 内存总大小（字节）。 */
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    /** @brief 内存基地址。 */
    [[nodiscard]] u64 base() const noexcept { return base_address_; }

private:
    /** @brief 虚拟地址 → data_ 下标；越界抛 out_of_range。 */
    [[nodiscard]] std::size_t translate(u64 address) const;
    /** @brief 16 位非对齐读取回退实现。 */
    [[nodiscard]] u16 read16_unaligned_slow(u64 address) const;
    /** @brief 32 位非对齐读取回退实现。 */
    [[nodiscard]] u32 read32_unaligned_slow(u64 address) const;
    /** @brief 64 位非对齐读取回退实现。 */
    [[nodiscard]] u64 read64_unaligned_slow(u64 address) const;
    /** @brief 16 位非对齐写入回退实现。 */
    void write16_unaligned_slow(u64 address, u16 value);
    /** @brief 32 位非对齐写入回退实现。 */
    void write32_unaligned_slow(u64 address, u32 value);
    /** @brief 64 位非对齐写入回退实现。 */
    void write64_unaligned_slow(u64 address, u64 value);

    std::vector<u8> data_;
    u64 base_address_;
};

}  // namespace riscv


