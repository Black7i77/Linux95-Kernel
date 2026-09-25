#include <assert.h>
#include <stdint.h>

#include "boot_info.hpp"
#include "memory/memory.hpp"

extern "C" char _kernel_end;
char _kernel_end = 0;

int main()
{
    linux95::E820Entry entries[] = {
        {0x00000000ULL, 0x0009FC00ULL, 1, 1},
        {0x00100000ULL, 0x07F00000ULL, 1, 1},
        {0x1000000000ULL, 0x1000ULL, 2, 1},
    };

    linux95::BootInfo info{
        linux95::kBootInfoMagic,
        3,
        reinterpret_cast<uint64_t>(entries),
        0x80,
        linux95::FramebufferInfo{},
    };

    assert(
        linux95::memory::maximum_physical_address(info) >
        (64ULL * 1024ULL * 1024ULL * 1024ULL));

    assert(
        linux95::memory::maximum_usable_physical_address(info) ==
        0x08000000ULL);

    return 0;
}
