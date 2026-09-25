#include "arch/x86_64/tss.hpp"

#include "arch/x86_64/segments.hpp"
#include "syscall/syscall.hpp"

#include <stdint.h>

namespace linux95::arch::x86_64 {

TaskStateSegment g_tss{};

namespace {

alignas(16) uint8_t g_nmi_stack[4096];
alignas(16) uint8_t g_double_fault_stack[4096];
alignas(16) uint8_t g_machine_check_stack[4096];

extern "C" void linux95_load_task_register(uint16_t selector);

}

void initialize_tss()
{
    g_tss = {};
    g_tss.io_map_base = sizeof(TaskStateSegment);
    g_tss.ist1 = reinterpret_cast<uint64_t>(g_nmi_stack + sizeof(g_nmi_stack));
    g_tss.ist2 = reinterpret_cast<uint64_t>(
        g_double_fault_stack + sizeof(g_double_fault_stack));
    g_tss.ist3 = reinterpret_cast<uint64_t>(
        g_machine_check_stack + sizeof(g_machine_check_stack));
    linux95_load_task_register(kTssSelector);
    syscall::initialize_fast_path();
}

void set_tss_rsp0(uint64_t rsp0)
{
    g_tss.rsp0 = rsp0;
}

uint64_t tss_rsp0()
{
    return g_tss.rsp0;
}

}
