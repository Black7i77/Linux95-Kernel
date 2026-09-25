#include "syscall/syscall.hpp"

#include "arch/debug.hpp"
#include "arch/x86_64/control_regs.hpp"
#include "arch/x86_64/msr.hpp"
#include "arch/x86_64/segments.hpp"
#include "memory/user_space.hpp"
#include "process/scheduler.hpp"

namespace linux95::syscall {

constexpr uint64_t kFlagTrap = 1ULL << 8;
constexpr uint64_t kFlagInterrupt = 1ULL << 9;
constexpr uint64_t kFlagDirection = 1ULL << 10;
CpuLocalState g_cpu_local_state{};

namespace {

extern "C" void syscall_entry();

constexpr int64_t kBadDescriptor = -9;
constexpr int64_t kFault = -14;
constexpr int64_t kInvalidArgument = -22;
constexpr int64_t kNotImplemented = -38;
constexpr uint64_t kMaxWriteLength = 4096;
uint8_t g_write_buffer[kMaxWriteLength];

Result dispatch_control(process::Process& process, Frame& frame)
{
    const Number number = static_cast<Number>(frame.rax);
    if (number == Number::Yield) {
        return {0, Action::YieldToHost};
    }
    if (number == Number::Exit) {
        process.exit_code = static_cast<int64_t>(frame.rdi);
        process.state = process::State::Exited;
        return {0, Action::ExitToHost};
    }
    return {kNotImplemented, Action::ReturnToUser};
}

void emit_process_exit_marker(const process::Process& process, bool from_user)
{
    static bool pid1_emitted = false;
    static bool pid2_emitted = false;
    if (!from_user) {
        return;
    }
    if (process.pid == 1 && !pid1_emitted) {
        pid1_emitted = true;
        debug::write("[PASS] pid1 exited\n");
    } else if (process.pid == 2 && !pid2_emitted) {
        pid2_emitted = true;
        debug::write("[PASS] pid2 exited\n");
    }
}

void restore_host_gs_after_syscall()
{
    asm volatile("swapgs" ::: "memory");
}

Result safe_write(process::Process& process, Frame& frame)
{
    if (frame.rdi != 1 && frame.rdi != 2) {
        return {kBadDescriptor, Action::ReturnToUser};
    }
    if (frame.rdx > kMaxWriteLength) {
        return {kInvalidArgument, Action::ReturnToUser};
    }
    if (!memory::validate_user_range(
            process.page_table_physical,
            frame.rsi,
            static_cast<size_t>(frame.rdx),
            memory::UserAccess::Read)) {
        return {kFault, Action::ReturnToUser};
    }
    if (!memory::copy_from_user(
            process.page_table_physical,
            g_write_buffer,
            frame.rsi,
            static_cast<size_t>(frame.rdx))) {
        return {kFault, Action::ReturnToUser};
    }
    for (uint64_t index = 0; index < frame.rdx; ++index) {
        debug::put_char(static_cast<char>(g_write_buffer[index]));
    }
    return {static_cast<int64_t>(frame.rdx), Action::ReturnToUser};
}

void capture_context(
    process::Process& process,
    const Frame& frame,
    process::ReturnKind return_kind)
{
    process.context.r15 = frame.r15;
    process.context.r14 = frame.r14;
    process.context.r13 = frame.r13;
    process.context.r12 = frame.r12;
    process.context.r11 = frame.r11;
    process.context.r10 = frame.r10;
    process.context.r9 = frame.r9;
    process.context.r8 = frame.r8;
    process.context.rbp = frame.rbp;
    process.context.rdi = frame.rdi;
    process.context.rsi = frame.rsi;
    process.context.rdx = frame.rdx;
    process.context.rcx = frame.rcx;
    process.context.rbx = frame.rbx;
    process.context.rax = frame.rax;
    process.context.rip = frame.rip;
    process.context.rsp = frame.rsp;
    process.context.rflags = frame.rflags;
    process.context.cs = static_cast<uint16_t>(frame.cs);
    process.context.ss = static_cast<uint16_t>(frame.ss);
    process.context.return_kind = return_kind;
}

} // namespace

CpuLocalState& cpu_local_state()
{
    return g_cpu_local_state;
}

void set_cpu_process(process::Process* process, uint64_t kernel_stack_top)
{
    g_cpu_local_state.current_process = process;
    g_cpu_local_state.kernel_stack_top = kernel_stack_top;
}

bool valid_sysret_target(uint64_t rip, uint64_t rsp)
{
    constexpr uint64_t kUserLimit = 0x0000800000000000ULL;
    return rip < kUserLimit && rsp < kUserLimit;
}

bool valid_sysret_context(const process::UserContext& context)
{
    return valid_sysret_target(context.rip, context.rsp);
}

uint64_t sanitize_user_rflags(uint64_t flags)
{
    constexpr uint64_t kUserFlags = 0x002008D5ULL;
    constexpr uint64_t kRequiredFlags = 0x202ULL;
    return (flags & kUserFlags) | kRequiredFlags;
}

void initialize_fast_path()
{
    arch::x86_64::disable_user_fp_state();
    g_cpu_local_state.current_process = nullptr;
    g_cpu_local_state.kernel_stack_top = 0;
    g_cpu_local_state.saved_user_rsp = 0;
    arch::x86_64::write_msr(
        arch::x86_64::kIa32KernelGsBase,
        reinterpret_cast<uint64_t>(&g_cpu_local_state));
    const uint64_t efer =
        arch::x86_64::read_msr(arch::x86_64::kIa32Efer);
    arch::x86_64::write_msr(
        arch::x86_64::kIa32Efer,
        efer | arch::x86_64::kEferSystemCallEnable);
    arch::x86_64::write_msr(
        arch::x86_64::kIa32Star,
        arch::x86_64::encode_star(
            arch::x86_64::kKernelCodeSelector,
            arch::x86_64::kKernelDataSelector,
            arch::x86_64::kUserCodeSelector,
            arch::x86_64::kUserDataSelector));
    arch::x86_64::write_msr(
        arch::x86_64::kIa32Lstar,
        reinterpret_cast<uint64_t>(&syscall_entry));
    arch::x86_64::write_msr(
        arch::x86_64::kIa32Fmask,
        kFlagTrap | kFlagInterrupt | kFlagDirection);
}

Result dispatch(process::Process& process, Frame& frame)
{
    if (frame.rax == static_cast<uint64_t>(Number::Write)) {
        return safe_write(process, frame);
    }
    return dispatch_control(process, frame);
}

Result dispatch_for_test(process::Process& process, Frame& frame)
{
    if (frame.rax == static_cast<uint64_t>(Number::Write)) {
        return {kNotImplemented, Action::ReturnToUser};
    }
    return dispatch_control(process, frame);
}

} // namespace linux95::syscall

