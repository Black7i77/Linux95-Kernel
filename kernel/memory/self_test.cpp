#include "memory/self_test.hpp"

#include "memory/address.hpp"
#include "memory/paging.hpp"
#include "memory/physical.hpp"

#include <stdint.h>

namespace linux95::memory::self_test {
namespace {

constexpr uint64_t kTestVa = 0xFFFF900000100000ULL;

bool valid_allocated_page(uint64_t address)
{
    return address != physical::kInvalidPhysicalAddress &&
           is_page_aligned(address);
}

} // namespace

bool run()
{
    const uint64_t free_before = physical::free_pages();

    const uint64_t page_a = physical::allocate_page();
    const uint64_t page_b = physical::allocate_page();
    const uint64_t page_c = physical::allocate_page();

    if (!valid_allocated_page(page_a) ||
        !valid_allocated_page(page_b) ||
        !valid_allocated_page(page_c) ||
        page_a == page_b || page_a == page_c || page_b == page_c) {
        return false;
    }

    volatile uint64_t* const a = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(physical_to_hhdm(page_a)));
    volatile uint64_t* const b = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(physical_to_hhdm(page_b)));
    volatile uint64_t* const c = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(physical_to_hhdm(page_c)));

    constexpr uint64_t kPatternA = 0x1122334455667788ULL;
    constexpr uint64_t kPatternB = 0x8877665544332211ULL;
    constexpr uint64_t kPatternC = 0xA5A55A5AF0F00F0FULL;

    a[0] = kPatternA;
    b[0] = kPatternB;
    c[0] = kPatternC;

    if (a[0] != kPatternA || b[0] != kPatternB || c[0] != kPatternC) {
        return false;
    }

    if (paging::is_mapped(kTestVa)) {
        return false;
    }

    if (!paging::map_page(kTestVa, page_a, paging::kPageWritable)) {
        return false;
    }

    // Existing mappings must not be silently overwritten.
    if (paging::map_page(kTestVa, page_b, paging::kPageWritable)) {
        return false;
    }

    if (paging::translate(kTestVa) != page_a) {
        return false;
    }

    volatile uint64_t* const mapped =
        reinterpret_cast<volatile uint64_t*>(static_cast<uintptr_t>(kTestVa));
    constexpr uint64_t kMappedPattern = 0xC001D00D5A5AA5A5ULL;
    mapped[0] = kMappedPattern;
    if (mapped[0] != kMappedPattern || a[0] != kMappedPattern) {
        return false;
    }

    if (!paging::unmap_page(kTestVa) || paging::is_mapped(kTestVa)) {
        return false;
    }

    if (!physical::free_page(page_a) ||
        !physical::free_page(page_b) ||
        !physical::free_page(page_c)) {
        return false;
    }

    return physical::free_pages() == free_before;
}

} // namespace linux95::memory::self_test
