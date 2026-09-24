#pragma once

#include "gui/app.hpp"

namespace linux95::gui {

class SystemInfoApp {
public:
    AppInstance instance();

private:
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
