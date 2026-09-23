#pragma once

#include <stdint.h>

namespace linux95::memory {

constexpr uint64_t kPageSize = 4096ULL;
constexpr uint64_t kHugePageSize = 2ULL * 1024ULL * 1024ULL;

constexpr uint64_t kKernelPhysicalBase = 0x00100000ULL;
constexpr uint64_t kKernelRegionBase = 0xFFFFFFFF80000000ULL;
constexpr uint64_t kKernelVirtualBase = 0xFFFFFFFF80100000ULL;
constexpr uint64_t kHhdmBase = 0xFFFF800000000000ULL;
constexpr uint64_t kBootstrapHhdmLimit = 64ULL * 1024ULL * 1024ULL * 1024ULL;

constexpr uint64_t align_down(uint64_t value, uint64_t alignment)
{
    return alignment == 0 ? value : value & ~(alignment - 1ULL);
}

constexpr uint64_t align_up(uint64_t value, uint64_t alignment)
{
    if (alignment == 0) {
        return value;
    }

    const uint64_t mask = alignment - 1ULL;
    if (value > UINT64_MAX - mask) {
        return UINT64_MAX & ~mask;
    }

    return (value + mask) & ~mask;
}

constexpr uint64_t physical_to_hhdm(uint64_t physical)
{
    return kHhdmBase + physical;
}

constexpr uint64_t hhdm_to_physical(uint64_t virtual_address)
{
    return virtual_address - kHhdmBase;
}

constexpr bool is_page_aligned(uint64_t value)
{
    return (value & (kPageSize - 1ULL)) == 0;
}

} // namespace linux95::memory
