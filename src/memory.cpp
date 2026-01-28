#include "riscv/memory.h"

#include <algorithm>
#include <stdexcept>

namespace riscv {

Memory::Memory(std::size_t size, u64 base) : data_(size, 0), base_address_(base) {}

void Memory::reset() {
    std::fill(data_.begin(), data_.end(), 0);
}

void Memory::load_program(const std::vector<u8>& binary, u64 offset) {
    if (offset + binary.size() > data_.size()) {
        throw std::out_of_range("Program does not fit in memory");
    }
    std::copy(binary.begin(), binary.end(), data_.begin() + static_cast<std::size_t>(offset));
}

u8 Memory::read8(u64 address) const {
    return data_.at(translate(address));
}

u16 Memory::read16(u64 address) const {
    if (address & 0x1u) {
        throw std::runtime_error("Unaligned halfword read");
    }
    const auto idx = translate(address);
    u16 value = data_.at(idx);
    value |= static_cast<u16>(data_.at(idx + 1)) << 8;
    return value;
}

u32 Memory::read32(u64 address) const {
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

u64 Memory::read64(u64 address) const {
    if (address & 0x7u) {
        throw std::runtime_error("Unaligned doubleword read");
    }
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

void Memory::write8(u64 address, u8 value) {
    data_.at(translate(address)) = value;
}

void Memory::write16(u64 address, u16 value) {
    if (address & 0x1u) {
        throw std::runtime_error("Unaligned halfword write");
    }
    const auto idx = translate(address);
    data_.at(idx) = static_cast<u8>(value & 0xFF);
    data_.at(idx + 1) = static_cast<u8>((value >> 8) & 0xFF);
}

void Memory::write32(u64 address, u32 value) {
    if (address & 0x3u) {
        throw std::runtime_error("Unaligned word write");
    }
    const auto idx = translate(address);
    data_.at(idx) = static_cast<u8>(value & 0xFF);
    data_.at(idx + 1) = static_cast<u8>((value >> 8) & 0xFF);
    data_.at(idx + 2) = static_cast<u8>((value >> 16) & 0xFF);
    data_.at(idx + 3) = static_cast<u8>((value >> 24) & 0xFF);
}

void Memory::write64(u64 address, u64 value) {
    if (address & 0x7u) {
        throw std::runtime_error("Unaligned doubleword write");
    }
    const auto idx = translate(address);
    data_.at(idx) = static_cast<u8>(value & 0xFF);
    data_.at(idx + 1) = static_cast<u8>((value >> 8) & 0xFF);
    data_.at(idx + 2) = static_cast<u8>((value >> 16) & 0xFF);
    data_.at(idx + 3) = static_cast<u8>((value >> 24) & 0xFF);
    data_.at(idx + 4) = static_cast<u8>((value >> 32) & 0xFF);
    data_.at(idx + 5) = static_cast<u8>((value >> 40) & 0xFF);
    data_.at(idx + 6) = static_cast<u8>((value >> 48) & 0xFF);
    data_.at(idx + 7) = static_cast<u8>((value >> 56) & 0xFF);
}

bool Memory::contains(u64 address) const {
    const u64 start = base_address_;
    const u64 end = base_address_ + data_.size();
    return address >= start && address < end;
}

std::size_t Memory::translate(u64 address) const {
    if (!contains(address)) {
        throw std::out_of_range("Address outside memory range");
    }
    return static_cast<std::size_t>(address - base_address_);
}

}  // namespace riscv
