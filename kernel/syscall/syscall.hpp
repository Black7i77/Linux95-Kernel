#pragma once

#include <stddef.h>
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

struct CpuLocalState {
    process::Process* current_process;
    uint64_t kernel_stack_top;
    uint64_t saved_user_rsp;
};

CpuLocalState& cpu_local_state();
void set_cpu_process(process::Process* process, uint64_t kernel_stack_top);

Result dispatch(process::Process& process, Frame& frame);
Result dispatch_for_test(process::Process& process, Frame& frame);
void initialize_fast_path();
bool valid_sysret_target(uint64_t rip, uint64_t rsp);
bool valid_sysret_context(const process::UserContext& context);
uint64_t sanitize_user_rflags(uint64_t flags);

static_assert(offsetof(Frame, r15) == 0);
static_assert(offsetof(Frame, rax) == 112);
static_assert(offsetof(Frame, rip) == 120);
static_assert(offsetof(Frame, cs) == 128);
static_assert(offsetof(Frame, rflags) == 136);
static_assert(offsetof(Frame, rsp) == 144);
static_assert(sizeof(Frame) == 160);
static_assert(offsetof(CpuLocalState, current_process) == 0);
static_assert(offsetof(CpuLocalState, kernel_stack_top) == 8);
static_assert(offsetof(CpuLocalState, saved_user_rsp) == 16);
constexpr uint64_t encode_star_for_test(uint16_t kernel_code,
                                        uint16_t kernel_data,
                                        uint16_t user_code,
                                        uint16_t user_data)
{
    const uint16_t user_base = user_data - 8;
    const uint16_t sysret_base = user_code - 16;
    return (static_cast<uint64_t>(sysret_base == user_base
                                      ? sysret_base : 0) << 48) |
           (static_cast<uint64_t>(kernel_code) << 32) |
           (static_cast<uint64_t>(kernel_data) << 16);
}

} // namespace linux95::syscall

extern "C" uint64_t int80_bridge(linux95::syscall::Frame* frame);
extern "C" uint64_t syscall_bridge(linux95::syscall::Frame* frame);
