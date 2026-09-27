#include "gui/editor_file.hpp"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

using namespace linux95;

namespace {

uint8_t file_bytes[gui::editor::kTextCapacity + 1];
size_t file_size;
size_t file_offset;
size_t read_calls;
size_t write_calls;
filesystem::Status file_status;
bool directory;
bool missing_parent;
bool parent_not_directory;
char current_path[filesystem::vfs::kPathCapacity];

void reset_file(const char* path, const uint8_t* data, size_t size)
{
    size_t path_length = strlen(path);
    assert(path_length < sizeof(current_path));
    memcpy(current_path, path, path_length + 1);
    memset(file_bytes, 0, sizeof(file_bytes));
    if (size != 0 && data != nullptr) memcpy(file_bytes, data, size);
    file_size = size;
    file_offset = 0;
    read_calls = 0;
    write_calls = 0;
    file_status = filesystem::Status::Ok;
    directory = false;
    missing_parent = false;
    parent_not_directory = false;
}

void test_supported_text_loads_byte_exactly()
{
    const uint8_t expected[] = {'A', 'b', 'c', '\n', ' ', '~'};
    reset_file("/DOCS/NOTE.TXT", expected, sizeof(expected));
    uint8_t storage[gui::editor::kTextCapacity];
    gui::editor::EditorModel model(storage, sizeof(storage));
    assert(gui::editor::open_file(model, "/DOCS/NOTE.TXT") == filesystem::Status::Ok);
    assert(model.length() == sizeof(expected));
    assert(memcmp(model.data(), expected, sizeof(expected)) == 0);
    assert(model.file_exists() && !model.modified());
    assert(write_calls == 0);
}

void test_capacity_and_oversize_are_checked_before_read()
{
    reset_file("/FULL.TXT", nullptr, gui::editor::kTextCapacity);
    memset(file_bytes, 'x', gui::editor::kTextCapacity);
    uint8_t storage[gui::editor::kTextCapacity];
    gui::editor::EditorModel model(storage, sizeof(storage));
    assert(gui::editor::open_file(model, "/FULL.TXT") == filesystem::Status::Ok);
    assert(model.length() == gui::editor::kTextCapacity);

    reset_file("/BIG.TXT", nullptr, gui::editor::kTextCapacity + 1);
    memset(file_bytes, 'x', sizeof(file_bytes));
    gui::editor::EditorModel too_big(storage, sizeof(storage));
    assert(gui::editor::open_file(too_big, "/BIG.TXT") == filesystem::Status::Unsupported);
    assert(read_calls == 0);
    assert(!too_big.file_exists());
}

void test_unsupported_bytes_are_rejected_without_mutation()
{
    const uint8_t unsupported[] = {'A', '\r', '\n'};
    const uint8_t original[] = {'A', '\r', '\n'};
    reset_file("/BAD.TXT", unsupported, sizeof(unsupported));
    uint8_t storage[gui::editor::kTextCapacity];
    gui::editor::EditorModel model(storage, sizeof(storage));
    assert(gui::editor::open_file(model, "/BAD.TXT") == filesystem::Status::Unsupported);
    assert(memcmp(file_bytes, original, sizeof(original)) == 0);
    assert(write_calls == 0);

    const uint8_t rejected[] = {'\t', 0, 1, 0x80};
    for (uint8_t byte : rejected) {
        reset_file("/BAD.TXT", &byte, 1);
        gui::editor::EditorModel rejected_model(storage, sizeof(storage));
        assert(gui::editor::open_file(rejected_model, "/BAD.TXT") == filesystem::Status::Unsupported);
        assert(file_bytes[0] == byte && write_calls == 0);
    }
}

void test_path_types_and_missing_leaf_rules()
{
    uint8_t storage[gui::editor::kTextCapacity];
    gui::editor::EditorModel model(storage, sizeof(storage));
    reset_file("/DOCS", nullptr, 0);
    directory = true;
    assert(gui::editor::open_file(model, "/DOCS") == filesystem::Status::IsDirectory);

    reset_file("/DOCS/MISSING.TXT", nullptr, 0);
    file_status = filesystem::Status::NotFound;
    assert(gui::editor::open_file(model, "/DOCS/MISSING.TXT") == filesystem::Status::Ok);
    assert(model.length() == 0 && !model.file_exists() && !model.modified());
    assert(write_calls == 0);

    reset_file("/MISSING/PATH.TXT", nullptr, 0);
    file_status = filesystem::Status::NotFound;
    missing_parent = true;
    assert(gui::editor::open_file(model, "/MISSING/PATH.TXT") == filesystem::Status::NotFound);
    reset_file("/FILE/PATH.TXT", nullptr, 0);
    file_status = filesystem::Status::NotFound;
    parent_not_directory = true;
    assert(gui::editor::open_file(model, "/FILE/PATH.TXT") == filesystem::Status::NotDirectory);
    reset_file("/BAD?.TXT", nullptr, 0);
    file_status = filesystem::Status::InvalidName;
    assert(gui::editor::open_file(model, "/BAD?.TXT") == filesystem::Status::InvalidName);
}

} // namespace

namespace linux95::filesystem::vfs {

Status stat(const char* path, FileStat& info)
{
    if (strcmp(path, current_path) == 0) {
        if (file_status != Status::Ok) return file_status;
        info = {directory, static_cast<uint32_t>(file_size)};
        return Status::Ok;
    }
    const char* slash = strrchr(current_path, '/');
    if (strcmp(path, "/") == 0 && slash == current_path) {
        info = {true, 0};
        return Status::Ok;
    }
    if (slash != nullptr) {
        char parent[filesystem::vfs::kPathCapacity];
        size_t length = static_cast<size_t>(slash - current_path);
        if (length == 0) length = 1;
        memcpy(parent, current_path, length);
        parent[length] = '\0';
        if (strcmp(path, parent) == 0) {
            if (missing_parent) return Status::NotFound;
            if (parent_not_directory) return Status::NotDirectory;
            info = {true, 0};
            return Status::Ok;
        }
    }
    return Status::NotFound;
}

int open(const char*, Status& status) { file_offset = 0; status = Status::Ok; return 3; }
Status read(int, uint8_t* buffer, size_t size, size_t& bytes_read)
{
    ++read_calls;
    const size_t remaining = file_size - file_offset;
    bytes_read = remaining < size ? remaining : size;
    if (bytes_read != 0) memcpy(buffer, file_bytes + file_offset, bytes_read);
    file_offset += bytes_read;
    return Status::Ok;
}
Status close(int) { return Status::Ok; }
Status write_file(const char*, const uint8_t*, size_t) { ++write_calls; return Status::Ok; }

} // namespace linux95::filesystem::vfs

int main()
{
    test_supported_text_loads_byte_exactly();
    test_capacity_and_oversize_are_checked_before_read();
    test_unsupported_bytes_are_rejected_without_mutation();
    test_path_types_and_missing_leaf_rules();
    return 0;
}
