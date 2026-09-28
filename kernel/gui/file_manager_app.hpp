#pragma once

#include "gui/app.hpp"
#include "gui/file_manager_model.hpp"

namespace linux95::gui::file_manager {

class FileManagerApp {
public:
    FileManagerApp();

    AppInstance instance();
    filesystem::Status open();
    FileManagerModel& model();
    const FileManagerModel& model() const;

private:
    FileManagerModel model_;

    void draw(graphics::Framebuffer& framebuffer, graphics::Rect content);
    void on_key(const keyboard::KeyEvent& event);
    void on_close();

    static void draw_callback(
        void* context,
        graphics::Framebuffer& framebuffer,
        graphics::Rect content);
    static void key_callback(
        void* context,
        const keyboard::KeyEvent& event);
    static void close_callback(void* context);
};

} // namespace linux95::gui::file_manager
