#ifndef RISCV_TEACHING_ELF_CONFIG_H
#define RISCV_TEACHING_ELF_CONFIG_H

#include <string>
#include <vector>
#include <map>

namespace riscv {

struct TeachingElfTest {
    std::string name;
    std::string display_name;
    std::string description;
    std::string elf_path;
    std::string scenario;
};

class TeachingElfConfig {
public:
    static const std::vector<TeachingElfTest>& get_all_elf_tests() {
        return s_elf_tests;
    }

    static const std::vector<TeachingElfTest> get_elf_tests_by_scenario(const std::string& scenario) {
        std::vector<TeachingElfTest> result;
        for (const auto& test : s_elf_tests) {
            if (test.scenario == scenario) {
                result.push_back(test);
            }
        }
        return result;
    }

    static const std::string& get_elf_base_path() {
        static const std::string base_path = "e:/platform/RISC-V_Platform/teaching/";
        return base_path;
    }

private:
    static const std::vector<TeachingElfTest> s_elf_tests;
};

inline const std::vector<TeachingElfTest> TeachingElfConfig::s_elf_tests = {
    {
        "teaching_regwrite",
        "RegWrite基础测试",
        "RegWrite信号基础：区分哪些指令写寄存器，哪些不写",
        "e:/platform/RISC-V_Platform/teaching/teaching_regwrite.elf",
        "scenario1"
    },
    {
        "teaching_alusrc",
        "ALUSrc操作数选择",
        "ALUSrc信号：理解ALU操作数选择（立即数vs寄存器）",
        "e:/platform/RISC-V_Platform/teaching/teaching_alusrc.elf",
        "scenario2"
    },
    {
        "teaching_mem",
        "内存访问控制",
        "MemRead/MemWrite信号：理解内存读写操作",
        "e:/platform/RISC-V_Platform/teaching/teaching_mem.elf",
        "scenario3"
    },
    {
        "teaching_branch",
        "分支控制",
        "Branch信号：理解分支跳转对流水线的影响",
        "e:/platform/RISC-V_Platform/teaching/teaching_branch.elf",
        "scenario4"
    },
    {
        "teaching_pipeline",
        "流水线综合",
        "流水线综合：展示典型流水线执行序列",
        "e:/platform/RISC-V_Platform/teaching/teaching_pipeline.elf",
        "all"
    }
};

}

#endif
