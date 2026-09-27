#include "filesystem/filesystem.hpp"

#include "filesystem/fat32.hpp"
#include "filesystem/fat32_write.hpp"

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

Status touch(const char* path)
{
    return fat32::write::touch(path);
}

Status write_file(const char* path, const uint8_t* data, size_t size)
{
    return fat32::write::write_file(path, data, size);
}

Status mkdir(const char* path)
{
    return fat32::write::mkdir(path);
}

Status remove(const char* path)
{
    return fat32::write::remove(path);
}

Status copy_file(const char* source, const char* destination)
{
    return fat32::write::copy_file(source, destination);
}

Status move(const char* source, const char* destination)
{
    return fat32::write::move(source, destination);
}

const VolumeInfo& volume_info()
{
    return current_volume;
}

} // namespace linux95::filesystem
