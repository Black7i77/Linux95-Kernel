#include "file_manager_fake_vfs.hpp"
#include "gui/file_manager_app.hpp"

#include <assert.h>
#include <stdint.h>
#include <string.h>

using namespace linux95;

int main()
{
    static uint8_t pixels[640 * 480 * 4];
    memset(pixels, 0xA5, sizeof(pixels));
    graphics::Framebuffer framebuffer{
        pixels, 640, 480, 640 * 4, {8, 16, 8, 8, 8, 0}};
    file_manager_fake_vfs::reset();
    gui::file_manager::FileManagerApp app;
    assert(app.open() == filesystem::Status::Ok);
    gui::AppInstance instance = app.instance();
    const graphics::Rect content{100, 100, 160, 200};
    instance.callbacks.draw(instance.context, framebuffer, content);

    for (int32_t y = content.y; y < content.y + content.height; ++y) {
        for (int32_t x = content.x + content.width; x < 640; ++x) {
            const size_t offset = static_cast<size_t>(y) * framebuffer.pitch +
                static_cast<size_t>(x) * 4;
            assert(pixels[offset] == 0xA5);
            assert(pixels[offset + 1] == 0xA5);
            assert(pixels[offset + 2] == 0xA5);
            assert(pixels[offset + 3] == 0xA5);
        }
    }
}
