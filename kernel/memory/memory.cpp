#include "memory/memory.hpp"

#include <stdint.h>

namespace linux95::memory {
namespace {

constexpr uintptr_t kIdentityMapEnd = 0x200000;
constexpr uintptr_t kHeapLimit = 0x1F0000;

uint64_t g_total_bytes = 0;
uint64_t g_usable_bytes = 0;
uint32_t g_usable_regions = 0;
uintptr_t g_heap_start = 0;
uintptr_t g_heap_end = 0;

extern "C" char _kernel_end;

uint64_t saturating_add(uint64_t a, uint64_t b)
{
    if (UINT64_MAX - a < b) {
        return UINT64_MAX;
    }

    return a + b;
}

uintptr_t align_up_16(uintptr_t value)
{
    if (value > UINTPTR_MAX - 15u) {
        return 0;
    }

    return (value + 15u) & ~static_cast<uintptr_t>(15u);
}

} // namespace

bool initialize(const BootInfo& boot_info)
{
    g_total_bytes = 0;
    g_usable_bytes = 0;
    g_usable_regions = 0;
    g_heap_start = 0;
    g_heap_end = 0;

    if (boot_info.e820_count == 0 ||
        boot_info.e820_count > kMaxE820Entries ||
        boot_info.e820_address == 0) {
        return false;
    }

    const auto* entries =
        reinterpret_cast<const E820Entry*>(
            static_cast<uintptr_t>(boot_info.e820_address));

    const uintptr_t kernel_end =
        align_up_16(reinterpret_cast<uintptr_t>(&_kernel_end));

    for (uint32_t i = 0; i < boot_info.e820_count; ++i) {
        const E820Entry& entry = entries[i];

        if (entry.length == 0) {
            continue;
        }

        g_total_bytes = saturating_add(g_total_bytes, entry.length);

        if (entry.type != 1) {
            continue;
        }

        g_usable_bytes = saturating_add(g_usable_bytes, entry.length);
        ++g_usable_regions;

        if (g_heap_start != 0 || kernel_end == 0) {
            continue;
        }

        const uint64_t region_end =
            entry.base > UINT64_MAX - entry.length
                ? UINT64_MAX
                : entry.base + entry.length;

        if (entry.base <= kernel_end &&
            region_end > kernel_end &&
            kernel_end < kIdentityMapEnd) {

            uint64_t candidate_end = region_end;
            if (candidate_end > kHeapLimit) {
                candidate_end = kHeapLimit;
            }

            if (candidate_end > kernel_end + 4096u) {
                g_heap_start = kernel_end;
                g_heap_end = static_cast<uintptr_t>(candidate_end);
            }
        }
    }

    return g_usable_regions > 0 &&
           g_heap_start != 0 &&
           g_heap_end > g_heap_start;
}

uint64_t total_bytes()
{
    return g_total_bytes;
}

uint64_t usable_bytes()
{
    return g_usable_bytes;
}

uint32_t usable_regions()
{
    return g_usable_regions;
}

uintptr_t heap_start()
{
    return g_heap_start;
}

uintptr_t heap_end()
{
    return g_heap_end;
}

} // namespace linux95::memory
