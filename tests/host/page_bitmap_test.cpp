#include <assert.h>
#include <stdint.h>

#include "memory/address.hpp"
#include "memory/page_bitmap.hpp"

int main()
{
    uint8_t storage[2] = {};
    linux95::memory::PageBitmap bitmap;

    bitmap.initialize(storage, 16);
    assert(bitmap.capacity() == 16);
    assert(bitmap.free_count() == 0);

    for (uint64_t i = 0; i < 16; ++i) {
        assert(bitmap.mark_free(i));
    }
    assert(bitmap.free_count() == 16);

    const uint64_t a = bitmap.allocate();
    const uint64_t b = bitmap.allocate();
    assert(a != linux95::memory::PageBitmap::kInvalid);
    assert(b != linux95::memory::PageBitmap::kInvalid);
    assert(a != b);
    assert(bitmap.free_count() == 14);

    assert(bitmap.mark_used(a) == false);
    assert(bitmap.mark_free(a));
    assert(bitmap.mark_free(a) == false);
    assert(bitmap.free_count() == 15);

    bitmap.mark_used(15);
    assert(bitmap.free_count() == 14);

    assert(linux95::memory::align_up(0x1003, 4096) == 0x2000);
    assert(linux95::memory::align_down(0x2FFF, 4096) == 0x2000);

    return 0;
}
