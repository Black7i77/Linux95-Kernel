#include "linux95_syscall.hpp"
#include <stdint.h>

extern "C" int user_main()
{
    static constexpr char scheduled[] =
        "[pid 2] scheduled by timer preemption\n";
    linux95::user::write_syscall(scheduled, sizeof(scheduled) - 1);

    volatile uint64_t value = 1;
    for (;;) {
        value = value * 1103515245ULL + 12345ULL;
        asm volatile("" : "+r"(value) : : "memory");
    }
}
