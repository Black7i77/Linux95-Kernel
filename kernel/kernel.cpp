#include "boot_info.hpp"
#include "vga.hpp"

extern "C" void kernel_main(const linux95::BootInfo* boot_info)
{
    linux95::vga::set_color(7, 0);
    linux95::vga::clear();

    if (boot_info == nullptr ||
        boot_info->magic != linux95::kBootInfoMagic)
    {
        linux95::vga::set_color(12, 0);

        linux95::vga::write("Linux95 Kernel v0.1\n");
        linux95::vga::write("ERROR: invalid BootInfo handoff\n");

        return;
    }

    linux95::vga::set_color(10, 0);
    linux95::vga::write("Linux95 Kernel v0.1\n");

    linux95::vga::set_color(7, 0);
    linux95::vga::write("Architecture: x86_64\n");
    linux95::vga::write("Bootloader: Linux95 BIOS Loader\n");
    linux95::vga::write("Kernel: freestanding C++\n");

    linux95::vga::set_color(10, 0);
    linux95::vga::write("Status: ONLINE\n");
}
