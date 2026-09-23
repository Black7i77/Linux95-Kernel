#include "vga.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::vga {

namespace {

constexpr size_t WIDTH  = 80;
constexpr size_t HEIGHT = 25;

volatile uint16_t* const VGA =
    reinterpret_cast<volatile uint16_t*>(0xB8000);

size_t row = 0;
size_t column = 0;
uint8_t color = 0x07;

uint16_t make_cell(char c)
{
    return static_cast<uint16_t>(
        static_cast<uint8_t>(c)
    ) | (static_cast<uint16_t>(color) << 8);
}

void newline()
{
    column = 0;

    if (row + 1 < HEIGHT) {
        ++row;
    }
}

}

void set_color(uint8_t foreground, uint8_t background)
{
    color = static_cast<uint8_t>(
        (background << 4) |
        (foreground & 0x0F)
    );
}

void clear()
{
    row = 0;
    column = 0;

    for (size_t y = 0; y < HEIGHT; ++y) {
        for (size_t x = 0; x < WIDTH; ++x) {
            VGA[y * WIDTH + x] = make_cell(' ');
        }
    }
}

void put_char(char c)
{
    if (c == '\n') {
        newline();
        return;
    }

    VGA[row * WIDTH + column] = make_cell(c);

    ++column;

    if (column >= WIDTH) {
        newline();
    }
}

void write(const char* text)
{
    while (*text != '\0') {
        put_char(*text);
        ++text;
    }
}

}
