#include "riscv/elf_loader.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>

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
        vaddr_max = (std::max)(vaddr_max, phdr.p_vaddr + phdr.p_memsz);
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
        vaddr_max = (std::max)(vaddr_max, phdr.p_vaddr + phdr.p_memsz);
    }

    if (segments.empty()) {
        out.error = "无 PT_LOAD 段";
        return out;
    }
    if (vaddr_min < memory_base) {
        out.error = "段起始地址低于 memory base，无法加载";
        return out;
    }
    
    // 首先回到文件开头读取 section headers
    file.clear();
    file.seekg(0);
    
    Elf64_Ehdr ehdr_copy{};
    file.read(reinterpret_cast<char*>(&ehdr_copy), sizeof(ehdr_copy));
    
    // 检查是否有 section headers
    if (ehdr_copy.e_shoff != 0 && ehdr_copy.e_shnum != 0) {
        // 读取 section string table
        std::vector<u8> shstrtab;
        if (ehdr_copy.e_shstrndx != SHN_UNDEF) {
            file.seekg(ehdr_copy.e_shoff + ehdr_copy.e_shstrndx * ehdr_copy.e_shentsize);
            Elf64_Shdr shstrtab_hdr{};
            file.read(reinterpret_cast<char*>(&shstrtab_hdr), sizeof(shstrtab_hdr));
            if (file.gcount() == sizeof(shstrtab_hdr) && shstrtab_hdr.sh_size > 0) {
                shstrtab.resize(shstrtab_hdr.sh_size);
                file.seekg(shstrtab_hdr.sh_offset);
                file.read(reinterpret_cast<char*>(shstrtab.data()), shstrtab_hdr.sh_size);
            }
        }
        
        // 收集所有 loadable sections
        struct SecInfo { std::string name; u64 addr; u64 size; u64 offset; };
        std::vector<SecInfo> secs;
        
        // std::cerr << "[DEBUG] Total sections in ELF: " << ehdr_copy.e_shnum << "\n";
        
        for (std::uint16_t i = 0; i < ehdr_copy.e_shnum; ++i) {
            file.seekg(ehdr_copy.e_shoff + i * ehdr_copy.e_shentsize);
            Elf64_Shdr shdr{};
            file.read(reinterpret_cast<char*>(&shdr), sizeof(shdr));
            
            if ((shdr.sh_flags & SHF_ALLOC) && shdr.sh_size > 0) {
                std::string name;
                if (shdr.sh_name < shstrtab.size()) {
                    name = reinterpret_cast<const char*>(shstrtab.data() + shdr.sh_name);
                }
                
                // 打印所有 sections
                // std::cerr << "[DEBUG] Section: " << name << " addr=0x" << std::hex << shdr.sh_addr 
                //           << " file_offset=0x" << shdr.sh_offset
                //           << " size=0x" << shdr.sh_size << std::dec << "\n";
                
                secs.push_back({name, shdr.sh_addr, shdr.sh_size, shdr.sh_offset});
            }
        }
        
        if (!secs.empty()) {
            // 使用 sections 计算范围并加载
            u64 sec_vaddr_min = 0xFFFFFFFFFFFFFFFFULL;
            u64 sec_vaddr_max = 0;
            for (const auto& s : secs) {
                sec_vaddr_min = (std::min)(sec_vaddr_min, s.addr);
                sec_vaddr_max = (std::max)(sec_vaddr_max, s.addr + s.size);
            }
            
            // std::cerr << "[DEBUG] sec_vaddr_min=0x" << std::hex << sec_vaddr_min 
            //           << " sec_vaddr_max=0x" << sec_vaddr_max << std::dec << "\n";
            
            out.load_offset = sec_vaddr_min - memory_base;
            u64 sec_total_size = sec_vaddr_max - sec_vaddr_min;
            out.binary.assign(static_cast<std::size_t>(sec_total_size), 0);
            
            // std::cerr << "[DEBUG] binary.size=0x" << std::hex << out.binary.size() 
            //           << " data ptr=0x" << (void*)out.binary.data() << std::dec << "\n";
            
            // 保存 binary data 指针
            const u8* original_data_ptr = out.binary.data();
            
            // 加载每个 section
            for (const auto& s : secs) {
                u64 off = s.addr - sec_vaddr_min;
                // std::cerr << "[DEBUG] Loading section " << s.name << " to off=0x" << std::hex << off 
                //           << " (addr=0x" << s.addr << " size=0x" << s.size << ")" << std::dec << "\n";
                
                file.seekg(s.offset);
                file.read(reinterpret_cast<char*>(out.binary.data() + off), s.size);
                
                // 检查 data 指针是否改变
                // if (out.binary.data() != original_data_ptr) {
                //     std::cerr << "[ERROR] Binary data pointer changed from 0x" << (void*)original_data_ptr 
                //               << " to 0x" << (void*)out.binary.data() << " after loading " << s.name << "!\n";
                // }
                
                // 验证读取后的前几个字节
                // std::cerr << "[DEBUG] After read " << s.name << " at off=0x" << std::hex << off << ": ";
                // for (size_t j = 0; j < std::min(size_t(8), (size_t)s.size); ++j) {
                //     std::cerr << std::setw(2) << std::setfill('0') << (int)out.binary[off + j] << " ";
                // }
                // std::cerr << std::dec << "\n";
            }
            
            // 验证 - 在所有 sections 加载后
            // std::cerr << "[DEBUG] Final binary ptr=0x" << (void*)out.binary.data() << std::dec << "\n";
            // 只在 binary 足够大时验证
            // if (out.binary.size() > 0x3003) {
            //     std::cerr << "[DEBUG] Final binary[0x2000]=" << std::hex << (int)out.binary[0x2000] 
            //               << " [0x2047]=" << (int)out.binary[0x2047]
            //               << " [0x3000]=" << (int)out.binary[0x3000] 
            //               << " [0x3001]=" << (int)out.binary[0x3001]
            //               << " [0x3002]=" << (int)out.binary[0x3002]
            //               << " [0x3003]=" << (int)out.binary[0x3003] << std::dec << "\n";
            // } else {
            //     std::cerr << "[DEBUG] Final binary size=0x" << std::hex << out.binary.size() << std::dec << "\n";
            // }
            
            out.success = true;
            return out;
        }
    }
    
    // Fallback: 使用程序段
    out.load_offset = vaddr_min - memory_base;
    const u64 total_size = vaddr_max - vaddr_min;
    out.binary.assign(static_cast<std::size_t>(total_size), 0);

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

