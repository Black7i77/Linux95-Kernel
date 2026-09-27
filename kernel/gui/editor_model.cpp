#include "gui/editor_model.hpp"

namespace linux95::gui::editor {
namespace {

size_t bounded_length(const char* text, size_t limit)
{
    if (text == nullptr) return limit;
    size_t length = 0;
    while (length < limit && text[length] != '\0') ++length;
    return length;
}

bool supported_text_byte(uint8_t byte)
{
    return byte == '\n' || (byte >= 0x20 && byte <= 0x7E);
}

} // namespace

EditorModel::EditorModel(uint8_t* storage, size_t capacity)
    : storage_(storage),
      capacity_(capacity),
      path_{},
      status_{},
      length_(0),
      cursor_(0),
      file_exists_(false),
      modified_(false),
      initialized_(false)
{
}

bool EditorModel::initialize(
    const char* path,
    const uint8_t* data,
    size_t length,
    bool file_exists)
{
    if (storage_ == nullptr || capacity_ < kTextCapacity || path == nullptr ||
        length > kTextCapacity || (length != 0 && data == nullptr)) {
        return false;
    }
    const size_t path_length = bounded_length(path, sizeof(path_));
    if (path_length == 0 || path_length >= sizeof(path_)) return false;
    for (size_t i = 0; i < length; ++i) {
        if (!supported_text_byte(data[i])) return false;
    }

    if (length != 0 && data != storage_) {
        for (size_t i = 0; i < length; ++i) storage_[i] = data[i];
    }
    for (size_t i = 0; i <= path_length; ++i) path_[i] = path[i];
    status_[0] = '\0';
    length_ = length;
    cursor_ = length;
    file_exists_ = file_exists;
    modified_ = false;
    initialized_ = true;
    return true;
}

bool EditorModel::insert(uint8_t byte)
{
    if (!initialized_ || byte < 0x20 || byte > 0x7E || length_ >= kTextCapacity) {
        return false;
    }
    for (size_t i = length_; i > cursor_; --i) storage_[i] = storage_[i - 1];
    storage_[cursor_++] = byte;
    ++length_;
    modified_ = true;
    return true;
}

bool EditorModel::insert_newline()
{
    if (!initialized_ || length_ >= kTextCapacity) return false;
    for (size_t i = length_; i > cursor_; --i) storage_[i] = storage_[i - 1];
    storage_[cursor_++] = '\n';
    ++length_;
    modified_ = true;
    return true;
}

bool EditorModel::backspace()
{
    if (!initialized_ || cursor_ == 0) return false;
    const size_t removed = cursor_ - 1;
    for (size_t i = removed; i + 1 < length_; ++i) storage_[i] = storage_[i + 1];
    --cursor_;
    --length_;
    modified_ = true;
    return true;
}

void EditorModel::move_left()
{
    if (initialized_ && cursor_ != 0) --cursor_;
}

size_t EditorModel::length() const { return length_; }
size_t EditorModel::cursor() const { return cursor_; }
const uint8_t* EditorModel::data() const { return storage_; }
const char* EditorModel::path() const { return path_; }
bool EditorModel::file_exists() const { return file_exists_; }
bool EditorModel::modified() const { return modified_; }
const char* EditorModel::status_message() const { return status_; }

} // namespace linux95::gui::editor
