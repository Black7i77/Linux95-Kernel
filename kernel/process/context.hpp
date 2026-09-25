#pragma once

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

}