extern "C" uint64_t int80_bridge(linux95::syscall::Frame* frame)
{
    if (frame == nullptr) return 0;
    linux95::process::Process* process =
        linux95::syscall::cpu_local_state().current_process;
    if (process == nullptr) return 0;
    static bool ring3_seen = false;
    if (!ring3_seen && (frame->cs & 3U) == 3U) {
        ring3_seen = true;
        linux95::debug::write("[PASS] entered ring3\n");
        linux95::debug::write("[PASS] int80 syscall path\n");
    }
    frame->rflags = linux95::syscall::sanitize_user_rflags(frame->rflags);
    const linux95::syscall::Result result =
        linux95::syscall::dispatch(*process, *frame);
    frame->rax = static_cast<uint64_t>(result.value);
    if (result.action == linux95::syscall::Action::ReturnToUser) {
        return 1;
    }
    linux95::syscall::capture_context(
        *process,
        *frame,
        linux95::process::ReturnKind::Iret);
    if (result.action == linux95::syscall::Action::YieldToHost) {
        process->state = linux95::process::State::Ready;
        linux95::scheduler::return_to_host(
            *process,
            process->context,
            linux95::scheduler::HostReason::Yield);
    }
    linux95::syscall::emit_process_exit_marker(
        *process,
        (frame->cs & 3U) == 3U);
    linux95::scheduler::return_to_host(
        *process,
        process->context,
        linux95::scheduler::HostReason::Exit);
}

extern "C" uint64_t syscall_bridge(linux95::syscall::Frame* frame)
{
    if (frame == nullptr ||
        linux95::syscall::g_cpu_local_state.current_process == nullptr) {
        return 0;
    }
    frame->rflags = linux95::syscall::sanitize_user_rflags(frame->r11);
    const linux95::syscall::Result result = linux95::syscall::dispatch(
        *linux95::syscall::g_cpu_local_state.current_process, *frame);
    static bool ring3_syscall_seen = false;
    if (!ring3_syscall_seen && (frame->cs & 3U) == 3U) {
        ring3_syscall_seen = true;
        linux95::debug::write("[PASS] syscall path\n");
    }
    frame->rax = static_cast<uint64_t>(result.value);
    linux95::process::Process& process =
        *linux95::syscall::g_cpu_local_state.current_process;
    if (result.action != linux95::syscall::Action::ReturnToUser) {
        linux95::syscall::capture_context(
            process,
            *frame,
            linux95::process::ReturnKind::Sysret);
        if (result.action == linux95::syscall::Action::YieldToHost) {
            if (!linux95::syscall::valid_sysret_context(process.context)) {
                linux95::process::mark_exited(process, -14);
                linux95::syscall::restore_host_gs_after_syscall();
                linux95::scheduler::return_to_host(
                    process,
                    process.context,
                    linux95::scheduler::HostReason::Fault);
            }
            process.state = linux95::process::State::Ready;
            linux95::syscall::restore_host_gs_after_syscall();
            linux95::scheduler::return_to_host(
                process,
                process.context,
                linux95::scheduler::HostReason::Yield);
        }
        linux95::syscall::emit_process_exit_marker(
            process,
            (frame->cs & 3U) == 3U);
        linux95::syscall::restore_host_gs_after_syscall();
        linux95::scheduler::return_to_host(
            process,
            process.context,
            linux95::scheduler::HostReason::Exit);
    }
    if (!linux95::syscall::valid_sysret_target(
            frame->rcx,
            linux95::syscall::g_cpu_local_state.saved_user_rsp)) {
        linux95::process::mark_exited(process, -14);
        linux95::syscall::restore_host_gs_after_syscall();
        linux95::scheduler::return_to_host(
            process,
            process.context,
            linux95::scheduler::HostReason::Fault);
    }
    return 1;
}
