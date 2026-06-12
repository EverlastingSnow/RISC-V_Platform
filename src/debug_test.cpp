// Quick debug runner: print last PC / cycle for a single ELF
#include <cstring>
#include <iostream>
#include "riscv/elf_loader.h"
#include "riscv/simulator.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: debug_test <elf_path> [max_cycles]\n";
        return 1;
    }
    int max_cycles = 500000;
    if (argc >= 3) max_cycles = std::atoi(argv[2]);

    auto result = riscv::load_elf(argv[1]);
    if (!result.success) {
        std::cerr << "ELF failed: " << result.error << "\n";
        return 1;
    }

    riscv::RISCVSimulator sim;
    sim.load_program(result.binary, result.load_offset);
    sim.run(max_cycles);

    std::cout << "halted=" << sim.halted()
              << " reason=" << (int)sim.halt_reason()
              << " pc=0x" << std::hex << sim.pc()
              << " halt_pc=0x" << sim.halt_pc()
              << " inst=0x" << sim.halt_inst()
              << " cycle=" << std::dec << sim.cycle() << "\n";

    // Dump all registers
    const auto& r = sim.registers().raw();
    std::cout << "Registers:\n";
    for (int i = 0; i < 32; ++i) {
        std::cout << "  x" << i << "=0x" << std::hex << r[i] << std::dec << "\n";
    }
    std::cout << "  mip=0x" << std::hex << sim.csr().read(0x344)
              << " mie=0x" << sim.csr().read(0x304)
              << " mstatus=0x" << sim.csr().read(0x300)
              << " mepc=0x" << sim.csr().read(0x341)
              << " mcause=0x" << sim.csr().read(0x342) << std::dec << "\n";
    std::cout << "  mtval=0x" << std::hex << sim.csr().read(0x343)
              << " mtvec=0x" << sim.csr().read(0x305) << std::dec << "\n";
    return 0;
}
