#include "gui/terminal_app.hpp"
#include "gui/editor_file.hpp"

#include "arch/debug.hpp"
#include "graphics/renderer.hpp"
#include "net/network.hpp"
#include "net/dns.hpp"
#include "terminal/shell.hpp"
#include "terminal/key_event_adapter.hpp"

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
constexpr size_t kEditorMaxColumns = TerminalModel::kColumns;
uint8_t g_editor_storage[editor::kTextCapacity];
#if defined(LINUX95_QEMU_EDITOR_SELF_TEST)
bool g_editor_test_waiting_for_shell_command = false;
#endif

void append_text(char* output, size_t capacity, size_t& length, const char* text)
{
    if (text == nullptr) return;
    size_t index = 0;
    while (text[index] != '\0') {
        if (length + 1 >= capacity) return;
        output[length++] = text[index++];
    }
}

void append_unsigned(char* output, size_t capacity, size_t& length, size_t value)
{
    char reversed[24];
    size_t count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(reversed));
    while (count != 0 && length + 1 < capacity) output[length++] = reversed[--count];
}

void draw_clipped_text(
    graphics::Framebuffer& framebuffer,
    graphics::Rect content,
    size_t row,
    size_t column,
    size_t columns,
    const char* text,
    graphics::Color color)
{
    if (text == nullptr) return;
    size_t index = 0;
    while (text[index] != '\0' && column + index < columns) {
        char cell[2] = {text[index], '\0'};
        graphics::draw_text(
            framebuffer,
            content.x + static_cast<int32_t>((column + index) * kCharacterWidth),
            content.y + static_cast<int32_t>(row * kCharacterHeight),
            cell,
            color);
        ++index;
    }
}

const char* open_error(filesystem::Status status)
{
    switch (status) {
    case filesystem::Status::NotFound: return "not found";
    case filesystem::Status::NotDirectory: return "parent is not a directory";
    case filesystem::Status::IsDirectory: return "is a directory";
    case filesystem::Status::InvalidName: return "invalid 8.3 path";
    case filesystem::Status::Unsupported: return "unsupported text or file too large";
    case filesystem::Status::NotMounted: return "filesystem not mounted";
    case filesystem::Status::IoError: return "I/O error";
    case filesystem::Status::Corrupt: return "filesystem corrupt";
    default: return "unable to open file";
    }
}

network::Status network_status(void*)
{
    return network::status();
}

bool network_start_ping(void*, net::Ipv4Address destination)
{
    return network::start_ping(destination);
}

net::icmp::PingResult network_ping_result(void*)
{
    return network::ping_result();
}

void network_clear_ping_result(void*)
{
    network::clear_ping_result();
}

net::dns::Status dns_begin_lookup(void*, const char* hostname)
{
    return net::dns::begin_lookup(hostname);
}

net::dns::Status dns_lookup_status(void*)
{
    return net::dns::lookup_status();
}

size_t dns_result_count(void*)
{
    return net::dns::result_count();
}

bool dns_result_address(void*, size_t index, net::Ipv4Address& out)
{
    return net::dns::result_address(index, out);
}

net::Ipv4Address dns_server(void*)
{
    return net::dns::server();
}

net::dns::Status dns_set_server(void*, const net::Ipv4Address& address)
{
    return net::dns::set_server(address);
}

