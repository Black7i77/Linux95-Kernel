#pragma once

#include "gui/app.hpp"
#include "gui/terminal_model.hpp"
#include "terminal/output.hpp"
#include "terminal/shell_session.hpp"

namespace linux95::gui {

class TerminalApp {
public:
    TerminalApp();

    AppInstance instance();
    bool poll();

private:
    TerminalModel model_;
    terminal::Output output_;
    terminal::NetworkCallbacks network_callbacks_;
    terminal::ShellSession session_;

    void draw(
        graphics::Framebuffer& framebuffer,
        graphics::Rect content);

    void on_key(char c);
    void on_close();

    static void draw_callback(
        void* context,
        graphics::Framebuffer& framebuffer,
        graphics::Rect content);

    static void key_callback(
        void* context,
        char c);

    static void close_callback(
        void* context);
};

} // namespace linux95::gui
