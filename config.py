"""
全局配置模块。

集中管理项目运行所需的路径、端口与编译选项常量。
所有配置均允许通过同名环境变量覆盖，未设置时回退到项目内的默认值。
"""

import os
from pathlib import Path

# 项目根目录：当前配置文件所在目录
PROJECT_ROOT = Path(__file__).parent.resolve()

# RISC-V GCC 工具链 bin 目录（可通过 RISCV_TOOLCHAIN_PATH 环境变量覆盖）
RISCV_TOOLCHAIN_PATH = os.environ.get(
    "RISCV_TOOLCHAIN_PATH",
    str(PROJECT_ROOT / "xpack-riscv-none-elf-gcc-15.2.0-1" / "bin")
)

# 教学材料目录（用户上传的源码、参考资料等）
TEACHING_DIR = PROJECT_ROOT / "teaching"
# 编译产物输出目录（生成的 ELF / bin / lst 等）
COMPILE_OUTPUT_DIR = PROJECT_ROOT / "compile_output"
# C++ 构建目录（存放编译后的 riscv_sim_server 可执行文件）
BUILD_DIR = PROJECT_ROOT / "build"

# 前端状态/信号 HTTP 服务端口
API_SERVER_PORT = int(os.environ.get("API_SERVER_PORT", 8080))
# WebSocket 主服务端口（前端与 C++ 模拟器通信的桥梁）
WEBSOCKET_PORT = int(os.environ.get("WEBSOCKET_PORT", 8081))
# 访问统计 HTTP 服务端口
STATS_PORT = int(os.environ.get("STATS_PORT", 8082))

# C++ 模拟器可执行文件路径：优先使用环境变量，否则在 build 目录中自动探测
SIM_SERVER_PATH = os.environ.get("SIM_SERVER_PATH")
if SIM_SERVER_PATH is None:
    # 自动探测：依次尝试 Linux 与 Windows 两种可执行文件名
    for exe_name in ["riscv_sim_server", "riscv_sim_server.exe"]:
        sim_path = BUILD_DIR / exe_name
        if sim_path.exists():
            SIM_SERVER_PATH = str(sim_path)
            break
    else:
        # 未找到可执行文件，使用默认 .exe 路径（运行时由上层捕获）
        SIM_SERVER_PATH = str(BUILD_DIR / "riscv_sim_server.exe")

# 指令集源码与链接脚本所在目录
SOURCE_DIR = PROJECT_ROOT / "isa"
# 链接脚本（用于指定内存布局与入口点）
LINKER_SCRIPT = SOURCE_DIR / "link.ld"
# 启动文件（提供 _start 入口与基本运行时环境）
START_FILE = SOURCE_DIR / "start.S"

# 目标架构：RV64G 通用指令集
ARCH = "rv64g"
# 应用程序二进制接口：LP64D（64 位长整形 + 双精度浮点）
ABI = "lp64d"
# 通用 GCC 编译选项：静态链接、中等代码模型、隐藏符号、不使用标准库与启动文件
GCC_OPTS = "-static -mcmodel=medany -fvisibility=hidden -nostdlib -nostartfiles"
