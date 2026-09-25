#pragma once

#include <stdint.h>

namespace linux95::arch::x86_64 {

constexpr uint32_t kIa32Star = 0xC0000081;
constexpr uint32_t kIa32Lstar = 0xC0000082;
constexpr uint32_t kIa32Fmask = 0xC0000084;
constexpr uint32_t kIa32Efer = 0xC0000080;
constexpr uint32_t kIa32KernelGsBase = 0xC0000102;
constexpr uint64_t kEferSystemCallEnable = 1ULL;
constexpr uint64_t kEferNoExecuteEnable = 1ULL << 11;
constexpr uint32_t kCpuidNxBit = 1U << 20;

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

inline bool cpu_supports_nx()
{
    uint32_t eax = 0x80000000U;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    asm volatile("cpuid"
                 : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000001U) {
        return false;
    }

    eax = 0x80000001U;
    ecx = 0;
    asm volatile("cpuid"
                 : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    (void)ebx;
    (void)ecx;
    return (edx & kCpuidNxBit) != 0;
}

inline bool enable_nxe()
{
    if (!cpu_supports_nx()) {
        return false;
    }
    const uint64_t efer = read_msr(kIa32Efer);
    if ((efer & kEferNoExecuteEnable) == 0) {
        write_msr(kIa32Efer, efer | kEferNoExecuteEnable);
    }
    return (read_msr(kIa32Efer) & kEferNoExecuteEnable) != 0;
}

inline bool nxe_enabled()
{
    return (read_msr(kIa32Efer) & kEferNoExecuteEnable) != 0;
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
