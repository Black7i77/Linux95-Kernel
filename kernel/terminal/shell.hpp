#pragma once

#include "terminal/output.hpp"

namespace linux95::shell {

void execute_command(
    terminal::Output& output,
    char* command);

[[noreturn]] void run_vga();

// Compatibility entry point while kernel.cpp still uses shell::run().
// The graphics integration task will switch the boot path explicitly
// to run_vga() for fallback mode.
[[noreturn]] void run();

} // namespace linux95::shell
