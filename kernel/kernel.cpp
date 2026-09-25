#include "boot_info.hpp"
#include "graphics/framebuffer.hpp"
#include "gui/desktop.hpp"

#include "arch/debug.hpp"
#include "arch/io.hpp"
#include "arch/interrupts.hpp"
#include "arch/keyboard.hpp"
#include "arch/mouse.hpp"
#include "arch/pic.hpp"
#include "arch/pit.hpp"
#include "drivers/rtl8139.hpp"
#include "memory/address.hpp"
#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "memory/paging.hpp"
#include "memory/physical.hpp"
#include "memory/self_test.hpp"
#include "memory/virtual.hpp"
#include "panic/panic.hpp"
#include "pci/pci.hpp"
#include "filesystem/filesystem.hpp"
#include "filesystem/filesystem_self_test.hpp"
#include "filesystem/vfs.hpp"
#include "filesystem/vfs_self_test.hpp"
#include "storage/storage_self_test.hpp"
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

    // The memory self-test owns a temporary VA inside the graphics
    // address region. Run it first, then install the persistent
    // framebuffer MMIO mapping.
    const graphics::FramebufferInitResult framebuffer_result =
        graphics::initialize_framebuffer(*boot_info);

    if (framebuffer_result ==
        graphics::FramebufferInitResult::Ready) {
        debug::write("[PASS] framebuffer_mapped\n");
    } else if (framebuffer_result ==
               graphics::FramebufferInitResult::Unavailable) {
        debug::write("[INFO] framebuffer_unavailable\n");
    } else if (framebuffer_result ==
               graphics::FramebufferInitResult::InvalidMetadata) {
        debug::write("[PANIC] framebuffer_invalid\n");
        panic::halt("Framebuffer metadata invalid");
    } else if (framebuffer_result ==
               graphics::FramebufferInitResult::MappingFailed) {
        debug::write("[PANIC] framebuffer_mapping\n");
        panic::halt("Framebuffer mapping failed");
    }

    if (!storage::self_test::run()) {
        debug::write("[PANIC] storage_self_test\n");
        panic::halt("Storage self-test failed");
    }

    if (!heap::initialize(memory::heap_start(), memory::heap_end())) {
        panic::halt("Could not initialize kernel heap");
    }

    if (!filesystem::initialize()) {
        debug::write("[PANIC] fat32_mount\n");
        panic::halt("FAT32 mount failed");
    }

    debug::write("[PASS] fat32_mount\n");

    if (!filesystem::self_test::run()) {
        debug::write("[PANIC] filesystem_self_test\n");
        panic::halt("Filesystem self-test failed");
    }

    filesystem::vfs::initialize();
    debug::write("[PASS] vfs_initialize\n");

    if (!filesystem::vfs::self_test::run()) {
        debug::write("[PANIC] vfs_self_test\n");
        panic::halt("VFS self-test failed");
    }

    debug::write("[PASS] pci_bus_ready\n");
    pci::Address rtl8139_address{};
    if (!pci::find_device(0x10EC, 0x8139, rtl8139_address)) {
        debug::write("[WARN] pci_no_rtl8139\n");
        debug::write("[WARN] network_offline\n");
    } else {
        debug::write("[PASS] rtl8139_detected\n");
        if (rtl8139::initialize()) {
            debug::write("[PASS] rtl8139_initialized\n");
        } else {
            debug::write("[WARN] network_offline\n");
        }
    }

    interrupts::initialize();
    pic::initialize();
    pit::initialize(100);
    keyboard::initialize();

    const bool mouse_online =
        mouse::initialize();

    pic::unmask_irq(0);
    pic::unmask_irq(1);

    if (mouse_online) {
        pic::unmask_irq(12);
        debug::write("[PASS] mouse_initialized\n");
    } else {
        debug::write("[INFO] mouse_unavailable\n");
    }

    io::enable_interrupts();

    if (framebuffer_result ==
        graphics::FramebufferInitResult::Unavailable) {
        debug::write("[INFO] graphics_fallback_vga\n");

        vga::set_color(10, 0);
        vga::write("Linux95 Kernel v1.0 Graphics Desktop Foundation\n");
        vga::set_color(7, 0);
        vga::write("Graphics: VGA text fallback\n");
        vga::write("Architecture: x86_64 higher-half\n");
        vga::write("Bootloader: Linux95 BIOS Loader\n");
        vga::write("Storage: ATA PIO + LBA28 (boot disk read-only)\n");
        vga::set_color(10, 0);
        vga::write("Status: VGA FALLBACK ONLINE\n\n");
        vga::set_color(7, 0);
        vga::write("Type 'help' for commands.\n\n");

        debug::write("[PASS] shell_ready\n");
        shell::run_vga();
    }

    graphics::Framebuffer* const active_framebuffer =
        graphics::framebuffer();

    if (active_framebuffer == nullptr) {
        debug::write("[PANIC] framebuffer_missing\n");
        panic::halt("Framebuffer ready but accessor returned null");
    }

    debug::write("[PASS] renderer_online\n");

    desktop::run(
        *active_framebuffer,
        mouse_online);
}
