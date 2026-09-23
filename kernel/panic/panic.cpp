#include "panic/panic.hpp"

#include "arch/io.hpp"
#include "arch/debug.hpp"
#include "terminal/vga.hpp"

namespace linux95::panic {

[[noreturn]] void halt(const char* message)
{
    io::disable_interrupts();
    debug::write("[PANIC] ");
    debug::write(message != nullptr ? message : "(null)");
    debug::write("\n");

    vga::set_color(15, 4);
    vga::clear();
    vga::write("LINUX95 KERNEL PANIC\n\n");

    if (message != nullptr) {
        vga::write(message);
        vga::put_char('\n');
    }

    vga::write("\nCPU halted.");

    for (;;) {
        io::halt();
    }
}

[[noreturn]] void exception(uint64_t vector, uint64_t error_code)
{
    io::disable_interrupts();
    debug::write("[PANIC] cpu_exception\n");

    vga::set_color(15, 4);
    vga::clear();
    vga::write("LINUX95 KERNEL PANIC\n\n");
    vga::write("Unhandled CPU exception\n");
    vga::write("Vector: ");
    vga::write_uint(vector);
    vga::write("\nError code: ");
    vga::write_hex(error_code);
    vga::write("\n\nCPU halted.");

    for (;;) {
        io::halt();
    }
}

} // namespace linux95::panic
