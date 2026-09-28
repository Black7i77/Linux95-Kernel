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
    assert(instance.callbacks.on_mouse != nullptr);
    assert(app.open() == filesystem::Status::Ok);
    assert(strcmp(app.model().current_path(), "/") == 0);
    assert(app.model().entry_count() == 1);
    assert(strcmp(app.model().entry(0)->name, "Readme.TXT") == 0);
}

void send_key(gui::AppInstance instance, keyboard::KeyCode key, char character = '\0', bool ctrl = false)
{
    assert(instance.callbacks.on_key != nullptr);
    instance.callbacks.on_key(instance.context, keyboard::KeyEvent{key, character, ctrl, false, true});
}

void click(gui::AppInstance instance, int32_t x, int32_t y)
{
    assert(instance.callbacks.on_mouse != nullptr);
    instance.callbacks.on_mouse(instance.context, gui::AppMouseEvent{x, y, true, false, true});
}

void test_keyboard_name_dialog_input_confirm_and_cancel()
{
    file_manager_fake_vfs::reset();
    gui::file_manager::FileManagerApp app;
    assert(app.open() == filesystem::Status::Ok);
    gui::AppInstance instance = app.instance();

    send_key(instance, keyboard::KeyCode::Character, 'n', true);
    assert(app.dialog_mode() == gui::file_manager::DialogMode::Name);
    assert(app.pending_operation() == gui::file_manager::PendingOperation::NewFolder);
    send_key(instance, keyboard::KeyCode::Character, 'T');
    send_key(instance, keyboard::KeyCode::Character, 'M');
    send_key(instance, keyboard::KeyCode::Backspace);
    assert(strcmp(app.name_buffer(), "T") == 0);
    send_key(instance, keyboard::KeyCode::Enter);
    assert(app.dialog_mode() == gui::file_manager::DialogMode::None);
    assert(file_manager_fake_vfs::mkdir_count() == 1);
    assert(strcmp(app.model().entry(app.model().selected_index())->name, "T") == 0);

    send_key(instance, keyboard::KeyCode::Character, 'f', true);
    send_key(instance, keyboard::KeyCode::Character, 'X');
    send_key(instance, keyboard::KeyCode::Escape);
    assert(app.dialog_mode() == gui::file_manager::DialogMode::None);
    assert(file_manager_fake_vfs::touch_count() == 0);
}

void test_toolbar_name_actions_rename_prefill_and_delete_cancel()
{
    file_manager_fake_vfs::reset();
    file_manager_fake_vfs::add("/", "Readme.TXT", false, 4);
    gui::file_manager::FileManagerApp app;
    assert(app.open() == filesystem::Status::Ok);
    gui::AppInstance instance = app.instance();
    assert(app.model().select(0));

    click(instance, 145, 12); // Rename toolbar control
    assert(app.dialog_mode() == gui::file_manager::DialogMode::Name);
    assert(app.pending_operation() == gui::file_manager::PendingOperation::Rename);
    assert(strcmp(app.name_buffer(), "Readme.TXT") == 0);
    send_key(instance, keyboard::KeyCode::Escape);

    click(instance, 205, 12); // Delete toolbar control
    assert(app.dialog_mode() == gui::file_manager::DialogMode::DeleteConfirm);
    assert(file_manager_fake_vfs::remove_count() == 0);
    send_key(instance, keyboard::KeyCode::Escape);
    assert(app.dialog_mode() == gui::file_manager::DialogMode::None);
    assert(file_manager_fake_vfs::remove_count() == 0);

    click(instance, 20, 12); // New Folder toolbar control
    assert(app.dialog_mode() == gui::file_manager::DialogMode::Name);
    assert(app.pending_operation() == gui::file_manager::PendingOperation::NewFolder);
    send_key(instance, keyboard::KeyCode::Escape);

    click(instance, 80, 12); // New File toolbar control
    assert(app.pending_operation() == gui::file_manager::PendingOperation::NewFile);
}

