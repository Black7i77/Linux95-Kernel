#include "arch/keyboard.hpp"
#include "gui/desktop.hpp"
#include "terminal/key_event_adapter.hpp"

#include <assert.h>

using namespace linux95;

static keyboard::KeyEvent received{};
static unsigned calls = 0;

static void key_stub(void*, const keyboard::KeyEvent& event)
{
    received = event;
    ++calls;
}
static void close_stub(void*) {}

static gui::AppInstance test_app()
{
    return gui::AppInstance{nullptr, gui::AppCallbacks{
        [](void*, graphics::Framebuffer&, graphics::Rect) {},
        key_stub,
        close_stub}};
}

static void test_shell_character_adapter()
{
    char out = 0;
    assert(terminal::shell_character_for_key(
        keyboard::KeyEvent{keyboard::KeyCode::Character, 'a', false, false, true}, out));
    assert(out == 'a');
    assert(terminal::shell_character_for_key(
        keyboard::KeyEvent{keyboard::KeyCode::Enter, 0, false, false, true}, out));
    assert(out == '\n');
    assert(terminal::shell_character_for_key(
        keyboard::KeyEvent{keyboard::KeyCode::Backspace, 0, false, false, true}, out));
    assert(out == '\b');

    const keyboard::KeyEvent rejected[] = {
        {keyboard::KeyCode::ArrowLeft, 0, false, false, true},
        {keyboard::KeyCode::Character, 'a', false, false, false},
        {keyboard::KeyCode::Unknown, 0, false, false, true},
        {keyboard::KeyCode::Character, 's', true, false, true},
        {keyboard::KeyCode::Character, 'q', true, false, true},
    };
    for (const auto& event : rejected) {
        assert(!terminal::shell_character_for_key(event, out));
    }
}

static void test_desktop_routes_complete_event_to_focused_app()
{
    gui::WindowManager windows;
    assert(desktop::initialize_default_windows(windows));
    gui::AppInstance terminal_app = test_app();
    gui::AppInstance system_app = test_app();
    const keyboard::KeyEvent event{
        keyboard::KeyCode::ArrowUp, 0, true, true, false};

    calls = 0;
    assert(desktop::route_key(windows, terminal_app, system_app, event));
    assert(calls == 1);
    assert(received.key == keyboard::KeyCode::ArrowUp);
    assert(received.ctrl && received.shift && !received.pressed);

    assert(windows.focus(desktop::kSystemInfoWindowId));
    assert(desktop::route_key(windows, terminal_app, system_app, event));
    assert(calls == 2);
    assert(received.key == event.key && received.ctrl && received.shift);

    assert(windows.close(desktop::kSystemInfoWindowId));
    assert(windows.focused() == desktop::kTerminalWindowId);
    assert(desktop::route_key(windows, terminal_app, system_app, event));
    assert(calls == 3);
}

int main()
{
    test_shell_character_adapter();
    test_desktop_routes_complete_event_to_focused_app();
    return 0;
}
