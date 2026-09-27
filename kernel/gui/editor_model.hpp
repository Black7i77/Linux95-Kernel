#pragma once

#include "filesystem/vfs.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::gui::editor {

constexpr size_t kTextCapacity = 65536;
constexpr size_t kStatusCapacity = 96;
constexpr size_t kPathCapacity = filesystem::vfs::kPathCapacity;

class EditorModel {
public:
    EditorModel(uint8_t* storage, size_t capacity);

    bool initialize(const char* path, const uint8_t* data, size_t length, bool file_exists);
    bool insert(uint8_t byte);
    bool insert_newline();
    bool backspace();
    void move_left();
    void move_right();
    void move_up();
    void move_down();
    void update_viewport(size_t visible_rows, size_t visible_columns);
    size_t line_count() const;
    bool line_bounds(size_t line, size_t& begin, size_t& end) const;
    size_t line_number() const;
    size_t column_number() const;
    size_t viewport_top_line() const;
    size_t viewport_left_column() const;

    size_t length() const;
    size_t cursor() const;
    const uint8_t* data() const;
    const char* path() const;
    bool file_exists() const;
    bool modified() const;
    const char* status_message() const;

private:
    uint8_t* storage_;
    size_t capacity_;
    char path_[kPathCapacity];
    char status_[kStatusCapacity];
    size_t length_;
    size_t cursor_;
    size_t desired_column_;
    size_t viewport_top_line_;
    size_t viewport_left_column_;
    bool file_exists_;
    bool modified_;
    bool initialized_;

    void cursor_location(size_t& line, size_t& column) const;
    void reset_desired_column();
};

} // namespace linux95::gui::editor
