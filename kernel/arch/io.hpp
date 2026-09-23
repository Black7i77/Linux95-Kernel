#pragma once

#include <stdint.h>

namespace linux95::io {

inline void outb(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void io_wait()
{
    asm volatile("outb %%al, $0x80" : : "a"(0));
}

inline void disable_interrupts()
{
    asm volatile("cli" : : : "memory");
}

inline void enable_interrupts()
{
    asm volatile("sti" : : : "memory");
}

inline void halt()
{
    asm volatile("hlt");
}

inline void pause()
{
    asm volatile("pause");
}

} // namespace linux95::io
