#include "memory/user_space.hpp"

#include "memory/address.hpp"
#include "memory/physical.hpp"

#include "arch/x86_64/control_regs.hpp"
#include "arch/x86_64/msr.hpp"

namespace linux95::memory {
namespace {

constexpr size_t kEntriesPerTable = 512;
constexpr uint64_t kUserBit = paging::kPageUser;
constexpr uint64_t kWriteBit = paging::kPageWritable;
constexpr uint64_t kPresentBit = paging::kPagePresent;
constexpr uint64_t kHugeBit = paging::kPageHuge;
constexpr uint64_t kAddressMask = paging::kAddressMask;

uint64_t* table(uint64_t physical)
{
    return reinterpret_cast<uint64_t*>(
        static_cast<uintptr_t>(physical_to_hhdm(physical)));
}

uint64_t allocate_table()
{
    const uint64_t physical = physical::allocate_page();
    if (physical == physical::kInvalidPhysicalAddress) {
        return paging::kInvalidAddress;
    }
    uint64_t* entries = table(physical);
    for (size_t i = 0; i < kEntriesPerTable; ++i) {
        entries[i] = 0;
    }
    return physical;
}

uint64_t child(uint64_t* parent, uint16_t index)
{
    uint64_t& entry = parent[index];
    if ((entry & kPresentBit) != 0) {
        return (entry & kHugeBit) != 0 ? paging::kInvalidAddress
                                      : entry & kAddressMask;
    }
    const uint64_t physical = allocate_table();
    if (physical == paging::kInvalidAddress) {
        return physical;
    }
    entry = physical | kPresentBit | kWriteBit | kUserBit;
    return physical;
}

void release_table(uint64_t physical, int level)
{
    if (physical == 0 || physical == paging::kInvalidAddress) {
        return;
    }
    if (level > 1) {
        uint64_t* entries = table(physical);
        for (size_t i = 0; i < kEntriesPerTable; ++i) {
            if ((entries[i] & kPresentBit) != 0 &&
                (entries[i] & kHugeBit) == 0) {
                release_table(entries[i] & kAddressMask, level - 1);
            }
        }
    }
    physical::free_page(physical);
}

} // namespace

bool create_user_address_space(UserAddressSpace& address_space)
{
    address_space.root_physical = paging::kInvalidAddress;
    const uint64_t root = allocate_table();
    if (root == paging::kInvalidAddress) {
        return false;
    }

    const uint64_t current_root =
        arch::x86_64::read_cr3() & paging::kAddressMask;
    const uint64_t* source = table(current_root);
    uint64_t* destination = table(root);
    for (size_t i = 0; i < kEntriesPerTable; ++i) {
        destination[i] = source[i] & ~kUserBit;
    }

    const size_t image_slot = paging::pml4_index(kUserImageBase);
    const size_t stack_slot = paging::pml4_index(kUserStackTop);
    if ((source[image_slot] & kPresentBit) != 0 ||
        (source[stack_slot] & kPresentBit) != 0) {
        physical::free_page(root);
        return false;
    }
    destination[image_slot] = 0;
    destination[stack_slot] = 0;
    address_space.root_physical = root;
    return true;
}

void destroy_user_address_space(UserAddressSpace& address_space)
{
    if (address_space.root_physical == paging::kInvalidAddress ||
        address_space.root_physical == 0) {
        return;
    }
    uint64_t* root = table(address_space.root_physical);
    const size_t image_slot = paging::pml4_index(kUserImageBase);
    const size_t stack_slot = paging::pml4_index(kUserStackTop);
    if ((root[image_slot] & kPresentBit) != 0) {
        release_table(root[image_slot] & kAddressMask, 3);
    }
    if ((root[stack_slot] & kPresentBit) != 0 && stack_slot != image_slot) {
        release_table(root[stack_slot] & kAddressMask, 3);
    }
    physical::free_page(address_space.root_physical);
    address_space.root_physical = paging::kInvalidAddress;
}

bool map_user_page(UserAddressSpace& address_space,
                   uint64_t virtual_address,
                   uint64_t physical_address,
                   bool writable,
                   bool executable)
{
    if (address_space.root_physical == paging::kInvalidAddress ||
        !is_page_aligned(virtual_address) ||
        !is_page_aligned(physical_address) ||
        !is_canonical_user_address(virtual_address)) {
        return false;
    }
    uint64_t flags = 0;
    if (!make_user_page_flags(
            writable, executable, arch::x86_64::nxe_enabled(), flags)) {
        return false;
    }
    uint64_t* pml4 = table(address_space.root_physical);
    uint64_t pdpt_physical = child(pml4, paging::pml4_index(virtual_address));
    if (pdpt_physical == paging::kInvalidAddress) return false;
    uint64_t* pdpt = table(pdpt_physical);
    uint64_t pd_physical = child(pdpt, paging::pdpt_index(virtual_address));
    if (pd_physical == paging::kInvalidAddress) return false;
    uint64_t* pd = table(pd_physical);
    uint64_t pt_physical = child(pd, paging::pd_index(virtual_address));
    if (pt_physical == paging::kInvalidAddress) return false;
    uint64_t* pt = table(pt_physical);
    uint64_t& pte = pt[paging::pt_index(virtual_address)];
    if ((pte & kPresentBit) != 0) return false;
    pte = (physical_address & kAddressMask) | flags;
    return true;
}

bool query_page(uint64_t root_physical, uint64_t virtual_address, PageInfo& out)
{
    return paging::query_page(root_physical, virtual_address, out);
}

namespace {

bool lookup_root_page(void* context,
                      uint64_t virtual_address,
                      PageInfo& out)
{
    const uint64_t root_physical = *static_cast<uint64_t*>(context);
    return query_page(root_physical, virtual_address, out);
}

} // namespace

bool validate_user_range_with_lookup(uint64_t address,
                                     size_t length,
                                     UserAccess access,
                                     PageLookup lookup,
                                     void* context)
{
    if (lookup == nullptr || !user_range_arithmetic_valid(address, length)) {
        return false;
    }
    if (length == 0) return true;
    const UserPageSpan span = user_page_span(address, length);
    for (uint64_t page = span.first_page;; page += kPageSize) {
        PageInfo info{};
        if (!lookup(context, page, info) ||
            !user_page_access_allowed(info, access)) {
            return false;
        }
        if (page == span.last_page) break;
    }
    return true;
}

bool validate_user_range(uint64_t root_physical,
                         uint64_t address,
                         size_t length,
                         UserAccess access)
{
    return validate_user_range_with_lookup(
        address, length, access, lookup_root_page, &root_physical);
}

bool copy_from_user(uint64_t root_physical,
                    void* destination,
                    uint64_t source_user,
                    size_t length)
{
    if (!validate_user_range(root_physical, source_user, length,
                             UserAccess::Read)) return false;
    auto* destination_bytes = static_cast<uint8_t*>(destination);
    size_t remaining = length;
    uint64_t source = source_user;
    while (remaining != 0) {
        PageInfo info{};
        if (!query_page(root_physical, source, info)) return false;
        const size_t offset = static_cast<size_t>(source & (kPageSize - 1));
        const size_t chunk = (remaining < kPageSize - offset)
                                 ? remaining : kPageSize - offset;
        const auto* source_bytes = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(physical_to_hhdm(info.physical)));
        for (size_t i = 0; i < chunk; ++i) destination_bytes[i] = source_bytes[i];
        destination_bytes += chunk;
        source += chunk;
        remaining -= chunk;
    }
    return true;
}

} // namespace linux95::memory
