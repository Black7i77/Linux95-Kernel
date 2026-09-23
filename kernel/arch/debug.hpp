#pragma once

#include <stdint.h>

#include "arch/io.hpp"

namespace linux95::debug {

constexpr uint16_t kDebugPort = 0x00E9;

inline void put_char(char c)
{
    io::outb(kDebugPort, static_cast<uint8_t>(c));
}

inline void write(const char* text)
{
    if (text == nullptr) {
        return;
    }

    while (*text != '\0') {
        put_char(*text);
        ++text;
    }
}

} // namespace linux95::debug
