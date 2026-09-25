#include <cassert>
#include <cstdint>

#include "memory/user_space.hpp"

int main() {
    using namespace linux95::memory;

    assert(is_canonical_user_address(kUserImageBase));
    assert(is_canonical_user_address(kUserStackTop - 8));
    assert(!is_canonical_user_address(0xFFFF800000000000ULL));

    assert(user_range_arithmetic_valid(kUserImageBase, 16));
    assert(!user_range_arithmetic_valid(UINT64_MAX - 3, 8));
    assert(user_range_arithmetic_valid(kUserImageBase, 0));
    assert(!user_range_arithmetic_valid(0x0000800000000000ULL, 0));

    const UserPageSpan one = user_page_span(0x4000, 1);
    assert(one.first_page == 0x4000);
    assert(one.last_page == 0x4000);

    const UserPageSpan cross = user_page_span(0x4FFF, 2);
    assert(cross.first_page == 0x4000);
    assert(cross.last_page == 0x5000);

    const PageInfo pages[2] = {
        {true, true, true, false, 0x1000},
        {true, false, true, false, 0x2000},
    };
    assert(!validate_page_sequence(pages, 2, UserAccess::Read));

    return 0;
}
