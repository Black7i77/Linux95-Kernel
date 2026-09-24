#include "filesystem/vfs.hpp"

namespace linux95::filesystem::vfs {

namespace {

struct FileDescriptor {
    bool in_use;
    char path[kPathCapacity];
    uint32_t offset;
    uint32_t size;
};

struct DirectoryHandle {
    bool in_use;
    char path[kPathCapacity];
    uint32_t next_index;
};

FileDescriptor file_descriptors[kFileDescriptorCount] = {};
DirectoryHandle directory_handles[kDirectoryHandleCount] = {};

bool copy_path(
    const char* source,
    char destination[kPathCapacity])
{
    if (source == nullptr ||
        source[0] == '\0') {
        return false;
    }

    for (size_t i = 0;
         i < kPathCapacity - 1;
         ++i) {

        destination[i] = source[i];

        if (source[i] == '\0') {
            return true;
        }
    }

    if (source[kPathCapacity - 1] != '\0') {
        return false;
    }

    destination[kPathCapacity - 1] = '\0';
    return true;
}

bool valid_file_descriptor(int fd)
{
    if (fd < kFirstFileDescriptor ||
        fd >= static_cast<int>(
            kFileDescriptorCount)) {
        return false;
    }

    return file_descriptors[fd].in_use;
}

bool valid_dir_handle(int dir_handle)
{
    if (dir_handle < 0 ||
        dir_handle >= static_cast<int>(
            kDirectoryHandleCount)) {
        return false;
    }

    return directory_handles[
        dir_handle].in_use;
}

} // namespace

void initialize()
{
    for (size_t i = 0;
         i < kFileDescriptorCount;
         ++i) {
        file_descriptors[i] =
            FileDescriptor{};
    }

    for (size_t i = 0;
         i < kDirectoryHandleCount;
         ++i) {
        directory_handles[i] =
            DirectoryHandle{};
    }
}

Status stat(
    const char* path,
    FileStat& info)
{
    char safe_path[kPathCapacity] = {};

    if (!copy_path(
            path,
            safe_path)) {
        return Status::Unsupported;
    }

    Entry entry = {};

    const Status status =
        ::linux95::filesystem::stat_path(
            safe_path,
            entry);

    if (status != Status::Ok) {
        return status;
    }

    info.is_directory =
        entry.is_directory;

    info.size =
        entry.size;

    return Status::Ok;
}

int open(
    const char* path,
    Status& status)
{
    char safe_path[kPathCapacity] = {};

    if (!copy_path(
            path,
            safe_path)) {
        status = Status::Unsupported;
        return -1;
    }

    FileStat info = {};

    status =
        vfs::stat(
            safe_path,
            info);

    if (status != Status::Ok) {
        return -1;
    }

    if (info.is_directory) {
        status = Status::IsDirectory;
        return -1;
    }

    for (int fd = kFirstFileDescriptor;
         fd < static_cast<int>(
             kFileDescriptorCount);
         ++fd) {

        if (file_descriptors[fd].in_use) {
            continue;
        }

        FileDescriptor& slot =
            file_descriptors[fd];

        slot = FileDescriptor{};

        for (size_t i = 0;
             i < kPathCapacity;
             ++i) {
            slot.path[i] =
                safe_path[i];
        }

        slot.in_use = true;
        slot.offset = 0;
        slot.size = info.size;

        status = Status::Ok;
        return fd;
    }

    status = Status::TooManyOpenFiles;
    return -1;
}

Status read(
    int fd,
    uint8_t* buffer,
    size_t size,
    size_t& bytes_read)
{
    bytes_read = 0;

    if (!valid_file_descriptor(fd)) {
        return Status::InvalidDescriptor;
    }

    FileDescriptor& slot =
        file_descriptors[fd];

    size_t got = 0;
    uint32_t reported_size = 0;

    const Status status =
        ::linux95::filesystem::read_file(
            slot.path,
            slot.offset,
            buffer,
            size,
            got,
            reported_size);

    if (status != Status::Ok) {
        return status;
    }

    if (reported_size != slot.size) {
        return Status::Corrupt;
    }

    if (got >
        static_cast<size_t>(
            UINT32_MAX - slot.offset)) {
        return Status::Corrupt;
    }

    slot.offset +=
        static_cast<uint32_t>(got);

    bytes_read = got;

    return Status::Ok;
}

Status close(int fd)
{
    if (!valid_file_descriptor(fd)) {
        return Status::InvalidDescriptor;
    }

    file_descriptors[fd] =
        FileDescriptor{};

    return Status::Ok;
}

Status fstat(
    int fd,
    FileStat& info)
{
    if (!valid_file_descriptor(fd)) {
        return Status::InvalidDescriptor;
    }

    const FileDescriptor& slot =
        file_descriptors[fd];

    info.is_directory = false;
    info.size = slot.size;

    return Status::Ok;
}

int opendir(
    const char* path,
    Status& status)
{
    char safe_path[kPathCapacity] = {};

    if (!copy_path(
            path,
            safe_path)) {
        status = Status::Unsupported;
        return -1;
    }

    FileStat info = {};

    status =
        vfs::stat(
            safe_path,
            info);

    if (status != Status::Ok) {
        return -1;
    }

    if (!info.is_directory) {
        status = Status::NotDirectory;
        return -1;
    }

    for (int handle = 0;
         handle < static_cast<int>(
             kDirectoryHandleCount);
         ++handle) {

        if (directory_handles[
                handle].in_use) {
            continue;
        }

        DirectoryHandle& slot =
            directory_handles[handle];

        slot = DirectoryHandle{};

        for (size_t i = 0;
             i < kPathCapacity;
             ++i) {
            slot.path[i] =
                safe_path[i];
        }

        slot.in_use = true;
        slot.next_index = 0;

        status = Status::Ok;
        return handle;
    }

    status =
        Status::TooManyOpenDirectories;

    return -1;
}

Status readdir(
    int dir_handle,
    DirectoryEntry& entry,
    bool& end)
{
    end = false;

    if (!valid_dir_handle(
            dir_handle)) {
        return Status::InvalidHandle;
    }

    DirectoryHandle& slot =
        directory_handles[dir_handle];

    Entry backend_entry = {};
    bool backend_end = false;

    const Status status =
        ::linux95::filesystem::
            read_directory_entry(
                slot.path,
                slot.next_index,
                backend_entry,
                backend_end);

    if (status != Status::Ok) {
        return status;
    }

    if (backend_end) {
        end = true;
        return Status::Ok;
    }

    for (size_t i = 0;
         i < 13;
         ++i) {
        entry.name[i] =
            backend_entry.name[i];
    }

    entry.is_directory =
        backend_entry.is_directory;

    entry.size =
        backend_entry.size;

    ++slot.next_index;

    return Status::Ok;
}

Status closedir(int dir_handle)
{
    if (!valid_dir_handle(
            dir_handle)) {
        return Status::InvalidHandle;
    }

    directory_handles[dir_handle] =
        DirectoryHandle{};

    return Status::Ok;
}

} // namespace linux95::filesystem::vfs