shell::CommandResult execute_shell_command(
    void*,
    terminal::Output& output,
    char* command)
{
    return shell::execute_command(
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
      network_callbacks_{
          nullptr,
          network_status,
          network_start_ping,
          network_ping_result,
          network_clear_ping_result,
      },
      dns_callbacks_{
          nullptr,
          dns_begin_lookup,
          dns_lookup_status,
          dns_result_count,
          dns_result_address,
          dns_server,
          dns_set_server,
      },
      session_(
          output_,
          nullptr,
          execute_shell_command,
          &network_callbacks_,
          &dns_callbacks_),
      editor_(g_editor_storage, sizeof(g_editor_storage)),
      mode_(Mode::Shell)
{
    session_.begin();
}

bool TerminalApp::poll()
{
    return mode_ == Mode::Shell ? session_.poll() : false;
}

TerminalApp::Mode TerminalApp::mode() const { return mode_; }

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

    if (mode_ == Mode::Editor) {
        draw_editor(framebuffer, clipped);
        return;
    }

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
    const keyboard::KeyEvent& event)
{
    if (mode_ == Mode::Editor) {
        const editor::EditorAction action = editor_.handle_key(event);
        if (!event.pressed) return;
        if (action == editor::EditorAction::Save) {
            const filesystem::Status status = editor::save_file(editor_);
#if defined(LINUX95_QEMU_EDITOR_SELF_TEST)
            if (status == filesystem::Status::Ok) {
                debug::write("[PASS] editor saved\n");
            }
#else
            static_cast<void>(status);
#endif
        } else if (action == editor::EditorAction::Quit) {
            mode_ = Mode::Shell;
            session_.resume_prompt();
#if defined(LINUX95_QEMU_EDITOR_SELF_TEST)
            debug::write("[PASS] editor returned to shell\n");
            g_editor_test_waiting_for_shell_command = true;
#endif
        }
        return;
    }

    char c = 0;
    if (!terminal::shell_character_for_key(event, c)) return;
    const shell::CommandResult result = session_.on_char(c);
    if (result.action == shell::CommandAction::OpenEditor) {
        const filesystem::Status status = editor::open_file(editor_, result.path);
        if (status == filesystem::Status::Ok) {
            mode_ = Mode::Editor;
#if defined(LINUX95_QEMU_EDITOR_SELF_TEST)
            debug::write("[PASS] editor opened\n");
#endif
            return;
        }
        terminal::write(output_, "edit: ");
        terminal::write(output_, open_error(status));
        terminal::write(output_, "\n");
        session_.resume_prompt();
        return;
    }
#if defined(LINUX95_QEMU_EDITOR_SELF_TEST)
    if (g_editor_test_waiting_for_shell_command && c == '\n') {
        debug::write("[PASS] shell accepted input\n");
        g_editor_test_waiting_for_shell_command = false;
    }
#endif
}

void TerminalApp::draw_editor(
    graphics::Framebuffer& framebuffer,
    graphics::Rect content)
{
    const size_t total_rows = static_cast<size_t>(content.height / kCharacterHeight);
    const size_t columns = smaller(
        static_cast<size_t>(content.width / kCharacterWidth),
        kEditorMaxColumns);
    if (total_rows == 0 || columns == 0) return;

    const size_t text_rows = total_rows > 3 ? total_rows - 3 : 0;
    editor_.update_viewport(text_rows, columns);

    char header[kEditorMaxColumns + 1];
    size_t header_length = 0;
    append_text(header, sizeof(header), header_length, "Linux95 Editor - ");
    append_text(header, sizeof(header), header_length, editor_.path());
    if (editor_.modified()) append_text(header, sizeof(header), header_length, " * Modified");
    header[header_length] = '\0';
    draw_clipped_text(framebuffer, content, 0, 0, columns, header, kTerminalForeground);

    const size_t first_line = editor_.viewport_top_line();
    for (size_t row = 0; row < text_rows; ++row) {
        size_t begin = 0;
        size_t end = 0;
        if (!editor_.line_bounds(first_line + row, begin, end)) break;
        const size_t left = editor_.viewport_left_column();
        const size_t line_length = end - begin;
        if (left >= line_length) continue;
        const size_t amount = smaller(line_length - left, columns);
        for (size_t col = 0; col < amount; ++col) {
            char cell[2] = {static_cast<char>(editor_.data()[begin + left + col]), '\0'};
            graphics::draw_text(
                framebuffer,
                content.x + static_cast<int32_t>(col * kCharacterWidth),
                content.y + static_cast<int32_t>((row + 1) * kCharacterHeight),
                cell,
                kTerminalForeground);
        }
    }

    if (total_rows >= 2) {
        char state[kEditorMaxColumns + 1];
        size_t length = 0;
        append_text(state, sizeof(state), length, "Ln ");
        append_unsigned(state, sizeof(state), length, editor_.line_number());
        append_text(state, sizeof(state), length, ", Col ");
        append_unsigned(state, sizeof(state), length, editor_.column_number());
        if (editor_.status_message()[0] != '\0') {
            append_text(state, sizeof(state), length, "  ");
            append_text(state, sizeof(state), length, editor_.status_message());
        }
        state[length] = '\0';
        draw_clipped_text(framebuffer, content, total_rows - 2, 0, columns, state,
            graphics::Color{170, 190, 205});
    }
    if (total_rows >= 1) {
        draw_clipped_text(framebuffer, content, total_rows - 1, 0, columns,
            "^S Save    ^Q Quit", graphics::Color{170, 190, 205});
    }

    if (text_rows != 0) {
        const size_t cursor_line = editor_.line_number() - 1;
        const size_t cursor_column = editor_.column_number() - 1;
        if (cursor_line >= first_line && cursor_line - first_line < text_rows &&
            cursor_column >= editor_.viewport_left_column() &&
            cursor_column - editor_.viewport_left_column() < columns) {
            graphics::draw_rect(
                framebuffer,
                {content.x + static_cast<int32_t>((cursor_column - editor_.viewport_left_column()) * kCharacterWidth),
                 content.y + static_cast<int32_t>((cursor_line - first_line + 1) * kCharacterHeight),
                 kCharacterWidth, kCharacterHeight},
                graphics::Color{255, 190, 80});
        }
    }
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
    const keyboard::KeyEvent& event)
{
    if (context == nullptr) {
        return;
    }

    static_cast<TerminalApp*>(
        context)->on_key(event);
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
