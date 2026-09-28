#pragma once

#include "filesystem/filesystem.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::filesystem::vfs {

constexpr size_t kFileDescriptorCount = 64;
constexpr int kFirstFileDescriptor = 3;
constexpr size_t kDirectoryHandleCount = 32;
constexpr size_t kPathCapacity = 128;

struct FileStat {
    bool is_directory;
    uint32_t size;
};

struct DirectoryEntry {
    char name[13];
    bool is_directory;
    uint32_t size;
};

void initialize();
Status validate_name(const char* name);
int open(const char* path, Status& status);
Status read(
    int fd,
    uint8_t* buffer,
    size_t size,
    size_t& bytes_read);
Status close(int fd);
Status stat(const char* path, FileStat& info);
Status fstat(int fd, FileStat& info);

int opendir(const char* path, Status& status);
Status readdir(
    int dir_handle,
    DirectoryEntry& entry,
    bool& end);
Status closedir(int dir_handle);

Status touch(const char* path);
Status write_file(const char* path, const uint8_t* data, size_t size);
Status mkdir(const char* path);
Status remove(const char* path);
Status copy_file(const char* source, const char* destination);
Status move(const char* source, const char* destination);

} // namespace linux95::filesystem::vfs