void test_delete_keyboard_confirmation_and_confirmed_remove()
{
    file_manager_fake_vfs::reset();
    file_manager_fake_vfs::add("/", "REMOVE.TXT", false, 1);
    gui::file_manager::FileManagerApp app;
    assert(app.open() == filesystem::Status::Ok);
    assert(app.model().select(0));
    gui::AppInstance instance = app.instance();

    send_key(instance, keyboard::KeyCode::Delete);
    assert(app.dialog_mode() == gui::file_manager::DialogMode::DeleteConfirm);
    assert(file_manager_fake_vfs::remove_count() == 0);
    send_key(instance, keyboard::KeyCode::Enter);
    assert(app.dialog_mode() == gui::file_manager::DialogMode::None);
    assert(file_manager_fake_vfs::remove_count() == 1);
    assert(app.model().selected_index() == -1);
}

void test_no_selection_feedback_and_outside_toolbar_is_safe()
{
    file_manager_fake_vfs::reset();
    gui::file_manager::FileManagerApp app;
    assert(app.open() == filesystem::Status::Ok);
    gui::AppInstance instance = app.instance();

    send_key(instance, keyboard::KeyCode::Character, 'r', true);
    assert(app.dialog_mode() == gui::file_manager::DialogMode::None);
    assert(app.status_message()[0] != '\0');
    send_key(instance, keyboard::KeyCode::Delete);
    assert(app.dialog_mode() == gui::file_manager::DialogMode::None);
    assert(file_manager_fake_vfs::remove_count() == 0);

    const size_t mkdirs_before = file_manager_fake_vfs::mkdir_count();
    click(instance, -1, 12);
    click(instance, 400, 12);
    click(instance, 145, 40);
    assert(app.dialog_mode() == gui::file_manager::DialogMode::None);
    assert(file_manager_fake_vfs::mkdir_count() == mkdirs_before);
    assert(file_manager_fake_vfs::move_count() == 0);
}

void test_root_open_failure_is_reported_in_app_status()
{
    file_manager_fake_vfs::reset();
    file_manager_fake_vfs::set_open_status("/", filesystem::Status::NotMounted);
    gui::file_manager::FileManagerApp app;
    assert(app.open() == filesystem::Status::NotMounted);
    assert(app.status_message()[0] != '\0');
    assert(strstr(app.status_message(), "mounted") != nullptr);
}

void test_shared_keyboard_selection_navigation_and_view_toggle()
{
    file_manager_fake_vfs::reset();
    file_manager_fake_vfs::add("/", "DIR", true, 0);
    file_manager_fake_vfs::add("/", "FILE.TXT", false, 3);
    file_manager_fake_vfs::add("/DIR", "CHILD.TXT", false, 2);
    gui::file_manager::FileManagerApp app;
    assert(app.open() == filesystem::Status::Ok);
    gui::AppInstance instance = app.instance();

    send_key(instance, keyboard::KeyCode::ArrowDown);
    assert(app.model().selected_index() == 0);
    send_key(instance, keyboard::KeyCode::ArrowDown);
    assert(app.model().selected_index() == 1);
    send_key(instance, keyboard::KeyCode::Character, 'v', true);
    assert(app.model().view_mode() == gui::file_manager::ViewMode::Details);
    assert(app.model().selected_index() == 1);
    send_key(instance, keyboard::KeyCode::ArrowUp);
    assert(app.model().selected_index() == 0);
    send_key(instance, keyboard::KeyCode::Enter);
    assert(strcmp(app.model().current_path(), "/DIR") == 0);
    send_key(instance, keyboard::KeyCode::Backspace);
    assert(strcmp(app.model().current_path(), "/") == 0);
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
    test_keyboard_name_dialog_input_confirm_and_cancel();
    test_toolbar_name_actions_rename_prefill_and_delete_cancel();
    test_delete_keyboard_confirmation_and_confirmed_remove();
    test_no_selection_feedback_and_outside_toolbar_is_safe();
    test_root_open_failure_is_reported_in_app_status();
    test_shared_keyboard_selection_navigation_and_view_toggle();
    test_file_manager_window_is_created_once_and_restored();
    test_key_routes_to_third_app_when_its_window_is_focused();
    test_registration_respects_fixed_window_capacity();
}
