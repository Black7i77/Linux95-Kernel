#include "arch/pit.hpp"

#include "arch/io.hpp"

namespace linux95::pit {
namespace {

constexpr uint32_t kPitInputHz = 1193182u;
volatile uint64_t g_ticks = 0;
uint32_t g_hz = 100;

} // namespace

void initialize(uint32_t hz)
{
    if (hz == 0) {
        hz = 100;
    }

    uint32_t divisor = kPitInputHz / hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFFu) {
        divisor = 0xFFFFu;
    }

    g_hz = kPitInputHz / divisor;
    if (g_hz == 0) {
        g_hz = 1;
    }

    io::outb(0x43, 0x36);
    io::outb(0x40, static_cast<uint8_t>(divisor & 0xFF));
    io::outb(0x40, static_cast<uint8_t>((divisor >> 8) & 0xFF));
}

void on_irq()
{
    ++g_ticks;
}

uint64_t ticks()
{
    return g_ticks;
}

uint32_t ticks_per_second()
{
    return g_hz;
}

uint64_t uptime_seconds()
{
    return g_ticks / g_hz;
}

} // namespace linux95::pit
