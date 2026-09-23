#include "terminal/shell.hpp"

#include "arch/io.hpp"
#include "arch/keyboard.hpp"
#include "arch/pit.hpp"
#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "terminal/vga.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::shell {
namespace {

constexpr size_t kCommandCapacity = 64;

bool equals(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return false;
        }

        ++a;
        ++b;
    }

    return *a == '\0' && *b == '\0';
}

void prompt()
{
    vga::set_color(10, 0);
    vga::write("linux95");
    vga::set_color(7, 0);
    vga::write("> ");
}

void print_version()
{
    vga::write("Linux95 Kernel v0.2 Interactive\n");
    vga::write("Architecture: x86_64\n");
    vga::write("Kernel: freestanding C++\n");
}

void print_help()
{
    vga::write("Commands:\n");
    vga::write("  help     Show this command list\n");
    vga::write("  clear    Clear the screen\n");
    vga::write("  version  Show kernel version\n");
    vga::write("  mem      Show memory statistics\n");
    vga::write("  uptime   Show uptime in seconds\n");
    vga::write("  reboot   Reboot the machine\n");
}

void print_memory()
{
    vga::write("Total RAM: ");
    vga::write_uint(memory::total_bytes() / (1024u * 1024u));
    vga::write(" MiB\n");

    vga::write("Usable RAM: ");
    vga::write_uint(memory::usable_bytes() / (1024u * 1024u));
    vga::write(" MiB\n");

    vga::write("Usable regions: ");
    vga::write_uint(memory::usable_regions());
    vga::put_char('\n');

    vga::write("Heap used: ");
    vga::write_uint(heap::used_bytes() / 1024u);
    vga::write(" KiB / ");
    vga::write_uint(heap::capacity_bytes() / 1024u);
    vga::write(" KiB\n");
}

void reboot()
{
    vga::write("Rebooting...\n");

    constexpr uint32_t kWaitLimit = 1000000u;

    for (uint32_t i = 0; i < kWaitLimit; ++i) {
        if ((io::inb(0x64) & 0x02u) == 0) {
            io::outb(0x64, 0xFE);

            for (uint32_t spin = 0; spin < kWaitLimit; ++spin) {
                io::pause();
            }

            vga::write("Reboot request did not reset the system.\n");
            return;
        }

        io::pause();
    }

    vga::write("Keyboard controller stayed busy; reboot cancelled.\n");
}

void execute(const char* command)
{
    if (command[0] == '\0') {
        return;
    }

    if (equals(command, "help")) {
        print_help();
        return;
    }

    if (equals(command, "clear")) {
        vga::clear();
        return;
    }

    if (equals(command, "version")) {
        print_version();
        return;
    }

    if (equals(command, "mem")) {
        print_memory();
        return;
    }

    if (equals(command, "uptime")) {
        vga::write("Uptime: ");
        vga::write_uint(pit::uptime_seconds());
        vga::write(" seconds\n");
        return;
    }

    if (equals(command, "reboot")) {
        reboot();
        return;
    }

    vga::write("Unknown command: ");
    vga::write(command);
    vga::write("\nType 'help' for commands.\n");
}

} // namespace

[[noreturn]] void run()
{
    char command[kCommandCapacity];
    size_t length = 0;

    prompt();

    for (;;) {
        if (!keyboard::has_char()) {
            io::halt();
            continue;
        }

        const char c = keyboard::read_char();

        if (c == '\n') {
            vga::put_char('\n');
            command[length] = '\0';
            execute(command);
            length = 0;
            prompt();
            continue;
        }

        if (c == '\b') {
            if (length > 0) {
                --length;
                vga::put_char('\b');
            }
            continue;
        }

        if (c >= 32 && c <= 126) {
            if (length + 1 < kCommandCapacity) {
                command[length++] = c;
                vga::put_char(c);
            }
        }
    }
}

} // namespace linux95::shell
