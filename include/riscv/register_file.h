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

    void write(u32 index, u32 value);
    [[nodiscard]] u32 read(u32 index) const;

    const std::array<u32, REGISTER_COUNT>& raw() const { return registers_; }

private:
    std::array<u32, REGISTER_COUNT> registers_{};
};

std::string reg_name(u32 index);

}  // namespace riscv


