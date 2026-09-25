#pragma once

#include "graphics/renderer.hpp"

namespace linux95::gui {

struct AppCallbacks {
    void (*draw)(
        void* context,
        graphics::Framebuffer& framebuffer,
        graphics::Rect content);

    void (*on_key)(
        void* context,
        char c);

    void (*on_close)(
        void* context);
};

struct AppInstance {
    void* context;
    AppCallbacks callbacks;
};

} // namespace linux95::gui
