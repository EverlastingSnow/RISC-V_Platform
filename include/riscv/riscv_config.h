#ifndef RISCV_CONFIG_H
#define RISCV_CONFIG_H

#include <string>
#include <cstdlib>
#include <filesystem>

namespace riscv {

class Config {
public:
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
