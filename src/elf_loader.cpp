#include "riscv/elf_loader.h"

#include <algorithm>
#include <fstream>

namespace riscv {

namespace {

constexpr std::uint8_t EI_MAG0 = 0;
constexpr std::uint8_t EI_CLASS = 4;
constexpr std::uint8_t EI_DATA = 5;
constexpr std::uint8_t ELFCLASS32 = 1;
constexpr std::uint8_t ELFCLASS64 = 2;
constexpr std::uint8_t ELFDATA2LSB = 1;
constexpr std::uint16_t ET_EXEC = 2;
constexpr std::uint16_t EM_RISCV = 243;
constexpr std::uint32_t PT_LOAD = 1;

#pragma pack(push, 1)
struct Elf32_Ehdr {
    unsigned char e_ident[16];
    std::uint16_t e_type;
    std::uint16_t e_machine;
    std::uint32_t e_version;
    std::uint32_t e_entry;
    std::uint32_t e_phoff;
    std::uint32_t e_shoff;
    std::uint32_t e_flags;
    std::uint16_t e_ehsize;
    std::uint16_t e_phentsize;
    std::uint16_t e_phnum;
    std::uint16_t e_shentsize;
    std::uint16_t e_shnum;
    std::uint16_t e_shstrndx;
};

struct Elf32_Phdr {
    std::uint32_t p_type;
    std::uint32_t p_offset;
    std::uint32_t p_vaddr;
    std::uint32_t p_paddr;
    std::uint32_t p_filesz;
    std::uint32_t p_memsz;
    std::uint32_t p_flags;
    std::uint32_t p_align;
};

struct Elf64_Ehdr {
    unsigned char e_ident[16];
    std::uint16_t e_type;
    std::uint16_t e_machine;
    std::uint32_t e_version;
    std::uint64_t e_entry;
    std::uint64_t e_phoff;
    std::uint64_t e_shoff;
    std::uint32_t e_flags;
    std::uint16_t e_ehsize;
    std::uint16_t e_phentsize;
    std::uint16_t e_phnum;
    std::uint16_t e_shentsize;
    std::uint16_t e_shnum;
    std::uint16_t e_shstrndx;
};

struct Elf64_Phdr {
    std::uint32_t p_type;
    std::uint32_t p_flags;
    std::uint64_t p_offset;
    std::uint64_t p_vaddr;
    std::uint64_t p_paddr;
    std::uint64_t p_filesz;
    std::uint64_t p_memsz;
    std::uint64_t p_align;
};
#pragma pack(pop)

}  // namespace

ElfLoadResult load_elf32(const std::string& path, u64 memory_base) {
    ElfLoadResult out;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        out.error = "无法打开文件: " + path;
        return out;
    }

    Elf32_Ehdr ehdr{};
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(ehdr))) {
        out.error = "ELF 文件过短或读失败";
        return out;
    }
    if (ehdr.e_ident[EI_MAG0] != 0x7f || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        out.error = "非 ELF 魔数";
        return out;
    }
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS32) {
        out.error = "需要 32 位 ELF";
        return out;
    }
    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        out.error = "需要小端 ELF";
        return out;
    }
    if (ehdr.e_type != ET_EXEC && ehdr.e_type != 0) { /* 0 有时用于 relocatable */
        /* 放宽：部分测试可能为 ET_REL 等，仍尝试按 PT_LOAD 加载 */
    }
    if (ehdr.e_machine != EM_RISCV) {
        out.error = "需要 RISC-V ELF";
        return out;
    }
    if (ehdr.e_phnum == 0 || ehdr.e_phentsize < sizeof(Elf32_Phdr)) {
        out.error = "无有效程序头";
        return out;
    }

    u32 vaddr_min = 0xFFFFFFFFu;
    u32 vaddr_max = 0u;
    std::vector<std::pair<u32, std::vector<u8>>> segments;

    for (std::uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        file.seekg(ehdr.e_phoff + i * ehdr.e_phentsize);
        Elf32_Phdr phdr{};
        file.read(reinterpret_cast<char*>(&phdr), sizeof(phdr));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(phdr)))
            continue;
        if (phdr.p_type != PT_LOAD || phdr.p_filesz == 0)
            continue;

        std::vector<u8> buf(phdr.p_filesz);
        file.seekg(phdr.p_offset);
        file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(phdr.p_filesz));
        if (file.gcount() != static_cast<std::streamsize>(phdr.p_filesz)) {
            out.error = "读 PT_LOAD 段失败";
            return out;
        }
        segments.emplace_back(phdr.p_vaddr, std::move(buf));
        vaddr_min = (std::min)(vaddr_min, phdr.p_vaddr);
        vaddr_max = (std::max)(vaddr_max, phdr.p_vaddr + static_cast<u32>(segments.back().second.size()));
    }

    if (segments.empty()) {
        out.error = "无 PT_LOAD 段";
        return out;
    }
    if (static_cast<u64>(vaddr_min) < memory_base) {
        out.error = "段起始地址低于 memory base，无法加载";
        return out;
    }

    out.load_offset = static_cast<u64>(vaddr_min) - memory_base;
    out.binary.assign(vaddr_max - vaddr_min, 0);
    for (const auto& seg : segments) {
        u32 vaddr = seg.first;
        const std::vector<u8>& data = seg.second;
        u32 off = vaddr - vaddr_min;
        if (off + data.size() > out.binary.size()) {
            out.error = "段越界";
            return out;
        }
        std::copy(data.begin(), data.end(), out.binary.begin() + off);
    }
    out.success = true;
    return out;
}

