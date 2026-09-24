#include "filesystem/vfs.hpp"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {

constexpr char kReadmeData[] = "HELLO\n";
constexpr uint32_t kReadmeSize = 6;

bool fake_io_error = false;

bool same_path(
    const char* path,
    const char* first,
    const char* second)
{
    if (path == nullptr) {
        return false;
    }

    return strcmp(path, first) == 0 ||
           strcmp(path, second) == 0;
}

bool readme_path(const char* path)
{
    return same_path(
        path,
        "/README.TXT",
        "README.TXT");
}

bool docs_path(const char* path)
{
    return same_path(
        path,
        "/DOCS",
        "DOCS");
}

} // namespace

namespace linux95::filesystem {

Status stat_path(
    const char* path,
    Entry& entry)
{
    if (path == nullptr ||
        path[0] == '\0') {
        return Status::Unsupported;
    }

    if (readme_path(path)) {
        entry = Entry{};
        entry.is_directory = false;
        entry.size = kReadmeSize;
        return Status::Ok;
    }

    if (docs_path(path)) {
        entry = Entry{};
        entry.is_directory = true;
        entry.size = 0;
        return Status::Ok;
    }

    if (strcmp(path, "/") == 0) {
        entry = Entry{};
        entry.name[0] = '/';
        entry.name[1] = '\0';
        entry.is_directory = true;
        entry.size = 0;
        return Status::Ok;
    }

    return Status::NotFound;
}

Status read_file(
    const char* path,
    uint32_t offset,
    uint8_t* buffer,
    size_t buffer_size,
    size_t& bytes_read,
    uint32_t& file_size)
{
    bytes_read = 0;
    file_size = 0;

    if (!readme_path(path)) {
        return Status::NotFound;
    }

    file_size = kReadmeSize;

    if (fake_io_error) {
        return Status::IoError;
    }

    if (buffer == nullptr &&
        buffer_size != 0) {
        return Status::Unsupported;
    }

    if (offset >= kReadmeSize ||
        buffer_size == 0) {
        return Status::Ok;
    }

    const size_t start =
        static_cast<size_t>(offset);

    size_t amount =
        static_cast<size_t>(kReadmeSize) -
        start;

    if (amount > buffer_size) {
        amount = buffer_size;
    }

    memcpy(
        buffer,
        kReadmeData + start,
        amount);

    bytes_read = amount;
    return Status::Ok;
}

Status read_directory_entry(
    const char* path,
    uint32_t index,
    Entry& entry,
    bool& end)
{
    (void) index;
    (void) entry;

    end = true;

    if (path == nullptr ||
        path[0] == '\0') {
        return Status::Unsupported;
    }

    if (readme_path(path)) {
        return Status::NotDirectory;
    }

    if (docs_path(path) ||
        strcmp(path, "/") == 0) {
        return Status::Ok;
    }

    return Status::NotFound;
}

} // namespace linux95::filesystem

int main()
{
    using namespace linux95::filesystem;
    namespace vfs =
        linux95::filesystem::vfs;

    Status status = Status::Corrupt;

    vfs::initialize();

    int fd =
        vfs::open(
            "/README.TXT",
            status);

    assert(fd == 3);
    assert(status == Status::Ok);

    vfs::FileStat info = {};

    assert(
        vfs::fstat(fd, info) ==
        Status::Ok);

    assert(!info.is_directory);
    assert(info.size == 6u);

    assert(
        vfs::stat(
            "README.TXT",
            info) == Status::Ok);

    assert(!info.is_directory);
    assert(info.size == 6u);

    uint8_t buffer[8] = {};
    size_t got = 999;

    assert(
        vfs::read(
            fd,
            buffer,
            2,
            got) == Status::Ok);

    assert(got == 2u);
    assert(buffer[0] == 'H');
    assert(buffer[1] == 'E');

    got = 999;

    assert(
        vfs::read(
            fd,
            buffer,
            sizeof(buffer),
            got) == Status::Ok);

    assert(got == 4u);
    assert(buffer[0] == 'L');
    assert(buffer[1] == 'L');
    assert(buffer[2] == 'O');
    assert(buffer[3] == '\n');

    got = 999;

    assert(
        vfs::read(
            fd,
            buffer,
            sizeof(buffer),
            got) == Status::Ok);

    assert(got == 0u);

    assert(vfs::close(fd) == Status::Ok);
    assert(
        vfs::close(fd) ==
        Status::InvalidDescriptor);

    status = Status::Corrupt;

    assert(
        vfs::open(
            "/DOCS",
            status) == -1);

    assert(status == Status::IsDirectory);

    for (int reserved = 0;
         reserved < 3;
         ++reserved) {

        got = 999;

        assert(
            vfs::read(
                reserved,
                buffer,
                1,
                got) ==
            Status::InvalidDescriptor);

        assert(
            vfs::fstat(
                reserved,
                info) ==
            Status::InvalidDescriptor);

        assert(
            vfs::close(reserved) ==
            Status::InvalidDescriptor);
    }

    vfs::initialize();

    for (int expected_fd = 3;
         expected_fd <= 63;
         ++expected_fd) {

        status = Status::Corrupt;

        const int opened =
            vfs::open(
                "README.TXT",
                status);

        assert(opened == expected_fd);
        assert(status == Status::Ok);
    }

    status = Status::Corrupt;

    assert(
        vfs::open(
            "README.TXT",
            status) == -1);

    assert(
        status ==
        Status::TooManyOpenFiles);

    assert(vfs::close(3) == Status::Ok);

    status = Status::Corrupt;

    assert(
        vfs::open(
            "README.TXT",
            status) == 3);

    assert(status == Status::Ok);

    vfs::initialize();

    assert(
        vfs::fstat(
            3,
            info) ==
        Status::InvalidDescriptor);

    status = Status::Corrupt;

    assert(vfs::open(nullptr, status) == -1);
    assert(status == Status::Unsupported);

    status = Status::Corrupt;

    assert(vfs::open("", status) == -1);
    assert(status == Status::Unsupported);

    char too_long[vfs::kPathCapacity + 1];

    for (size_t i = 0;
         i < vfs::kPathCapacity;
         ++i) {
        too_long[i] = 'A';
    }

    too_long[vfs::kPathCapacity] = '\0';

    status = Status::Corrupt;

    assert(
        vfs::open(
            too_long,
            status) == -1);

    assert(status == Status::Unsupported);

    vfs::initialize();

    status = Status::Corrupt;

    fd = vfs::open(
        "/README.TXT",
        status);

    assert(fd == 3);
    assert(status == Status::Ok);

    got = 999;

    assert(
        vfs::read(
            fd,
            buffer,
            2,
            got) == Status::Ok);

    assert(got == 2u);
    assert(buffer[0] == 'H');
    assert(buffer[1] == 'E');

    fake_io_error = true;
    got = 999;

    assert(
        vfs::read(
            fd,
            buffer,
            1,
            got) == Status::IoError);

    fake_io_error = false;
    got = 999;

    assert(
        vfs::read(
            fd,
            buffer,
            1,
            got) == Status::Ok);

    assert(got == 1u);
    assert(buffer[0] == 'L');

    return 0;
}
