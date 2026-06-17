#ifndef RISCV_CONFIG_H
#define RISCV_CONFIG_H

#include <string>
#include <cstdlib>
#include <filesystem>

namespace riscv {

/**
 * @brief 平台/项目级静态配置。
 *
 * 当前只暴露一个教学资源目录的获取入口；
 * 后续如需增加 ISA 开关、内存大小等全局配置，可在本类扩展。
 */
class Config {
public:
    /**
     * @brief 返回教学资源目录的绝对路径（教学 ELF、teaching 测试等所在目录）。
     *
     * 解析优先级：
     *   1. 环境变量 `RISCV_TEACHING_DIR`（非空即用）
     *   2. 当前工作目录下的 `teaching` 子目录
     *
     * 第一次调用后结果会被缓存，后续调用直接返回缓存值（线程安全依赖 C++11 静态局部初始化）。
     *
     * @return 教学目录绝对路径
     */
    static const std::string& get_teaching_dir() {
        static std::string path = []() {
            const char* env = std::getenv("RISCV_TEACHING_DIR");
            if (env && env[0] != '\0') {
                return std::string(env);
            }
            std::filesystem::path exe_dir = std::filesystem::current_path();
            return (exe_dir / "teaching").string();
        }();
        return path;
    }
};

}

#endif
