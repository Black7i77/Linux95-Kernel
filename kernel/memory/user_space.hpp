#pragma once

#include <stddef.h>
#include <stdint.h>

#include "memory/address.hpp"
#include "memory/paging.hpp"

namespace linux95::memory {

constexpr uint64_t kUserImageBase = 0x0000400000000000ULL;
constexpr uint64_t kUserImageLimit = 0x0000400100000000ULL;
constexpr uint64_t kUserStackTop = 0x00007FFFFFF00000ULL;
constexpr size_t kUserStackPages = 8;

enum class UserAccess {
    Read,
    Write,
    Execute,
};

struct UserAddressSpace {
    uint64_t root_physical;
};

struct UserPageSpan {
    uint64_t first_page;
    uint64_t last_page;
};

using PageLookup = bool (*)(void* context,
                            uint64_t virtual_address,
                            PageInfo& out);

constexpr bool make_user_page_flags(bool writable,
                                    bool executable,
                                    bool nxe_enabled,
                                    uint64_t& flags)
{
    flags = 0;
    if (!executable && !nxe_enabled) {
        return false;
    }
    flags = paging::kPagePresent | paging::kPageUser |
            (writable ? paging::kPageWritable : 0) |
            (!executable ? paging::kPageNoExecute : 0);
    return true;
}

constexpr bool is_canonical_user_address(uint64_t address)
{
    return address < 0x0000800000000000ULL;
}

constexpr bool user_range_arithmetic_valid(uint64_t address, size_t length)
{
    return is_canonical_user_address(address) &&
           (length == 0 ||
            (address <= UINT64_MAX - static_cast<uint64_t>(length) &&
             is_canonical_user_address(
                 address + static_cast<uint64_t>(length) - 1ULL)));
}

constexpr UserPageSpan user_page_span(uint64_t address, size_t length)
{
    const uint64_t first = address & ~(kPageSize - 1ULL);
    const uint64_t last = length == 0
                              ? first
                              : (address + static_cast<uint64_t>(length) - 1ULL) &
                                    ~(kPageSize - 1ULL);
    return {first, last};
}

constexpr bool user_page_access_allowed(const PageInfo& page,
                                        UserAccess access)
{
    if (!page.present || !page.user) {
        return false;
    }
    if (access == UserAccess::Write) {
        return page.writable;
    }
    if (access == UserAccess::Execute) {
        return page.executable;
    }
    return true;
}

inline bool validate_page_sequence(const PageInfo* pages,
                                   size_t count,
                                   UserAccess access)
{
    for (size_t i = 0; i < count; ++i) {
        if (!user_page_access_allowed(pages[i], access)) {
            return false;
        }
    }
    return true;
}

bool create_user_address_space(UserAddressSpace& address_space);
void destroy_user_address_space(UserAddressSpace& address_space);
bool map_user_page(UserAddressSpace& address_space,
                   uint64_t virtual_address,
                   uint64_t physical_address,
                   bool writable,
                   bool executable);
bool query_page(uint64_t root_physical,
                uint64_t virtual_address,
                PageInfo& out);
bool validate_user_range(uint64_t root_physical,
                         uint64_t address,
                         size_t length,
                         UserAccess access);
bool validate_user_range_with_lookup(uint64_t address,
                                     size_t length,
                                     UserAccess access,
                                     PageLookup lookup,
                                     void* context);
bool copy_from_user(uint64_t root_physical,
                    void* destination,
                    uint64_t source_user,
                    size_t length);

} // namespace linux95::memory
