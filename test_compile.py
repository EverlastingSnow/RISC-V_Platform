import subprocess
import tempfile
from pathlib import Path

source = '''
.text
.globl main

main:
    addi x1, x0, 10
    addi x2, x0, 20
    add x3, x1, x2
    ebreak
'''

import config
from pathlib import Path

temp_dir = Path(tempfile.mkdtemp())
source_file = temp_dir / 'test.S'
source_file.write_text(source)

# Simple linker script
linker_script = temp_dir / 'link.ld'
linker_script.write_text('''OUTPUT_ARCH("riscv")
ENTRY(main)

SECTIONS {
    . = 0x80000000;
    .text : { *(.text) }
}
''')

elf_file = temp_dir / 'output.elf'

riscv_gcc = Path(config.RISCV_TOOLCHAIN_PATH) / 'riscv-none-elf-gcc.exe'
objdump = riscv_gcc.parent / 'riscv-none-elf-objdump.exe'

compile_cmd = [
    str(riscv_gcc),
    '-march=' + config.ARCH,
    '-mabi=' + config.ABI,
    '-static', '-mcmodel=medany', '-nostdlib', '-nostartfiles',
    '-T', str(linker_script),
    str(source_file),
    '-o', str(elf_file),
]

result = subprocess.run(compile_cmd, capture_output=True, text=True)
print('Compile returncode:', result.returncode)
if result.stderr:
    print('Stderr:', result.stderr[:300])

# Disassemble
result = subprocess.run([str(objdump), '-D', '-m', 'riscv:rv64', str(elf_file)], capture_output=True, text=True)
print(result.stdout[:1500])
