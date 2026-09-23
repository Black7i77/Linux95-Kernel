#pragma once

#include <stdint.h>

namespace linux95::vga {

void clear();
void set_color(uint8_t foreground, uint8_t background);
void put_char(char c);
void write(const char* text);

}
