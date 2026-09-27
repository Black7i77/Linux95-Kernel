#include "gui/editor_file.hpp"

#include "filesystem/vfs.hpp"

namespace linux95::gui::editor {
namespace {

filesystem::Status validate_parent(const char* path)
{
    char parent[kPathCapacity];
    size_t length = 0;
    while (path[length] != '\0') ++length;
    if (length == 0 || length >= kPathCapacity) {
        return filesystem::Status::InvalidName;
    }

    size_t separator = length;
    while (separator != 0 && path[separator - 1] != '/') --separator;
    if (separator == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else if (separator == 1) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        const size_t parent_length = separator - 1;
        for (size_t i = 0; i < parent_length; ++i) parent[i] = path[i];
        parent[parent_length] = '\0';
    }

    filesystem::vfs::FileStat info{};
    const filesystem::Status status = filesystem::vfs::stat(parent, info);
    if (status != filesystem::Status::Ok) return status;
    return info.is_directory
        ? filesystem::Status::Ok
        : filesystem::Status::NotDirectory;
}

} // namespace

filesystem::Status open_file(EditorModel& model, const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        return filesystem::Status::InvalidName;
    }

    filesystem::vfs::FileStat info{};
    filesystem::Status status = filesystem::vfs::stat(path, info);
    if (status == filesystem::Status::NotFound) {
        status = validate_parent(path);
        if (status != filesystem::Status::Ok) return status;
        return model.initialize(path, nullptr, 0, false)
            ? filesystem::Status::Ok
            : filesystem::Status::InvalidName;
    }
    if (status != filesystem::Status::Ok) return status;
    if (info.is_directory) return filesystem::Status::IsDirectory;
    if (info.size > kTextCapacity) return filesystem::Status::Unsupported;

    uint8_t* const buffer = model.load_buffer();
    if (buffer == nullptr) return filesystem::Status::Unsupported;

    int fd = filesystem::vfs::open(path, status);
    if (fd < 0) return status;

    size_t offset = 0;
    while (offset < info.size) {
        size_t bytes_read = 0;
        status = filesystem::vfs::read(
            fd,
            buffer + offset,
            static_cast<size_t>(info.size) - offset,
            bytes_read);
        if (status != filesystem::Status::Ok) break;
        if (bytes_read == 0 || bytes_read > static_cast<size_t>(info.size) - offset) {
            status = filesystem::Status::IoError;
            break;
        }
        offset += bytes_read;
    }

    const filesystem::Status close_status = filesystem::vfs::close(fd);
    if (status == filesystem::Status::Ok && close_status != filesystem::Status::Ok) {
        status = close_status;
    }
    if (status != filesystem::Status::Ok) return status;

    if (!model.initialize_from_storage(path, info.size, true)) {
        return filesystem::Status::Unsupported;
    }
    return filesystem::Status::Ok;
}

filesystem::Status save_file(EditorModel& model)
{
    const filesystem::Status status = filesystem::vfs::write_file(
        model.path(), model.data(), model.length());
    if (status == filesystem::Status::Ok) model.save_succeeded();
    else model.save_failed(status);
    return status;
}

} // namespace linux95::gui::editor
