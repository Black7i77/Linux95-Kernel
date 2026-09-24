#include "filesystem/filesystem.hpp"

#include "filesystem/fat32.hpp"

namespace linux95::filesystem {

namespace {

VolumeInfo current_volume = {};

} // namespace

bool initialize()
{
    current_volume = VolumeInfo{};

    VolumeInfo mounted_volume = {};

    if (!fat32::mount(mounted_volume)) {
        return false;
    }

    current_volume = mounted_volume;
    return true;
}

Status list_directory(
    const char* path,
    EntryVisitor visitor,
    void* context)
{
    return fat32::list_directory(
        path,
        visitor,
        context);
}

Status stat_path(
    const char* path,
    Entry& entry)
{
    return fat32::stat_path(
        path,
        entry);
}

Status read_directory_entry(
    const char* path,
    uint32_t index,
    Entry& entry,
    bool& end)
{
    return fat32::read_directory_entry(
        path,
        index,
        entry,
        end);
}

Status read_file(
    const char* path,
    uint32_t offset,
    uint8_t* buffer,
    size_t buffer_size,
    size_t& bytes_read,
    uint32_t& file_size)
{
    return fat32::read_file(
        path,
        offset,
        buffer,
        buffer_size,
        bytes_read,
        file_size);
}

const VolumeInfo& volume_info()
{
    return current_volume;
}

} // namespace linux95::filesystem