ElfLoadResult load_raw_binary(const std::string& path) {
    ElfLoadResult out;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        out.error = "无法打开文件: " + path;
        return out;
    }
    const std::streamsize file_size = file.tellg();
    if (file_size < 0) {
        out.error = "无法获取文件大小: " + path;
        return out;
    }
    file.seekg(std::ios_base::beg);
    out.binary.assign(static_cast<std::size_t>(file_size), 0);
    if (file_size > 0) {
        file.read(reinterpret_cast<char*>(out.binary.data()), file_size);
        if (file.gcount() != file_size) {
            out.error = "读取文件失败: " + path;
            return out;
        }
    }
    // 裸二进制按 lab9 约定：整体映射到 memory_base 起始
    out.load_offset = 0;
    out.success = true;
    return out;
}

namespace {

// 64 位符号表项
#pragma pack(push, 1)
struct Elf64_Sym {
    std::uint32_t st_name;
    std::uint8_t  st_info;
    std::uint8_t  st_other;
    std::uint16_t st_shndx;
    std::uint64_t st_value;
    std::uint64_t st_size;
};
#pragma pack(pop)

constexpr std::uint32_t SHT_SYMTAB = 2;
constexpr std::uint32_t SHT_STRTAB = 3;

}  // namespace

u64 find_tohost_address(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;

    Elf64_Ehdr ehdr{};
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(ehdr))) return 0;
    if (ehdr.e_ident[EI_MAG0] != 0x7f || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') return 0;
    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0) return 0;

    // 读取所有 section headers
    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    for (std::uint16_t i = 0; i < ehdr.e_shnum; ++i) {
        file.seekg(static_cast<std::streamoff>(ehdr.e_shoff + i * ehdr.e_shentsize));
        file.read(reinterpret_cast<char*>(&shdrs[i]), sizeof(shdrs[i]));
    }

    // 找 .symtab
    u64 symtab_offset = 0, symtab_size = 0, symtab_entsize = 0, symtab_link = 0;
    for (const auto& sh : shdrs) {
        if (sh.sh_type == SHT_SYMTAB) {
            symtab_offset = sh.sh_offset;
            symtab_size = sh.sh_size;
            symtab_entsize = sh.sh_entsize ? sh.sh_entsize : sizeof(Elf64_Sym);
            symtab_link = sh.sh_link;
            break;
        }
    }
    if (symtab_size == 0 || symtab_link == 0 || symtab_link >= shdrs.size()) return 0;

    // 用 symtab.sh_link 找对应的 strtab（避免与 .shstrtab 混淆）
    const auto& strtab_sh = shdrs[symtab_link];
    u64 strtab_offset = strtab_sh.sh_offset;
    u64 strtab_size = strtab_sh.sh_size;

    // 读取 strtab
    std::vector<u8> strtab(strtab_size);
    file.seekg(static_cast<std::streamoff>(strtab_offset));
    file.read(reinterpret_cast<char*>(strtab.data()), strtab_size);

    // 遍历 symtab
    const std::size_t n = static_cast<std::size_t>(symtab_size / symtab_entsize);
    for (std::size_t i = 0; i < n; ++i) {
        Elf64_Sym sym{};
        file.seekg(static_cast<std::streamoff>(symtab_offset + i * symtab_entsize));
        file.read(reinterpret_cast<char*>(&sym), sizeof(sym));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(sym))) continue;
        if (sym.st_name >= strtab.size()) continue;
        const char* name = reinterpret_cast<const char*>(strtab.data() + sym.st_name);
        if (std::string(name) == "tohost") {
            return sym.st_value;
        }
    }
    return 0;
}

}  // namespace riscv
