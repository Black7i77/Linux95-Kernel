#pragma once

#include "graphics/framebuffer.hpp"

#include <stdint.h>

namespace linux95::graphics {

struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct Rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

uint32_t pack_color(const PixelFormat& format, Color color);

Rect clip_rect(Rect rect, int32_t width, int32_t height);

void put_pixel(
    Framebuffer& framebuffer,
    int32_t x,
    int32_t y,
    Color color);

void fill_rect(
    Framebuffer& framebuffer,
    Rect rect,
    Color color);

void draw_rect(
    Framebuffer& framebuffer,
    Rect rect,
    Color color);

void draw_line(
    Framebuffer& framebuffer,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    Color color);

void draw_char(
    Framebuffer& framebuffer,
    int32_t x,
    int32_t y,
    char c,
    Color color);

void draw_text(
    Framebuffer& framebuffer,
    int32_t x,
    int32_t y,
    const char* text,
    Color color);

} // namespace linux95::graphics
