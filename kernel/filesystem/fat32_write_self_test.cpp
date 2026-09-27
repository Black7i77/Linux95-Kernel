#include "filesystem/fat32_write_self_test.hpp"

#include "arch/debug.hpp"
#include "filesystem/vfs.hpp"
#include "storage/disk.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::filesystem::fat32_write_self_test {
namespace {

constexpr uint8_t kPayload[] = {
    'L', 'i', 'n', 'u', 'x', '9', '5', ' ', 'F', 'A', 'T', '3', '2', ' ',
    'w', 'r', 'i', 't', 'e', ' ', 'p', 'r', 'o', 'o', 'f', 0, 0xff, '\n',
};

bool matches_file(const char* path)
{
    using namespace filesystem;
    vfs::FileStat info = {};
    if (vfs::stat(path, info) != Status::Ok || info.is_directory ||
        info.size != sizeof(kPayload)) {
        return false;
    }

    Status status = Status::Corrupt;
    const int fd = vfs::open(path, status);
    if (fd < 0 || status != Status::Ok) {
        return false;
    }

    uint8_t actual[sizeof(kPayload) + 1];
    size_t got = 0;
    bool match = vfs::read(fd, actual, sizeof(actual), got) == Status::Ok &&
                 got == sizeof(kPayload);
    for (size_t index = 0; match && index < sizeof(kPayload); ++index) {
        match = actual[index] == kPayload[index];
    }
    size_t eof_bytes = 1;
    if (match) {
        match = vfs::read(fd, actual, 1, eof_bytes) == Status::Ok &&
                eof_bytes == 0;
    }
    return vfs::close(fd) == Status::Ok && match;
}

bool boot_write_rejected_and_unchanged()
{
    uint8_t before[512];
    uint8_t probe[512];
    uint8_t after[512];
    if (!storage::read_sector(storage::DiskId::Boot, 0, before)) {
        return false;
    }
    for (size_t index = 0; index < sizeof(before); ++index) {
        probe[index] = before[index];
    }
    probe[0] ^= 0x5a;
    if (storage::write_sector(storage::DiskId::Boot, 0, probe) ||
        !storage::read_sector(storage::DiskId::Boot, 0, after)) {
        return false;
    }
    for (size_t index = 0; index < sizeof(before); ++index) {
        if (before[index] != after[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

bool run()
{
    using namespace filesystem;
    vfs::FileStat info = {};

    if (vfs::touch("/TOUCH.TXT") != Status::Ok ||
        vfs::stat("/TOUCH.TXT", info) != Status::Ok ||
        info.is_directory || info.size != 0) {
        return false;
    }
    debug::write("[PASS] fat32_write_touch\n");

    if (vfs::write_file("/TOUCH.TXT", kPayload, sizeof(kPayload)) !=
            Status::Ok || !matches_file("/TOUCH.TXT")) {
        return false;
    }
    debug::write("[PASS] fat32_write_readback\n");

    if (vfs::mkdir("/WRPROOF") != Status::Ok ||
        vfs::stat("/WRPROOF", info) != Status::Ok ||
        !info.is_directory) {
        return false;
    }
    debug::write("[PASS] fat32_write_mkdir\n");

    if (vfs::copy_file("/TOUCH.TXT", "/WRPROOF/COPY.BIN") != Status::Ok ||
        !matches_file("/TOUCH.TXT") || !matches_file("/WRPROOF/COPY.BIN")) {
        return false;
    }
    debug::write("[PASS] fat32_write_copy_readback\n");

    if (vfs::move("/TOUCH.TXT", "/WRPROOF/MOVED.BIN") != Status::Ok ||
        vfs::stat("/TOUCH.TXT", info) != Status::NotFound ||
        !matches_file("/WRPROOF/MOVED.BIN") ||
        !matches_file("/WRPROOF/COPY.BIN")) {
        return false;
    }
    debug::write("[PASS] fat32_write_move_readback\n");

    if (vfs::touch("/SCRATCH.TXT") != Status::Ok ||
        vfs::mkdir("/EMPTY") != Status::Ok ||
        vfs::remove("/SCRATCH.TXT") != Status::Ok ||
        vfs::remove("/EMPTY") != Status::Ok ||
        vfs::stat("/SCRATCH.TXT", info) != Status::NotFound ||
        vfs::stat("/EMPTY", info) != Status::NotFound ||
        !matches_file("/WRPROOF/MOVED.BIN")) {
        return false;
    }
    debug::write("[PASS] fat32_write_remove\n");

    if (!boot_write_rejected_and_unchanged()) {
        return false;
    }
    debug::write("[PASS] fat32_write_boot_guard\n");
    debug::write("[PASS] fat32_write_complete\n");
    return true;
}

} // namespace linux95::filesystem::fat32_write_self_test
