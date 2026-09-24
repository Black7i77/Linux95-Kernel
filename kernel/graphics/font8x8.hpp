#pragma once

#include <stdint.h>

namespace linux95::graphics {

constexpr uint8_t kFontFirst = 0x20;
constexpr uint8_t kFontLast = 0x7F;

extern const uint8_t kFont8x8[96][8];

static_assert(
    kFontLast - kFontFirst + 1 == 96,
    "Linux95 font range changed");

} // namespace linux95::graphics
