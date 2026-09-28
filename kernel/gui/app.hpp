#pragma once

#include "arch/keyboard.hpp"
#include "graphics/renderer.hpp"

namespace linux95::gui {

struct AppMouseEvent {
    int32_t x;
    int32_t y;
    bool left_pressed;
    bool left_released;
    bool left_down;
};

struct AppCallbacks {
    void (*draw)(
        void* context,
        graphics::Framebuffer& framebuffer,
        graphics::Rect content);

    void (*on_key)(
        void* context,
        const keyboard::KeyEvent& event);

    void (*on_close)(
        void* context);

    void (*on_mouse)(
        void* context,
        const AppMouseEvent& event);
};

struct AppInstance {
    void* context;
    AppCallbacks callbacks;
};

} // namespace linux95::gui
