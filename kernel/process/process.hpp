#pragma once

#include <cstddef>
#include <cstdint>

#include "process/context.hpp"

namespace linux95::process {

enum class State {
    Unused,
    Created,
    Ready,
    Running,
    Blocked,
    Exited,
};

struct Process {
    uint32_t pid;
    State state;
    uint64_t page_table_physical;
    uint64_t user_entry;
    uint64_t user_stack_top;
    uint64_t kernel_stack_base;
    uint64_t kernel_stack_top;
    UserContext context;
    int64_t exit_code;
    uint32_t fault_vector;
};

void initialize();
Process* allocate();
Process* find(uint32_t pid);
void release(Process& process);
size_t capacity();

}
