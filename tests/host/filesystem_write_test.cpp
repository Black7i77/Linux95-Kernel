#include "filesystem/filesystem.hpp"
#include "filesystem/fat32.hpp"
#include "filesystem/fat32_write.hpp"
#include "filesystem/vfs.hpp"
#include "storage/disk.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <type_traits>

namespace {

using linux95::filesystem::Status;

enum class Operation { Touch, Write, Mkdir, Remove, Copy, Move };

Operation expected_operation;
const char* expected_source = nullptr;
const char* expected_destination = nullptr;
const uint8_t* expected_data = nullptr;
size_t expected_size = 0;
Status backend_status = Status::Ok;
unsigned calls = 0;
bool mounted = true;

Status called(Operation operation, const char* source,
              const char* destination = nullptr,
              const uint8_t* data = nullptr, size_t size = 0)
{
    assert(operation == expected_operation);
    assert(source == expected_source);
    assert(strcmp(source, expected_source) == 0);
    assert(destination == expected_destination);
    if (destination != nullptr) {
        assert(strcmp(destination, expected_destination) == 0);
    }
    assert(data == expected_data);
    assert(size == expected_size);
    if (size != 0) {
        assert(memcmp(data, expected_data, size) == 0);
    }
    ++calls;
    return mounted ? backend_status : Status::NotMounted;
}

void expect(Operation operation, const char* source, Status status,
            const char* destination = nullptr,
            const uint8_t* data = nullptr, size_t size = 0)
{
    expected_operation = operation;
    expected_source = source;
    expected_destination = destination;
    expected_data = data;
    expected_size = size;
    backend_status = status;
    calls = 0;
}

template <typename Call>
void check(Call call, Status status)
{
    assert(call() == status);
    assert(calls == 1);
}

uint8_t boot_sector[512] = {};
uint8_t test_sector[512] = {};
unsigned ata_boot_writes = 0;
unsigned ata_test_writes = 0;

} // namespace

namespace linux95::filesystem::fat32 {

bool mount(VolumeInfo& volume)
{
    volume = {};
    volume.mounted = ::mounted;
    return ::mounted;
}

Status list_directory(const char*, EntryVisitor, void*)
{
    return Status::Unsupported;
}

Status stat_path(const char* path, Entry& entry)
{
    if (strcmp(path, "/READ.TXT") == 0) {
        entry = {};
        entry.size = 3;
        return Status::Ok;
    }
    if (strcmp(path, "/DIR") == 0) {
        entry = {};
        entry.is_directory = true;
        return Status::Ok;
    }
    return Status::NotFound;
}

Status read_directory_entry(const char* path, uint32_t index,
                            Entry& entry, bool& end)
{
    assert(strcmp(path, "/DIR") == 0);
    end = index != 0;
    if (!end) {
        entry = {};
        memcpy(entry.name, "READ.TXT", 9);
        entry.size = 3;
    }
    return Status::Ok;
}

Status read_file(const char* path, uint32_t offset, uint8_t* buffer,
                 size_t size, size_t& bytes_read, uint32_t& file_size)
{
    assert(strcmp(path, "/READ.TXT") == 0);
    assert(offset <= 3);
    file_size = 3;
    bytes_read = size < 3 - offset ? size : 3 - offset;
    if (bytes_read != 0) {
        memcpy(buffer, "A\0B" + offset, bytes_read);
    }
    return Status::Ok;
}

} // namespace linux95::filesystem::fat32

namespace linux95::filesystem::fat32::write {

Status touch(const char* path) { return called(Operation::Touch, path); }
Status write_file(const char* path, const uint8_t* data, size_t size)
{ return called(Operation::Write, path, nullptr, data, size); }
Status mkdir(const char* path) { return called(Operation::Mkdir, path); }
Status remove(const char* path) { return called(Operation::Remove, path); }
Status copy_file(const char* source, const char* destination)
{ return called(Operation::Copy, source, destination); }
Status move(const char* source, const char* destination)
{ return called(Operation::Move, source, destination); }

} // namespace linux95::filesystem::fat32::write

namespace linux95::storage::ata {

bool identify(Drive, DeviceInfo& info)
{
    info = {true, true, 2, {}};
    return true;
}

bool read_sector(Drive drive, uint32_t lba, uint8_t* buffer)
{
    assert(lba == 0);
    memcpy(buffer, drive == Drive::Master ? boot_sector : test_sector, 512);
    return true;
}

bool write_sector(Drive drive, uint32_t lba, const uint8_t* buffer)
{
    assert(lba == 0);
    if (drive == Drive::Master) {
        ++ata_boot_writes;
        memcpy(boot_sector, buffer, 512);
    } else {
        ++ata_test_writes;
        memcpy(test_sector, buffer, 512);
    }
    return true;
}

} // namespace linux95::storage::ata

