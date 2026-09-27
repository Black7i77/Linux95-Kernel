#pragma once

#include "terminal/output.hpp"

namespace linux95::terminal {

// Mutates the command buffer only after recognizing a writable command.
// Returns false for every command outside the six writable operations.
bool execute_filesystem_command(Output& output, char* command);

} // namespace linux95::terminal
