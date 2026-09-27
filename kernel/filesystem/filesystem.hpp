#pragma once

#include <stddef.h>
#include <stdint.h>

namespace linux95::filesystem {

enum class Status : uint8_t {
    Ok,
    NotMounted,
    IoError,
    InvalidFilesystem,
    NotFound,
    NotDirectory,
    IsDirectory,
    Corrupt,
    Unsupported,
    InvalidDescriptor,
    InvalidHandle,
    TooManyOpenFiles,
    TooManyOpenDirectories,
    AlreadyExists,
    InvalidName,
    NoSpace,
    DirectoryNotEmpty,
    ReadOnly,
};

struct Entry {
    char name[13];
    bool is_directory;
    uint32_t size;
};

struct VolumeInfo {
    bool mounted;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t root_cluster;
};

using EntryVisitor = bool (*)(
    const Entry& entry,
    void* context);

bool initialize();

Status list_directory(
    const char* path,
    EntryVisitor visitor,
    void* context);

Status stat_path(
    const char* path,
    Entry& entry);

Status read_directory_entry(
    const char* path,
    uint32_t index,
    Entry& entry,
    bool& end);

Status read_file(
    const char* path,
    uint32_t offset,
    uint8_t* buffer,
    size_t buffer_size,
    size_t& bytes_read,
    uint32_t& file_size);

Status touch(const char* path);
Status write_file(const char* path, const uint8_t* data, size_t size);
Status mkdir(const char* path);
Status remove(const char* path);
Status copy_file(const char* source, const char* destination);
Status move(const char* source, const char* destination);

const VolumeInfo& volume_info();

} // namespace linux95::filesystem
