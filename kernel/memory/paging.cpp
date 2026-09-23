#include "memory/paging.hpp"

#include "arch/x86_64/control_regs.hpp"
#include "memory/address.hpp"
#include "memory/physical.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::memory::paging {
namespace {

constexpr size_t kEntriesPerTable = 512;
constexpr uint64_t kOneGiBAddressMask = 0x000FFFFFC0000000ULL;
constexpr uint64_t kTwoMiBAddressMask = 0x000FFFFFFFE00000ULL;

uint64_t* table_from_physical(uint64_t physical_address)
{
    return reinterpret_cast<uint64_t*>(
        static_cast<uintptr_t>(physical_to_hhdm(physical_address)));
}

uint64_t allocate_table()
{
    const uint64_t physical_address = physical::allocate_page();
    if (physical_address == physical::kInvalidPhysicalAddress) {
        return kInvalidAddress;
    }

    uint64_t* table = table_from_physical(physical_address);
    for (size_t i = 0; i < kEntriesPerTable; ++i) {
        table[i] = 0;
    }

    return physical_address;
}

uint64_t child_table(uint64_t* table, uint16_t index, bool create)
{
    uint64_t& entry = table[index];
    if ((entry & kPagePresent) != 0) {
        if ((entry & kPageHuge) != 0) {
            return kInvalidAddress;
        }
        return entry & kAddressMask;
    }

    if (!create) {
        return kInvalidAddress;
    }

    const uint64_t child = allocate_table();
    if (child == kInvalidAddress) {
        return kInvalidAddress;
    }

    entry = child | kPagePresent | kPageWritable;
    return child;
}

bool table_empty(const uint64_t* table)
{
    for (size_t i = 0; i < kEntriesPerTable; ++i) {
        if ((table[i] & kPagePresent) != 0) {
            return false;
        }
    }
    return true;
}

uint64_t root_physical()
{
    return arch::x86_64::read_cr3() & kAddressMask;
}

} // namespace

bool map_page(uint64_t virtual_address,
              uint64_t physical_address,
              uint64_t flags)
{
    if (!is_page_aligned(virtual_address) ||
        !is_page_aligned(physical_address)) {
        return false;
    }

    uint64_t* pml4 = table_from_physical(root_physical());
    const uint64_t pdpt_physical =
        child_table(pml4, pml4_index(virtual_address), true);
    if (pdpt_physical == kInvalidAddress) {
        return false;
    }

    uint64_t* pdpt = table_from_physical(pdpt_physical);
    const uint64_t pd_physical =
        child_table(pdpt, pdpt_index(virtual_address), true);
    if (pd_physical == kInvalidAddress) {
        return false;
    }

    uint64_t* pd = table_from_physical(pd_physical);
    const uint64_t pt_physical =
        child_table(pd, pd_index(virtual_address), true);
    if (pt_physical == kInvalidAddress) {
        return false;
    }

    uint64_t* pt = table_from_physical(pt_physical);
    uint64_t& pte = pt[pt_index(virtual_address)];
    if ((pte & kPagePresent) != 0) {
        return false;
    }

    const uint64_t allowed =
        kPageWritable | kPageUser | kPageGlobal | kPageNoExecute;
    pte = (physical_address & kAddressMask) |
          (flags & allowed) |
          kPagePresent;

    arch::x86_64::invlpg(virtual_address);
    return true;
}

uint64_t translate(uint64_t virtual_address)
{
    uint64_t* pml4 = table_from_physical(root_physical());
    const uint64_t pml4e = pml4[pml4_index(virtual_address)];
    if ((pml4e & kPagePresent) == 0) {
        return kInvalidAddress;
    }

    uint64_t* pdpt = table_from_physical(pml4e & kAddressMask);
    const uint64_t pdpte = pdpt[pdpt_index(virtual_address)];
    if ((pdpte & kPagePresent) == 0) {
        return kInvalidAddress;
    }

    if ((pdpte & kPageHuge) != 0) {
        return (pdpte & kOneGiBAddressMask) |
               (virtual_address & 0x3FFFFFFFULL);
    }

    uint64_t* pd = table_from_physical(pdpte & kAddressMask);
    const uint64_t pde = pd[pd_index(virtual_address)];
    if ((pde & kPagePresent) == 0) {
        return kInvalidAddress;
    }

    if ((pde & kPageHuge) != 0) {
        return (pde & kTwoMiBAddressMask) |
               (virtual_address & 0x1FFFFFULL);
    }

    uint64_t* pt = table_from_physical(pde & kAddressMask);
    const uint64_t pte = pt[pt_index(virtual_address)];
    if ((pte & kPagePresent) == 0) {
        return kInvalidAddress;
    }

    return (pte & kAddressMask) |
           (virtual_address & 0xFFFULL);
}

bool is_mapped(uint64_t virtual_address)
{
    return translate(virtual_address) != kInvalidAddress;
}

bool unmap_page(uint64_t virtual_address)
{
    if (!is_page_aligned(virtual_address)) {
        return false;
    }

    uint64_t* pml4 = table_from_physical(root_physical());
    uint64_t& pml4e = pml4[pml4_index(virtual_address)];
    if ((pml4e & kPagePresent) == 0 || (pml4e & kPageHuge) != 0) {
        return false;
    }

    const uint64_t pdpt_physical = pml4e & kAddressMask;
    uint64_t* pdpt = table_from_physical(pdpt_physical);
    uint64_t& pdpte = pdpt[pdpt_index(virtual_address)];
    if ((pdpte & kPagePresent) == 0 || (pdpte & kPageHuge) != 0) {
        return false;
    }

    const uint64_t pd_physical = pdpte & kAddressMask;
    uint64_t* pd = table_from_physical(pd_physical);
    uint64_t& pde = pd[pd_index(virtual_address)];
    if ((pde & kPagePresent) == 0 || (pde & kPageHuge) != 0) {
        return false;
    }

    const uint64_t pt_physical = pde & kAddressMask;
    uint64_t* pt = table_from_physical(pt_physical);
    uint64_t& pte = pt[pt_index(virtual_address)];
    if ((pte & kPagePresent) == 0) {
        return false;
    }

    pte = 0;
    arch::x86_64::invlpg(virtual_address);

    if (table_empty(pt)) {
        pde = 0;
        if (!physical::free_page(pt_physical)) {
            return false;
        }

        if (table_empty(pd)) {
            pdpte = 0;
            if (!physical::free_page(pd_physical)) {
                return false;
            }

            if (table_empty(pdpt)) {
                pml4e = 0;
                if (!physical::free_page(pdpt_physical)) {
                    return false;
                }
            }
        }
    }

    return true;
}

} // namespace linux95::memory::paging
