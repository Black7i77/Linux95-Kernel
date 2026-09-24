#include "graphics/renderer.hpp"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

using namespace linux95::graphics;

static Framebuffer make_framebuffer(
    uint8_t* pixels,
    uint32_t width,
    uint32_t height)
{
    Framebuffer fb{};
    fb.data = pixels;
    fb.width = width;
    fb.height = height;
    fb.pitch = width * 4;

    fb.format = PixelFormat{
        8, 16,
        8, 8,
        8, 0,
    };

    return fb;
}

static void test_pack_color()
{
    PixelFormat format{
        8, 16,
        8, 8,
        8, 0,
    };

    const uint32_t packed =
        pack_color(format, Color{0x12, 0x34, 0x56});

    assert(packed == 0x00123456u);
}

static void test_clip_rect()
{
    Rect r{-2, -3, 5, 7};

    const Rect clipped =
        clip_rect(r, 8, 8);

    assert(clipped.x == 0);
    assert(clipped.y == 0);
    assert(clipped.width == 3);
    assert(clipped.height == 4);

    const Rect outside =
        clip_rect(Rect{20, 20, 5, 5}, 8, 8);

    assert(outside.width == 0);
    assert(outside.height == 0);
}

static void test_offscreen_pixel_does_not_write()
{
    uint8_t pixels[8 * 8 * 4];

    for (size_t i = 0; i < sizeof(pixels); ++i) {
        pixels[i] = 0xA5;
    }

    Framebuffer fb =
        make_framebuffer(pixels, 8, 8);

    put_pixel(fb, -1, 0, Color{255, 0, 0});
    put_pixel(fb, 0, -1, Color{255, 0, 0});
    put_pixel(fb, 8, 0, Color{255, 0, 0});
    put_pixel(fb, 0, 8, Color{255, 0, 0});

    for (size_t i = 0; i < sizeof(pixels); ++i) {
        assert(pixels[i] == 0xA5);
    }
}

static void test_fill_rect_exact_pixels()
{
    uint32_t pixels[8 * 8] = {};

    Framebuffer fb =
        make_framebuffer(
            reinterpret_cast<uint8_t*>(pixels),
            8,
            8);

    fill_rect(
        fb,
        Rect{2, 3, 3, 2},
        Color{255, 0, 0});

    for (int32_t y = 0; y < 8; ++y) {
        for (int32_t x = 0; x < 8; ++x) {
            const bool expected =
                x >= 2 && x < 5 &&
                y >= 3 && y < 5;

            if (expected) {
                assert(
                    pixels[y * 8 + x] ==
                    0x00FF0000u);
            } else {
                assert(
                    pixels[y * 8 + x] == 0);
            }
        }
    }
}

static void test_draw_char_stays_inside_cell()
{
    uint32_t guarded[66];

    for (size_t i = 0; i < 66; ++i) {
        guarded[i] = 0xDEADBEEFu;
    }

    uint32_t* pixels = &guarded[1];

    for (size_t i = 0; i < 64; ++i) {
        pixels[i] = 0;
    }

    Framebuffer fb =
        make_framebuffer(
            reinterpret_cast<uint8_t*>(pixels),
            8,
            8);

    draw_char(
        fb,
        0,
        0,
        'A',
        Color{255, 255, 255});

    assert(guarded[0] == 0xDEADBEEFu);
    assert(guarded[65] == 0xDEADBEEFu);

    bool wrote_pixel = false;

    for (size_t i = 0; i < 64; ++i) {
        if (pixels[i] != 0) {
            wrote_pixel = true;
        }
    }

    assert(wrote_pixel);
}


static void test_printable_b_has_pixels()
{
    uint32_t pixels[8 * 8] = {};

    Framebuffer fb =
        make_framebuffer(
            reinterpret_cast<uint8_t*>(pixels),
            8,
            8);

    draw_char(
        fb,
        0,
        0,
        'B',
        Color{255, 255, 255});

    bool wrote_pixel = false;

    for (size_t i = 0; i < 64; ++i) {
        if (pixels[i] != 0) {
            wrote_pixel = true;
            break;
        }
    }

    assert(wrote_pixel);
}


static void test_required_font_glyphs_are_populated()
{
    for (uint16_t code = 0x21; code <= 0x7F; ++code) {
        uint32_t pixels[8 * 8] = {};

        Framebuffer fb =
            make_framebuffer(
                reinterpret_cast<uint8_t*>(pixels),
                8,
                8);

        draw_char(
            fb,
            0,
            0,
            static_cast<char>(code),
            Color{255, 255, 255});

        bool wrote_pixel = false;

        for (size_t i = 0; i < 64; ++i) {
            if (pixels[i] != 0) {
                wrote_pixel = true;
                break;
            }
        }

        assert(wrote_pixel);
    }
}

static void test_space_is_blank()
{
    uint32_t pixels[8 * 8] = {};

    Framebuffer fb =
        make_framebuffer(
            reinterpret_cast<uint8_t*>(pixels),
            8,
            8);

    draw_char(
        fb,
        0,
        0,
        ' ',
        Color{255, 255, 255});

    for (size_t i = 0; i < 64; ++i) {
        assert(pixels[i] == 0);
    }
}

int main()
{
    test_pack_color();
    test_clip_rect();
    test_offscreen_pixel_does_not_write();
    test_fill_rect_exact_pixels();
    test_draw_char_stays_inside_cell();
    test_printable_b_has_pixels();
    test_required_font_glyphs_are_populated();
    test_space_is_blank();

    return 0;
}
