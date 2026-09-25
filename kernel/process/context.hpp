#pragma once

#include <cstddef>
#include <cstdint>

namespace linux95::process {

enum class ReturnKind {
    Iret,
    Sysret,
};

struct UserContext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip;
    uint64_t rsp;
    uint64_t rflags;
    uint16_t cs;
    uint16_t ss;
    ReturnKind return_kind;
};

struct HostContext {
    uint64_t rsp;
    uint64_t rip;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};

static_assert(offsetof(UserContext, rip) == 120);
static_assert(offsetof(UserContext, rsp) == 128);
static_assert(offsetof(UserContext, rflags) == 136);
static_assert(offsetof(UserContext, cs) == 144);
static_assert(offsetof(UserContext, ss) == 146);
static_assert(offsetof(UserContext, return_kind) == 148);
static_assert(sizeof(UserContext) == 152);

extern "C" int process_save_host(HostContext* context);
extern "C" [[noreturn]] void process_restore_host(const HostContext* context);
extern "C" [[noreturn]] void process_resume_user(const UserContext* context);

}
