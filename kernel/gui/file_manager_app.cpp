#include "gui/file_manager_app.hpp"

namespace linux95::gui::file_manager {

FileManagerApp::FileManagerApp() : model_() {}

AppInstance FileManagerApp::instance()
{
    return AppInstance{
        this,
        AppCallbacks{
            draw_callback,
            key_callback,
            close_callback,
            nullptr,
        },
    };
}

filesystem::Status FileManagerApp::open()
{
    return model_.load_root();
}

FileManagerModel& FileManagerApp::model() { return model_; }
const FileManagerModel& FileManagerApp::model() const { return model_; }

void FileManagerApp::draw(
    graphics::Framebuffer&,
    graphics::Rect)
{
}

void FileManagerApp::on_key(const keyboard::KeyEvent&) {}
void FileManagerApp::on_close() {}

void FileManagerApp::draw_callback(
    void* context,
    graphics::Framebuffer& framebuffer,
    graphics::Rect content)
{
    if (context != nullptr) {
        static_cast<FileManagerApp*>(context)->draw(framebuffer, content);
    }
}

void FileManagerApp::key_callback(
    void* context,
    const keyboard::KeyEvent& event)
{
    if (context != nullptr) {
        static_cast<FileManagerApp*>(context)->on_key(event);
    }
}

void FileManagerApp::close_callback(void* context)
{
    if (context != nullptr) {
        static_cast<FileManagerApp*>(context)->on_close();
    }
}

} // namespace linux95::gui::file_manager
