#include <stdint.h>

extern "C" int user_main()
{
    volatile const uint64_t* bad =
        reinterpret_cast<volatile const uint64_t*>(
            0x0000500000000000ULL);
    volatile uint64_t value = *bad;
    (void) value;
    return 1;
}
