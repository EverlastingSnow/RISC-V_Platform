#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "riscv/types.h"

namespace riscv {

/**
 * @brief 32 个 RISC-V 通用整数寄存器的存储与访问。
 *
 * - 索引 0..31 对应 x0..x31
 * - 写 x0 被静默忽略（RISC-V 规范要求 x0 恒为 0）
 * - 提供 raw() 直接访问内部 std::array，用于寄存器 dump 等场景
 */
class RegisterFile {
public:
    /** @brief 构造一个全 0 的寄存器文件。 */
    RegisterFile();

    /** @brief 把所有寄存器清零。 */
    void reset();

    /**
     * @brief 写入寄存器。
     * @param index 寄存器编号（0..31）
     * @param value 要写入的 64 位值
     *
     * x0 (index==0) 写入被忽略；index>=REGISTER_COUNT 也忽略。
     */
    void write(u32 index, u64 value);

    /**
     * @brief 读取寄存器值。
     * @param index 寄存器编号（0..31）
     * @return 64 位寄存器值
     * @throws std::out_of_range 当 index >= REGISTER_COUNT 时
     */
    [[nodiscard]] u64 read(u32 index) const;

    /** @brief 返回内部 std::array 引用，用于序列化/dump。 */
    const std::array<u64, REGISTER_COUNT>& raw() const { return registers_; }

private:
    std::array<u64, REGISTER_COUNT> registers_{};
};

/**
 * @brief 返回寄存器编号对应的 ABI 简称。
 * @param index 寄存器编号（0..31）
 * @return 简称字符串（如 "zero"/"ra"/"sp"/"a0"），越界返回 "invalid"
 */
std::string reg_name(u32 index);

}  // namespace riscv


