#include "filesystem/vfs_self_test.hpp"

#include "arch/debug.hpp"
#include "filesystem/vfs.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::filesystem::vfs::self_test {

namespace {

bool string_equal(
    const char* left,
    const char* right)
{
    if (left == nullptr ||
        right == nullptr) {
        return false;
    }

    size_t i = 0;

    while (left[i] != '\0' &&
           right[i] != '\0') {
        if (left[i] != right[i]) {
            return false;
        }

        ++i;
    }

    return left[i] == right[i];
}

} // namespace

bool run()
{
    Status status = Status::Corrupt;

    int fd =
        ::linux95::filesystem::vfs::open(
            "/README.TXT",
            status);

    if (fd != kFirstFileDescriptor ||
        status != Status::Ok) {
        return false;
    }

    debug::write("[PASS] vfs_file_open\n");

    uint8_t buffer[64] = {};
    size_t bytes_read = 999;

    const Status read_status =
        ::linux95::filesystem::vfs::read(
            fd,
            buffer,
            sizeof(buffer),
            bytes_read);

    if (read_status != Status::Ok ||
        bytes_read != 34) {
        ::linux95::filesystem::vfs::close(fd);
        return false;
    }

    constexpr char expected[] =
        "Linux95 FAT32 filesystem online.\r\n";

    static_assert(
        sizeof(expected) - 1 == 34,
        "README fixture must remain 34 bytes");

    for (size_t i = 0;
         i < 34;
         ++i) {
        if (buffer[i] !=
            static_cast<uint8_t>(
                expected[i])) {
            ::linux95::filesystem::vfs::close(fd);
            return false;
        }
    }

    debug::write("[PASS] vfs_file_read\n");

    FileStat file_info = {};

    if (::linux95::filesystem::vfs::fstat(
            fd,
            file_info) != Status::Ok ||
        file_info.is_directory ||
        file_info.size != 34) {
        ::linux95::filesystem::vfs::close(fd);
        return false;
    }

    FileStat docs_info = {};

    if (::linux95::filesystem::vfs::stat(
            "/DOCS",
            docs_info) != Status::Ok ||
        !docs_info.is_directory ||
        docs_info.size != 0) {
        ::linux95::filesystem::vfs::close(fd);
        return false;
    }

    debug::write("[PASS] vfs_stat\n");

    if (::linux95::filesystem::vfs::close(fd) !=
        Status::Ok) {
        return false;
    }

    status = Status::Corrupt;

    const int dir =
        ::linux95::filesystem::vfs::opendir(
            "/",
            status);

    if (dir != 0 ||
        status != Status::Ok) {
        return false;
    }

    debug::write("[PASS] vfs_directory_open\n");

    bool found_readme = false;
    bool found_chain = false;
    bool found_docs = false;

    for (;;) {
        DirectoryEntry entry = {};
        bool end = false;

        const Status dir_status =
            ::linux95::filesystem::vfs::readdir(
                dir,
                entry,
                end);

        if (dir_status != Status::Ok) {
            ::linux95::filesystem::vfs::closedir(dir);
            return false;
        }

        if (end) {
            break;
        }

        if (string_equal(
                entry.name,
                "README.TXT")) {

            if (entry.is_directory ||
                entry.size != 34) {
                ::linux95::filesystem::vfs::closedir(dir);
                return false;
            }

            found_readme = true;

        } else if (string_equal(
                       entry.name,
                       "CHAIN.TXT")) {

            if (entry.is_directory ||
                entry.size != 1536) {
                ::linux95::filesystem::vfs::closedir(dir);
                return false;
            }

            found_chain = true;

        } else if (string_equal(
                       entry.name,
                       "DOCS")) {

            if (!entry.is_directory) {
                ::linux95::filesystem::vfs::closedir(dir);
                return false;
            }

            found_docs = true;
        }
    }

    if (!found_readme ||
        !found_chain ||
        !found_docs) {
        ::linux95::filesystem::vfs::closedir(dir);
        return false;
    }

    debug::write("[PASS] vfs_readdir\n");

    if (::linux95::filesystem::vfs::closedir(dir) !=
        Status::Ok) {
        return false;
    }

    debug::write("[PASS] vfs_self_test\n");

    return true;
}

} // namespace linux95::filesystem::vfs::self_test
