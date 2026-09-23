#include "memory/physical.hpp"

#include "memory/address.hpp"
#include "memory/memory.hpp"
#include "memory/page_bitmap.hpp"

#include <stdint.h>

namespace linux95::memory::physical {
namespace {

constexpr uint64_t kLowReservedEnd = 0x00200000ULL;

PageBitmap g_used;
PageBitmap g_reserved;
uint64_t g_page_count = 0;
uint64_t g_bitmap_physical = 0;
uint64_t g_bitmap_bytes = 0;

uint64_t saturating_end(uint64_t base, uint64_t length)
{
    return length > UINT64_MAX - base ? UINT64_MAX : base + length;
}

const E820Entry* entries_for(const BootInfo& boot_info)
{
    return reinterpret_cast<const E820Entry*>(
        static_cast<uintptr_t>(boot_info.e820_address));
}

void mark_range_free(uint64_t start, uint64_t end)
{
    if (end <= start) {
        return;
    }

    for (uint64_t address = start; address < end; address += kPageSize) {
        const uint64_t index = address / kPageSize;
        g_used.mark_free(index);
        g_reserved.mark_free(index);
    }
}

void reserve_range_internal(uint64_t start, uint64_t length)
{
    if (length == 0 || g_page_count == 0) {
        return;
    }

    const uint64_t raw_end = saturating_end(start, length);
    uint64_t first = align_down(start, kPageSize);
    uint64_t end = align_up(raw_end, kPageSize);
    const uint64_t managed_end = g_page_count * kPageSize;

    if (first >= managed_end) {
        return;
    }
    if (end > managed_end || end == UINT64_MAX) {
        end = managed_end;
    }

    for (uint64_t address = first; address < end; address += kPageSize) {
        const uint64_t index = address / kPageSize;
        g_used.mark_used(index);
        g_reserved.mark_used(index);
    }
}

bool find_bitmap_storage(const BootInfo& boot_info,
                         uint64_t metadata_bytes,
                         uint64_t& physical_out)
{
    const E820Entry* entries = entries_for(boot_info);

    for (uint32_t i = 0; i < boot_info.e820_count; ++i) {
        const E820Entry& entry = entries[i];
        if (entry.type != 1 || entry.length == 0) {
            continue;
        }

        const uint64_t raw_end = saturating_end(entry.base, entry.length);
        uint64_t first = align_up(entry.base, kPageSize);
        uint64_t last = align_down(raw_end, kPageSize);

        if (first < kLowReservedEnd) {
            first = kLowReservedEnd;
        }
        if (last > kBootstrapHhdmLimit) {
            last = kBootstrapHhdmLimit;
        }

        if (first == UINT64_MAX || last <= first) {
            continue;
        }

        if (metadata_bytes <= last - first) {
            physical_out = first;
            return true;
        }
    }

    return false;
}

} // namespace

bool initialize(const linux95::BootInfo& boot_info)
{
    g_page_count = 0;
    g_bitmap_physical = 0;
    g_bitmap_bytes = 0;

    if (!valid_boot_info(&boot_info)) {
        return false;
    }

    const uint64_t maximum = memory::maximum_usable_physical_address(boot_info);
    if (maximum == 0 || maximum > kBootstrapHhdmLimit) {
        return false;
    }

    const uint64_t managed_end = align_up(maximum, kPageSize);
    if (managed_end == UINT64_MAX || managed_end > kBootstrapHhdmLimit) {
        return false;
    }

    g_page_count = managed_end / kPageSize;
    g_bitmap_bytes = (g_page_count + 7ULL) / 8ULL;
    const uint64_t one_bitmap_storage = align_up(g_bitmap_bytes, kPageSize);
    if (one_bitmap_storage == UINT64_MAX ||
        one_bitmap_storage > UINT64_MAX / 2ULL) {
        return false;
    }

    const uint64_t metadata_bytes = one_bitmap_storage * 2ULL;
    if (!find_bitmap_storage(boot_info, metadata_bytes, g_bitmap_physical)) {
        return false;
    }

    auto* used_storage = reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(physical_to_hhdm(g_bitmap_physical)));
    auto* reserved_storage = reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(physical_to_hhdm(
            g_bitmap_physical + one_bitmap_storage)));

    g_used.initialize(used_storage, g_page_count);
    g_reserved.initialize(reserved_storage, g_page_count);

    const E820Entry* entries = entries_for(boot_info);
    for (uint32_t i = 0; i < boot_info.e820_count; ++i) {
        const E820Entry& entry = entries[i];
        if (entry.type != 1 || entry.length == 0) {
            continue;
        }

        const uint64_t raw_end = saturating_end(entry.base, entry.length);
        uint64_t first = align_up(entry.base, kPageSize);
        uint64_t last = align_down(raw_end, kPageSize);
        if (first == UINT64_MAX || last <= first) {
            continue;
        }
        if (last > managed_end) {
            last = managed_end;
        }
        if (first < last) {
            mark_range_free(first, last);
        }
    }

    // Keep all legacy bootstrap state out of the allocator. This covers
    // BIOS structures, Stage 1/2, BootInfo/E820, VGA, the kernel image,
    // bootstrap page tables, stack, and the v0.2 bump heap.
    reserve_range_internal(0, kLowReservedEnd);
    reserve_range_internal(g_bitmap_physical, metadata_bytes);

    return g_used.free_count() > 0;
}

uint64_t allocate_page()
{
    for (;;) {
        const uint64_t index = g_used.allocate();
        if (index == PageBitmap::kInvalid) {
            return kInvalidPhysicalAddress;
        }

        if (!g_reserved.is_used(index)) {
            return index * kPageSize;
        }
    }
}

bool free_page(uint64_t physical_address)
{
    if (!is_page_aligned(physical_address) || g_page_count == 0) {
        return false;
    }

    const uint64_t index = physical_address / kPageSize;
    if (index >= g_page_count || g_reserved.is_used(index)) {
        return false;
    }

    return g_used.mark_free(index);
}

void reserve_range(uint64_t start, uint64_t length)
{
    reserve_range_internal(start, length);
}

uint64_t total_pages()
{
    return g_page_count;
}

uint64_t free_pages()
{
    return g_used.free_count();
}

uint64_t used_pages()
{
    return g_page_count - g_used.free_count();
}

uint64_t bitmap_physical_address()
{
    return g_bitmap_physical;
}

uint64_t bitmap_bytes()
{
    return g_bitmap_bytes;
}

} // namespace linux95::memory::physical
