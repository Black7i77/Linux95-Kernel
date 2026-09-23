#include "terminal/vga.hpp"
#include "arch/io.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::vga {
namespace {

constexpr size_t kWidth = 80;
constexpr size_t kHeight = 25;
constexpr uintptr_t kVgaAddress = 0xB8000;

volatile uint16_t* const kVga =
    reinterpret_cast<volatile uint16_t*>(kVgaAddress);

size_t g_row = 0;
size_t g_column = 0;
uint8_t g_color = 0x07;

uint16_t make_cell(char c)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(c)) |
           (static_cast<uint16_t>(g_color) << 8);
}

void update_cursor()
{
    const uint16_t position =
        static_cast<uint16_t>(g_row * kWidth + g_column);

    io::outb(0x3D4, 0x0F);
    io::outb(0x3D5, static_cast<uint8_t>(position & 0xFF));
    io::outb(0x3D4, 0x0E);
    io::outb(0x3D5, static_cast<uint8_t>((position >> 8) & 0xFF));
}

void clear_row(size_t row)
{
    for (size_t x = 0; x < kWidth; ++x) {
        kVga[row * kWidth + x] = make_cell(' ');
    }
}

void scroll_if_needed()
{
    if (g_row < kHeight) {
        return;
    }

    for (size_t y = 1; y < kHeight; ++y) {
        for (size_t x = 0; x < kWidth; ++x) {
            kVga[(y - 1) * kWidth + x] = kVga[y * kWidth + x];
        }
    }

    clear_row(kHeight - 1);
    g_row = kHeight - 1;
}

void newline()
{
    g_column = 0;
    ++g_row;
    scroll_if_needed();
}

} // namespace

void set_color(uint8_t foreground, uint8_t background)
{
    g_color = static_cast<uint8_t>(
        (background << 4) | (foreground & 0x0F));
}

void clear()
{
    g_row = 0;
    g_column = 0;

    for (size_t y = 0; y < kHeight; ++y) {
        clear_row(y);
    }

    update_cursor();
}

void put_char(char c)
{
    if (c == '\n') {
        newline();
        update_cursor();
        return;
    }

    if (c == '\r') {
        g_column = 0;
        update_cursor();
        return;
    }

    if (c == '\b') {
        if (g_column > 0) {
            --g_column;
        } else if (g_row > 0) {
            --g_row;
            g_column = kWidth - 1;
        } else {
            update_cursor();
            return;
        }

        kVga[g_row * kWidth + g_column] = make_cell(' ');
        update_cursor();
        return;
    }

    kVga[g_row * kWidth + g_column] = make_cell(c);
    ++g_column;

    if (g_column >= kWidth) {
        newline();
    }

    update_cursor();
}

void write(const char* text)
{
    if (text == nullptr) {
        return;
    }

    while (*text != '\0') {
        put_char(*text);
        ++text;
    }
}

void write_uint(uint64_t value)
{
    if (value == 0) {
        put_char('0');
        return;
    }

    char buffer[32];
    size_t length = 0;

    while (value != 0 && length < sizeof(buffer)) {
        buffer[length++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }

    while (length > 0) {
        put_char(buffer[--length]);
    }
}

void write_hex(uint64_t value)
{
    constexpr char kDigits[] = "0123456789ABCDEF";

    write("0x");

    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t digit =
            static_cast<uint8_t>((value >> shift) & 0x0F);

        if (digit != 0 || started || shift == 0) {
            started = true;
            put_char(kDigits[digit]);
        }
    }
}

} // namespace linux95::vga
