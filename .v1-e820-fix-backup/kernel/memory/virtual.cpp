#include "memory/virtual.hpp"

#include "arch/debug.hpp"
#include "memory/address.hpp"
#include "memory/memory.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::memory::virtual_memory {
namespace {

constexpr uint64_t kPresent = 1ULL << 0;
constexpr uint64_t kWritable = 1ULL << 1;
constexpr uint64_t kHuge = 1ULL << 7;
constexpr uint64_t kAddressMask = 0x000FFFFFFFFFF000ULL;
constexpr size_t kEntriesPerTable = 512;

extern "C" uint8_t __bootstrap_pt_pool_start[];
extern "C" uint8_t __bootstrap_pt_pool_end[];

uint64_t g_pool_next = 0;
uint64_t g_bootstrap_cr3 = 0;
uint64_t g_hhdm_limit = 0;

uint16_t pml4_index(uint64_t virtual_address)
{
    return static_cast<uint16_t>((virtual_address >> 39) & 0x1FFULL);
}

uint16_t pdpt_index(uint64_t virtual_address)
{
    return static_cast<uint16_t>((virtual_address >> 30) & 0x1FFULL);
}

uint16_t pd_index(uint64_t virtual_address)
{
    return static_cast<uint16_t>((virtual_address >> 21) & 0x1FFULL);
}

uint64_t* table_at(uint64_t physical_address)
{
    return reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(physical_address));
}

void zero_table(uint64_t physical_address)
{
    uint64_t* table = table_at(physical_address);
    for (size_t i = 0; i < kEntriesPerTable; ++i) {
        table[i] = 0;
    }
}

uint64_t allocate_bootstrap_table()
{
    const uint64_t start =
        reinterpret_cast<uint64_t>(__bootstrap_pt_pool_start);
    const uint64_t end =
        reinterpret_cast<uint64_t>(__bootstrap_pt_pool_end);

    const uint64_t address = start + g_pool_next;
    if (address < start ||
        address > end ||
        end - address < kPageSize) {
        return 0;
    }

    g_pool_next += kPageSize;
    zero_table(address);
    return address;
}

uint64_t get_or_create_child(uint64_t* parent, uint16_t index)
{
    const uint64_t entry = parent[index];
    if ((entry & kPresent) != 0) {
        if ((entry & kHuge) != 0) {
            return 0;
        }
        return entry & kAddressMask;
    }

    const uint64_t child = allocate_bootstrap_table();
    if (child == 0) {
        return 0;
    }

    parent[index] = child | kPresent | kWritable;
    return child;
}

bool map_huge_2m(uint64_t root_physical,
                 uint64_t virtual_address,
                 uint64_t physical_address)
{
    if ((virtual_address & (kHugePageSize - 1ULL)) != 0 ||
        (physical_address & (kHugePageSize - 1ULL)) != 0) {
        return false;
    }

    uint64_t* pml4 = table_at(root_physical);
    const uint64_t pdpt_physical =
        get_or_create_child(pml4, pml4_index(virtual_address));
    if (pdpt_physical == 0) {
        return false;
    }

    uint64_t* pdpt = table_at(pdpt_physical);
    const uint64_t pd_physical =
        get_or_create_child(pdpt, pdpt_index(virtual_address));
    if (pd_physical == 0) {
        return false;
    }

    uint64_t* pd = table_at(pd_physical);
    uint64_t& entry = pd[pd_index(virtual_address)];
    const uint64_t desired =
        (physical_address & 0x000FFFFFFFE00000ULL) |
        kPresent | kWritable | kHuge;

    if ((entry & kPresent) != 0) {
        return entry == desired;
    }

    entry = desired;
    return true;
}

} // namespace

bool prepare_bootstrap(const linux95::BootInfo& boot_info)
{
    g_pool_next = 0;
    g_bootstrap_cr3 = 0;
    g_hhdm_limit = 0;

    const uint64_t maximum = memory::maximum_physical_address(boot_info);
    if (maximum == 0) {
        return false;
    }

    if (maximum > kBootstrapHhdmLimit) {
        debug::write("[PANIC] bootstrap_hhdm_limit\n");
        return false;
    }

    const uint64_t rounded_limit = align_up(maximum, kHugePageSize);
    if (rounded_limit == UINT64_MAX || rounded_limit > kBootstrapHhdmLimit) {
        debug::write("[PANIC] bootstrap_hhdm_limit\n");
        return false;
    }

    const uint64_t root = allocate_bootstrap_table();
    if (root == 0) {
        return false;
    }

    // Keep the first 2 MiB identity mapped for the bootstrap stack,
    // BootInfo/E820 structures, VGA, and legacy interrupt paths.
    if (!map_huge_2m(root, 0, 0)) {
        return false;
    }

    // Alias physical 0..2 MiB into the kernel higher-half region.
    if (!map_huge_2m(root, kKernelRegionBase, 0)) {
        return false;
    }

    // Direct-map all reported physical address space up to the 64 GiB limit.
    for (uint64_t physical = 0;
         physical < rounded_limit;
         physical += kHugePageSize) {
        if (!map_huge_2m(root, kHhdmBase + physical, physical)) {
            return false;
        }
    }

    g_bootstrap_cr3 = root;
    g_hhdm_limit = rounded_limit;
    return true;
}

uint64_t bootstrap_cr3()
{
    return g_bootstrap_cr3;
}

uint64_t higher_half_alias(uint64_t low_address)
{
    return kKernelRegionBase + low_address;
}

bool hhdm_contains(uint64_t physical_address)
{
    return physical_address < g_hhdm_limit;
}

uint64_t hhdm_limit()
{
    return g_hhdm_limit;
}

} // namespace linux95::memory::virtual_memory
