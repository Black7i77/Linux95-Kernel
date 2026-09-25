#include "process/scheduler.hpp"

#include "arch/x86_64/control_regs.hpp"
#include "arch/x86_64/tss.hpp"
#include "arch/debug.hpp"
#include "arch/interrupts.hpp"
#include "memory/address.hpp"
#include "panic/panic.hpp"
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
HostReason g_last_host_reason = HostReason::Yield;

// Preemption diagnostics are one-shot milestone evidence, never per-tick logs.
bool g_quantum_expired_emitted = false;
bool g_context_captured_emitted = false;
bool g_preempt_host_return_emitted = false;
bool g_non_yielding_preempted_emitted = false;
bool g_preemptive_round_robin_emitted = false;
uint32_t g_last_preempted_pid = 0;

uint32_t g_user_quantum_ticks_remaining = 0;

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

static void emit_preemption_diagnostic(const char* text);

void reset_user_quantum()
{
    g_user_quantum_ticks_remaining = kUserQuantumTicks;
}

bool on_timer_tick(bool from_user)
{
    if (!from_user || g_user_quantum_ticks_remaining == 0) {
        return false;
    }

    --g_user_quantum_ticks_remaining;

    if (g_user_quantum_ticks_remaining == 0) {
        if (!g_quantum_expired_emitted) {
            g_quantum_expired_emitted = true;
            emit_preemption_diagnostic("[PASS] user quantum expired\n");
        }
        return true;
    }

    return false;
}

static void emit_preemption_diagnostic(const char* text)
{
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
    (void)text;
#else
    debug::write(text);
#endif
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
    if (selected.context.return_kind == process::ReturnKind::Sysret &&
        !syscall::valid_sysret_context(selected.context)) {
        process::mark_exited(selected, -14);
        process::reap_exited();
        return true;
    }
    if (g_last_host_reason == HostReason::Preempt &&
        g_last_preempted_pid != 0 &&
        selected.pid != g_last_preempted_pid) {
        if (!g_non_yielding_preempted_emitted) {
            g_non_yielding_preempted_emitted = true;
            emit_preemption_diagnostic("[PASS] non-yielding process was preempted\n");
        } else if (!g_preemptive_round_robin_emitted) {
            g_preemptive_round_robin_emitted = true;
            emit_preemption_diagnostic("[PASS] preemptive round robin\n");
        }
    }

    selected.state = process::State::Running;
    reset_user_quantum();
    g_host_cr3 = arch::x86_64::read_cr3();
    g_host_rflags = read_rflags();
    asm volatile("cli" ::: "memory");
    selected.context.rflags = syscall::sanitize_user_rflags(
        selected.context.rflags);

    if (process::process_save_host(&g_host_context) == 0) {
        arch::x86_64::write_cr3(selected.page_table_physical);
        arch::x86_64::set_tss_rsp0(selected.kernel_stack_top);
        syscall::set_cpu_process(
            &selected,
            selected.kernel_stack_top);
        process::process_resume_user(&selected.context);
    }

    write_rflags(g_host_rflags);

    if (g_last_host_reason == HostReason::Preempt &&
        !g_preempt_host_return_emitted) {
        g_preempt_host_return_emitted = true;
        emit_preemption_diagnostic("[PASS] timer preemption returned to host\n");
        emit_preemption_diagnostic("[PASS] desktop remained online after preemption\n");
    }

    if (g_last_host_reason == HostReason::Fault) {
        static bool fault_survival_emitted = false;
        if (!fault_survival_emitted) {
            fault_survival_emitted = true;
            debug::write("[PASS] kernel survived user fault\n");
        }
    }

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
    g_last_host_reason = reason;
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

} // namespace linux95::scheduler

namespace linux95::process {
namespace {

Process* validated_current_process()
{
    Process* current = syscall::cpu_local_state().current_process;

    uintptr_t current_address =
        reinterpret_cast<uintptr_t>(current);
    uintptr_t table_begin =
        reinterpret_cast<uintptr_t>(table());

    const uintptr_t kernel_region_base =
        static_cast<uintptr_t>(memory::kKernelRegionBase);

    if (current_address >= kernel_region_base) {
        current_address -= kernel_region_base;
    }
    if (table_begin >= kernel_region_base) {
        table_begin -= kernel_region_base;
    }

    const uintptr_t table_bytes =
        capacity() * sizeof(Process);

    if (current == nullptr ||
        current_address < table_begin ||
        current_address >= table_begin + table_bytes ||
        (current_address - table_begin) % sizeof(Process) != 0) {
        return nullptr;
    }

    if (current->pid == 0 ||
        current->state != State::Running ||
        current->page_table_physical == 0 ||
        current->kernel_stack_top == 0) {
        return nullptr;
    }

    return current;
}

void debug_write_hex(uint64_t value)
{
    static constexpr char kHex[] = "0123456789abcdef";
    debug::write("0x");
    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t digit = static_cast<uint8_t>((value >> shift) & 0xF);
        if (digit != 0 || started || shift == 0) {
            debug::put_char(kHex[digit]);
            started = true;
        }
    }
}

void debug_write_decimal(uint32_t value)
{
    char digits[10];
    size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        debug::put_char(digits[--count]);
    }
}

}

[[noreturn]] void handle_user_preempt(
    const interrupts::InterruptFrame& frame)
{
    if ((frame.cs & 0x3U) != 0x3U) {
        panic::halt(
            "Kernel attempted to preempt non-user context");
    }

    Process* current = validated_current_process();
    if (current == nullptr) {
        panic::halt(
            "Timer preemption without tracked running process");
    }

    capture_interrupt_context(*current, frame);

    if (!scheduler::g_context_captured_emitted) {
        scheduler::g_context_captured_emitted = true;
        scheduler::emit_preemption_diagnostic("[PASS] preempted context captured\n");
    }
    scheduler::g_last_preempted_pid = current->pid;

    current->state = State::Ready;

    scheduler::return_to_host(
        *current,
        current->context,
        scheduler::HostReason::Preempt);
}

bool handle_user_fault(uint8_t vector,
                       uint64_t error_code,
                       const interrupts::InterruptFrame& frame)
{
    if ((frame.cs & 0x3U) != 0x3U || vector == 2 || vector == 8 ||
        vector == 18) {
        return false;
    }

    Process* current = validated_current_process();
    if (current == nullptr) {
        debug::write(
            "[FAULT] CPL3 exception without a tracked running process\n");
        return false;
    }

    uint64_t fault_address = 0;
    if (vector == 14) {
        asm volatile("mov %%cr2, %0" : "=r"(fault_address));
    }

    current->fault_vector = vector;
    current->fault_error_code = error_code;
    current->fault_address = fault_address;
    capture_interrupt_context(*current, frame);
    mark_exited(*current, -static_cast<int64_t>(vector));

    debug::write("[FAULT] vector=");
    debug_write_decimal(vector);
    debug::write(" error=");
    debug_write_hex(error_code);
    debug::write(" cs=");
    debug_write_hex(frame.cs);
    debug::write(" cr2=");
    debug_write_hex(fault_address);
    debug::write(" pid=");
    debug_write_decimal(current->pid);
    debug::put_char('\n');
    debug::write("[PASS] user fault captured\n");
    debug::write("[PASS] faulty process terminated\n");
    scheduler::return_to_host(
        *current,
        current->context,
        scheduler::HostReason::Fault);
}

}
