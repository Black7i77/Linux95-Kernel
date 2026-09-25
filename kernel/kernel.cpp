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
#include "arch/x86_64/segments.hpp"
#include "arch/x86_64/tss.hpp"
#include "memory/address.hpp"
#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "memory/paging.hpp"
#include "memory/physical.hpp"
#include "memory/self_test.hpp"
#include "memory/virtual.hpp"
#include "process/process.hpp"
#include "syscall/syscall.hpp"
#include "user/elf.hpp"
#include "net/network.hpp"
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

namespace {

void write_decimal(uint32_t value)
{
    char digits[10];
    size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(digits));
    while (count != 0) {
        const char digit[] = {digits[--count], '\0'};
        linux95::debug::write(digit);
    }
}

void log_user_image_failure(const char* path,
                            linux95::user::ElfStatus status)
{
    linux95::debug::write("[WARN] user image load failed: ");
    linux95::debug::write(path);
    linux95::debug::write(" status=");
    write_decimal(static_cast<uint32_t>(status));
    linux95::debug::write("\n");
}

void discard_startup_processes()
{
    using namespace linux95;

    // Startup runs before scheduler::run_once(), so the host CR3 and host
    // stack are still active. Reuse the normal Task 10 reaper for any image
    // that completed before a later startup step failed.
    syscall::set_cpu_process(nullptr, 0);
    arch::x86_64::set_tss_rsp0(0);
    process::Process* const table = process::table();
    if (table != nullptr) {
        for (size_t index = 0; index < process::capacity(); ++index) {
            if (table[index].state != process::State::Unused) {
                process::mark_exited(table[index], -1);
            }
        }
    }
    process::reap_exited();
}

}

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

    process::initialize();
    debug::write("[PASS] process subsystem initialized\n");

    bool user_processes_online =
        process::table() != nullptr && process::capacity() == 16;
    if (!user_processes_online) {
        debug::write("[WARN] process table initialization failed\n");
    }

    if (user_processes_online) {
        process::Process* const pid1 = process::allocate();
        if (pid1 == nullptr) {
            debug::write("[WARN] could not allocate PID1 process slot\n");
            user_processes_online = false;
        } else {
            const user::ElfStatus status =
                user::load_process_image("/USER/INIT.ELF", *pid1);
            if (status == user::ElfStatus::Ok) {
                debug::write("[PASS] pid1 ELF loaded\n");
            } else {
                log_user_image_failure("/USER/INIT.ELF", status);
                user_processes_online = false;
            }
        }
    }

    if (user_processes_online) {
        process::Process* const pid2 = process::allocate();
        if (pid2 == nullptr) {
            debug::write("[WARN] could not allocate PID2 process slot\n");
            user_processes_online = false;
        } else {
            const user::ElfStatus status =
                user::load_process_image("/USER/WORKER.ELF", *pid2);
            if (status == user::ElfStatus::Ok) {
                debug::write("[PASS] pid2 ELF loaded\n");
            } else {
                log_user_image_failure("/USER/WORKER.ELF", status);
                user_processes_online = false;
            }
        }
    }

    if (!user_processes_online) {
        discard_startup_processes();
        debug::write("[WARN] user_processes_offline\n");
    }

    debug::write("[PASS] pci_bus_ready\n");
    if (!network::initialize()) {
        debug::write("[WARN] network_offline\n");
    }

    arch::x86_64::initialize_segments();
    arch::x86_64::initialize_tss();
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

    debug::write("[DBG] before_sti\n");
    debug::write("[DBG] before_sti\n");
    io::enable_interrupts();
    debug::write("[DBG] after_sti\n");
    debug::write("[DBG] after_sti\n");

#ifdef LINUX95_QEMU_NETWORK_SELF_TEST
    (void)network::start_ping(
        net::Ipv4Address{{10, 0, 2, 2}});
#endif

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
