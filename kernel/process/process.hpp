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

struct ReapOperations {
    void* context;
    bool (*lookup_user_page)(void* context,
                             uint64_t root_physical,
                             uint64_t virtual_address,
                             uint64_t& physical_address);
    void (*release_physical_page)(void* context,
                                  uint64_t physical_address);
    void (*destroy_address_space)(void* context,
                                  uint64_t root_physical);
    uint64_t (*kernel_stack_physical)(void* context,
                                      uint64_t kernel_stack_base);
};

void initialize();
Process* allocate();
Process* find(uint32_t pid);
void release(Process& process);
size_t capacity();
Process* table();
void mark_exited(Process& process, int64_t code);
void reap_exited();
void reap_one_for_test(Process& process);
void reap_one_for_test(Process& process, const ReapOperations& operations);

}
