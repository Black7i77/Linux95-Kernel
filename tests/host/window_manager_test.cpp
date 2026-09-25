#include "gui/window_manager.hpp"

#include <assert.h>

using namespace linux95::gui;

namespace {

void test_first_window_becomes_focused()
{
    WindowManager manager;

    assert(
        manager.add_window(
            1,
            Rect{100, 100, 400, 300},
            true,
            true));

    assert(manager.focused() == 1);
}

void test_focus_raises_lower_window()
{
    WindowManager manager;

    assert(manager.add_window(
        1,
        Rect{100, 100, 400, 300},
        true,
        true));

    assert(manager.add_window(
        2,
        Rect{150, 150, 400, 300},
        true,
        true));

    // Window 2 starts above window 1.
    assert(manager.hit_test(Point{200, 200}) == 2);

    assert(manager.focus(1));
    assert(manager.focused() == 1);

    // Focusing the lower window raises it.
    assert(manager.hit_test(Point{200, 200}) == 1);

    const Window* first = manager.find(1);
    const Window* second = manager.find(2);

    assert(first != nullptr);
    assert(second != nullptr);
    assert(first->z > second->z);
}

void test_drag_changes_position()
{
    WindowManager manager;

    assert(manager.add_window(
        1,
        Rect{100, 100, 300, 200},
        true,
        true));

    assert(
        manager.begin_drag(
            1,
            Point{120, 110}));

    manager.pointer_move(
        Point{220, 160});

    manager.end_pointer_action();

    const Window* window =
        manager.find(1);

    assert(window != nullptr);
    assert(window->bounds.x == 200);
    assert(window->bounds.y == 150);
}

void test_drag_clamps_to_work_area()
{
    WindowManager manager;

    assert(manager.add_window(
        1,
        Rect{100, 100, 300, 200},
        true,
        true));

    assert(
        manager.begin_drag(
            1,
            Point{120, 110}));

    manager.pointer_move(
        Point{-1000, -1000});

    const Window* window =
        manager.find(1);

    assert(window != nullptr);
    assert(window->bounds.x == 0);
    assert(
        window->bounds.y ==
        WindowManager::kPanelHeight);

    manager.pointer_move(
        Point{5000, 5000});

    window = manager.find(1);

    assert(window != nullptr);
    assert(
        window->bounds.x ==
        1280 - window->bounds.width);

    assert(
        window->bounds.y ==
        720 - window->bounds.height);

    manager.end_pointer_action();
}

void test_resize_honors_minimum_size()
{
    WindowManager manager;

    assert(manager.add_window(
        1,
        Rect{100, 100, 400, 300},
        true,
        true));

    assert(
        manager.begin_resize(
            1,
            Point{500, 400}));

    manager.pointer_move(
        Point{110, 110});

    manager.end_pointer_action();

    const Window* window =
        manager.find(1);

    assert(window != nullptr);

    assert(
        window->bounds.width ==
        WindowManager::kMinWidth);

    assert(
        window->bounds.height ==
        WindowManager::kMinHeight);
}

void test_resize_clamps_to_screen_edges()
{
    WindowManager manager;

    assert(manager.add_window(
        1,
        Rect{100, 100, 300, 200},
        true,
        true));

    assert(
        manager.begin_resize(
            1,
            Point{400, 300}));

    manager.pointer_move(
        Point{5000, 5000});

    manager.end_pointer_action();

    const Window* window =
        manager.find(1);

    assert(window != nullptr);

    assert(
        window->bounds.x +
        window->bounds.width ==
        1280);

    assert(
        window->bounds.y +
        window->bounds.height ==
        720);
}

void test_minimized_window_is_not_hit()
{
    WindowManager manager;

    assert(manager.add_window(
        1,
        Rect{100, 100, 400, 300},
        true,
        true));

    assert(
        manager.hit_test(
            Point{150, 150}) == 1);

    assert(manager.minimize(1));

    assert(
        manager.hit_test(
            Point{150, 150}) == 0);
}

void test_restore_preserves_geometry_and_focuses()
{
    WindowManager manager;

    assert(manager.add_window(
        1,
        Rect{123, 145, 444, 255},
        true,
        true));

    assert(manager.minimize(1));

    assert(manager.restore(1));

    const Window* window =
        manager.find(1);

    assert(window != nullptr);

    assert(window->state == WindowState::Open);

    assert(window->bounds.x == 123);
    assert(window->bounds.y == 145);
    assert(window->bounds.width == 444);
    assert(window->bounds.height == 255);

    assert(manager.focused() == 1);
}

void test_closed_window_disappears()
{
    WindowManager manager;

    assert(manager.add_window(
        1,
        Rect{100, 100, 400, 300},
        true,
        true));

    assert(manager.close(1));

    assert(
        manager.hit_test(
            Point{150, 150}) == 0);

    // Closed windows are no longer active tasks.
    assert(manager.find(1) == nullptr);
    assert(manager.focused() == 0);
}

void test_non_resizable_window_rejects_resize()
{
    WindowManager manager;

    assert(manager.add_window(
        1,
        Rect{100, 100, 400, 300},
        false,
        true));

    assert(
        !manager.begin_resize(
            1,
            Point{500, 400}));
}

void test_non_closable_window_rejects_close()
{
    WindowManager manager;

    assert(manager.add_window(
        1,
        Rect{100, 100, 400, 300},
        true,
        false));

    assert(!manager.close(1));
    assert(manager.find(1) != nullptr);
}

void test_capacity_is_eight_windows()
{
    WindowManager manager;

    for (WindowId id = 1;
         id <= WindowManager::kMaxWindows;
         ++id) {
        assert(manager.add_window(
            id,
            Rect{
                static_cast<int32_t>(id * 10),
                40,
                160,
                100,
            },
            true,
            true));
    }

    assert(
        !manager.add_window(
            99,
            Rect{20, 40, 160, 100},
            true,
            true));
}

} // namespace

int main()
{
    test_first_window_becomes_focused();
    test_focus_raises_lower_window();
    test_drag_changes_position();
    test_drag_clamps_to_work_area();
    test_resize_honors_minimum_size();
    test_resize_clamps_to_screen_edges();
    test_minimized_window_is_not_hit();
    test_restore_preserves_geometry_and_focuses();
    test_closed_window_disappears();
    test_non_resizable_window_rejects_resize();
    test_non_closable_window_rejects_close();
    test_capacity_is_eight_windows();

    return 0;
}
