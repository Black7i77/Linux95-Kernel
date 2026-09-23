#pragma once

#include <stdint.h>

namespace linux95::arch::x86_64 {

inline uint64_t read_cr3()
{
    uint64_t value;
    asm volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

inline void write_cr3(uint64_t value)
{
    asm volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

inline void invlpg(uint64_t virtual_address)
{
    asm volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

} // namespace linux95::arch::x86_64
