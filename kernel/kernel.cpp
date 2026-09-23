#include "boot_info.hpp"

#include "arch/io.hpp"
#include "arch/debug.hpp"
#include "arch/interrupts.hpp"
#include "arch/keyboard.hpp"
#include "arch/pic.hpp"
#include "arch/pit.hpp"
#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "panic/panic.hpp"
#include "terminal/shell.hpp"
#include "terminal/vga.hpp"

extern "C" void kernel_main(const linux95::BootInfo* boot_info)
{
    using namespace linux95;

    io::disable_interrupts();
    debug::write("[BOOT] kernel_entry\n");

    const bool boot_info_ok = valid_boot_info(boot_info);

    vga::set_color(7, 0);
    vga::clear();

    if (!boot_info_ok) {
        panic::halt("Invalid BootInfo handoff");
    }

    if (!memory::initialize(*boot_info)) {
        panic::halt("Could not initialize E820 memory map");
    }

    if (!heap::initialize(
            memory::heap_start(),
            memory::heap_end())) {
        panic::halt("Could not initialize kernel heap");
    }

    interrupts::initialize();
    pic::initialize();

    pit::initialize(100);
    keyboard::initialize();

    pic::unmask_irq(0);
    pic::unmask_irq(1);

    io::enable_interrupts();

    vga::set_color(10, 0);
    vga::write("Linux95 Kernel v0.2 Interactive\n");

    vga::set_color(7, 0);
    vga::write("Architecture: x86_64\n");
    vga::write("Bootloader: Linux95 BIOS Loader\n");
    vga::write("Kernel: freestanding C++\n");

    vga::set_color(10, 0);
    vga::write("Status: ONLINE\n\n");

    vga::set_color(7, 0);
    vga::write("Type 'help' for commands.\n\n");

    debug::write("[PASS] shell_ready\n");

    shell::run();
}
