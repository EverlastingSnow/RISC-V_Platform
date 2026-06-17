#ifndef RISCV_TEACHING_TESTS_H
#define RISCV_TEACHING_TESTS_H

#include <cstdint>
#include <vector>
#include <string>
#include <map>

namespace riscv {

/**
 * @brief 单个内嵌教学测试用例。
 *
 * 用一组手工编码的 RV32 指令流描述一个教学场景，
 * 前端在"无 ELF"模式下直接喂入模拟器执行。
 */
struct TeachingTestCase {
    /** 用例唯一名（前端下拉选项的 key）。 */
    std::string name;
    /** 中文/英文描述（前端展示）。 */
    std::string description;
    /** 场景分组（scenario1..4 / all）。 */
    std::string scenario;
    /** 已编码的 32 位指令流，最后一条通常为 EBREAK。 */
    std::vector<uint32_t> instructions;
};

/**
 * @brief 内嵌教学测试用例库。
 *
 * 提供 RV32 指令流级别的固定测试用例（区别于 ELF 级别的 teaching_elf_config）：
 *   - get_all_tests()      返回全部用例
 *   - get_test(name)       按名字查找
 *   - get_tests_by_scenario(scenario) 按场景过滤，空字符串 = 全部
 *
 * 内部 ENCODE_* 静态方法是 RV32 各指令格式的人工编码器，
 * 把 (rd/rs1/rs2/imm) 组装成 32 位指令字。
 */
class TeachingTests {
public:
    /** @brief 返回所有测试用例。 */
    static const std::vector<TeachingTestCase>& get_all_tests() {
        return s_tests;
    }

    /**
     * @brief 按名字查找测试用例。
     * @param name 用例名
     * @return 指向用例的指针，未找到返回 nullptr
     */
    static const TeachingTestCase* get_test(const std::string& name) {
        for (const auto& test : s_tests) {
            if (test.name == name) {
                return &test;
            }
        }
        return nullptr;
    }

    /**
     * @brief 按场景过滤测试用例。
     * @param scenario 场景名（scenario1..4）；空字符串表示不过滤
     * @return 过滤后的用例列表
     */
    static const std::vector<TeachingTestCase>& get_tests_by_scenario(const std::string& scenario) {
        static std::vector<TeachingTestCase> filtered;
        filtered.clear();
        for (const auto& test : s_tests) {
            if (test.scenario == scenario || scenario.empty()) {
                filtered.push_back(test);
            }
        }
        return filtered;
    }

private:
    /** @brief 编码 ADDI 指令（I-type）。 */
    static constexpr uint32_t ENCODE_ADDI(uint32_t rd, uint32_t rs1, int32_t imm) {
        uint32_t imm12 = static_cast<uint32_t>(imm) & 0xFFFu;
        return (imm12 << 20) | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0b0010011;
    }

    /** @brief 编码 R-type 指令（funct3/funct7 决定具体运算）。 */
    static constexpr uint32_t ENCODE_R(uint32_t rd, uint32_t rs1, uint32_t rs2, uint32_t funct3, uint32_t funct7) {
        return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | 0b0110011;
    }

    /** @brief 编码 AUIPC 指令（U-type）。 */
    static constexpr uint32_t ENCODE_AUIPC(uint32_t rd, uint32_t imm20) {
        return (imm20 << 12) | (rd << 7) | 0b0010111;
    }

