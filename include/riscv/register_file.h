#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "riscv/types.h"

namespace riscv {

class RegisterFile {
public:
    RegisterFile();

    void reset();

    void write(u32 index, u64 value);
    [[nodiscard]] u64 read(u32 index) const;

    const std::array<u64, REGISTER_COUNT>& raw() const { return registers_; }

private:
    std::array<u64, REGISTER_COUNT> registers_{};
};

std::string reg_name(u32 index);

}  // namespace riscv


