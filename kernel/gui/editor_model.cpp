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
      initialized_(false),
      forced_quit_pending_(false)
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
    forced_quit_pending_ = false;
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

void EditorModel::set_status_message(const char* message)
{
    size_t i = 0;
    if (message != nullptr) {
        while (i + 1 < sizeof(status_) && message[i] != '\0') {
            status_[i] = message[i];
            ++i;
        }
    }
    status_[i] = '\0';
}

void EditorModel::save_succeeded()
{
    file_exists_ = true;
    modified_ = false;
    forced_quit_pending_ = false;
    char message[kStatusCapacity];
    constexpr char prefix[] = "Saved ";
    size_t out = 0;
    for (size_t i = 0; i + 1 < sizeof(message) && i < sizeof(prefix) - 1; ++i) {
        message[out++] = prefix[i];
    }
    for (size_t i = 0; path_[i] != '\0' && out + 1 < sizeof(message); ++i) {
        message[out++] = path_[i];
    }
    message[out] = '\0';
    set_status_message(message);
}

void EditorModel::save_failed(filesystem::Status status)
{
    modified_ = true;
    forced_quit_pending_ = false;
    const char* reason = "unknown error";
    switch (status) {
    case filesystem::Status::NotMounted: reason = "filesystem not mounted"; break;
    case filesystem::Status::IoError: reason = "I/O error"; break;
    case filesystem::Status::InvalidFilesystem: reason = "invalid filesystem"; break;
    case filesystem::Status::NotFound: reason = "not found"; break;
    case filesystem::Status::NotDirectory: reason = "not a directory"; break;
    case filesystem::Status::IsDirectory: reason = "is a directory"; break;
    case filesystem::Status::Corrupt: reason = "filesystem corrupt"; break;
    case filesystem::Status::Unsupported: reason = "unsupported operation"; break;
    case filesystem::Status::InvalidDescriptor: reason = "invalid file descriptor"; break;
    case filesystem::Status::InvalidHandle: reason = "invalid handle"; break;
    case filesystem::Status::TooManyOpenFiles: reason = "too many open files"; break;
    case filesystem::Status::TooManyOpenDirectories: reason = "too many open directories"; break;
    case filesystem::Status::AlreadyExists: reason = "already exists"; break;
    case filesystem::Status::InvalidName: reason = "invalid 8.3 name or path"; break;
    case filesystem::Status::NoSpace: reason = "disk full"; break;
    case filesystem::Status::DirectoryNotEmpty: reason = "directory not empty"; break;
    case filesystem::Status::ReadOnly: reason = "read-only volume"; break;
    case filesystem::Status::Ok: reason = "unknown error"; break;
    }
    char message[kStatusCapacity];
    constexpr char prefix[] = "Save failed: ";
    size_t out = 0;
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i) message[out++] = prefix[i];
    for (size_t i = 0; reason[i] != '\0' && out + 1 < sizeof(message); ++i) {
        message[out++] = reason[i];
    }
    message[out] = '\0';
    set_status_message(message);
}

EditorAction EditorModel::handle_key(const keyboard::KeyEvent& event)
{
    if (!initialized_ || !event.pressed) return EditorAction::None;

    if (event.key == keyboard::KeyCode::Character && event.ctrl &&
        (event.character == 's' || event.character == 'S')) {
        forced_quit_pending_ = false;
        set_status_message("");
        return EditorAction::Save;
    }

    if (event.key == keyboard::KeyCode::Character && event.ctrl &&
        (event.character == 'q' || event.character == 'Q')) {
        if (!modified_) {
            forced_quit_pending_ = false;
            set_status_message("");
            return EditorAction::Quit;
        }
        if (forced_quit_pending_) {
            forced_quit_pending_ = false;
            return EditorAction::Quit;
        }
        forced_quit_pending_ = true;
        set_status_message(
            "Unsaved changes! Press Ctrl+Q again to quit without saving.");
        return EditorAction::None;
    }

    forced_quit_pending_ = false;
    set_status_message("");
    if (event.key == keyboard::KeyCode::Character) {
        if (!event.ctrl && !insert(static_cast<uint8_t>(event.character))) {
            set_status_message("Buffer full or unsupported character");
        }
    } else if (event.key == keyboard::KeyCode::Enter) {
        if (!insert_newline()) set_status_message("Buffer full");
    } else if (event.key == keyboard::KeyCode::Backspace) {
        static_cast<void>(backspace());
    } else if (event.key == keyboard::KeyCode::ArrowLeft) {
        move_left();
    } else if (event.key == keyboard::KeyCode::ArrowRight) {
        move_right();
    } else if (event.key == keyboard::KeyCode::ArrowUp) {
        move_up();
    } else if (event.key == keyboard::KeyCode::ArrowDown) {
        move_down();
    }
    return EditorAction::None;
}

} // namespace linux95::gui::editor
