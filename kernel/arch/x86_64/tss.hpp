#pragma once

#include <stdint.h>

namespace linux95::arch::x86_64 {

struct [[gnu::packed]] TaskStateSegment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
};

static_assert(sizeof(TaskStateSegment) == 104);

void initialize_tss();
void set_tss_rsp0(uint64_t rsp0);
uint64_t tss_rsp0();

}
