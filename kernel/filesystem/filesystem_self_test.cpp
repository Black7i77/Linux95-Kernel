#include "filesystem/filesystem_self_test.hpp"

#include "arch/debug.hpp"
#include "filesystem/filesystem.hpp"

#include <stddef.h>

namespace linux95::filesystem::self_test {

namespace {

bool string_equal(
    const char* left,
    const char* right)
{
    if (left == nullptr || right == nullptr) {
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

struct RootState {
    bool readme;
    bool chain;
    bool docs;
};

bool inspect_root(
    const Entry& entry,
    void* context)
{
    RootState* const state =
        static_cast<RootState*>(context);

    if (state == nullptr) {
        return false;
    }

    if (string_equal(
            entry.name,
            "README.TXT")) {
        state->readme = true;
    } else if (string_equal(
                   entry.name,
                   "CHAIN.TXT")) {
        state->chain = true;
    } else if (string_equal(
                   entry.name,
                   "DOCS")) {
        state->docs =
            entry.is_directory;
    }

    return true;
}

struct DocsState {
    bool kernel;
};

bool inspect_docs(
    const Entry& entry,
    void* context)
{
    DocsState* const state =
        static_cast<DocsState*>(context);

    if (state == nullptr) {
        return false;
    }

    if (string_equal(
            entry.name,
            "KERNEL.TXT")) {
        state->kernel = true;
    }

    return true;
}

} // namespace

bool run()
{
    RootState root = {
        false,
        false,
        false,
    };

    const Status status =
        list_directory(
            "/",
            inspect_root,
            &root);

    if (status != Status::Ok) {
        return false;
    }

    if (!root.readme ||
        !root.chain ||
        !root.docs) {
        return false;
    }

    debug::write("[PASS] fat32_root_list\n");

    uint8_t lookup_buffer[1];
    size_t lookup_bytes = 999;
    uint32_t lookup_size = 999;

    const Status lookup_status =
        read_file(
            "/readme.txt",
            0,
            lookup_buffer,
            0,
            lookup_bytes,
            lookup_size);

    if (lookup_status != Status::Ok ||
        lookup_bytes != 0 ||
        lookup_size != 34) {
        return false;
    }

    debug::write("[PASS] fat32_file_lookup\n");

    uint8_t read_buffer[64];
    size_t read_bytes = 999;
    uint32_t read_size = 999;

    const Status read_status =
        read_file(
            "/README.TXT",
            0,
            read_buffer,
            sizeof(read_buffer),
            read_bytes,
            read_size);

    if (read_status != Status::Ok ||
        read_bytes != 34 ||
        read_size != 34) {
        return false;
    }

    const char expected_readme[] =
        "Linux95 FAT32 filesystem online.\r\n";

    for (size_t i = 0; i < 34; ++i) {
        if (read_buffer[i] !=
            static_cast<uint8_t>(
                expected_readme[i])) {
            return false;
        }
    }

    debug::write("[PASS] fat32_file_read\n");

    DocsState docs = {
        false,
    };

    const Status docs_status =
        list_directory(
            "/dOcS",
            inspect_docs,
            &docs);

    if (docs_status != Status::Ok ||
        !docs.kernel) {
        return false;
    }

    debug::write("[PASS] fat32_subdirectory\n");

    uint8_t chain_buffer[1536];
    size_t chain_bytes = 999;
    uint32_t chain_size = 999;

    const Status chain_status =
        read_file(
            "/CHAIN.TXT",
            0,
            chain_buffer,
            sizeof(chain_buffer),
            chain_bytes,
            chain_size);

    if (chain_status != Status::Ok ||
        chain_bytes != 1536 ||
        chain_size != 1536) {
        return false;
    }

    for (size_t i = 0;
         i < sizeof(chain_buffer);
         ++i) {

        const uint8_t expected =
            static_cast<uint8_t>(
                'A' + (i % 26));

        if (chain_buffer[i] != expected) {
            return false;
        }
    }

    debug::write("[PASS] fat32_cluster_chain\n");
    debug::write("[PASS] filesystem_self_test\n");

    return true;
}

} // namespace linux95::filesystem::self_test
