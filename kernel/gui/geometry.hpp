#pragma once

#include <stdint.h>

namespace linux95::gui {

struct Point {
    int32_t x;
    int32_t y;
};

struct Rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

inline bool contains(
    Rect rect,
    Point point)
{
    return
        point.x >= rect.x &&
        point.y >= rect.y &&
        point.x < rect.x + rect.width &&
        point.y < rect.y + rect.height;
}

} // namespace linux95::gui
