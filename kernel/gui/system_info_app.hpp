#pragma once

#include "arch/keyboard.hpp"
#include "gui/app.hpp"

namespace linux95::gui {

class SystemInfoApp {
public:
    AppInstance instance();

private:
    void draw(
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
