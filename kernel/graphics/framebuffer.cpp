#include "graphics/framebuffer.hpp"

#include "graphics/framebuffer_helpers.hpp"
#include "memory/paging.hpp"

#include <stdint.h>

namespace linux95::graphics {

namespace {

Framebuffer g_framebuffer{};
bool g_framebuffer_ready = false;

void rollback_mappings(uint64_t mapped_pages)
{
    for (uint64_t i = 0; i < mapped_pages; ++i) {
        const uint64_t virtual_address =
            kFramebufferVirtualBase +
            (i * kFramebufferPageSize);

        memory::paging::unmap_page(virtual_address);
    }
}

} // namespace

FramebufferInitResult initialize_framebuffer(const BootInfo& boot_info)
{
    g_framebuffer = Framebuffer{};
    g_framebuffer_ready = false;

    uint64_t byte_size = 0;

    const ValidationStatus validation =
        validate_framebuffer(
            boot_info.framebuffer,
            byte_size);

    if (validation == ValidationStatus::Unavailable) {
        return FramebufferInitResult::Unavailable;
    }

    if (validation != ValidationStatus::Ok) {
        return FramebufferInitResult::InvalidMetadata;
    }

    const MappingLayout layout =
        mapping_layout(boot_info.framebuffer);

    if (layout.page_count == 0) {
        return FramebufferInitResult::InvalidMetadata;
    }

    constexpr uint64_t kUint64Max = ~uint64_t{0};

    if ((layout.page_count - 1) >
        ((kUint64Max - kFramebufferVirtualBase) /
         kFramebufferPageSize)) {
        return FramebufferInitResult::InvalidMetadata;
    }

    if ((layout.page_count - 1) >
        ((kUint64Max - layout.physical_base) /
         kFramebufferPageSize)) {
        return FramebufferInitResult::InvalidMetadata;
    }

    constexpr uint64_t kFramebufferPageFlags =
        memory::paging::kPageWritable |
        memory::paging::kPageNoExecute;

    uint64_t mapped_pages = 0;

    for (uint64_t i = 0; i < layout.page_count; ++i) {
        const uint64_t virtual_address =
            kFramebufferVirtualBase +
            (i * kFramebufferPageSize);

        const uint64_t physical_address =
            layout.physical_base +
            (i * kFramebufferPageSize);

        if (!memory::paging::map_page(
                virtual_address,
                physical_address,
                kFramebufferPageFlags)) {
            rollback_mappings(mapped_pages);
            return FramebufferInitResult::MappingFailed;
        }

        ++mapped_pages;
    }

    g_framebuffer.data =
        reinterpret_cast<volatile uint8_t*>(
            kFramebufferVirtualBase + layout.offset);

    g_framebuffer.width =
        boot_info.framebuffer.width;

    g_framebuffer.height =
        boot_info.framebuffer.height;

    g_framebuffer.pitch =
        boot_info.framebuffer.pitch;

    g_framebuffer.format = PixelFormat{
        boot_info.framebuffer.red_mask_size,
        boot_info.framebuffer.red_mask_shift,
        boot_info.framebuffer.green_mask_size,
        boot_info.framebuffer.green_mask_shift,
        boot_info.framebuffer.blue_mask_size,
        boot_info.framebuffer.blue_mask_shift,
    };

    g_framebuffer_ready = true;

    return FramebufferInitResult::Ready;
}

const Framebuffer* framebuffer()
{
    if (!g_framebuffer_ready) {
        return nullptr;
    }

    return &g_framebuffer;
}

} // namespace linux95::graphics
