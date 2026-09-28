#include "gui/file_manager_app.hpp"

#include <string.h>

namespace linux95::gui::file_manager {
namespace {

constexpr int32_t kToolbarHeight = 24;
constexpr int32_t kDialogTop = 56;
constexpr int32_t kDialogBottom = 96;

char lower_ascii(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

const char* status_text(filesystem::Status status)
{
    switch (status) {
    case filesystem::Status::Ok: return "OK";
    case filesystem::Status::NotMounted: return "filesystem not mounted";
    case filesystem::Status::NotFound: return "entry not found";
    case filesystem::Status::NotDirectory: return "not a directory";
    case filesystem::Status::IsDirectory: return "is a directory";
    case filesystem::Status::AlreadyExists: return "name already exists";
    case filesystem::Status::InvalidName: return "invalid 8.3 name";
    case filesystem::Status::NoSpace: return "disk is full";
    case filesystem::Status::DirectoryNotEmpty: return "directory is not empty";
    case filesystem::Status::ReadOnly: return "filesystem is read only";
    case filesystem::Status::IoError: return "disk I/O error";
    case filesystem::Status::Corrupt: return "filesystem is corrupt";
    case filesystem::Status::Unsupported: return "operation unavailable";
    default: return "filesystem operation failed";
    }
}

} // namespace

FileManagerApp::FileManagerApp()
    : model_(), dialog_mode_(DialogMode::None), pending_operation_(PendingOperation::None),
      name_buffer_{}, status_message_{}
{}

AppInstance FileManagerApp::instance()
{
    return AppInstance{
        this,
        AppCallbacks{draw_callback, key_callback, close_callback, mouse_callback},
    };
}

filesystem::Status FileManagerApp::open()
{
    const filesystem::Status status = model_.load_root();
    set_status_for(status, "open");
    return status;
}

FileManagerModel& FileManagerApp::model() { return model_; }
const FileManagerModel& FileManagerApp::model() const { return model_; }
DialogMode FileManagerApp::dialog_mode() const { return dialog_mode_; }
PendingOperation FileManagerApp::pending_operation() const { return pending_operation_; }
const char* FileManagerApp::name_buffer() const { return name_buffer_; }
const char* FileManagerApp::status_message() const { return status_message_; }

void FileManagerApp::draw(graphics::Framebuffer&, graphics::Rect) {}

void FileManagerApp::on_key(const keyboard::KeyEvent& event)
{
    if (!event.pressed) return;

    if (dialog_mode_ == DialogMode::Name) {
        if (event.key == keyboard::KeyCode::Escape) {
            cancel_dialog();
        } else if (event.key == keyboard::KeyCode::Backspace) {
            const size_t length = strlen(name_buffer_);
            if (length != 0) name_buffer_[length - 1] = '\0';
        } else if (event.key == keyboard::KeyCode::Enter) {
            confirm_name();
        } else if (event.key == keyboard::KeyCode::Character &&
                   event.character >= 32 && event.character <= 126 && event.character != '/') {
            const size_t length = strlen(name_buffer_);
            if (length < sizeof(name_buffer_) - 1) {
                name_buffer_[length] = event.character;
                name_buffer_[length + 1] = '\0';
            }
        }
        return;
    }

    if (dialog_mode_ == DialogMode::DeleteConfirm) {
        if (event.key == keyboard::KeyCode::Escape) cancel_dialog();
        else if (event.key == keyboard::KeyCode::Enter) confirm_delete();
        return;
    }

    if (event.ctrl && event.key == keyboard::KeyCode::Character) {
        switch (lower_ascii(event.character)) {
        case 'n': activate_action(PendingOperation::NewFolder); return;
        case 'f': activate_action(PendingOperation::NewFile); return;
        case 'r': activate_action(PendingOperation::Rename); return;
        case 'v': toggle_view(); return;
        default: return;
        }
    }

    if (event.key == keyboard::KeyCode::Delete) {
        begin_delete_confirmation();
    } else if (event.key == keyboard::KeyCode::ArrowUp || event.key == keyboard::KeyCode::ArrowDown) {
        const size_t count = model_.entry_count();
        if (count == 0) return;
        const int selected = model_.selected_index();
        size_t next = 0;
        if (event.key == keyboard::KeyCode::ArrowUp) {
            next = selected < 0 ? count - 1 : (selected == 0 ? 0 : static_cast<size_t>(selected - 1));
        } else {
            next = selected < 0 ? 0 : (static_cast<size_t>(selected + 1) < count
                ? static_cast<size_t>(selected + 1) : count - 1);
        }
        model_.select(next);
    } else if (event.key == keyboard::KeyCode::Enter) {
        const int selected = model_.selected_index();
        const auto* entry = selected < 0 ? nullptr : model_.entry(static_cast<size_t>(selected));
        if (entry && entry->is_directory) {
            set_status_for(model_.navigate_into(static_cast<size_t>(selected)), "open directory");
        }
    } else if (event.key == keyboard::KeyCode::Backspace) {
        set_status_for(model_.navigate_parent(), "go to parent");
    }
}

void FileManagerApp::on_mouse(const AppMouseEvent& event)
{
    if (!event.left_pressed) return;
    if (dialog_mode_ != DialogMode::None) {
        if (event.y >= kDialogTop && event.y < kDialogBottom) {
            if (event.x >= 24 && event.x < 104) {
                if (dialog_mode_ == DialogMode::Name) confirm_name();
                else confirm_delete();
            } else if (event.x >= 112 && event.x < 192) {
                cancel_dialog();
            }
        }
        return;
    }
    if (event.y < 0 || event.y >= kToolbarHeight || event.x < 0) return;
    if (event.x < 76) activate_action(PendingOperation::NewFolder);
    else if (event.x < 136) activate_action(PendingOperation::NewFile);
    else if (event.x < 196) activate_action(PendingOperation::Rename);
    else if (event.x < 256) begin_delete_confirmation();
    else if (event.x < 316) toggle_view();
}

void FileManagerApp::on_close() { cancel_dialog(); }

void FileManagerApp::begin_name_dialog(PendingOperation operation)
{
    pending_operation_ = operation;
    dialog_mode_ = DialogMode::Name;
    name_buffer_[0] = '\0';
    if (operation == PendingOperation::Rename) {
        const int selected = model_.selected_index();
        if (selected < 0) {
            cancel_dialog();
            set_status("select an entry first");
            return;
        }
        const auto* entry = model_.entry(static_cast<size_t>(selected));
        size_t i = 0;
        while (entry->name[i] && i + 1 < sizeof(name_buffer_)) {
            name_buffer_[i] = entry->name[i];
            ++i;
        }
        name_buffer_[i] = '\0';
    }
    set_status(operation == PendingOperation::NewFolder ? "new folder name" :
        operation == PendingOperation::NewFile ? "new file name" : "rename entry");
}

void FileManagerApp::begin_delete_confirmation()
{
    if (model_.selected_index() < 0) {
        set_status("select an entry first");
        return;
    }
    pending_operation_ = PendingOperation::None;
    dialog_mode_ = DialogMode::DeleteConfirm;
    set_status("confirm delete");
}

void FileManagerApp::cancel_dialog()
{
    dialog_mode_ = DialogMode::None;
    pending_operation_ = PendingOperation::None;
    name_buffer_[0] = '\0';
}

void FileManagerApp::confirm_name()
{
    filesystem::Status result = filesystem::Status::InvalidName;
    if (pending_operation_ == PendingOperation::NewFolder) result = model_.create_folder(name_buffer_);
    else if (pending_operation_ == PendingOperation::NewFile) result = model_.create_file(name_buffer_);
    else if (pending_operation_ == PendingOperation::Rename) result = model_.rename_selected(name_buffer_);
    set_status_for(result, pending_operation_ == PendingOperation::NewFolder ? "create folder" :
        pending_operation_ == PendingOperation::NewFile ? "create file" : "rename");
    if (result == filesystem::Status::Ok) cancel_dialog();
}

void FileManagerApp::confirm_delete()
{
    const filesystem::Status result = model_.remove_selected();
    set_status_for(result, "delete");
    cancel_dialog();
}

void FileManagerApp::activate_action(PendingOperation operation)
{
    if (operation == PendingOperation::Rename && model_.selected_index() < 0) {
        set_status("select an entry first");
        return;
    }
    begin_name_dialog(operation);
}

void FileManagerApp::toggle_view()
{
    model_.set_view(model_.view_mode() == ViewMode::Icons ? ViewMode::Details : ViewMode::Icons);
}

void FileManagerApp::set_status(const char* message)
{
    if (!message) message = "";
    size_t length = 0;
    while (message[length] && length + 1 < sizeof(status_message_)) ++length;
    memcpy(status_message_, message, length);
    status_message_[length] = '\0';
}

void FileManagerApp::set_status_for(filesystem::Status status, const char* operation)
{
    if (status == filesystem::Status::Ok) {
        set_status("OK");
        return;
    }
    char message[64]{};
    size_t offset = 0;
    while (operation[offset] && offset + 3 < sizeof(message)) {
        message[offset] = operation[offset];
        ++offset;
    }
    message[offset++] = ':';
    message[offset++] = ' ';
    const char* reason = status_text(status);
    size_t index = 0;
    while (reason[index] && offset + 1 < sizeof(message)) message[offset++] = reason[index++];
    message[offset] = '\0';
    set_status(message);
}

void FileManagerApp::draw_callback(void* context, graphics::Framebuffer& framebuffer, graphics::Rect content)
{
    if (context) static_cast<FileManagerApp*>(context)->draw(framebuffer, content);
}

void FileManagerApp::key_callback(void* context, const keyboard::KeyEvent& event)
{
    if (context) static_cast<FileManagerApp*>(context)->on_key(event);
}

void FileManagerApp::close_callback(void* context)
{
    if (context) static_cast<FileManagerApp*>(context)->on_close();
}

void FileManagerApp::mouse_callback(void* context, const AppMouseEvent& event)
{
    if (context) static_cast<FileManagerApp*>(context)->on_mouse(event);
}

} // namespace linux95::gui::file_manager
