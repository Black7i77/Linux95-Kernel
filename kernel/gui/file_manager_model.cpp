#include "gui/file_manager_model.hpp"

#include <string.h>

namespace linux95::gui::file_manager {
namespace {

size_t string_length(const char* value) {
    size_t length = 0;
    while (value[length] != '\0') ++length;
    return length;
}

char fold_ascii(char value) {
    return value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A') : value;
}

bool same_name(const char* left, const char* right) {
    size_t i = 0;
    while (left[i] && right[i]) {
        if (fold_ascii(left[i]) != fold_ascii(right[i])) return false;
        ++i;
    }
    return left[i] == right[i];
}

bool copy_bounded(char* destination, const char* source, size_t capacity) {
    size_t length = 0;
    while (source[length]) {
        if (length + 1 >= capacity) return false;
        ++length;
    }
    for (size_t i = 0; i <= length; ++i) destination[i] = source[i];
    return true;
}

bool build_child_path(const char* parent, const char* name, char* destination, size_t capacity) {
    const size_t parent_length = string_length(parent);
    const size_t name_length = string_length(name);
    const bool root = parent_length == 1 && parent[0] == '/';
    const size_t separator_length = root ? 0 : 1;
    if (parent[0] != '/' || parent_length + separator_length + name_length + 1 > capacity) return false;
    for (size_t i = 0; i < parent_length; ++i) destination[i] = parent[i];
    size_t offset = parent_length;
    if (!root) destination[offset++] = '/';
    for (size_t i = 0; i <= name_length; ++i) destination[offset + i] = name[i];
    return true;
}

void select_name(FileManagerModel& model, const char* name) {
    for (size_t i = 0; i < model.entry_count(); ++i) {
        const filesystem::vfs::DirectoryEntry* item = model.entry(i);
        if (item && same_name(item->name, name)) {
            model.select(i);
            return;
        }
    }
}

}

FileManagerModel::FileManagerModel()
    : path_{'/'}, entries_{}, entry_count_(0), selected_name_{}, has_selection_(false),
      view_(ViewMode::Icons), truncated_(false) {}

filesystem::Status FileManagerModel::load_root() {
    return load_path("/");
}

filesystem::Status FileManagerModel::refresh() {
    return load_path(path_);
}

filesystem::Status FileManagerModel::load_path(const char* candidate_path) {
    filesystem::Status status = filesystem::Status::Ok;
    const int handle = filesystem::vfs::opendir(candidate_path, status);
    if (status != filesystem::Status::Ok) return status;
    if (handle < 0) return filesystem::Status::InvalidHandle;

    filesystem::vfs::DirectoryEntry replacement[kMaxEntries];
    size_t replacement_count = 0;
    bool replacement_truncated = false;
    bool reached_end = false;
    while (replacement_count < kMaxEntries) {
        filesystem::vfs::DirectoryEntry next{};
        bool end = false;
        status = filesystem::vfs::readdir(handle, next, end);
        if (status != filesystem::Status::Ok) break;
        if (end) {
            reached_end = true;
            break;
        }
        replacement[replacement_count++] = next;
    }
    if (status == filesystem::Status::Ok && !reached_end && replacement_count == kMaxEntries) {
        filesystem::vfs::DirectoryEntry probe{};
        bool end = false;
        status = filesystem::vfs::readdir(handle, probe, end);
        if (status == filesystem::Status::Ok) {
            if (end) reached_end = true;
            else replacement_truncated = true;
        }
    }

    const filesystem::Status close_status = filesystem::vfs::closedir(handle);
    if (status != filesystem::Status::Ok) return status;
    if (close_status != filesystem::Status::Ok) return close_status;

    char new_path[filesystem::vfs::kPathCapacity]{};
    if (!copy_bounded(new_path, candidate_path, sizeof(new_path))) return filesystem::Status::InvalidName;

    int preserved_selection = -1;
    if (has_selection_) {
        for (size_t i = 0; i < replacement_count; ++i) {
            if (same_name(selected_name_, replacement[i].name)) {
                preserved_selection = static_cast<int>(i);
                break;
            }
        }
    }

    for (size_t i = 0; i < replacement_count; ++i) entries_[i] = replacement[i];
    entry_count_ = replacement_count;
    for (size_t i = 0; i < sizeof(path_); ++i) path_[i] = new_path[i];
    truncated_ = replacement_truncated;
    has_selection_ = preserved_selection >= 0;
    if (has_selection_) copy_bounded(selected_name_, entries_[preserved_selection].name, sizeof(selected_name_));
    else selected_name_[0] = '\0';
    return filesystem::Status::Ok;
}

filesystem::Status FileManagerModel::navigate_into(size_t entry_index) {
    if (entry_index >= entry_count_) return filesystem::Status::NotFound;
    const filesystem::vfs::DirectoryEntry& selected = entries_[entry_index];
    if (!selected.is_directory) return filesystem::Status::NotDirectory;
    const filesystem::Status name_status = filesystem::vfs::validate_name(selected.name);
    if (name_status != filesystem::Status::Ok) return name_status;

    char candidate[filesystem::vfs::kPathCapacity];
    const size_t path_length = string_length(path_);
    const size_t name_length = string_length(selected.name);
    const bool root = path_length == 1 && path_[0] == '/';
    const size_t separator_length = root ? 0 : 1;
    if (path_length + separator_length + name_length + 1 > sizeof(candidate)) return filesystem::Status::InvalidName;
    for (size_t i = 0; i < path_length; ++i) candidate[i] = path_[i];
    size_t offset = path_length;
    if (!root) candidate[offset++] = '/';
    for (size_t i = 0; i <= name_length; ++i) candidate[offset + i] = selected.name[i];
    return load_path(candidate);
}

filesystem::Status FileManagerModel::navigate_parent() {
    if (path_[0] == '/' && path_[1] == '\0') return filesystem::Status::Ok;
    char parent[filesystem::vfs::kPathCapacity];
    copy_bounded(parent, path_, sizeof(parent));
    size_t length = string_length(parent);
    while (length > 1 && parent[length - 1] != '/') --length;
    if (length > 1) parent[length - 1] = '\0';
    else parent[1] = '\0';
    return load_path(parent);
}

filesystem::Status FileManagerModel::create_folder(const char* name) {
    const filesystem::Status validation = filesystem::vfs::validate_name(name);
    if (validation != filesystem::Status::Ok) return validation;
    char destination[filesystem::vfs::kPathCapacity];
    if (!build_child_path(path_, name, destination, sizeof(destination))) return filesystem::Status::InvalidName;
    const filesystem::Status mutation = filesystem::vfs::mkdir(destination);
    if (mutation != filesystem::Status::Ok) return mutation;
    const filesystem::Status refreshed = refresh();
    if (refreshed == filesystem::Status::Ok) select_name(*this, name);
    return refreshed;
}

filesystem::Status FileManagerModel::create_file(const char* name) {
    const filesystem::Status validation = filesystem::vfs::validate_name(name);
    if (validation != filesystem::Status::Ok) return validation;
    char destination[filesystem::vfs::kPathCapacity];
    if (!build_child_path(path_, name, destination, sizeof(destination))) return filesystem::Status::InvalidName;
    filesystem::vfs::FileStat existing{};
    const filesystem::Status stat_status = filesystem::vfs::stat(destination, existing);
    if (stat_status == filesystem::Status::Ok) return filesystem::Status::AlreadyExists;
    if (stat_status != filesystem::Status::NotFound) return stat_status;
    const filesystem::Status mutation = filesystem::vfs::touch(destination);
    if (mutation != filesystem::Status::Ok) return mutation;
    const filesystem::Status refreshed = refresh();
    if (refreshed == filesystem::Status::Ok) select_name(*this, name);
    return refreshed;
}

filesystem::Status FileManagerModel::rename_selected(const char* name) {
    if (selected_index() < 0) return filesystem::Status::Unsupported;
    const filesystem::Status validation = filesystem::vfs::validate_name(name);
    if (validation != filesystem::Status::Ok) return validation;
    const int selected = selected_index();
    char source[filesystem::vfs::kPathCapacity];
    char destination[filesystem::vfs::kPathCapacity];
    if (!build_child_path(path_, entries_[selected].name, source, sizeof(source)) ||
        !build_child_path(path_, name, destination, sizeof(destination))) return filesystem::Status::InvalidName;
    if (same_name(entries_[selected].name, name)) return filesystem::Status::AlreadyExists;
    const filesystem::Status mutation = filesystem::vfs::move(source, destination);
    if (mutation != filesystem::Status::Ok) return mutation;
    const filesystem::Status refreshed = refresh();
    if (refreshed == filesystem::Status::Ok) select_name(*this, name);
    return refreshed;
}

filesystem::Status FileManagerModel::remove_selected() {
    const int selected = selected_index();
    if (selected < 0) return filesystem::Status::Unsupported;
    char target[filesystem::vfs::kPathCapacity];
    if (!build_child_path(path_, entries_[selected].name, target, sizeof(target))) return filesystem::Status::InvalidName;
    const filesystem::Status mutation = filesystem::vfs::remove(target);
    if (mutation != filesystem::Status::Ok) return mutation;
    const filesystem::Status refreshed = refresh();
    return refreshed;
}

bool FileManagerModel::select(size_t entry_index) {
    if (entry_index >= entry_count_) return false;
    copy_bounded(selected_name_, entries_[entry_index].name, sizeof(selected_name_));
    has_selection_ = true;
    return true;
}

void FileManagerModel::set_view(ViewMode view) { view_ = view; }
const char* FileManagerModel::current_path() const { return path_; }
size_t FileManagerModel::entry_count() const { return entry_count_; }
const filesystem::vfs::DirectoryEntry* FileManagerModel::entry(size_t index) const {
    return index < entry_count_ ? &entries_[index] : nullptr;
}
int FileManagerModel::selected_index() const { return find_selected(); }
ViewMode FileManagerModel::view_mode() const { return view_; }
bool FileManagerModel::truncated() const { return truncated_; }

int FileManagerModel::find_selected() const {
    if (!has_selection_) return -1;
    for (size_t i = 0; i < entry_count_; ++i) {
        if (same_name(selected_name_, entries_[i].name)) return static_cast<int>(i);
    }
    return -1;
}

} // namespace linux95::gui::file_manager
