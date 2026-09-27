#include "gui/desktop.hpp"

#include <assert.h>

using namespace linux95;

namespace {

struct MouseCapture {
    gui::AppMouseEvent event{};
    int calls = 0;
};

void capture_mouse(
    void* context,
    const gui::AppMouseEvent& event)
{
    auto& capture = *static_cast<MouseCapture*>(context);
    capture.event = event;
    ++capture.calls;
}

gui::AppInstance app_for(MouseCapture& capture)
{
    return gui::AppInstance{
        &capture,
        gui::AppCallbacks{
            nullptr,
            nullptr,
            nullptr,
            capture_mouse,
        },
    };
}

void test_content_coordinates_and_button_state_are_routed()
{
    gui::WindowManager windows;
    assert(windows.add_window(7, gui::Rect{100, 80, 300, 220}, true, true));
    assert(windows.focus(7));

    MouseCapture capture;
    gui::AppInstance app = app_for(capture);
    const gui::Rect content{100, 104, 300, 196};

    assert(desktop::route_mouse(
        windows, 7, app, content, gui::Point{137, 129},
        true, false, true));
    assert(capture.calls == 1);
    assert(capture.event.x == 37);
    assert(capture.event.y == 25);
    assert(capture.event.left_pressed);
    assert(!capture.event.left_released);
    assert(capture.event.left_down);

    assert(desktop::route_mouse(
        windows, 7, app, content, gui::Point{140, 132},
        false, true, false));
    assert(capture.calls == 2);
    assert(capture.event.x == 40);
    assert(capture.event.y == 28);
    assert(!capture.event.left_pressed);
    assert(capture.event.left_released);
    assert(!capture.event.left_down);
}

void test_null_callback_is_harmless()
{
    gui::WindowManager windows;
    assert(windows.add_window(7, gui::Rect{100, 80, 300, 220}, true, true));
    assert(windows.focus(7));

    gui::AppInstance app{nullptr, gui::AppCallbacks{nullptr, nullptr, nullptr, nullptr}};
    assert(!desktop::route_mouse(
        windows, 7, app, gui::Rect{100, 104, 300, 196},
        gui::Point{137, 129}, true, false, true));
}

void test_chrome_panel_outside_content_and_unfocused_apps_do_not_route()
{
    gui::WindowManager windows;
    assert(windows.add_window(7, gui::Rect{100, 80, 300, 220}, true, true));
    assert(windows.add_window(8, gui::Rect{450, 80, 300, 220}, true, true));
    assert(windows.focus(7));

    MouseCapture capture;
    gui::AppInstance app = app_for(capture);
    const gui::Rect content{100, 104, 300, 196};

    assert(!desktop::route_mouse(
        windows, 7, app, content, gui::Point{110, 90},
        true, false, true));
    assert(!desktop::route_mouse(
        windows, 7, app, content, gui::Point{10, 10},
        true, false, true));
    assert(!desktop::route_mouse(
        windows, 8, app, gui::Rect{450, 104, 300, 196},
        gui::Point{470, 120}, true, false, true));
    assert(capture.calls == 0);
}

void test_pointer_owned_by_resize_or_drag_does_not_route_to_app()
{
    gui::WindowManager windows;
    assert(windows.add_window(7, gui::Rect{100, 80, 300, 220}, true, true));
    assert(windows.focus(7));

    MouseCapture capture;
    gui::AppInstance app = app_for(capture);
    const gui::Rect content{100, 104, 300, 196};

    // The point is within the content rectangle, but the desktop chrome has
    // claimed this pointer action for a resize/drag.
    assert(!desktop::route_mouse_after_chrome(
        windows, 7, app, content, gui::Point{397, 297},
        true, false, true, false, 7));
    assert(capture.calls == 0);

    // The same post-chrome boundary also suppresses motion/release while the
    // action was already active before this mouse event.
    assert(!desktop::route_mouse_after_chrome(
        windows, 7, app, content, gui::Point{397, 297},
        false, true, false, true, 7));
    assert(capture.calls == 0);
}

} // namespace

int main()
{
    test_content_coordinates_and_button_state_are_routed();
    test_null_callback_is_harmless();
    test_chrome_panel_outside_content_and_unfocused_apps_do_not_route();
    test_pointer_owned_by_resize_or_drag_does_not_route_to_app();
}