ElfLoadResult load_elf64(const std::string& path, u64 memory_base) {
    ElfLoadResult out;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        out.error = "无法打开文件: " + path;
        return out;
    }

    Elf64_Ehdr ehdr{};
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(ehdr))) {
        out.error = "ELF 文件过短或读失败";
        return out;
    }
    if (ehdr.e_ident[EI_MAG0] != 0x7f || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        out.error = "非 ELF 魔数";
        return out;
    }
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        out.error = "需要 64 位 ELF";
        return out;
    }
    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        out.error = "需要小端 ELF";
        return out;
    }
    if (ehdr.e_machine != EM_RISCV) {
        out.error = "需要 RISC-V ELF";
        return out;
    }
    if (ehdr.e_phnum == 0 || ehdr.e_phentsize < sizeof(Elf64_Phdr)) {
        out.error = "无有效程序头";
        return out;
    }

    u64 vaddr_min = 0xFFFFFFFFFFFFFFFFULL;
    u64 vaddr_max = 0;
    std::vector<std::pair<u64, std::vector<u8>>> segments;

    for (std::uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        file.seekg(static_cast<std::streamoff>(ehdr.e_phoff + i * ehdr.e_phentsize));
        Elf64_Phdr phdr{};
        file.read(reinterpret_cast<char*>(&phdr), sizeof(phdr));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(phdr)))
            continue;
        if (phdr.p_type != PT_LOAD || phdr.p_filesz == 0)
            continue;

        std::vector<u8> buf(static_cast<std::size_t>(phdr.p_filesz));
        file.seekg(static_cast<std::streamoff>(phdr.p_offset));
        file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(phdr.p_filesz));
        if (file.gcount() != static_cast<std::streamsize>(phdr.p_filesz)) {
            out.error = "读 PT_LOAD 段失败";
            return out;
        }
        segments.emplace_back(phdr.p_vaddr, std::move(buf));
        vaddr_min = (std::min)(vaddr_min, phdr.p_vaddr);
        vaddr_max = (std::max)(vaddr_max, phdr.p_vaddr + phdr.p_filesz);
    }

    if (segments.empty()) {
        out.error = "无 PT_LOAD 段";
        return out;
    }
    if (vaddr_min < memory_base) {
        out.error = "段起始地址低于 memory base，无法加载";
        return out;
    }

    out.load_offset = vaddr_min - memory_base;
    out.binary.assign(static_cast<std::size_t>(vaddr_max - vaddr_min), 0);
    for (const auto& seg : segments) {
        u64 vaddr = seg.first;
        const std::vector<u8>& data = seg.second;
        u64 off = vaddr - vaddr_min;
        if (off + data.size() > out.binary.size()) {
            out.error = "段越界";
            return out;
        }
        std::copy(data.begin(), data.end(), out.binary.begin() + static_cast<std::size_t>(off));
    }
    out.success = true;
    return out;
}

ElfLoadResult load_elf(const std::string& path, u64 memory_base) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ElfLoadResult out;
        out.error = "无法打开文件: " + path;
        return out;
    }
    unsigned char ident[16];
    file.read(reinterpret_cast<char*>(ident), 16);
    file.close();
    if (file.gcount() >= 5 && ident[0] == 0x7f && ident[1] == 'E' && ident[2] == 'L' && ident[3] == 'F') {
        if (ident[EI_CLASS] == ELFCLASS64)
            return load_elf64(path, memory_base);
        if (ident[EI_CLASS] == ELFCLASS32)
            return load_elf32(path, memory_base);
    }
    ElfLoadResult out;
    out.error = "非 ELF32/ELF64 或无法识别";
    return out;
}

}  // namespace riscv
