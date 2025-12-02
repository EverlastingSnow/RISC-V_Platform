#include "riscv/memory.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace riscv {

Memory::Memory(std::size_t size, u32 base) : data_(size, 0), base_address_(base) {}

void Memory::reset() {
    std::fill(data_.begin(), data_.end(), 0);
}

void Memory::load_program(const std::vector<u8>& binary, u32 offset) {
    if (offset + binary.size() > data_.size()) {
        throw std::out_of_range("Program does not fit in memory");
    }
    std::copy(binary.begin(), binary.end(), data_.begin() + offset);
}

u8 Memory::read8(u32 address) const {
    return data_.at(translate(address));
}

u16 Memory::read16(u32 address) const {
    if (address & 0x1u) {
        throw std::runtime_error("Unaligned halfword read");
    }
    const auto idx = translate(address);
    u16 value = data_.at(idx);
    value |= static_cast<u16>(data_.at(idx + 1)) << 8;
    return value;
}

u32 Memory::read32(u32 address) const {
    if (address & 0x3u) {
        throw std::runtime_error("Unaligned word read");
    }
    const auto idx = translate(address);
    u32 value = data_.at(idx);
    value |= static_cast<u32>(data_.at(idx + 1)) << 8;
    value |= static_cast<u32>(data_.at(idx + 2)) << 16;
    value |= static_cast<u32>(data_.at(idx + 3)) << 24;
    return value;
}

void Memory::write8(u32 address, u8 value) {
    data_.at(translate(address)) = value;
}

void Memory::write16(u32 address, u16 value) {
    if (address & 0x1u) {
        throw std::runtime_error("Unaligned halfword write");
    }
    const auto idx = translate(address);
    data_.at(idx) = static_cast<u8>(value & 0xFF);
    data_.at(idx + 1) = static_cast<u8>((value >> 8) & 0xFF);
}

void Memory::write32(u32 address, u32 value) {
    if (address & 0x3u) {
        throw std::runtime_error("Unaligned word write");
    }
    const auto idx = translate(address);
    data_.at(idx) = static_cast<u8>(value & 0xFF);
    data_.at(idx + 1) = static_cast<u8>((value >> 8) & 0xFF);
    data_.at(idx + 2) = static_cast<u8>((value >> 16) & 0xFF);
    data_.at(idx + 3) = static_cast<u8>((value >> 24) & 0xFF);
}

bool Memory::contains(u32 address) const {
    const auto start = base_address_;
    const auto end = base_address_ + static_cast<u32>(data_.size());
    return address >= start && address < end;
}

std::size_t Memory::translate(u32 address) const {
    if (!contains(address)) {
        throw std::out_of_range("Address outside memory range");
    }
    return static_cast<std::size_t>(address - base_address_);
}

}  // namespace riscv

