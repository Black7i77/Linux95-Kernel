#include "syscall/syscall.hpp"

#include "arch/debug.hpp"
#include "memory/user_space.hpp"

namespace linux95::syscall {
namespace {

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
