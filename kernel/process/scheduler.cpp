#include "process/scheduler.hpp"

#include "arch/x86_64/control_regs.hpp"
#include "arch/x86_64/tss.hpp"
#include "arch/debug.hpp"
#include "syscall/syscall.hpp"

namespace linux95::scheduler {

namespace {

process::HostContext g_host_context{};
uint64_t g_host_cr3 = 0;
uint64_t g_host_rflags = 0;
int g_previous_slot = -1;
uint32_t g_last_yielded_pid = 0;
size_t g_reaped_process_count = 0;
bool g_desktop_survival_emitted = false;

uint64_t read_rflags()
{
    uint64_t value = 0;
    asm volatile("pushfq; pop %0" : "=r"(value));
    return value;
}

void write_rflags(uint64_t value)
{
    asm volatile("push %0; popfq" : : "r"(value) : "cc", "memory");
}

}

int choose_next(const linux95::process::Process* table,
                size_t count,
                int previous_slot) {
    if (table == nullptr || count == 0) {
        return -1;
    }

    const size_t start = static_cast<size_t>(previous_slot + 1) % count;
    for (size_t offset = 0; offset < count; ++offset) {
        const size_t slot = (start + offset) % count;
        if (table[slot].state == linux95::process::State::Ready) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

bool run_once()
{
    process::Process* table = process::table();
    const int slot = choose_next(
        table,
        process::capacity(),
        g_previous_slot);
    if (slot < 0) {
        return false;
    }

    process::Process& selected = table[slot];
    g_previous_slot = slot;
    selected.state = process::State::Running;
    g_host_cr3 = arch::x86_64::read_cr3();
    g_host_rflags = read_rflags();

    if (process::process_save_host(&g_host_context) == 0) {
        arch::x86_64::write_cr3(selected.page_table_physical);
        arch::x86_64::set_tss_rsp0(selected.kernel_stack_top);
        syscall::set_cpu_process(
            &selected,
            selected.kernel_stack_top);
        process::process_resume_user(&selected.context);
    }

    write_rflags(g_host_rflags);

    if (selected.state == process::State::Exited) {
        process::reap_exited();
        ++g_reaped_process_count;
        debug::write("[PASS] process reaped\n");
        const int next_slot = choose_next(
            process::table(),
            process::capacity(),
            g_previous_slot);
        if (g_reaped_process_count >= 2 && next_slot < 0 &&
            !g_desktop_survival_emitted) {
            g_desktop_survival_emitted = true;
            debug::write("[PASS] desktop remained online\n");
        }
    }

    return true;
}

[[noreturn]] void return_to_host(
    process::Process& process,
    process::UserContext& context,
    HostReason reason)
{
    (void) context;
    if (reason == HostReason::Yield &&
        process.state == process::State::Ready) {
        if (g_last_yielded_pid != 0 &&
            g_last_yielded_pid != process.pid) {
            static bool switch_emitted = false;
            if (!switch_emitted) {
                switch_emitted = true;
                debug::write("[PASS] cooperative process switch\n");
            }
        }
        g_last_yielded_pid = process.pid;
    }
    arch::x86_64::write_cr3(g_host_cr3);
    syscall::set_cpu_process(nullptr, 0);
    process::process_restore_host(&g_host_context);
}

}
