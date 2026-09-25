#pragma once

#include "graphics/framebuffer.hpp"
#include "graphics/renderer.hpp"
#include "gui/app.hpp"
#include "gui/geometry.hpp"
#include "gui/window_manager.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::desktop {

constexpr int32_t kScreenWidth = 1280;
constexpr int32_t kScreenHeight = 720;
constexpr int32_t kPanelHeight = 28;

constexpr int32_t kCursorWidth = 12;
constexpr int32_t kCursorHeight = 18;

constexpr size_t kMaxDirtyRects = 32;

constexpr gui::WindowId kTerminalWindowId = 1;
constexpr gui::WindowId kSystemInfoWindowId = 2;

enum class PanelHit : uint8_t {
    None,
    Applications,
    Terminal,
    SystemInfo,
    TaskArea,
    Status,
};

enum class ChromeHit : uint8_t {
    None,
    Close,
    Minimize,
    Resize,
    TitleBar,
    Content,
};

constexpr int32_t kTitleBarHeight = 24;
constexpr int32_t kChromeButtonWidth = 20;
constexpr int32_t kChromeButtonHeight = 18;
constexpr int32_t kResizeGrip = 8;

PanelHit panel_hit(gui::Point point);

gui::Point clamp_cursor(gui::Point point);

graphics::Rect cursor_rect_at(
    gui::Point point);

gui::Rect terminal_default_bounds();

gui::Rect system_info_default_bounds();

bool initialize_default_windows(
    gui::WindowManager& windows);

bool activate_window(
    gui::WindowManager& windows,
    gui::WindowId id);

bool route_key(
    const gui::WindowManager& windows,
    gui::AppInstance& terminal,
    gui::AppInstance& system_info,
    char c);

gui::Rect content_rect(
    gui::Rect bounds);

ChromeHit chrome_hit(
    gui::Rect bounds,
    gui::Point point,
    bool resizable,
    bool closable);

class DirtyRegionQueue {
public:
    DirtyRegionQueue();

    void invalidate(
        graphics::Rect rect);

    void clear();

    size_t size() const;

    graphics::Rect rect(
        size_t index) const;

private:
    graphics::Rect rects_[
        kMaxDirtyRects];

    size_t count_;
};

[[noreturn]] void run(
    graphics::Framebuffer& framebuffer,
    bool mouse_online);

} // namespace linux95::desktop
