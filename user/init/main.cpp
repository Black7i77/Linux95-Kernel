#include "linux95_syscall.hpp"

extern "C" int user_main()
{
    static constexpr char hello[] = "[pid 1] hello through int 0x80\n";
    static constexpr char resumed[] = "[pid 1] resumed through syscall\n";

    linux95::user::int80_call(
        0, 1, reinterpret_cast<long>(hello), sizeof(hello) - 1);
    linux95::user::yield_int80();
    linux95::user::syscall_call(
        0, 1, reinterpret_cast<long>(resumed), sizeof(resumed) - 1);
    linux95::user::yield_syscall();
    return 0;
}
