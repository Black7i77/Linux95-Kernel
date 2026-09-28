#pragma once

#include "filesystem/vfs.hpp"

#include <stddef.h>

namespace linux95::gui::file_manager {

constexpr size_t kMaxEntries = 128;

enum class ViewMode {
    Icons,
    Details,
};

class FileManagerModel {
public:
    FileManagerModel();

    filesystem::Status load_root();
    filesystem::Status refresh();
    filesystem::Status navigate_into(size_t entry_index);
    filesystem::Status navigate_parent();
    bool select(size_t entry_index);
    void set_view(ViewMode view);

    const char* current_path() const;
    size_t entry_count() const;
    const filesystem::vfs::DirectoryEntry* entry(size_t index) const;
    int selected_index() const;
    ViewMode view_mode() const;
    bool truncated() const;

private:
    char path_[filesystem::vfs::kPathCapacity];
    filesystem::vfs::DirectoryEntry entries_[kMaxEntries];
    size_t entry_count_;
    char selected_name_[13];
    bool has_selection_;
    ViewMode view_;
    bool truncated_;

    filesystem::Status load_path(const char* path);
    int find_selected() const;
};

} // namespace linux95::gui::file_manager
