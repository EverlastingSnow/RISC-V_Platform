#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riscv/types.h"

namespace riscv {

class Memory {
public:
    explicit Memory(std::size_t size = DEFAULT_MEMORY_SIZE, u32 base = RESET_VECTOR);

    void reset();
    void load_program(const std::vector<u8>& binary, u32 offset = 0);

    u8 read8(u32 address) const;
    u16 read16(u32 address) const;
    u32 read32(u32 address) const;

    void write8(u32 address, u8 value);
    void write16(u32 address, u16 value);
    void write32(u32 address, u32 value);

    [[nodiscard]] bool contains(u32 address) const;
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] u32 base() const noexcept { return base_address_; }

private:
    [[nodiscard]] std::size_t translate(u32 address) const;

    std::vector<u8> data_;
    u32 base_address_;
};

}  // namespace riscv