    /** @brief 编码 LW 指令（I-type, funct3=010）。 */
    static constexpr uint32_t ENCODE_LW(uint32_t rd, uint32_t rs1, int32_t imm) {
        uint32_t imm12 = static_cast<uint32_t>(imm) & 0xFFFu;
        return (imm12 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0b0000011;
    }

    /** @brief 编码 SW 指令（S-type, funct3=010）。 */
    static constexpr uint32_t ENCODE_SW(uint32_t rs2, uint32_t rs1, int32_t imm) {
        uint32_t uimm = static_cast<uint32_t>(imm);
        uint32_t imm11_5 = (uimm >> 5) & 0x7F;
        uint32_t imm4_0 = uimm & 0x1F;
        return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (imm4_0 << 7) | 0b0100011;
    }

    /** @brief 编码 BEQ 指令（B-type, funct3=000）。 */
    static constexpr uint32_t ENCODE_BEQ(uint32_t rs1, uint32_t rs2, int32_t imm) {
        uint32_t uimm = static_cast<uint32_t>(imm);
        uint32_t bit12 = (uimm >> 12) & 0x1;
        uint32_t bit11 = (uimm >> 11) & 0x1;
        uint32_t bits10_5 = (uimm >> 5) & 0x3F;
        uint32_t bits4_1 = (uimm >> 1) & 0xF;
        uint32_t encoded = (bit12 << 31) | (bits10_5 << 25) | (bits4_1 << 8) | (bit11 << 7);
        return encoded | (rs2 << 20) | (rs1 << 15) | (0b000 << 12) | 0b1100011;
    }

    /** @brief 编码 BNE 指令（B-type, funct3=001）。 */
    static constexpr uint32_t ENCODE_BNE(uint32_t rs1, uint32_t rs2, int32_t imm) {
        uint32_t uimm = static_cast<uint32_t>(imm);
        uint32_t bit12 = (uimm >> 12) & 0x1;
        uint32_t bit11 = (uimm >> 11) & 0x1;
        uint32_t bits10_5 = (uimm >> 5) & 0x3F;
        uint32_t bits4_1 = (uimm >> 1) & 0xF;
        uint32_t encoded = (bit12 << 31) | (bits10_5 << 25) | (bits4_1 << 8) | (bit11 << 7);
        return encoded | (rs2 << 20) | (rs1 << 15) | (0b001 << 12) | 0b1100011;
    }

    /** @brief 编码 JAL 指令（J-type）。 */
    static constexpr uint32_t ENCODE_JAL(uint32_t rd, int32_t imm) {
        uint32_t uimm = static_cast<uint32_t>(imm);
        uint32_t bit20 = (uimm >> 20) & 0x1;
        uint32_t bits10_1 = (uimm >> 1) & 0x3FF;
        uint32_t bit11 = (uimm >> 11) & 0x1;
        uint32_t bits19_12 = (uimm >> 12) & 0xFF;
        uint32_t encoded = (bit20 << 31) | (bits19_12 << 12) | (bit11 << 20) | (bits10_1 << 21);
        return encoded | (rd << 7) | 0b1101111;
    }

    /** @brief 通用 Store 编码器（funct3 决定 SB/SH/SW）。 */
    static constexpr uint32_t ENCODE_STORE(uint32_t rs2, uint32_t rs1, int32_t imm, uint32_t funct3) {
        uint32_t uimm = static_cast<uint32_t>(imm);
        uint32_t imm11_5 = (uimm >> 5) & 0x7F;
        uint32_t imm4_0 = uimm & 0x1F;
        return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm4_0 << 7) | 0b0100011;
    }

