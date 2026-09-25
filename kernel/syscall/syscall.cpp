#include "syscall/syscall.hpp"

#include "arch/debug.hpp"
#include "arch/x86_64/msr.hpp"
#include "arch/x86_64/segments.hpp"
#include "memory/user_space.hpp"

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

Result number_action(process::Process& process, Frame& frame)
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

Result safe_write(process::Process& process, Frame& frame)
{
    if (frame.rdi != 1 && frame.rdi != 2) {
        return {kBadDescriptor, Action::ReturnToUser};
    }
    if (frame.rdx > kMaxWriteLength) {
        return {kInvalidArgument, Action::ReturnToUser};
    }
    uint8_t buffer[kMaxWriteLength];
    if (!memory::validate_user_range(
            process.page_table_physical,
            frame.rsi,
            static_cast<size_t>(frame.rdx),
            memory::UserAccess::Read)) {
        return {kFault, Action::ReturnToUser};
    }
    if (!memory::copy_from_user(
            process.page_table_physical,
            buffer,
            frame.rsi,
            static_cast<size_t>(frame.rdx))) {
        return {kFault, Action::ReturnToUser};
    }
    for (uint64_t index = 0; index < frame.rdx; ++index) {
        debug::put_char(static_cast<char>(buffer[index]));
    }
    return {static_cast<int64_t>(frame.rdx), Action::ReturnToUser};
}

} // namespace

bool valid_sysret_target(uint64_t rip, uint64_t rsp)
{
    constexpr uint64_t kUserLimit = 0x0000800000000000ULL;
    return rip < kUserLimit && rsp < kUserLimit;
}

void initialize_fast_path()
{
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
    return number_action(process, frame);
}

Result dispatch_for_test(process::Process& process, Frame& frame)
{
    if (frame.rax == static_cast<uint64_t>(Number::Yield)) {
        return {0, Action::YieldToHost};
    }
    if (frame.rax == static_cast<uint64_t>(Number::Exit)) {
        process.exit_code = static_cast<int64_t>(frame.rdi);
        process.state = process::State::Exited;
        return {0, Action::ExitToHost};
    }
    return {kNotImplemented, Action::ReturnToUser};
}

} // namespace linux95::syscall

extern "C" void int80_bridge(linux95::syscall::Frame* frame)
{
    if (frame == nullptr) return;
    linux95::process::Process* process =
        linux95::process::find(1);
    if (process == nullptr) return;
    const linux95::syscall::Result result =
        linux95::syscall::dispatch(*process, *frame);
    frame->rax = static_cast<uint64_t>(result.value);
}

extern "C" uint64_t syscall_bridge(linux95::syscall::Frame* frame)
{
    if (frame == nullptr ||
        linux95::syscall::g_cpu_local_state.current_process == nullptr) {
        return 0;
    }
    const linux95::syscall::Result result = linux95::syscall::dispatch(
        *linux95::syscall::g_cpu_local_state.current_process, *frame);
    frame->rax = static_cast<uint64_t>(result.value);
    if (result.action != linux95::syscall::Action::ReturnToUser ||
        !linux95::syscall::valid_sysret_target(
            frame->rcx,
            linux95::syscall::g_cpu_local_state.saved_user_rsp)) {
        linux95::syscall::g_cpu_local_state.current_process->state =
            linux95::process::State::Exited;
        return 0;
    }
    frame->rflags = (frame->r11 | 0x2ULL) &
                    ~(linux95::syscall::kFlagTrap |
                      linux95::syscall::kFlagInterrupt |
                      linux95::syscall::kFlagDirection);
    return 1;
}
