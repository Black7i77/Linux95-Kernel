#include "arch/keyboard.hpp"

#include "arch/io.hpp"
#include "arch/keyboard_helpers.hpp"

#include <stdint.h>

namespace linux95::keyboard {
namespace {

Set1Decoder g_decoder;
EventQueue g_events;

} // namespace

void initialize()
{
    g_decoder.reset();
    g_events.reset();

    for (uint32_t i = 0; i < 64; ++i) {
        if ((io::inb(0x64) & 0x01u) == 0) break;
        static_cast<void>(io::inb(0x60));
    }
}

void on_irq()
{
    KeyEvent event{};
    if (!g_decoder.feed(io::inb(0x60), event)) return;
    static_cast<void>(g_events.push(event));
}

bool has_event()
{
    return !g_events.empty();
}

KeyEvent read_event()
{
    return g_events.pop();
}

} // namespace linux95::keyboard
