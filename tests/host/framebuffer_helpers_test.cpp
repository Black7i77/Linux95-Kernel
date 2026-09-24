#include "boot_info.hpp"
#include "graphics/framebuffer_helpers.hpp"
#include <assert.h>
#include <stdint.h>

using namespace linux95;
using namespace linux95::graphics;

static FramebufferInfo valid_info()
{
    return FramebufferInfo{
        0xFD000123ULL, 1280, 720, 1280 * 4, 32,
        8, 16, 8, 8, 8, 0, 1,
    };
}

int main()
{
    uint64_t bytes = 0;
    FramebufferInfo info = valid_info();

    assert(validate_framebuffer(info, bytes) == ValidationStatus::Ok);
    assert(bytes == 1280ULL * 4ULL * 720ULL);

    info.available = 0;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::Unavailable);

    info = valid_info();
    info.width = 1024;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidMode);

    info = valid_info();
    info.pitch = 5119;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidPitch);

    info = valid_info();
    info.physical_address = 0;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidAddress);

    info = valid_info();
    info.red_mask_shift = 8;
    info.green_mask_shift = 8;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidColorMasks);

    info = valid_info();
    info.physical_address = UINT64_MAX - 100;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::Overflow);

    MappingLayout layout = mapping_layout(valid_info());
    assert(layout.physical_base == 0xFD000000ULL);
    assert(layout.offset == 0x123ULL);
    assert(layout.page_count > 0);

    return 0;
}
