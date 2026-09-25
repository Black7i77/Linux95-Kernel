#include "gui/system_info_app.hpp"

#include "arch/pit.hpp"
#include "filesystem/filesystem.hpp"
#include "graphics/renderer.hpp"
#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "memory/physical.hpp"
#include "storage/disk.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::gui {
namespace {

constexpr graphics::Color kBackground{
    192,
    192,
    192,
};

constexpr graphics::Color kForeground{
    0,
    0,
    0,
};

constexpr int32_t kCharacterWidth = 8;
constexpr int32_t kCharacterHeight = 8;
constexpr int32_t kLineAdvance = 12;

void format_u64(
    uint64_t value,
    char* out,
    size_t capacity)
{
    if (capacity == 0) {
        return;
    }

    char reversed[21];
    size_t count = 0;

    do {
        reversed[count++] =
            static_cast<char>(
                '0' + (value % 10));

        value /= 10;
    } while (
        value != 0 &&
        count < sizeof(reversed));

    size_t out_count = 0;

    while (
        count > 0 &&
        out_count + 1 < capacity) {
        out[out_count++] =
            reversed[--count];
    }

    out[out_count] = '\0';
}

void format_value_line(
    const char* prefix,
    uint64_t value,
    char* out,
    size_t capacity)
{
    if (capacity == 0) {
        return;
    }

    size_t position = 0;

    while (
        prefix[position] != '\0' &&
        position + 1 < capacity) {
        out[position] =
            prefix[position];

        ++position;
    }

    char number[21];

    format_u64(
        value,
        number,
        sizeof(number));

    size_t number_index = 0;

    while (
        number[number_index] != '\0' &&
        position + 1 < capacity) {
        out[position++] =
            number[number_index++];
    }

    out[position] = '\0';
}

void draw_clipped_text(
    graphics::Framebuffer& framebuffer,
    graphics::Rect content,
    int32_t y,
    const char* text)
{
    if (
        content.width <= 0 ||
        content.height <= 0) {
        return;
    }

    if (
        y < content.y ||
        y + kCharacterHeight >
            content.y + content.height) {
        return;
    }

    const size_t visible_columns =
        static_cast<size_t>(
            content.width /
            kCharacterWidth);

    if (visible_columns == 0) {
        return;
    }

    char clipped_text[96];

    size_t limit =
        visible_columns;

    if (
        limit >=
        sizeof(clipped_text)) {
        limit =
            sizeof(clipped_text) - 1;
    }

    size_t length = 0;

    while (
        text[length] != '\0' &&
        length < limit) {
        clipped_text[length] =
            text[length];

        ++length;
    }

    clipped_text[length] = '\0';

    graphics::draw_text(
        framebuffer,
        content.x,
        y,
        clipped_text,
        kForeground);
}

} // namespace

AppInstance SystemInfoApp::instance()
{
    return AppInstance{
        this,
        AppCallbacks{
            draw_callback,
            key_callback,
            close_callback,
        },
    };
}

void SystemInfoApp::draw(
    graphics::Framebuffer& framebuffer,
    graphics::Rect content)
{
    const graphics::Rect clipped =
        graphics::clip_rect(
            content,
            static_cast<int32_t>(
                framebuffer.width),
            static_cast<int32_t>(
                framebuffer.height));

    if (
        clipped.width <= 0 ||
        clipped.height <= 0) {
        return;
    }

    graphics::fill_rect(
        framebuffer,
        clipped,
        kBackground);

    int32_t y = clipped.y;
    char line[96];

    const auto draw_literal =
        [&](
            const char* text) {
            draw_clipped_text(
                framebuffer,
                clipped,
                y,
                text);

            y += kLineAdvance;
        };

    draw_literal(
        "Linux95 Kernel v1.0");

    draw_literal(
        "Architecture: x86_64");

    format_value_line(
        "Uptime seconds: ",
        pit::uptime_seconds(),
        line,
        sizeof(line));

    draw_literal(line);

    format_value_line(
        "Total RAM MiB: ",
        memory::total_bytes() /
            (1024ULL * 1024ULL),
        line,
        sizeof(line));

    draw_literal(line);

    format_value_line(
        "Usable RAM MiB: ",
        memory::usable_bytes() /
            (1024ULL * 1024ULL),
        line,
        sizeof(line));

    draw_literal(line);

    format_value_line(
        "Physical total pages: ",
        memory::physical::total_pages(),
        line,
        sizeof(line));

    draw_literal(line);

    format_value_line(
        "Physical used pages: ",
        memory::physical::used_pages(),
        line,
        sizeof(line));

    draw_literal(line);

    format_value_line(
        "Physical free pages: ",
        memory::physical::free_pages(),
        line,
        sizeof(line));

    draw_literal(line);

    format_value_line(
        "Heap used KiB: ",
        heap::used_bytes() / 1024ULL,
        line,
        sizeof(line));

    draw_literal(line);

    format_value_line(
        "Heap capacity KiB: ",
        heap::capacity_bytes() / 1024ULL,
        line,
        sizeof(line));

    draw_literal(line);

    draw_literal(
        storage::info(
            storage::DiskId::Boot).present
            ? "Boot disk present: yes"
            : "Boot disk present: no");

    draw_literal(
        storage::info(
            storage::DiskId::Test).present
            ? "Test disk present: yes"
            : "Test disk present: no");

    draw_literal(
        filesystem::volume_info().mounted
            ? "FAT32 mounted: yes"
            : "FAT32 mounted: no");

    draw_literal(
        "Filesystem: read-only");
}

void SystemInfoApp::on_key(
    char)
{
}

void SystemInfoApp::on_close()
{
}

void SystemInfoApp::draw_callback(
    void* context,
    graphics::Framebuffer& framebuffer,
    graphics::Rect content)
{
    if (context == nullptr) {
        return;
    }

    static_cast<SystemInfoApp*>(
        context)->draw(
            framebuffer,
            content);
}

void SystemInfoApp::key_callback(
    void* context,
    char c)
{
    if (context == nullptr) {
        return;
    }

    static_cast<SystemInfoApp*>(
        context)->on_key(c);
}

void SystemInfoApp::close_callback(
    void* context)
{
    if (context == nullptr) {
        return;
    }

    static_cast<SystemInfoApp*>(
        context)->on_close();
}

} // namespace linux95::gui
