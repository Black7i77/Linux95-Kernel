#pragma once

#include "boot_info.hpp"
#include <stdint.h>

namespace linux95::graphics {

enum class ValidationStatus : uint8_t {
    Ok,
    Unavailable,
    InvalidMode,
    InvalidAddress,
    InvalidPitch,
    InvalidColorMasks,
    Overflow,
};

struct MappingLayout {
    uint64_t physical_base;
    uint64_t offset;
    uint64_t page_count;
};

constexpr uint64_t kFramebufferPageSize = 4096ULL;

inline ValidationStatus validate_framebuffer(
    const FramebufferInfo& info,
    uint64_t& byte_size)
{
    byte_size = 0;

    if (info.available != 1) {
        return ValidationStatus::Unavailable;
    }

    if (info.width != 1280 ||
        info.height != 720 ||
        info.bits_per_pixel != 32) {
        return ValidationStatus::InvalidMode;
    }

    if (info.physical_address == 0) {
        return ValidationStatus::InvalidAddress;
    }

    constexpr uint64_t kRequiredPitch = 1280ULL * 4ULL;

    if (static_cast<uint64_t>(info.pitch) < kRequiredPitch) {
        return ValidationStatus::InvalidPitch;
    }

    if (info.red_mask_size != 8 ||
        info.green_mask_size != 8 ||
        info.blue_mask_size != 8) {
        return ValidationStatus::InvalidColorMasks;
    }

    if (info.red_mask_shift > 24 ||
        info.green_mask_shift > 24 ||
        info.blue_mask_shift > 24) {
        return ValidationStatus::InvalidColorMasks;
    }

    const uint32_t red_mask = 0xFFu << info.red_mask_shift;
    const uint32_t green_mask = 0xFFu << info.green_mask_shift;
    const uint32_t blue_mask = 0xFFu << info.blue_mask_shift;

    if ((red_mask & green_mask) != 0 ||
        (red_mask & blue_mask) != 0 ||
        (green_mask & blue_mask) != 0) {
        return ValidationStatus::InvalidColorMasks;
    }

    constexpr uint64_t kUint64Max = ~uint64_t{0};

    if (info.height != 0 &&
        static_cast<uint64_t>(info.pitch) >
            kUint64Max / static_cast<uint64_t>(info.height)) {
        return ValidationStatus::Overflow;
    }

    const uint64_t bytes =
        static_cast<uint64_t>(info.pitch) *
        static_cast<uint64_t>(info.height);

    if (info.physical_address > kUint64Max - bytes) {
        return ValidationStatus::Overflow;
    }

    byte_size = bytes;
    return ValidationStatus::Ok;
}

inline MappingLayout mapping_layout(const FramebufferInfo& info)
{
    uint64_t byte_size = 0;

    if (validate_framebuffer(info, byte_size) != ValidationStatus::Ok) {
        return MappingLayout{0, 0, 0};
    }

    constexpr uint64_t kPageMask = kFramebufferPageSize - 1ULL;
    constexpr uint64_t kUint64Max = ~uint64_t{0};

    const uint64_t physical_base =
        info.physical_address & ~kPageMask;

    const uint64_t offset =
        info.physical_address - physical_base;

    if (offset > kUint64Max - byte_size) {
        return MappingLayout{0, 0, 0};
    }

    const uint64_t coverage = offset + byte_size;

    if (coverage > kUint64Max - kPageMask) {
        return MappingLayout{0, 0, 0};
    }

    return MappingLayout{
        physical_base,
        offset,
        (coverage + kPageMask) / kFramebufferPageSize,
    };
}

} // namespace linux95::graphics
