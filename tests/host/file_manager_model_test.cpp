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
    const size_t bounded_mutations = file_manager_fake_vfs::mkdir_count();
    assert(model.create_folder("A") == Status::InvalidName);
    assert(file_manager_fake_vfs::mkdir_count() == bounded_mutations);

    file_manager_fake_vfs::reset();
    FileManagerModel operations;
    assert(operations.load_root() == Status::Ok);
    assert(operations.create_folder("NewDir") == Status::Ok);
    assert(file_manager_fake_vfs::mkdir_count() == 1);
    assert(operations.selected_index() >= 0);
    assert(strcmp(operations.entry(operations.selected_index())->name, "NewDir") == 0);
    assert(operations.create_file("New.TXT") == Status::Ok);
    assert(file_manager_fake_vfs::stat_count() == 1 && file_manager_fake_vfs::touch_count() == 1);
    assert(strcmp(operations.entry(operations.selected_index())->name, "New.TXT") == 0);
    assert(operations.create_file("New.TXT") == Status::AlreadyExists);
    assert(file_manager_fake_vfs::stat_count() == 2);
    assert(file_manager_fake_vfs::touch_count() == 1);
    assert(operations.create_folder("bad/name") == Status::InvalidName);
    assert(file_manager_fake_vfs::mkdir_count() == 1);
    FileManagerModel unselected;
    assert(unselected.load_root() == Status::Ok);
    assert(unselected.rename_selected("Ignored.TXT") == Status::Unsupported);
    assert(file_manager_fake_vfs::move_count() == 0);
    file_manager_fake_vfs::set_stat_error(Status::IoError);
    assert(operations.create_file("Other.TXT") == Status::IoError);
    assert(file_manager_fake_vfs::touch_count() == 1);
    file_manager_fake_vfs::set_stat_error(Status::Ok);

    file_manager_fake_vfs::add("/", "Old.TXT", false, 4);
    file_manager_fake_vfs::add("/", "Folder", true, 0);
    assert(operations.refresh() == Status::Ok);
    int old_file = -1, old_dir = -1;
    for (size_t i = 0; i < operations.entry_count(); ++i) {
        if (!strcmp(operations.entry(i)->name, "Old.TXT")) old_file = static_cast<int>(i);
        if (!strcmp(operations.entry(i)->name, "Folder")) old_dir = static_cast<int>(i);
    }
    assert(old_file >= 0 && operations.select(static_cast<size_t>(old_file)));
    assert(operations.rename_selected("Renamed.TXT") == Status::Ok);
    assert(file_manager_fake_vfs::move_count() == 1);
    assert(strcmp(operations.entry(operations.selected_index())->name, "Renamed.TXT") == 0);
    for (size_t i = 0; i < operations.entry_count(); ++i)
        if (!strcmp(operations.entry(i)->name, "Folder")) old_dir = static_cast<int>(i);
    assert(old_dir >= 0 && operations.select(static_cast<size_t>(old_dir)));
    assert(operations.rename_selected("RenamedDir") == Status::Ok);
    assert(operations.selected_index() >= 0);
    assert(operations.rename_selected("New.TXT") == Status::AlreadyExists);
    assert(operations.remove_selected() == Status::Ok);
    assert(operations.selected_index() == -1);
    assert(operations.remove_selected() == Status::Unsupported);

    int renamed_file = -1;
    for (size_t i = 0; i < operations.entry_count(); ++i)
        if (!strcmp(operations.entry(i)->name, "Renamed.TXT")) renamed_file = static_cast<int>(i);
    assert(renamed_file >= 0 && operations.select(static_cast<size_t>(renamed_file)));
    assert(operations.remove_selected() == Status::Ok);
    assert(file_manager_fake_vfs::remove_count() == 2);
    int new_directory = -1;
    for (size_t i = 0; i < operations.entry_count(); ++i)
        if (!strcmp(operations.entry(i)->name, "NewDir")) new_directory = static_cast<int>(i);
    assert(new_directory >= 0 && operations.select(static_cast<size_t>(new_directory)));
    assert(operations.remove_selected() == Status::Ok);
    assert(file_manager_fake_vfs::remove_count() == 3);

    file_manager_fake_vfs::add("/", "NonEmpty", true, 0);
    file_manager_fake_vfs::add("/NonEmpty", "Child", false, 1);
    assert(operations.refresh() == Status::Ok);
    int nonempty = -1;
    for (size_t i = 0; i < operations.entry_count(); ++i)
        if (!strcmp(operations.entry(i)->name, "NonEmpty")) nonempty = static_cast<int>(i);
    assert(nonempty >= 0 && operations.select(static_cast<size_t>(nonempty)));
    file_manager_fake_vfs::set_mutation_status(Status::IoError);
    assert(operations.remove_selected() == Status::IoError);
    assert(operations.selected_index() == nonempty);
    assert(operations.entry_count() > static_cast<size_t>(nonempty));
    file_manager_fake_vfs::set_mutation_status(Status::Ok);
    assert(operations.remove_selected() == Status::DirectoryNotEmpty);
    assert(operations.selected_index() == nonempty);

    file_manager_fake_vfs::set_mutation_status(Status::IoError);
    assert(operations.create_folder("Failed") == Status::IoError);
    assert(operations.selected_index() == nonempty);
    file_manager_fake_vfs::set_mutation_status(Status::Ok);
    assert(operations.select(0));
    assert(operations.remove_selected() == Status::Ok);
    return 0;
}
