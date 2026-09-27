#include "arch/keyboard.hpp"

#include "arch/io.hpp"
#include "arch/keyboard_helpers.hpp"

#include <stdint.h>
#include <stddef.h>

namespace linux95::keyboard {
namespace {

Set1Decoder g_decoder;
EventQueue g_events;
char g_legacy_chars[128]{};
size_t g_char_head = 0;
size_t g_char_tail = 0;
size_t g_char_count = 0;

void push_legacy_char(char c)
{
    if (g_char_count == sizeof(g_legacy_chars)) return;
    g_legacy_chars[g_char_head] = c;
    g_char_head = (g_char_head + 1) % sizeof(g_legacy_chars);
    ++g_char_count;
}

} // namespace

void initialize()
{
    g_decoder.reset();
    g_events.reset();
    g_char_head = g_char_tail = g_char_count = 0;

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
    if (!event.pressed || event.ctrl) return;
    if (event.key == KeyCode::Character) push_legacy_char(event.character);
    else if (event.key == KeyCode::Enter) push_legacy_char('\n');
    else if (event.key == KeyCode::Backspace) push_legacy_char('\b');
}

bool has_event()
{
    return !g_events.empty();
}

KeyEvent read_event()
{
    return g_events.pop();
}

bool has_char()
{
    return g_char_count != 0;
}

char read_char()
{
    if (g_char_count == 0) return 0;
    const char c = g_legacy_chars[g_char_tail];
    g_char_tail = (g_char_tail + 1) % sizeof(g_legacy_chars);
    --g_char_count;
    return c;
}

} // namespace linux95::keyboard
