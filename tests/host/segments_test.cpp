#include <cassert>
#include <cstdint>
#include "arch/x86_64/segments.hpp"

int main() {
    using namespace linux95::arch::x86_64;

    static_assert(kKernelCodeSelector == 0x08);
    static_assert(kKernelDataSelector == 0x10);
    static_assert(kUserDataSelector == 0x1B);
    static_assert(kUserCodeSelector == 0x23);

    const uint64_t user_code = make_code_data_descriptor(
        0,
        0xFFFFF,
        DescriptorPrivilege::Ring3,
        SegmentKind::Code
    );

    // Present bit.
    assert((user_code & (1ULL << 47)) != 0);
    // DPL == 3.
    assert(((user_code >> 45) & 0x3ULL) == 3);
    // Executable code bit.
    assert((user_code & (1ULL << 43)) != 0);

    TssDescriptor tss = make_tss_descriptor(
        0x0000000012345000ULL,
        103
    );
    assert((tss.low & (1ULL << 47)) != 0);
    assert(((tss.low >> 40) & 0xFULL) == 0x9);
    assert(tss.high == 0);

    return 0;
}
