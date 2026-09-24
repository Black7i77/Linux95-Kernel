#pragma once

#include "filesystem/filesystem.hpp"

namespace linux95::filesystem::fat32 {

bool mount(VolumeInfo& volume);

Status list_directory(
    const char* path,
    EntryVisitor visitor,
    void* context);

Status read_file(
    const char* path,
    uint32_t offset,
    uint8_t* buffer,
    size_t buffer_size,
    size_t& bytes_read,
    uint32_t& file_size);

} // namespace linux95::filesystem::fat32
