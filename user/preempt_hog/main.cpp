#include "linux95_syscall.hpp"
#include <stdint.h>

extern "C" int user_main()
{
    static constexpr char started[] =
        "[pid 1] entered non-yielding loop\n";
    linux95::user::write_int80(started, sizeof(started) - 1);

    volatile uint64_t value = 0;
    for (;;) {
        value = value * 1664525ULL + 1013904223ULL;
        asm volatile("" : "+r"(value) : : "memory");
    }
}
