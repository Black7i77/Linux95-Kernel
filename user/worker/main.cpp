#include "linux95_syscall.hpp"

extern "C" int user_main()
{
    static constexpr char hello[] = "[pid 2] hello through syscall\n";
    static constexpr char resumed[] = "[pid 2] resumed through int 0x80\n";

    linux95::user::syscall_call(
        0, 1, reinterpret_cast<long>(hello), sizeof(hello) - 1);
    linux95::user::yield_syscall();
    linux95::user::int80_call(
        0, 1, reinterpret_cast<long>(resumed), sizeof(resumed) - 1);
    return 0;
}
