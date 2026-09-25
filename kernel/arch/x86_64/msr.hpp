#pragma once

#include <stdint.h>

namespace linux95::arch::x86_64 {

constexpr uint32_t kIa32Star = 0xC0000081;
constexpr uint32_t kIa32Lstar = 0xC0000082;
constexpr uint32_t kIa32Fmask = 0xC0000084;
constexpr uint32_t kIa32Efer = 0xC0000080;
constexpr uint32_t kIa32KernelGsBase = 0xC0000102;
constexpr uint64_t kEferSystemCallEnable = 1ULL;

inline uint64_t read_msr(uint32_t msr)
{
    uint32_t low = 0;
    uint32_t high = 0;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<uint64_t>(high) << 32) | low;
}

inline void write_msr(uint32_t msr, uint64_t value)
{
    const uint32_t low = static_cast<uint32_t>(value);
    const uint32_t high = static_cast<uint32_t>(value >> 32);
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

constexpr uint64_t encode_star(uint16_t kernel_code,
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

} // namespace linux95::arch::x86_64
