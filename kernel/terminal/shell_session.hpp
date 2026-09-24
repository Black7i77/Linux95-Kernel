#pragma once

#include "terminal/output.hpp"

#include <stddef.h>

namespace linux95::terminal {

using ExecuteCallback =
    void (*)(
        void* context,
        Output& output,
        char* command);

class ShellSession {
public:
    ShellSession(
        Output& output,
        void* context,
        ExecuteCallback execute);

    void begin();
    void on_char(char c);

private:
    static constexpr size_t kCommandCapacity = 64;

    Output& output_;
    void* execute_context_;
    ExecuteCallback execute_;

    char command_[kCommandCapacity];
    size_t length_;

    void prompt();
};

} // namespace linux95::terminal