int main()
{
    namespace filesystem = linux95::filesystem;
    namespace storage = linux95::storage;
    using linux95::filesystem::Status;
    namespace vfs = linux95::filesystem::vfs;

    static_assert(std::is_same_v<decltype(&filesystem::write_file),
        Status (*)(const char*, const uint8_t*, size_t)>);
    static_assert(std::is_same_v<decltype(&vfs::write_file),
        Status (*)(const char*, const uint8_t*, size_t)>);

    const char first[] = "/DIR/A.TXT";
    const char second[] = "/DIR/B.TXT";
    const uint8_t bytes[] = {0, 0xff, 'Z', 0x7f};

    assert(filesystem::initialize());
    vfs::initialize();

    expect(Operation::Touch, first, Status::AlreadyExists);
    check([&] { return filesystem::touch(first); }, Status::AlreadyExists);
    expect(Operation::Touch, first, Status::NoSpace);
    check([&] { return vfs::touch(first); }, Status::NoSpace);

    expect(Operation::Write, first, Status::IoError, nullptr, bytes, sizeof bytes);
    check([&] { return filesystem::write_file(first, bytes, sizeof bytes); }, Status::IoError);
    expect(Operation::Write, first, Status::InvalidName, nullptr, bytes, sizeof bytes);
    check([&] { return vfs::write_file(first, bytes, sizeof bytes); }, Status::InvalidName);
    expect(Operation::Write, first, Status::Ok, nullptr, nullptr, 0);
    check([&] { return vfs::write_file(first, nullptr, 0); }, Status::Ok);

    expect(Operation::Mkdir, first, Status::NotDirectory);
    check([&] { return filesystem::mkdir(first); }, Status::NotDirectory);
    expect(Operation::Mkdir, first, Status::AlreadyExists);
    check([&] { return vfs::mkdir(first); }, Status::AlreadyExists);

    expect(Operation::Remove, first, Status::DirectoryNotEmpty);
    check([&] { return filesystem::remove(first); }, Status::DirectoryNotEmpty);
    expect(Operation::Remove, first, Status::NotFound);
    check([&] { return vfs::remove(first); }, Status::NotFound);

    expect(Operation::Copy, first, Status::IsDirectory, second);
    check([&] { return filesystem::copy_file(first, second); }, Status::IsDirectory);
    expect(Operation::Copy, first, Status::AlreadyExists, second);
    check([&] { return vfs::copy_file(first, second); }, Status::AlreadyExists);

    expect(Operation::Move, first, Status::Unsupported, second);
    check([&] { return filesystem::move(first, second); }, Status::Unsupported);
    expect(Operation::Move, first, Status::Corrupt, second);
    check([&] { return vfs::move(first, second); }, Status::Corrupt);

    mounted = false;
    assert(!filesystem::initialize());
    expect(Operation::Touch, first, Status::ReadOnly);
    check([&] { return filesystem::touch(first); }, Status::NotMounted);
    expect(Operation::Touch, first, Status::ReadOnly);
    check([&] { return vfs::touch(first); }, Status::NotMounted);
    mounted = true;
    assert(filesystem::initialize());
    expect(Operation::Touch, first, Status::ReadOnly);
    check([&] { return vfs::touch(first); }, Status::ReadOnly);

    Status status = Status::Corrupt;
    const int fd = vfs::open("/READ.TXT", status);
    assert(fd == 3 && status == Status::Ok);
    vfs::FileStat stat = {};
    assert(vfs::fstat(fd, stat) == Status::Ok && stat.size == 3);
    assert(vfs::stat("/DIR", stat) == Status::Ok && stat.is_directory);
    uint8_t readback[3] = {};
    size_t got = 0;
    assert(vfs::read(fd, readback, sizeof readback, got) == Status::Ok);
    assert(got == 3 && memcmp(readback, "A\0B", 3) == 0);
    assert(vfs::close(fd) == Status::Ok);
    const int dir = vfs::opendir("/DIR", status);
    assert(dir == 0 && status == Status::Ok);
    vfs::DirectoryEntry entry = {};
    bool end = false;
    assert(vfs::readdir(dir, entry, end) == Status::Ok);
    assert(!end && strcmp(entry.name, "READ.TXT") == 0);
    assert(vfs::readdir(dir, entry, end) == Status::Ok && end);
    assert(vfs::closedir(dir) == Status::Ok);

    memset(boot_sector, 0x5a, sizeof boot_sector);
    uint8_t attempted[512];
    memset(attempted, 0xa5, sizeof attempted);
    assert(storage::initialize());
    assert(!storage::write_sector(storage::DiskId::Boot, 0, attempted));
    assert(ata_boot_writes == 0);
    uint8_t observed[512] = {};
    assert(storage::read_sector(storage::DiskId::Boot, 0, observed));
    assert(memcmp(observed, boot_sector, 512) == 0);
    assert(memcmp(observed, attempted, 512) != 0);
    assert(storage::write_sector(storage::DiskId::Test, 0, attempted));
    assert(ata_test_writes == 1);
    assert(memcmp(test_sector, attempted, 512) == 0);

    puts("filesystem write facade tests: PASS");
}
