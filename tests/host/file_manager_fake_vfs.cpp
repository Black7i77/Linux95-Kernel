#include "file_manager_fake_vfs.hpp"

#include <string.h>
#include <strings.h>

namespace linux95::filesystem::vfs {
struct Item {
    char directory[kPathCapacity];
    DirectoryEntry entry;
};
Item items[512];
size_t item_count;
char opened_path[kPathCapacity];
char injected_open_path[kPathCapacity];
Status open_status;
Status read_error;
Status close_status;
size_t read_error_after;
size_t read_count;
size_t closes;
size_t validations;
bool handle_open;
Status mutation_status;
Status stat_error;
size_t stats, touches, mkdirs, moves, removes;

void copy(char* to, const char* from, size_t capacity) {
    size_t i = 0;
    while (from[i] && i + 1 < capacity) { to[i] = from[i]; ++i; }
    to[i] = '\0';
}

Status validate_name(const char* name) {
    ++validations;
    if (!name || !name[0] || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) return Status::InvalidName;
    return Status::Ok;
}

int opendir(const char* path, Status& status) {
    if (open_status != Status::Ok && strcmp(path, injected_open_path) == 0) {
        status = open_status;
        return -1;
    }
    bool directory_exists = strcmp(path, "/") == 0;
    bool file_exists = false;
    for (size_t i = 0; i < item_count; ++i) {
        if (strcmp(items[i].directory, path) == 0 && items[i].entry.is_directory) directory_exists = true;
        char child_path[kPathCapacity];
        const size_t parent_length = strlen(items[i].directory);
        const size_t name_length = strlen(items[i].entry.name);
        const bool root = parent_length == 1 && items[i].directory[0] == '/';
        const size_t needed = parent_length + (root ? 0 : 1) + name_length + 1;
        if (needed <= sizeof(child_path)) {
            size_t offset = parent_length;
            memcpy(child_path, items[i].directory, parent_length);
            if (!root) child_path[offset++] = '/';
            memcpy(child_path + offset, items[i].entry.name, name_length + 1);
            if (strcmp(child_path, path) == 0) {
                if (items[i].entry.is_directory) directory_exists = true;
                else file_exists = true;
            }
        }
    }
    if (!directory_exists) {
        status = file_exists ? Status::NotDirectory : Status::NotFound;
        return -1;
    }
    copy(opened_path, path, sizeof(opened_path));
    read_count = 0;
    handle_open = true;
    status = Status::Ok;
    return 1;
}

Status readdir(int handle, DirectoryEntry& entry, bool& end) {
    if (handle != 1 || !handle_open) return Status::InvalidHandle;
    if (read_count == read_error_after && read_error != Status::Ok) return read_error;
    size_t seen = 0;
    for (size_t i = 0; i < item_count; ++i) {
        if (strcmp(items[i].directory, opened_path) == 0) {
            if (seen++ == read_count) { entry = items[i].entry; ++read_count; end = false; return Status::Ok; }
        }
    }
    end = true;
    return Status::Ok;
}

Status closedir(int handle) {
    if (handle != 1 || !handle_open) return Status::InvalidHandle;
    handle_open = false;
    ++closes;
    return close_status;
}

Status stat(const char* path, FileStat& result) {
    ++stats;
    if (stat_error != Status::Ok) return stat_error;
    for (size_t i = 0; i < item_count; ++i) {
        char full[kPathCapacity];
        const size_t parent = strlen(items[i].directory);
        const bool root = parent == 1 && items[i].directory[0] == '/';
        size_t offset = parent;
        memcpy(full, items[i].directory, parent);
        if (!root) full[offset++] = '/';
        const size_t length = strlen(items[i].entry.name);
        memcpy(full + offset, items[i].entry.name, length + 1);
        if (strcasecmp(full, path) == 0) {
            result.is_directory = items[i].entry.is_directory;
            result.size = items[i].entry.size;
            return Status::Ok;
        }
    }
    return Status::NotFound;
}

Status touch(const char* path) {
    ++touches;
    if (mutation_status != Status::Ok) return mutation_status;
    for (size_t i = 0; i < item_count; ++i) {
        char full[kPathCapacity];
        const size_t parent_length = strlen(items[i].directory);
        const bool root = parent_length == 1 && items[i].directory[0] == '/';
        size_t offset = parent_length;
        memcpy(full, items[i].directory, parent_length);
        if (!root) full[offset++] = '/';
        const size_t name_length = strlen(items[i].entry.name);
        memcpy(full + offset, items[i].entry.name, name_length + 1);
        if (strcasecmp(full, path) == 0)
            return items[i].entry.is_directory ? Status::IsDirectory : Status::Ok;
    }
    char parent[kPathCapacity]; copy(parent, path, sizeof(parent));
    char* slash = strrchr(parent, '/'); if (!slash) return Status::InvalidName;
    char name[13]; copy(name, slash + 1, sizeof(name));
    if (slash == parent) slash[1] = '\0'; else *slash = '\0';
    file_manager_fake_vfs::add(parent, name, false, 0);
    return Status::Ok;
}

Status mkdir(const char* path) {
    ++mkdirs;
    if (mutation_status != Status::Ok) return mutation_status;
    for (size_t i = 0; i < item_count; ++i) {
        char full[kPathCapacity];
        const size_t parent_length = strlen(items[i].directory);
        const bool root = parent_length == 1 && items[i].directory[0] == '/';
        size_t offset = parent_length;
        memcpy(full, items[i].directory, parent_length);
        if (!root) full[offset++] = '/';
        const size_t name_length = strlen(items[i].entry.name);
        memcpy(full + offset, items[i].entry.name, name_length + 1);
        if (strcasecmp(full, path) == 0) return Status::AlreadyExists;
    }
    char parent[kPathCapacity]; copy(parent, path, sizeof(parent));
    char* slash = strrchr(parent, '/'); if (!slash) return Status::InvalidName;
    char name[13]; copy(name, slash + 1, sizeof(name));
    if (slash == parent) slash[1] = '\0'; else *slash = '\0';
    file_manager_fake_vfs::add(parent, name, true, 0);
    return Status::Ok;
}

Status move(const char* from, const char* to) {
    ++moves;
    if (mutation_status != Status::Ok) return mutation_status;
    char old_parent[kPathCapacity], new_parent[kPathCapacity];
    copy(old_parent, from, sizeof(old_parent)); copy(new_parent, to, sizeof(new_parent));
    char *old_slash = strrchr(old_parent, '/'), *new_slash = strrchr(new_parent, '/');
    if (!old_slash || !new_slash) return Status::InvalidName;
    char old_name[13], new_name[13];
    copy(old_name, old_slash + 1, sizeof(old_name)); copy(new_name, new_slash + 1, sizeof(new_name));
    if (old_slash == old_parent) old_slash[1] = '\0'; else *old_slash = '\0';
    if (new_slash == new_parent) new_slash[1] = '\0'; else *new_slash = '\0';
    if (strcasecmp(old_parent, new_parent) != 0) return Status::Unsupported;
    for (size_t i = 0; i < item_count; ++i) {
        if (!strcmp(items[i].directory, old_parent) && !strcasecmp(items[i].entry.name, old_name)) {
            for (size_t j = 0; j < item_count; ++j)
                if (!strcmp(items[j].directory, new_parent) && !strcasecmp(items[j].entry.name, new_name))
                    return Status::AlreadyExists;
            copy(items[i].entry.name, new_name, sizeof(items[i].entry.name));
            return Status::Ok;
        }
    }
    return Status::NotFound;
}

Status remove(const char* path) {
    ++removes;
    if (mutation_status != Status::Ok) return mutation_status;
    char parent[kPathCapacity]; copy(parent, path, sizeof(parent));
    char* slash = strrchr(parent, '/'); if (!slash) return Status::InvalidName;
    char name[13]; copy(name, slash + 1, sizeof(name));
    if (slash == parent) slash[1] = '\0'; else *slash = '\0';
    for (size_t i = 0; i < item_count; ++i) {
        if (!strcmp(items[i].directory, parent) && !strcasecmp(items[i].entry.name, name)) {
            if (items[i].entry.is_directory)
                for (size_t j = 0; j < item_count; ++j)
                    if (!strcmp(items[j].directory, path)) return Status::DirectoryNotEmpty;
            for (size_t j = i + 1; j < item_count; ++j) items[j - 1] = items[j];
            --item_count;
            return Status::Ok;
        }
    }
    return Status::NotFound;
}
}

