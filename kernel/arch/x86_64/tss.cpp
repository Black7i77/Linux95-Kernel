#include "arch/x86_64/tss.hpp"

#include "arch/x86_64/segments.hpp"
#include "syscall/syscall.hpp"

namespace linux95::arch::x86_64 {

TaskStateSegment g_tss{};

namespace {

extern "C" void linux95_load_task_register(uint16_t selector);

}

void initialize_tss()
{
    g_tss = {};
    g_tss.io_map_base = sizeof(TaskStateSegment);
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
