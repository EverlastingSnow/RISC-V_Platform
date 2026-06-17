#include "riscv/register_file.h"

#include <array>
#include <stdexcept>

namespace riscv {

/**
 * @brief RegisterFile 默认构造函数。
 *
 * 调用 reset()，所有寄存器（包括 x0）初始化为 0。
 */
RegisterFile::RegisterFile() {
    reset();
}

/**
 * @brief 将 32 个整数寄存器全部清零。
 */
void RegisterFile::reset() {
    registers_.fill(0);
}

/**
 * @brief 写入寄存器。
 *
 * 特殊处理：
 *   - index == 0 (zero)：RISC-V 规范要求 x0 恒为 0，写入被忽略
 *   - index >= REGISTER_COUNT：越界写入被忽略
 *
 * @param index 寄存器编号（0..31）
 * @param value 写入的 64 位值
 */
void RegisterFile::write(u32 index, u64 value) {
    if (index == 0 || index >= REGISTER_COUNT) {
        return;
    }
    registers_[index] = value;
}

/**
 * @brief 读取寄存器值。
 *
 * @param index 寄存器编号（0..31）
 * @return 寄存器的 64 位值
 * @throws std::out_of_range 当 index >= REGISTER_COUNT 时
 */
u64 RegisterFile::read(u32 index) const {
    if (index >= REGISTER_COUNT) {
        throw std::out_of_range("Register index out of range");
    }
    return registers_[index];
}

/**
 * @brief 返回 RISC-V 通用寄存器的 ABI 简称。
 *
 * RISC-V 32 个通用寄存器的 ABI 别名表（x0..x31）：
 *   x0=zero, x1=ra, x2=sp, x3=gp, x4=tp, x5-x7=t0..t2,
 *   x8-x9=s0..s1(fp/s1), x10-x17=a0..a7,
 *   x18-x27=s2..s11, x28-x31=t3..t6
 *
 * @param index 寄存器编号（0..31）
 * @return 对应 ABI 别名字符串；越界时返回 "invalid"
 */
std::string reg_name(u32 index) {
    static constexpr const char* names[REGISTER_COUNT] = {
        "zero", "ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
        "s0",   "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
        "a6",   "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
        "s8",   "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6"};
    if (index >= REGISTER_COUNT) {
        return "invalid";
    }
    return names[index];
}

}  // namespace riscv