namespace file_manager_fake_vfs {
using namespace linux95::filesystem::vfs;
void reset() {
    item_count = 0; open_status = linux95::filesystem::Status::Ok;
    read_error = linux95::filesystem::Status::Ok; read_error_after = static_cast<size_t>(-1);
    close_status = linux95::filesystem::Status::Ok;
    read_count = closes = validations = 0; handle_open = false; opened_path[0] = '\0';
    injected_open_path[0] = '\0';
    mutation_status = stat_error = linux95::filesystem::Status::Ok;
    stats = touches = mkdirs = moves = removes = 0;
}
void add(const char* directory, const char* name, bool is_directory, uint32_t size) {
    for (size_t i = 0; i < linux95::filesystem::vfs::item_count; ++i) {
        auto& existing = linux95::filesystem::vfs::items[i];
        if (strcmp(existing.directory, directory) == 0 && strcasecmp(existing.entry.name, name) == 0) {
            linux95::filesystem::vfs::copy(existing.entry.name, name, sizeof(existing.entry.name));
            existing.entry.is_directory = is_directory;
            existing.entry.size = size;
            return;
        }
    }
    auto& item = linux95::filesystem::vfs::items[item_count++];
    linux95::filesystem::vfs::copy(item.directory, directory, sizeof(item.directory));
    linux95::filesystem::vfs::copy(item.entry.name, name, sizeof(item.entry.name));
    item.entry.is_directory = is_directory; item.entry.size = size;
}
void set_open_status(const char* path, linux95::filesystem::Status status) {
    linux95::filesystem::vfs::copy(injected_open_path, path, sizeof(injected_open_path)); open_status = status;
}
void set_read_error_after(size_t entries, linux95::filesystem::Status status) { read_error_after = entries; read_error = status; }
void set_close_status(linux95::filesystem::Status status) { close_status = status; }
size_t close_count() { return closes; }
size_t validate_count() { return validations; }
size_t stat_count() { return stats; }
size_t touch_count() { return touches; }
size_t mkdir_count() { return mkdirs; }
size_t move_count() { return moves; }
size_t remove_count() { return removes; }
void set_mutation_status(linux95::filesystem::Status status) { mutation_status = status; }
void set_stat_error(linux95::filesystem::Status status) { stat_error = status; }
}
