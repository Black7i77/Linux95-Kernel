#pragma once

#include "gui/geometry.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::gui {

using WindowId = uint32_t;

enum class WindowState : uint8_t {
    Open,
    Minimized,
    Closed,
};

struct Window {
    WindowId id = 0;
    Rect bounds{};
    Rect restore_bounds{};
    WindowState state = WindowState::Closed;
    uint8_t z = 0;
    bool resizable = false;
    bool closable = false;
};

class WindowManager {
public:
    static constexpr size_t kMaxWindows = 8;

    static constexpr int32_t kScreenWidth = 1280;
    static constexpr int32_t kScreenHeight = 720;

    static constexpr int32_t kPanelHeight = 28;
    static constexpr int32_t kMinWidth = 160;
    static constexpr int32_t kMinHeight = 100;

    bool add_window(
        WindowId id,
        Rect bounds,
        bool resizable,
        bool closable);

    WindowId focused() const;
    WindowId hit_test(Point point) const;

    bool focus(WindowId id);

    bool begin_drag(
        WindowId id,
        Point point);

    bool begin_resize(
        WindowId id,
        Point point);

    void pointer_move(Point point);
    void end_pointer_action();

    bool minimize(WindowId id);
    bool restore(WindowId id);
    bool close(WindowId id);

    const Window* find(WindowId id) const;

private:
    enum class PointerAction : uint8_t {
        None,
        Drag,
        Resize,
    };

    Window windows_[kMaxWindows]{};

    WindowId focused_ = 0;

    PointerAction pointer_action_ =
        PointerAction::None;

    WindowId pointer_window_ = 0;

    Point pointer_start_{};
    Rect pointer_start_bounds_{};

    Window* find_mutable(WindowId id);

    void raise(WindowId id);
    void compact_z();

    WindowId highest_open_window() const;

    static Rect clamp_bounds(Rect bounds);
};

} // namespace linux95::gui
