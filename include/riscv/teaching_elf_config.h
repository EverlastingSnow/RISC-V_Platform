#ifndef RISCV_TEACHING_ELF_CONFIG_H
#define RISCV_TEACHING_ELF_CONFIG_H

#include <string>
#include <vector>
#include <map>
#include "riscv_config.h"

namespace riscv {

/**
 * @brief 单个教学 ELF 测试用例描述。
 *
 * 指向 $RISCV_TEACHING_DIR 下编译好的 .elf 文件，
 * 区别于内嵌指令流的 TeachingTestCase——这里要由前端发起实际 ELF 加载。
 */
struct TeachingElfTest {
    /** 用例唯一名。 */
    std::string name;
    /** 前端展示名（中文）。 */
    std::string display_name;
    /** 用例描述。 */
    std::string description;
    /** 相对教学目录的 ELF 路径。 */
    std::string elf_path;
    /** 场景分组（scenario1..4 / all）。 */
    std::string scenario;
};

/**
 * @brief 教学 ELF 测试用例的静态注册表。
 *
 * 所有 ELF 路径都在初始化阶段由 Config::get_teaching_dir() 拼接得到。
 * 提供三个静态查询接口：
 *   - get_all_elf_tests()             返回全部
 *   - get_elf_tests_by_scenario(s)    按场景过滤
 *   - get_elf_base_path()             教学目录绝对路径
 */
class TeachingElfConfig {
public:
    /** @brief 返回所有 ELF 测试用例。 */
    static const std::vector<TeachingElfTest>& get_all_elf_tests() {
        return s_elf_tests;
    }

    /**
     * @brief 按场景过滤 ELF 测试用例。
     * @param scenario 场景名（scenario1..4）
     * @return 过滤后的用例列表
     */
    static const std::vector<TeachingElfTest> get_elf_tests_by_scenario(const std::string& scenario) {
        std::vector<TeachingElfTest> result;
        for (const auto& test : s_elf_tests) {
            if (test.scenario == scenario) {
                result.push_back(test);
            }
        }
        return result;
    }

    /** @brief 返回教学目录绝对路径（来自 Config::get_teaching_dir）。 */
    static const std::string& get_elf_base_path() {
        return Config::get_teaching_dir();
    }

private:
    /** ELF 用例表（定义在文件末尾的 inline 变量中）。 */
    static const std::vector<TeachingElfTest> s_elf_tests;
};

inline const std::vector<TeachingElfTest> TeachingElfConfig::s_elf_tests = {
    {
        "teaching_regwrite",
        "RegWrite基础测试",
        "RegWrite信号基础：区分哪些指令写寄存器，哪些不写",
        Config::get_teaching_dir() + "/teaching_regwrite.elf",
        "scenario1"
    },
    {
        "teaching_regwrite_v2",
        "RegWrite进阶测试",
        "RegWrite信号进阶：涵盖算术、逻辑、移位、比较、加载、存储、分支、跳转、LUI/AUIPC等指令类型",
        Config::get_teaching_dir() + "/teaching_regwrite_v2.elf",
        "scenario1"
    },
    {
        "teaching_alusrc",
        "ALUSrc操作数选择",
        "ALUSrc信号：理解ALU操作数选择（立即数vs寄存器）",
        Config::get_teaching_dir() + "/teaching_alusrc.elf",
        "scenario2"
    },
    {
        "teaching_alusrc_v2",
        "ALUSrc进阶测试",
        "ALUSrc信号进阶：混合ADD/SUB/AND/OR/XOR/SLT/SLL与ADDI/ANDI/ORI/XORI/SLTI/SLLI/SRLI等指令",
        Config::get_teaching_dir() + "/teaching_alusrc_v2.elf",
        "scenario2"
    },
    {
        "teaching_mem",
        "内存访问控制",
        "MemRead/MemWrite信号：理解内存读写操作",
        Config::get_teaching_dir() + "/teaching_mem.elf",
        "scenario3"
    },
    {
        "teaching_mem_v2",
        "内存访问进阶测试",
        "MemRead/MemWrite信号进阶：混合SB/SH/SW与LB/LBU/LH/LHU/LW等指令",
        Config::get_teaching_dir() + "/teaching_mem_v2.elf",
        "scenario3"
    },
    {
        "teaching_branch",
        "分支控制",
        "Branch信号：理解分支跳转对流水线的影响",
        Config::get_teaching_dir() + "/teaching_branch.elf",
        "scenario4"
    },
    {
        "teaching_branch_v2",
        "分支控制进阶测试",
        "Branch信号进阶：混合BEQ/BNE/BLT/BGE/BLTU/BGEU等条件分支指令",
        Config::get_teaching_dir() + "/teaching_branch_v2.elf",
        "scenario4"
    },
    {
        "teaching_pipeline",
        "流水线综合",
        "流水线综合：展示典型流水线执行序列",
        Config::get_teaching_dir() + "/teaching_pipeline.elf",
        "all"
    }
};

}

#endif
