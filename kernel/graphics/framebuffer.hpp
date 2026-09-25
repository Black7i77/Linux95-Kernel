#pragma once

#include "boot_info.hpp"
#include <stdint.h>

namespace linux95::graphics {

constexpr uint64_t kFramebufferVirtualBase =
    0xFFFF900000000000ULL;

enum class FramebufferInitResult : uint8_t {
    Ready,
    Unavailable,
    InvalidMetadata,
    MappingFailed,
};

struct PixelFormat {
    uint8_t red_size;
    uint8_t red_shift;
    uint8_t green_size;
    uint8_t green_shift;
    uint8_t blue_size;
    uint8_t blue_shift;
};

struct Framebuffer {
    volatile uint8_t* data;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    PixelFormat format;
};

FramebufferInitResult initialize_framebuffer(const BootInfo& boot_info);
Framebuffer* framebuffer();

} // namespace linux95::graphics