    /** @brief 通用 Load 编码器（funct3 决定 LB/LH/LW/LBU/LHU/LWU）。 */
    static constexpr uint32_t ENCODE_LOAD(uint32_t rd, uint32_t rs1, int32_t imm, uint32_t funct3) {
        uint32_t imm12 = static_cast<uint32_t>(imm) & 0xFFFu;
        return (imm12 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | 0b0000011;
    }

    /** @brief EBREAK 指令常量（用于标记用例结束）。 */
    static constexpr uint32_t INSTR_EBREAK = 0x00100073u;

    /** @brief 用例表（定义在文件末尾的 inline 变量中）。 */
    static const std::vector<TeachingTestCase> s_tests;
};

inline const std::vector<TeachingTestCase> TeachingTests::s_tests = {
    {
        "regwrite_basics",
        "RegWrite信号基础：区分哪些指令写寄存器(x1,x2)，哪些不写(x3,x4)。理解ADD/ADDI写寄存器，SB/BEQ不写。",
        "scenario1",
        {
            ENCODE_ADDI(1, 0, 10),
            ENCODE_ADDI(2, 1, 5),
            ENCODE_R(3, 2, 1, 0b000, 0b0000000),
            ENCODE_STORE(1, 0, 0, 0b000),
            ENCODE_BEQ(0, 0, 8),
            ENCODE_ADDI(4, 0, 1),
            ENCODE_ADDI(4, 0, 2),
            INSTR_EBREAK
        }
    },
    {
        "alusrc_operands",
        "ALUSrc信号：理解ALU操作数选择。ADDI使用立即数(ALUSrc=1)，ADD使用寄存器(ALUSrc=0)。观察x1,x2,x3的最终值。",
        "scenario2",
        {
            ENCODE_ADDI(1, 0, 10),
            ENCODE_ADDI(2, 0, 20),
            ENCODE_R(3, 1, 2, 0b000, 0b0000000),
            ENCODE_ADDI(4, 3, 5),
            INSTR_EBREAK
        }
    },
    {
        "mem_load_store",
        "MemRead/MemWrite信号：理解内存访问控制。SW(Store)设置MemWrite，LW(Load)设置MemRead。观察数据如何存入和取出内存。",
        "scenario3",
        {
            ENCODE_AUIPC(10, 0),
            ENCODE_ADDI(1, 0, 0xDE),
            ENCODE_STORE(1, 10, 0, 0b000),
            ENCODE_LOAD(2, 10, 0, 0b000),
            INSTR_EBREAK
        }
    },
    {
        "branch_control",
        "Branch信号：理解分支指令如何影响流水线。BEQ/BNE根据条件决定是否跳转，观察PC变化。",
        "scenario4",
        {
            ENCODE_ADDI(1, 0, 5),
            ENCODE_ADDI(2, 0, 5),
            ENCODE_BEQ(1, 2, 8),
            ENCODE_ADDI(3, 0, 1),
            ENCODE_ADDI(3, 0, 2),
            ENCODE_BNE(1, 2, 8),
            ENCODE_ADDI(4, 0, 3),
            ENCODE_ADDI(4, 0, 4),
            INSTR_EBREAK
        }
    },
    {
        "jump_jal",
        "Jump信号(JAL)：理解跳转指令如何保存返回地址到rd并跳转。JAL将PC+4保存到rd，然后跳转到目标地址。",
        "scenario4",
        {
            ENCODE_JAL(1, 12),
            ENCODE_ADDI(2, 0, 1),
            ENCODE_ADDI(2, 0, 2),
            ENCODE_ADDI(3, 0, 3),
            INSTR_EBREAK
        }
    },
    {
        "pipeline_overview",
        "流水线综合：展示典型流水线执行序列。包含算术、访存、分支指令，观察五级流水线如何并行工作。",
        "all",
        {
            ENCODE_ADDI(1, 0, 100),
            ENCODE_ADDI(2, 0, 50),
            ENCODE_R(3, 1, 2, 0b000, 0b0000000),
            ENCODE_ADDI(4, 3, 10),
            ENCODE_AUIPC(5, 0),
            ENCODE_STORE(4, 5, 0, 0b000),
            ENCODE_LOAD(6, 5, 0, 0b000),
            ENCODE_BEQ(3, 4, 12),
            ENCODE_ADDI(7, 0, 99),
            ENCODE_ADDI(7, 0, 100),
            INSTR_EBREAK
        }
    },
    {
        "load_variants",
        "Load指令变体：LB(符号扩展)、LBU(零扩展)、LW。理解不同Load指令如何处理数据宽度和符号扩展。",
        "scenario3",
        {
            ENCODE_AUIPC(10, 0),
            ENCODE_ADDI(1, 0, 0xFF),
            ENCODE_STORE(1, 10, 0, 0b000),
            ENCODE_LOAD(2, 10, 0, 0b000),
            ENCODE_LOAD(3, 10, 0, 0b100),
            ENCODE_LOAD(4, 10, 0, 0b010),
            INSTR_EBREAK
        }
    },
    {
        "store_variants",
        "Store指令变体：SB(字节)、SH(半字)、SW(字)。理解不同Store指令如何存储不同宽度的数据。",
        "scenario3",
        {
            ENCODE_AUIPC(10, 0),
            ENCODE_ADDI(1, 0, 0xABCD),
            ENCODE_STORE(1, 10, 0, 0b010),
            ENCODE_LOAD(2, 10, 0, 0b010),
            INSTR_EBREAK
        }
    }
};

}

#endif
