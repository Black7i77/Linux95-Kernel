#include "terminal/vga_output.hpp"

#include "terminal/vga.hpp"

namespace linux95::terminal {
namespace {

void vga_put_char(
    void*,
    char c)
{
    vga::put_char(c);
}

void vga_clear(void*)
{
    vga::clear();
}

void vga_set_color(
    void*,
    uint8_t foreground,
    uint8_t background)
{
    vga::set_color(
        foreground,
        background);
}

} // namespace

Output make_vga_output()
{
    return Output{
        nullptr,
        vga_put_char,
        vga_clear,
        vga_set_color,
    };
}

} // namespace linux95::terminal
