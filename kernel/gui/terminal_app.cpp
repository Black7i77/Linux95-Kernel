#include "gui/terminal_app.hpp"

#include "graphics/renderer.hpp"
#include "terminal/shell.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::gui {
namespace {

constexpr graphics::Color kTerminalBackground{
    12,
    16,
    22,
};

constexpr graphics::Color kTerminalForeground{
    220,
    226,
    232,
};

constexpr int32_t kCharacterWidth = 8;
constexpr int32_t kCharacterHeight = 8;

void execute_shell_command(
    void*,
    terminal::Output& output,
    char* command)
{
    shell::execute_command(
        output,
        command);
}

size_t smaller(
    size_t a,
    size_t b)
{
    return a < b ? a : b;
}

} // namespace

TerminalApp::TerminalApp()
    : model_{},
      output_(make_output(model_)),
      session_(
          output_,
          nullptr,
          execute_shell_command)
{
    session_.begin();
}

AppInstance TerminalApp::instance()
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

void TerminalApp::draw(
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
        kTerminalBackground);

    const size_t visible_rows =
        static_cast<size_t>(
            clipped.height /
            kCharacterHeight);

    const size_t visible_columns =
        static_cast<size_t>(
            clipped.width /
            kCharacterWidth);

    if (
        visible_rows == 0 ||
        visible_columns == 0) {
        return;
    }

    const size_t count =
        model_.line_count();

    const size_t rows_to_draw =
        smaller(
            count,
            visible_rows);

    const size_t first_line =
        count - rows_to_draw;

    for (
        size_t row = 0;
        row < rows_to_draw;
        ++row) {
        const char* source =
            model_.line(
                first_line + row);

        if (source == nullptr) {
            continue;
        }

        char text[
            TerminalModel::kColumns + 1];

        size_t length = 0;

        const size_t max_columns =
            smaller(
                visible_columns,
                TerminalModel::kColumns);

        while (
            source[length] != '\0' &&
            length < max_columns) {
            text[length] =
                source[length];

            ++length;
        }

        text[length] = '\0';

        graphics::draw_text(
            framebuffer,
            clipped.x,
            clipped.y +
                static_cast<int32_t>(row) *
                    kCharacterHeight,
            text,
            kTerminalForeground);
    }
}

void TerminalApp::on_key(
    char c)
{
    session_.on_char(c);
}

void TerminalApp::on_close()
{
}

void TerminalApp::draw_callback(
    void* context,
    graphics::Framebuffer& framebuffer,
    graphics::Rect content)
{
    if (context == nullptr) {
        return;
    }

    static_cast<TerminalApp*>(
        context)->draw(
            framebuffer,
            content);
}

void TerminalApp::key_callback(
    void* context,
    char c)
{
    if (context == nullptr) {
        return;
    }

    static_cast<TerminalApp*>(
        context)->on_key(c);
}

void TerminalApp::close_callback(
    void* context)
{
    if (context == nullptr) {
        return;
    }

    static_cast<TerminalApp*>(
        context)->on_close();
}

} // namespace linux95::gui
