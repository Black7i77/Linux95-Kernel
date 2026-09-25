#pragma once

#include <stdint.h>

namespace linux95::arch::x86_64 {

constexpr uint64_t kCr0Em = 1ULL << 2;
constexpr uint64_t kCr0Ts = 1ULL << 3;
constexpr uint64_t kCr4Osfxsr = 1ULL << 9;
constexpr uint64_t kCr4Osxmmexcpt = 1ULL << 10;
constexpr uint64_t kCr4Osxsave = 1ULL << 18;

inline void disable_user_fp_state()
{
    uint64_t cr0 = 0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= kCr0Em | kCr0Ts;
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    uint64_t cr4 = 0;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~(kCr4Osfxsr | kCr4Osxmmexcpt | kCr4Osxsave);
    asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
}

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
