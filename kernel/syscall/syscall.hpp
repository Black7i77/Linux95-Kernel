#pragma once

#include <stdint.h>

#include "process/process.hpp"

namespace linux95::syscall {

enum class Number : uint64_t {
    Write = 0,
    Yield = 1,
    Exit = 2,
};

enum class Action {
    ReturnToUser,
    YieldToHost,
    ExitToHost,
};

struct Result {
    int64_t value;
    Action action;
};

struct Frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

Result dispatch(process::Process& process, Frame& frame);
Result dispatch_for_test(process::Process& process, Frame& frame);

} // namespace linux95::syscall

extern "C" void int80_bridge(linux95::syscall::Frame* frame);
