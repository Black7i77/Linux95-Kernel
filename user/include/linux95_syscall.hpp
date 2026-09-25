#pragma once

namespace linux95::user {

inline long int80_call(long number, long a1 = 0, long a2 = 0, long a3 = 0)
{
    long result;
    asm volatile("int $0x80"
                 : "=a"(result)
                 : "a"(number), "D"(a1), "S"(a2), "d"(a3)
                 : "memory");
    return result;
}

inline long syscall_call(long number, long a1 = 0, long a2 = 0, long a3 = 0)
{
    long result;
    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(number), "D"(a1), "S"(a2), "d"(a3)
                 : "rcx", "r11", "memory");
    return result;
}

inline long write_int80(const char* data, unsigned long length)
{
    return int80_call(0, reinterpret_cast<long>(data),
                      static_cast<long>(length));
}

inline long write_syscall(const char* data, unsigned long length)
{
    return syscall_call(0, reinterpret_cast<long>(data),
                        static_cast<long>(length));
}

[[noreturn]] inline void exit_int80(long code)
{
    (void)int80_call(2, code);
    for (;;) asm volatile("pause");
}

[[noreturn]] inline void exit_syscall(long code)
{
    (void)syscall_call(2, code);
    for (;;) asm volatile("pause");
}

inline void yield_int80()
{
    (void)int80_call(1);
}

inline void yield_syscall()
{
    (void)syscall_call(1);
}

} // namespace linux95::user
