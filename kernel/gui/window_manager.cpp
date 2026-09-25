#include "gui/window_manager.hpp"

namespace linux95::gui {
namespace {

int32_t clamp_value(
    int32_t value,
    int32_t minimum,
    int32_t maximum)
{
    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}

} // namespace

Rect WindowManager::clamp_bounds(
    Rect bounds)
{
    bounds.width =
        clamp_value(
            bounds.width,
            kMinWidth,
            kScreenWidth);

    bounds.height =
        clamp_value(
            bounds.height,
            kMinHeight,
            kScreenHeight - kPanelHeight);

    const int32_t max_x =
        kScreenWidth - bounds.width;

    const int32_t max_y =
        kScreenHeight - bounds.height;

    bounds.x =
        clamp_value(
            bounds.x,
            0,
            max_x);

    bounds.y =
        clamp_value(
            bounds.y,
            kPanelHeight,
            max_y);

    return bounds;
}

WindowManager::WindowManager()
    : focused_(0),
      pointer_action_(PointerAction::None),
      pointer_window_(0),
      pointer_start_{0, 0},
      pointer_start_bounds_{0, 0, 0, 0}
{
    for (size_t i = 0;
         i < kMaxWindows;
         ++i) {
        windows_[i].state =
            WindowState::Closed;
    }
}

Window* WindowManager::find_mutable(
    WindowId id)
{
    if (id == 0) {
        return nullptr;
    }

    for (size_t i = 0;
         i < kMaxWindows;
         ++i) {
        if (
            windows_[i].state !=
                WindowState::Closed &&
            windows_[i].id == id) {
            return &windows_[i];
        }
    }

    return nullptr;
}

const Window* WindowManager::find(
    WindowId id) const
{
    if (id == 0) {
        return nullptr;
    }

    for (size_t i = 0;
         i < kMaxWindows;
         ++i) {
        if (
            windows_[i].state !=
                WindowState::Closed &&
            windows_[i].id == id) {
            return &windows_[i];
        }
    }

    return nullptr;
}

void WindowManager::compact_z()
{
    uint8_t next_z = 0;

    for (;;) {
        Window* lowest = nullptr;

        for (size_t i = 0;
             i < kMaxWindows;
             ++i) {
            Window& candidate =
                windows_[i];

            if (
                candidate.state ==
                    WindowState::Closed ||
                candidate.z < next_z) {
                continue;
            }

            if (
                lowest == nullptr ||
                candidate.z < lowest->z) {
                lowest = &candidate;
            }
        }

        if (lowest == nullptr) {
            break;
        }

        lowest->z = next_z;
        ++next_z;
    }
}

void WindowManager::raise(
    WindowId id)
{
    Window* target =
        find_mutable(id);

    if (target == nullptr) {
        return;
    }

    compact_z();

    uint8_t top_z = 0;
    bool found_other = false;

    for (size_t i = 0;
         i < kMaxWindows;
         ++i) {
        const Window& window =
            windows_[i];

        if (
            window.state ==
                WindowState::Closed ||
            window.id == id) {
            continue;
        }

        if (
            !found_other ||
            window.z > top_z) {
            top_z = window.z;
            found_other = true;
        }
    }

    const uint8_t old_z =
        target->z;

    for (size_t i = 0;
         i < kMaxWindows;
         ++i) {
        Window& window =
            windows_[i];

        if (
            window.state ==
                WindowState::Closed ||
            window.id == id) {
            continue;
        }

        if (window.z > old_z) {
            --window.z;
        }
    }

    target->z =
        found_other
            ? top_z
            : 0;

    compact_z();

    uint8_t highest = 0;

    for (size_t i = 0;
         i < kMaxWindows;
         ++i) {
        const Window& window =
            windows_[i];

        if (
            window.state ==
                WindowState::Closed ||
            window.id == id) {
            continue;
        }

        if (window.z >= highest) {
            highest =
                static_cast<uint8_t>(
                    window.z + 1);
        }
    }

    target->z = highest;
}

bool WindowManager::add_window(
    WindowId id,
    Rect bounds,
    bool resizable,
    bool closable)
{
    if (
        id == 0 ||
        find(id) != nullptr) {
        return false;
    }

    Window* slot = nullptr;
    size_t active_count = 0;

    for (size_t i = 0;
         i < kMaxWindows;
         ++i) {
        if (
            windows_[i].state ==
            WindowState::Closed) {
            if (slot == nullptr) {
                slot = &windows_[i];
            }

            continue;
        }

        ++active_count;
    }

    if (
        slot == nullptr ||
        active_count >= kMaxWindows) {
        return false;
    }

    bounds =
        clamp_bounds(bounds);

    *slot = Window{
        id,
        bounds,
        bounds,
        WindowState::Open,
        static_cast<uint8_t>(
            active_count),
        resizable,
        closable,
    };

    focused_ = id;
    raise(id);

    return true;
}

WindowId WindowManager::focused() const
{
    return focused_;
}

WindowId WindowManager::hit_test(
    Point point) const
{
    const Window* best = nullptr;

    for (size_t i = 0;
         i < kMaxWindows;
         ++i) {
        const Window& window =
            windows_[i];

        if (
            window.state !=
                WindowState::Open ||
            !contains(
                window.bounds,
                point)) {
            continue;
        }

        if (
            best == nullptr ||
            window.z > best->z) {
            best = &window;
        }
    }

    return
        best != nullptr
            ? best->id
            : 0;
}

bool WindowManager::focus(
    WindowId id)
{
    Window* window =
        find_mutable(id);

    if (
        window == nullptr ||
        window->state !=
            WindowState::Open) {
        return false;
    }

    focused_ = id;
    raise(id);

    return true;
}

bool WindowManager::begin_drag(
    WindowId id,
    Point point)
{
    Window* window =
        find_mutable(id);

    if (
        window == nullptr ||
        window->state !=
            WindowState::Open) {
        return false;
    }

    focus(id);

    pointer_action_ =
        PointerAction::Drag;

    pointer_window_ = id;
    pointer_start_ = point;

    pointer_start_bounds_ =
        window->bounds;

    return true;
}

bool WindowManager::begin_resize(
    WindowId id,
    Point point)
{
    Window* window =
        find_mutable(id);

    if (
        window == nullptr ||
        window->state !=
            WindowState::Open ||
        !window->resizable) {
        return false;
    }

    focus(id);

    pointer_action_ =
        PointerAction::Resize;

    pointer_window_ = id;
    pointer_start_ = point;

    pointer_start_bounds_ =
        window->bounds;

    return true;
}

void WindowManager::pointer_move(
    Point point)
{
    Window* window =
        find_mutable(
            pointer_window_);

    if (
        window == nullptr ||
        window->state !=
            WindowState::Open) {
        end_pointer_action();
        return;
    }

    const int32_t dx =
        point.x - pointer_start_.x;

    const int32_t dy =
        point.y - pointer_start_.y;

    if (
        pointer_action_ ==
        PointerAction::Drag) {
        Rect bounds =
            pointer_start_bounds_;

        bounds.x += dx;
        bounds.y += dy;

        const int32_t max_x =
            kScreenWidth -
            bounds.width;

        const int32_t max_y =
            kScreenHeight -
            bounds.height;

        bounds.x =
            clamp_value(
                bounds.x,
                0,
                max_x);

        bounds.y =
            clamp_value(
                bounds.y,
                kPanelHeight,
                max_y);

        window->bounds = bounds;
        return;
    }

    if (
        pointer_action_ ==
        PointerAction::Resize) {
        int32_t width =
            pointer_start_bounds_.width +
            dx;

        int32_t height =
            pointer_start_bounds_.height +
            dy;

        const int32_t max_width =
            kScreenWidth -
            pointer_start_bounds_.x;

        const int32_t max_height =
            kScreenHeight -
            pointer_start_bounds_.y;

        width =
            clamp_value(
                width,
                kMinWidth,
                max_width);

        height =
            clamp_value(
                height,
                kMinHeight,
                max_height);

        window->bounds.width = width;
        window->bounds.height = height;
    }
}

void WindowManager::end_pointer_action()
{
    pointer_action_ =
        PointerAction::None;

    pointer_window_ = 0;
}

WindowId WindowManager::highest_open_window() const
{
    const Window* best = nullptr;

    for (size_t i = 0;
         i < kMaxWindows;
         ++i) {
        const Window& window =
            windows_[i];

        if (
            window.state !=
            WindowState::Open) {
            continue;
        }

        if (
            best == nullptr ||
            window.z > best->z) {
            best = &window;
        }
    }

    return
        best != nullptr
            ? best->id
            : 0;
}

bool WindowManager::minimize(
    WindowId id)
{
    Window* window =
        find_mutable(id);

    if (
        window == nullptr ||
        window->state !=
            WindowState::Open) {
        return false;
    }

    window->restore_bounds =
        window->bounds;

    window->state =
        WindowState::Minimized;

    if (pointer_window_ == id) {
        end_pointer_action();
    }

    if (focused_ == id) {
        focused_ =
            highest_open_window();
    }

    return true;
}

bool WindowManager::restore(
    WindowId id)
{
    Window* window =
        find_mutable(id);

    if (
        window == nullptr ||
        window->state !=
            WindowState::Minimized) {
        return false;
    }

    window->bounds =
        window->restore_bounds;

    window->state =
        WindowState::Open;

    return focus(id);
}

bool WindowManager::close(
    WindowId id)
{
    Window* window =
        find_mutable(id);

    if (
        window == nullptr ||
        !window->closable) {
        return false;
    }

    const bool was_focused =
        focused_ == id;

    window->state =
        WindowState::Closed;

    window->id = 0;

    if (pointer_window_ == id) {
        end_pointer_action();
    }

    compact_z();

    if (was_focused) {
        focused_ =
            highest_open_window();
    }

    return true;
}

} // namespace linux95::gui
