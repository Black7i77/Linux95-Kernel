#pragma once

#include <stddef.h>
#include <stdint.h>

namespace linux95::terminal {

struct Output {
    void* context;
    void (*put_char)(void*, char);
    void (*clear)(void*);
    void (*set_color)(
        void*,
        uint8_t foreground,
        uint8_t background);
};

inline void write(
    Output& output,
    const char* text)
{
    if (text == nullptr ||
        output.put_char == nullptr) {
        return;
    }

    while (*text != '\0') {
        output.put_char(
            output.context,
            *text);

        ++text;
    }
}

inline void write_uint(
    Output& output,
    uint64_t value)
{
    if (output.put_char == nullptr) {
        return;
    }

    if (value == 0) {
        output.put_char(
            output.context,
            '0');
        return;
    }

    char buffer[32];
    size_t length = 0;

    while (
        value != 0 &&
        length < sizeof(buffer)) {
        buffer[length++] =
            static_cast<char>(
                '0' + (value % 10));

        value /= 10;
    }

    while (length > 0) {
        output.put_char(
            output.context,
            buffer[--length]);
    }
}

inline void write_hex(
    Output& output,
    uint64_t value)
{
    constexpr char kDigits[] =
        "0123456789ABCDEF";

    write(
        output,
        "0x");

    if (output.put_char == nullptr) {
        return;
    }

    bool started = false;

    for (
        int shift = 60;
        shift >= 0;
        shift -= 4) {
        const uint8_t digit =
            static_cast<uint8_t>(
                (value >> shift) &
                0x0F);

        if (
            digit != 0 ||
            started ||
            shift == 0) {
            started = true;

            output.put_char(
                output.context,
                kDigits[digit]);
        }
    }
}

} // namespace linux95::terminal
