#include "memory/address.hpp"
#include "memory/memory.hpp"

#include <assert.h>
#include <stdint.h>

extern "C" char _kernel_end;
char _kernel_end = 0;

int main()
{
    using namespace linux95::memory;

    uint64_t physical = UINT64_MAX;
    assert(kernel_virtual_to_physical(
        reinterpret_cast<const void*>(kKernelVirtualBase),
        physical));
    assert(physical == kKernelPhysicalBase);

    assert(kernel_virtual_to_physical(
        reinterpret_cast<const void*>(
            kKernelRegionBase + kHugePageSize - 1u),
        physical));
    assert(physical == kHugePageSize - 1u);

    assert(!kernel_virtual_to_physical(nullptr, physical));
    assert(!kernel_virtual_to_physical(
        reinterpret_cast<const void*>(kKernelRegionBase),
        physical));
    assert(!kernel_virtual_to_physical(
        reinterpret_cast<const void*>(
            kKernelRegionBase + kHugePageSize),
        physical));

    return 0;
}
