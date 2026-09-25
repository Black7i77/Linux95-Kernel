#include "linux95_syscall.hpp"

extern "C" int user_main()
{
    static constexpr char hello[] = "[pid 2] hello through syscall\n";
    static constexpr char resumed[] = "[pid 2] resumed through int 0x80\n";

    linux95::user::write_syscall(hello, sizeof(hello) - 1);
    linux95::user::yield_syscall();
    (void)linux95::user::syscall_call(999);
    (void)linux95::user::write_syscall(
        reinterpret_cast<const char*>(0x0000400000000FFFULL), 2);
    linux95::user::write_int80(resumed, sizeof(resumed) - 1);
    return 0;
}
