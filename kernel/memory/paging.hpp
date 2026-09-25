#pragma once

#include <stdint.h>

namespace linux95::memory {

struct PageInfo {
    bool present;
    bool user;
    bool writable;
    bool executable;
    uint64_t physical;
};

}

namespace linux95::memory::paging {

constexpr uint64_t kInvalidAddress = UINT64_MAX;
constexpr uint64_t kPagePresent = 1ULL << 0;
constexpr uint64_t kPageWritable = 1ULL << 1;
constexpr uint64_t kPageUser = 1ULL << 2;
constexpr uint64_t kPageHuge = 1ULL << 7;
constexpr uint64_t kPageGlobal = 1ULL << 8;
constexpr uint64_t kPageNoExecute = 1ULL << 63;
constexpr uint64_t kAddressMask = 0x000FFFFFFFFFF000ULL;

constexpr uint16_t pml4_index(uint64_t va)
{
    return static_cast<uint16_t>((va >> 39) & 0x1FFULL);
}

constexpr uint16_t pdpt_index(uint64_t va)
{
    return static_cast<uint16_t>((va >> 30) & 0x1FFULL);
}

constexpr uint16_t pd_index(uint64_t va)
{
    return static_cast<uint16_t>((va >> 21) & 0x1FFULL);
}

constexpr uint16_t pt_index(uint64_t va)
{
    return static_cast<uint16_t>((va >> 12) & 0x1FFULL);
}

enum class PageFlags : uint64_t {
    None = 0,
    Writable = kPageWritable,
    User = kPageUser,
    Global = kPageGlobal,
    NoExecute = kPageNoExecute,
};

bool map_page(uint64_t virtual_address,
              uint64_t physical_address,
              uint64_t flags);
bool unmap_page(uint64_t virtual_address);
uint64_t translate(uint64_t virtual_address);
bool is_mapped(uint64_t virtual_address);
bool query_page(uint64_t root_physical,
                uint64_t virtual_address,
                PageInfo& out);

} // namespace linux95::memory::paging
