#include "arch/keyboard.hpp"

#include "arch/io.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::keyboard {
namespace {

constexpr size_t kBufferSize = 128;

volatile char g_buffer[kBufferSize];
volatile size_t g_head = 0;
volatile size_t g_tail = 0;
bool g_shift = false;

char translate_letter(uint8_t scancode)
{
    switch (scancode) {
        case 0x10: return 'q';
        case 0x11: return 'w';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';
        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';
        default: return 0;
    }
}

char translate(uint8_t scancode, bool shift)
{
    char letter = translate_letter(scancode);
    if (letter != 0) {
        if (shift) {
            return static_cast<char>(letter - 'a' + 'A');
        }
        return letter;
    }

    switch (scancode) {
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x0E: return '\b';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x1C: return '\n';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x29: return shift ? '~' : '`';
        case 0x2B: return shift ? '|' : '\\';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        case 0x39: return ' ';
        default: return 0;
    }
}

void push_char(char c)
{
    const size_t next = (g_head + 1) % kBufferSize;

    if (next == g_tail) {
        return;
    }

    g_buffer[g_head] = c;
    g_head = next;
}

} // namespace

void initialize()
{
    g_head = 0;
    g_tail = 0;
    g_shift = false;

    for (uint32_t i = 0; i < 64; ++i) {
        if ((io::inb(0x64) & 0x01u) == 0) {
            break;
        }
        static_cast<void>(io::inb(0x60));
    }
}

void on_irq()
{
    const uint8_t scancode = io::inb(0x60);

    if (scancode == 0x2A || scancode == 0x36) {
        g_shift = true;
        return;
    }

    if (scancode == 0xAA || scancode == 0xB6) {
        g_shift = false;
        return;
    }

    if ((scancode & 0x80u) != 0 || scancode >= 128) {
        return;
    }

    const char c = translate(scancode, g_shift);

    if (c != 0) {
        push_char(c);
    }
}

bool has_char()
{
    return g_head != g_tail;
}

char read_char()
{
    if (g_head == g_tail) {
        return 0;
    }

    const char c = g_buffer[g_tail];
    g_tail = (g_tail + 1) % kBufferSize;
    return c;
}

} // namespace linux95::keyboard
