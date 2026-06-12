#include "riscv/memory.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace riscv {

Memory::Memory(std::size_t size, u64 base) : data_(size, 0), base_address_(base) {}

void Memory::reset() {
    std::fill(data_.begin(), data_.end(), 0);
}

void Memory::load_program(const std::vector<u8>& binary, u64 vaddr_offset) {
    // vaddr_offset 是程序加载的虚拟地址偏移（相对于 base_address_）
    // 转换为 data_ 数组索引
    u64 index = (vaddr_offset >= base_address_) ? (vaddr_offset - base_address_) : vaddr_offset;
    if (index + binary.size() > data_.size()) {
        throw std::out_of_range("Program does not fit in memory");
    }
    std::copy(binary.begin(), binary.end(), data_.begin() + static_cast<std::size_t>(index));
}

u8 Memory::read8(u64 address) const {
    if (contains(address)) {
        return data_.at(translate(address));
    }
    return 0;
}

u16 Memory::read16(u64 address) const {
    if ((address & 0x1u) == 0) {
        const auto idx = translate(address);
        if (idx + 1 < data_.size()) {
            u16 value = data_.at(idx);
            value |= static_cast<u16>(data_.at(idx + 1)) << 8;
            return value;
        }
    }
    return read16_unaligned_slow(address);
}

u16 Memory::read16_unaligned_slow(u64 address) const {
    u16 value = 0;
    for (int i = 0; i < 2; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            value |= static_cast<u16>(data_.at(translate(a))) << (i * 8);
        }
    }
    return value;
}

u32 Memory::read32(u64 address) const {
    if ((address & 0x3u) == 0 && contains(address) && contains(address + 3)) {
        const auto idx = translate(address);
        u32 value = data_.at(idx);
        value |= static_cast<u32>(data_.at(idx + 1)) << 8;
        value |= static_cast<u32>(data_.at(idx + 2)) << 16;
        value |= static_cast<u32>(data_.at(idx + 3)) << 24;
        return value;
    }
    return read32_unaligned_slow(address);
}

u32 Memory::read32_unaligned_slow(u64 address) const {
    u32 value = 0;
    for (int i = 0; i < 4; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            value |= static_cast<u32>(data_.at(translate(a))) << (i * 8);
        }
    }
    return value;
}

u64 Memory::read64(u64 address) const {
    if ((address & 0x7u) == 0 && contains(address) && contains(address + 7)) {
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
    return read64_unaligned_slow(address);
}

u64 Memory::read64_unaligned_slow(u64 address) const {
    u64 value = 0;
    for (int i = 0; i < 8; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            value |= static_cast<u64>(data_.at(translate(a))) << (i * 8);
        }
    }
    return value;
}

void Memory::write8(u64 address, u8 value) {
    if (contains(address)) {
        data_.at(translate(address)) = value;
    }
}

void Memory::write16(u64 address, u16 value) {
    if ((address & 0x1u) == 0 && contains(address) && contains(address + 1)) {
        const auto idx = translate(address);
        data_.at(idx) = static_cast<u8>(value & 0xFF);
        data_.at(idx + 1) = static_cast<u8>((value >> 8) & 0xFF);
        return;
    }
    write16_unaligned_slow(address, value);
}

void Memory::write16_unaligned_slow(u64 address, u16 value) {
    for (int i = 0; i < 2; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            data_.at(translate(a)) = static_cast<u8>((value >> (i * 8)) & 0xFF);
        }
    }
}

void Memory::write32(u64 address, u32 value) {
    if ((address & 0x3u) == 0 && contains(address) && contains(address + 3)) {
        const auto idx = translate(address);
        for (int i = 0; i < 4; ++i) {
            data_.at(idx + i) = static_cast<u8>((value >> (i * 8)) & 0xFF);
        }
        return;
    }
    write32_unaligned_slow(address, value);
}

void Memory::write32_unaligned_slow(u64 address, u32 value) {
    for (int i = 0; i < 4; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            data_.at(translate(a)) = static_cast<u8>((value >> (i * 8)) & 0xFF);
        }
    }
}

void Memory::write64(u64 address, u64 value) {
    if ((address & 0x7u) == 0 && contains(address) && contains(address + 7)) {
        const auto idx = translate(address);
        for (int i = 0; i < 8; ++i) {
            data_.at(idx + i) = static_cast<u8>((value >> (i * 8)) & 0xFF);
        }
        return;
    }
    write64_unaligned_slow(address, value);
}

void Memory::write64_unaligned_slow(u64 address, u64 value) {
    for (int i = 0; i < 8; ++i) {
        u64 a = address + i;
        if (contains(a)) {
            data_.at(translate(a)) = static_cast<u8>((value >> (i * 8)) & 0xFF);
        }
    }
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
