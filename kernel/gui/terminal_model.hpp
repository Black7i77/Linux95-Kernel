#pragma once

#include "terminal/output.hpp"

#include <stddef.h>

namespace linux95::gui {

class TerminalModel {
public:
    static constexpr size_t kColumns = 120;
    static constexpr size_t kHistoryLines = 64;

    TerminalModel();

    void put_char(char c);
    void clear();

    const char* line(
        size_t visible_index) const;

    size_t line_count() const;
    size_t cursor_column() const;

private:
    char lines_[
        kHistoryLines][
        kColumns + 1];

    size_t line_count_;
    size_t cursor_column_;

    void new_line();
    void scroll();
};

terminal::Output make_output(
    TerminalModel& model);

} // namespace linux95::gui
