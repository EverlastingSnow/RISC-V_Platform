#include "riscv/register_file.h"

#include <array>
#include <stdexcept>

namespace riscv {

RegisterFile::RegisterFile() {
    reset();
}

void RegisterFile::reset() {
    registers_.fill(0);
}

void RegisterFile::write(u32 index, u32 value) {
    if (index == 0 || index >= REGISTER_COUNT) {
        return;
    }
    registers_[index] = value;
}

u32 RegisterFile::read(u32 index) const {
    if (index >= REGISTER_COUNT) {
        throw std::out_of_range("Register index out of range");
    }
    return registers_[index];
}

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


