#include "file_manager_fake_vfs.hpp"
#include "gui/file_manager_model.hpp"

#include <assert.h>
#include <string.h>

using linux95::filesystem::Status;
using linux95::gui::file_manager::FileManagerModel;
using linux95::gui::file_manager::ViewMode;

int main() {
    file_manager_fake_vfs::reset();
    file_manager_fake_vfs::add("/", "Docs", true, 0);
    file_manager_fake_vfs::add("/", "Readme.TXT", false, 37);
    file_manager_fake_vfs::add("/Docs", "Inside", false, 9);
    FileManagerModel model;
    assert(strcmp(model.current_path(), "/") == 0);
    assert(model.load_root() == Status::Ok);
    assert(model.entry_count() == 2 && !model.truncated());
    assert(model.entry(0)->is_directory && strcmp(model.entry(1)->name, "Readme.TXT") == 0);
    assert(model.entry(1)->size == 37 && model.selected_index() == -1);
    assert(file_manager_fake_vfs::close_count() == 1);

    assert(model.select(1));
    model.set_view(ViewMode::Details);
    assert(model.view_mode() == ViewMode::Details && model.selected_index() == 1);
    assert(strcmp(model.current_path(), "/") == 0);
    file_manager_fake_vfs::set_open_status("/Docs", Status::IoError);
    assert(model.refresh() == Status::Ok);
    assert(model.navigate_into(0) == Status::IoError);
    assert(strcmp(model.current_path(), "/") == 0 && model.entry_count() == 2);
    file_manager_fake_vfs::set_open_status("/Docs", Status::Ok);
    assert(model.navigate_into(0) == Status::Ok);
    assert(strcmp(model.current_path(), "/Docs") == 0 && model.entry_count() == 1);
    assert(model.navigate_parent() == Status::Ok && strcmp(model.current_path(), "/") == 0);
    assert(model.selected_index() == -1);
    assert(model.navigate_parent() == Status::Ok && strcmp(model.current_path(), "/") == 0);

    assert(model.select(1));
    file_manager_fake_vfs::add("/", "README.txt", false, 41);
    assert(model.refresh() == Status::Ok && model.selected_index() == 1);
    assert(model.entry(1)->size == 41);
    file_manager_fake_vfs::reset();
    assert(model.refresh() == Status::Ok && model.selected_index() == -1);

    file_manager_fake_vfs::reset();
    file_manager_fake_vfs::add("/", "Keep", false, 1);
    assert(model.load_root() == Status::Ok);
    assert(model.select(0));
    file_manager_fake_vfs::set_read_error_after(0, Status::IoError);
    const size_t closes_before_error = file_manager_fake_vfs::close_count();
    assert(model.refresh() == Status::IoError);
    assert(model.entry_count() == 1 && strcmp(model.entry(0)->name, "Keep") == 0 && model.selected_index() == 0);
    assert(file_manager_fake_vfs::close_count() == closes_before_error + 1);

    file_manager_fake_vfs::set_close_status(Status::IoError);
    const size_t closes_before_close_error = file_manager_fake_vfs::close_count();
    assert(model.refresh() == Status::IoError);
    assert(model.entry_count() == 1 && strcmp(model.entry(0)->name, "Keep") == 0);
    assert(file_manager_fake_vfs::close_count() == closes_before_close_error + 1);
    file_manager_fake_vfs::set_close_status(Status::Ok);

    file_manager_fake_vfs::reset();
    file_manager_fake_vfs::add("/", "Missing", true, 0);
    assert(model.load_root() == Status::Ok);
    file_manager_fake_vfs::set_open_status("/Missing", Status::NotFound);
    assert(model.navigate_into(0) == Status::NotFound);
    assert(strcmp(model.current_path(), "/") == 0 && model.entry_count() == 1);

    file_manager_fake_vfs::reset();
    for (size_t i = 0; i < 129; ++i) {
        char name[13] = "ITEM0000.TXT";
        name[4] = static_cast<char>('0' + (i / 100) % 10);
        name[5] = static_cast<char>('0' + (i / 10) % 10);
        name[6] = static_cast<char>('0' + i % 10);
        file_manager_fake_vfs::add("/", name, false, static_cast<uint32_t>(i));
    }
    assert(model.load_root() == Status::Ok && model.entry_count() == 128 && model.truncated());
    assert(file_manager_fake_vfs::close_count() == 1);

    assert(model.select(0));
    assert(model.navigate_into(0) == Status::NotDirectory);
    assert(model.select(128) == false);

    file_manager_fake_vfs::reset();
    char directory[128] = "/";
    assert(model.load_root() == Status::Ok);
    for (size_t level = 0; level < 14; ++level) {
        file_manager_fake_vfs::add(directory, "ABCDEFGH", true, 0);
        size_t length = strlen(directory);
        if (length > 1) directory[length++] = '/';
        memcpy(directory + length, "ABCDEFGH", 9);
        assert(model.refresh() == Status::Ok && model.navigate_into(0) == Status::Ok);
    }
    assert(strlen(model.current_path()) == 126);
    file_manager_fake_vfs::add(directory, "TOOLONG", true, 0);
    assert(model.refresh() == Status::Ok);
    assert(model.navigate_into(0) == Status::InvalidName);
    assert(strlen(model.current_path()) == 126);

    return 0;
}
