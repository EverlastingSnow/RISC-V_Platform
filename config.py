import os
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.resolve()

RISCV_TOOLCHAIN_PATH = os.environ.get(
    "RISCV_TOOLCHAIN_PATH",
    str(PROJECT_ROOT / "xpack-riscv-none-elf-gcc-15.2.0-1" / "bin")
)

TEACHING_DIR = PROJECT_ROOT / "teaching"
COMPILE_OUTPUT_DIR = PROJECT_ROOT / "compile_output"
BUILD_DIR = PROJECT_ROOT / "build"

API_SERVER_PORT = int(os.environ.get("API_SERVER_PORT", 8080))
WEBSOCKET_PORT = int(os.environ.get("WEBSOCKET_PORT", 8081))
STATS_PORT = int(os.environ.get("STATS_PORT", 8082))

SIM_SERVER_PATH = os.environ.get("SIM_SERVER_PATH")
if SIM_SERVER_PATH is None:
    for exe_name in ["riscv_sim_server.exe", "riscv_sim_server"]:
        sim_path = BUILD_DIR / exe_name
        if sim_path.exists():
            SIM_SERVER_PATH = str(sim_path)
            break
    else:
        SIM_SERVER_PATH = str(BUILD_DIR / "riscv_sim_server.exe")

SOURCE_DIR = PROJECT_ROOT / "isa"
LINKER_SCRIPT = SOURCE_DIR / "link.ld"
START_FILE = SOURCE_DIR / "start.S"

ARCH = "rv64g"
ABI = "lp64d"
GCC_OPTS = "-static -mcmodel=medany -fvisibility=hidden -nostdlib -nostartfiles"
