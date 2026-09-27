#pragma once

#include "filesystem/vfs.hpp"
#include "terminal/output.hpp"

#include <stdint.h>

namespace linux95::shell {

enum class CommandAction : uint8_t {
    Continue,
    OpenEditor,
};

struct CommandResult {
    CommandAction action;
    char path[filesystem::vfs::kPathCapacity];
};

CommandResult execute_command(
    terminal::Output& output,
    char* command);

[[noreturn]] void run_vga();

// Compatibility entry point while kernel.cpp still uses shell::run().
// The graphics integration task will switch the boot path explicitly
// to run_vga() for fallback mode.
[[noreturn]] void run();

} // namespace linux95::shell
