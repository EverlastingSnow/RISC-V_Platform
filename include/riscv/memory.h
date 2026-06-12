#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riscv/types.h"

namespace riscv {

class Memory {
public:
    explicit Memory(std::size_t size = static_cast<std::size_t>(DEFAULT_MEMORY_SIZE), u64 base = RESET_VECTOR);

    void reset();
    void load_program(const std::vector<u8>& binary, u64 offset = 0);

    u8 read8(u64 address) const;
    u16 read16(u64 address) const;
    u32 read32(u64 address) const;
    u64 read64(u64 address) const;

    void write8(u64 address, u8 value);
    void write16(u64 address, u16 value);
    void write32(u64 address, u32 value);
    void write64(u64 address, u64 value);

    [[nodiscard]] bool contains(u64 address) const;
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] u64 base() const noexcept { return base_address_; }

private:
    [[nodiscard]] std::size_t translate(u64 address) const;
    [[nodiscard]] u16 read16_unaligned_slow(u64 address) const;
    [[nodiscard]] u32 read32_unaligned_slow(u64 address) const;
    [[nodiscard]] u64 read64_unaligned_slow(u64 address) const;
    void write16_unaligned_slow(u64 address, u16 value);
    void write32_unaligned_slow(u64 address, u32 value);
    void write64_unaligned_slow(u64 address, u64 value);

    std::vector<u8> data_;
    u64 base_address_;
};

}  // namespace riscv


