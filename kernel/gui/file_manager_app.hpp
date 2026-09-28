#pragma once

#include "gui/app.hpp"
#include "gui/file_manager_model.hpp"

namespace linux95::gui::file_manager {

enum class DialogMode : uint8_t { None, Name, DeleteConfirm };
enum class PendingOperation : uint8_t { None, NewFolder, NewFile, Rename };

class FileManagerApp {
public:
    FileManagerApp();

    AppInstance instance();
    filesystem::Status open();
    FileManagerModel& model();
    const FileManagerModel& model() const;
    DialogMode dialog_mode() const;
    PendingOperation pending_operation() const;
    const char* name_buffer() const;
    const char* status_message() const;

private:
    FileManagerModel model_;
    DialogMode dialog_mode_;
    PendingOperation pending_operation_;
    char name_buffer_[13];
    char status_message_[64];

    void draw(graphics::Framebuffer& framebuffer, graphics::Rect content);
    void on_key(const keyboard::KeyEvent& event);
    void on_mouse(const AppMouseEvent& event);
    void on_close();
    void begin_name_dialog(PendingOperation operation);
    void begin_delete_confirmation();
    void cancel_dialog();
    void confirm_name();
    void confirm_delete();
    void activate_action(PendingOperation operation);
    void toggle_view();
    void set_status(const char* message);
    void set_status_for(filesystem::Status status, const char* operation);

    static void draw_callback(
        void* context,
        graphics::Framebuffer& framebuffer,
        graphics::Rect content);
    static void key_callback(
        void* context,
        const keyboard::KeyEvent& event);
    static void close_callback(void* context);
    static void mouse_callback(void* context, const AppMouseEvent& event);
};

} // namespace linux95::gui::file_manager
