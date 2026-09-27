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
      desired_column_(0),
      viewport_top_line_(0),
      viewport_left_column_(0),
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
    desired_column_ = 0;
    viewport_top_line_ = 0;
    viewport_left_column_ = 0;
    file_exists_ = file_exists;
    modified_ = false;
    initialized_ = true;
    reset_desired_column();
    return true;
}

bool EditorModel::initialize_from_storage(
    const char* path,
    size_t length,
    bool file_exists)
{
    return initialize(path, storage_, length, file_exists);
}

uint8_t* EditorModel::load_buffer()
{
    return storage_ != nullptr && capacity_ >= kTextCapacity
        ? storage_ : nullptr;
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
    reset_desired_column();
    return true;
}

bool EditorModel::insert_newline()
{
    if (!initialized_ || length_ >= kTextCapacity) return false;
    for (size_t i = length_; i > cursor_; --i) storage_[i] = storage_[i - 1];
    storage_[cursor_++] = '\n';
    ++length_;
    modified_ = true;
    reset_desired_column();
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
    reset_desired_column();
    return true;
}

void EditorModel::move_left()
{
    if (initialized_ && cursor_ != 0) --cursor_;
    reset_desired_column();
}

void EditorModel::move_right()
{
    if (initialized_ && cursor_ < length_) ++cursor_;
    reset_desired_column();
}

size_t EditorModel::line_count() const
{
    size_t lines = 1;
    for (size_t i = 0; i < length_; ++i) {
        if (storage_[i] == '\n') ++lines;
    }
    return lines;
}

bool EditorModel::line_bounds(size_t requested, size_t& begin, size_t& end) const
{
    if (!initialized_ || requested >= line_count()) return false;
    size_t line = 0;
    size_t start = 0;
    for (size_t i = 0; i < length_; ++i) {
        if (storage_[i] != '\n') continue;
        if (line == requested) {
            begin = start;
            end = i;
            return true;
        }
        ++line;
        start = i + 1;
    }
    begin = start;
    end = length_;
    return line == requested;
}

void EditorModel::cursor_location(size_t& line, size_t& column) const
{
    line = 0;
    column = 0;
    for (size_t i = 0; i < cursor_; ++i) {
        if (storage_[i] == '\n') {
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }
}

void EditorModel::reset_desired_column()
{
    size_t line = 0;
    cursor_location(line, desired_column_);
    static_cast<void>(line);
}

void EditorModel::move_up()
{
    if (!initialized_) return;
    size_t line = 0;
    size_t ignored_column = 0;
    cursor_location(line, ignored_column);
    if (line == 0) return;
    size_t begin = 0;
    size_t end = 0;
    if (!line_bounds(line - 1, begin, end)) return;
    const size_t target_column = desired_column_ < end - begin
        ? desired_column_ : end - begin;
    cursor_ = begin + target_column;
}

void EditorModel::move_down()
{
    if (!initialized_) return;
    size_t line = 0;
    size_t ignored_column = 0;
    cursor_location(line, ignored_column);
    if (line + 1 >= line_count()) return;
    size_t begin = 0;
    size_t end = 0;
    if (!line_bounds(line + 1, begin, end)) return;
    const size_t target_column = desired_column_ < end - begin
        ? desired_column_ : end - begin;
    cursor_ = begin + target_column;
}

void EditorModel::update_viewport(size_t visible_rows, size_t visible_columns)
{
    if (!initialized_ || visible_rows == 0 || visible_columns == 0) return;
    size_t line = 0;
    size_t column = 0;
    cursor_location(line, column);

    if (line < viewport_top_line_) {
        viewport_top_line_ = line;
    } else if (line - viewport_top_line_ >= visible_rows) {
        viewport_top_line_ = line - visible_rows + 1;
    }
    const size_t lines = line_count();
    const size_t max_top = lines > visible_rows ? lines - visible_rows : 0;
    if (viewport_top_line_ > max_top) viewport_top_line_ = max_top;

    if (column < viewport_left_column_) {
        viewport_left_column_ = column;
    } else if (column - viewport_left_column_ >= visible_columns) {
        viewport_left_column_ = column - visible_columns + 1;
    }
}

size_t EditorModel::line_number() const
{
    size_t line = 0;
    size_t column = 0;
    cursor_location(line, column);
    static_cast<void>(column);
    return line + 1;
}

size_t EditorModel::column_number() const
{
    size_t line = 0;
    size_t column = 0;
    cursor_location(line, column);
    static_cast<void>(line);
    return column + 1;
}

size_t EditorModel::viewport_top_line() const { return viewport_top_line_; }
size_t EditorModel::viewport_left_column() const { return viewport_left_column_; }

size_t EditorModel::length() const { return length_; }
size_t EditorModel::cursor() const { return cursor_; }
const uint8_t* EditorModel::data() const { return storage_; }
const char* EditorModel::path() const { return path_; }
bool EditorModel::file_exists() const { return file_exists_; }
bool EditorModel::modified() const { return modified_; }
const char* EditorModel::status_message() const { return status_; }

} // namespace linux95::gui::editor
