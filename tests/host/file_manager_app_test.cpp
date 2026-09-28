#include "file_manager_fake_vfs.hpp"
#include "gui/desktop.hpp"
#include "gui/file_manager_app.hpp"

#include <assert.h>
#include <string.h>

using namespace linux95;

namespace {

unsigned key_calls;
keyboard::KeyEvent last_key{};

void key_capture(void*, const keyboard::KeyEvent& event)
{
    ++key_calls;
    last_key = event;
}

void test_app_callbacks_and_open_load_root()
{
    file_manager_fake_vfs::reset();
    file_manager_fake_vfs::add("/", "Readme.TXT", false, 19);
    gui::file_manager::FileManagerApp app;
    gui::AppInstance instance = app.instance();
    assert(instance.context == &app);
    assert(instance.callbacks.draw != nullptr);
    assert(instance.callbacks.on_key != nullptr);
    assert(instance.callbacks.on_close != nullptr);
    assert(app.open() == filesystem::Status::Ok);
    assert(strcmp(app.model().current_path(), "/") == 0);
    assert(app.model().entry_count() == 1);
    assert(strcmp(app.model().entry(0)->name, "Readme.TXT") == 0);
}

void test_file_manager_window_is_created_once_and_restored()
{
    gui::WindowManager windows;
    assert(desktop::initialize_default_windows(windows));
    assert(desktop::activate_window(windows, desktop::kFileManagerWindowId));
    const gui::Window* created = windows.find(3);
    assert(created != nullptr && created->state == gui::WindowState::Open);
    assert(windows.focused() == 3);
    assert(desktop::activate_window(windows, desktop::kFileManagerWindowId));
    assert(desktop::activate_window(windows, desktop::kTerminalWindowId));
    assert(windows.find(3) == created && windows.focused() == 1);
    assert(windows.minimize(3));
    assert(desktop::activate_window(windows, desktop::kFileManagerWindowId));
    assert(windows.find(3)->state == gui::WindowState::Open);
    assert(windows.focused() == 3);
}

void test_key_routes_to_third_app_when_its_window_is_focused()
{
    gui::WindowManager windows;
    assert(desktop::initialize_default_windows(windows));
    assert(desktop::activate_window(windows, desktop::kFileManagerWindowId));
    gui::AppInstance terminal{nullptr, gui::AppCallbacks{}};
    gui::AppInstance system_info{nullptr, gui::AppCallbacks{}};
    gui::AppInstance file_manager{nullptr, gui::AppCallbacks{}};
    file_manager.callbacks.on_key = key_capture;
    const keyboard::KeyEvent event{
        keyboard::KeyCode::Character, 'x', true, false, true};
    key_calls = 0;
    assert(desktop::route_key(windows, terminal, system_info, file_manager, event));
    assert(key_calls == 1);
    assert(last_key.key == keyboard::KeyCode::Character && last_key.character == 'x');
    assert(last_key.ctrl && last_key.pressed);
}

void test_registration_respects_fixed_window_capacity()
{
    gui::WindowManager windows;
    for (gui::WindowId id = 10; id < 18; ++id) {
        assert(windows.add_window(
            id, gui::Rect{20, 40, 300, 200}, true, true));
    }
    assert(!desktop::activate_window(windows, desktop::kFileManagerWindowId));
    assert(windows.find(desktop::kFileManagerWindowId) == nullptr);
}

} // namespace

int main()
{
    test_app_callbacks_and_open_load_root();
    test_file_manager_window_is_created_once_and_restored();
    test_key_routes_to_third_app_when_its_window_is_focused();
    test_registration_respects_fixed_window_capacity();
}
