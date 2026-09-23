#include "boot_info.hpp"

#include "arch/debug.hpp"
#include "arch/io.hpp"
#include "arch/interrupts.hpp"
#include "arch/keyboard.hpp"
#include "arch/pic.hpp"
#include "arch/pit.hpp"
#include "memory/address.hpp"
#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "memory/paging.hpp"
#include "memory/physical.hpp"
#include "memory/self_test.hpp"
#include "memory/virtual.hpp"
#include "panic/panic.hpp"
#include "terminal/shell.hpp"
#include "terminal/vga.hpp"

#include <stdint.h>

extern "C" [[noreturn]] void linux95_reload_cr3_and_reenter(
    uint64_t new_cr3,
    uint64_t higher_half_entry,
    uint64_t boot_info);

extern "C" [[noreturn]] void linux95_higher_half_entry(
    const linux95::BootInfo* boot_info);

extern "C" void kernel_main(const linux95::BootInfo* boot_info)
{
    using namespace linux95;

    io::disable_interrupts();
    debug::write("[BOOT] low_kernel_entry\n");

    vga::set_color(7, 0);
    vga::clear();

    if (!valid_boot_info(boot_info)) {
        panic::halt("Invalid BootInfo handoff");
    }

    if (!memory::initialize(*boot_info)) {
        panic::halt("Could not initialize E820 memory map");
    }

    if (!memory::virtual_memory::prepare_bootstrap(*boot_info)) {
        panic::halt("Could not build bootstrap page tables");
    }

    debug::write("[PASS] bootstrap_tables_created\n");

    const uint64_t low_entry =
        reinterpret_cast<uint64_t>(&linux95_higher_half_entry);
    const uint64_t high_entry =
        memory::virtual_memory::higher_half_alias(low_entry);

    linux95_reload_cr3_and_reenter(
        memory::virtual_memory::bootstrap_cr3(),
        high_entry,
        reinterpret_cast<uint64_t>(boot_info));
}

extern "C" [[noreturn]] void linux95_higher_half_entry(
    const linux95::BootInfo* boot_info)
{
    using namespace linux95;

    if (!valid_boot_info(boot_info)) {
        panic::halt("Invalid BootInfo after higher-half transition");
    }

    debug::write("[PASS] higher_half_entry\n");

    constexpr uint64_t kVgaPhysical = 0x000B8000ULL;
    if (!memory::virtual_memory::hhdm_contains(kVgaPhysical)) {
        panic::halt("HHDM does not contain VGA memory");
    }

    volatile uint16_t* const hhdm_vga =
        reinterpret_cast<volatile uint16_t*>(
            memory::physical_to_hhdm(kVgaPhysical));
    const uint16_t original = hhdm_vga[0];
    hhdm_vga[0] = original;
    debug::write("[PASS] hhdm_online\n");

    if (!memory::physical::initialize(*boot_info)) {
        debug::write("[PANIC] physical_allocator_init\n");
        panic::halt("Physical allocator initialization failed");
    }
    debug::write("[PASS] physical_allocator_online\n");

    if (memory::paging::translate(memory::kKernelVirtualBase) !=
        memory::kKernelPhysicalBase) {
        debug::write("[PANIC] virtual_memory_translation\n");
        panic::halt("Higher-half translation validation failed");
    }
    debug::write("[PASS] virtual_memory_online\n");

    if (!memory::self_test::run()) {
        debug::write("[PANIC] memory_self_test\n");
        panic::halt("Memory self-test failed");
    }
    debug::write("[PASS] memory_self_test\n");

    if (!heap::initialize(memory::heap_start(), memory::heap_end())) {
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
    vga::write("Linux95 Kernel v1.0 Memory Foundation\n");
    vga::set_color(7, 0);
    vga::write("Architecture: x86_64 higher-half\n");
    vga::write("Bootloader: Linux95 BIOS Loader\n");
    vga::write("Memory: HHDM + 4 KiB page allocator/VM\n");
    vga::set_color(10, 0);
    vga::write("Status: ONLINE\n\n");
    vga::set_color(7, 0);
    vga::write("Type 'help' for commands.\n\n");

    debug::write("[PASS] shell_ready\n");
    shell::run();
}
