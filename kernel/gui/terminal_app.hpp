#pragma once

#include "gui/app.hpp"
#include "gui/editor_model.hpp"
#include "gui/terminal_model.hpp"
#include "terminal/output.hpp"
#include "terminal/shell_session.hpp"

namespace linux95::gui {

class TerminalApp {
public:
    enum class Mode : uint8_t {
        Shell,
        Editor,
    };

    TerminalApp();

    AppInstance instance();
    bool poll();
    Mode mode() const;

private:
    TerminalModel model_;
    terminal::Output output_;
    terminal::NetworkCallbacks network_callbacks_;
    terminal::DnsCallbacks dns_callbacks_;
    terminal::ShellSession session_;
    editor::EditorModel editor_;
    Mode mode_;

    void draw(
        graphics::Framebuffer& framebuffer,
        graphics::Rect content);
    void draw_editor(
        graphics::Framebuffer& framebuffer,
        graphics::Rect content);

    void on_key(const keyboard::KeyEvent& event);
    void on_close();

    static void draw_callback(
        void* context,
        graphics::Framebuffer& framebuffer,
        graphics::Rect content);

    static void key_callback(
        void* context,
        const keyboard::KeyEvent& event);

    static void close_callback(
        void* context);
};

} // namespace linux95::gui
