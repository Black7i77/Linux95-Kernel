#include "gui/terminal_model.hpp"

namespace linux95::gui {
namespace {

void output_put_char(
    void* context,
    char c)
{
    if (context == nullptr) {
        return;
    }

    auto* model =
        static_cast<TerminalModel*>(
            context);

    model->put_char(c);
}

void output_clear(
    void* context)
{
    if (context == nullptr) {
        return;
    }

    auto* model =
        static_cast<TerminalModel*>(
            context);

    model->clear();
}

void output_set_color(
    void*,
    uint8_t,
    uint8_t)
{
    // The v1 graphical terminal stores text only.
    // Rendering colors belong to the GUI layer.
}

} // namespace

TerminalModel::TerminalModel()
    : lines_{},
      line_count_(1),
      cursor_column_(0)
{
}

void TerminalModel::clear()
{
    for (
        size_t row = 0;
        row < kHistoryLines;
        ++row) {
        for (
            size_t column = 0;
            column <= kColumns;
            ++column) {
            lines_[row][column] = '\0';
        }
    }

    line_count_ = 1;
    cursor_column_ = 0;
}

void TerminalModel::scroll()
{
    for (
        size_t row = 1;
        row < kHistoryLines;
        ++row) {
        for (
            size_t column = 0;
            column <= kColumns;
            ++column) {
            lines_[row - 1][column] =
                lines_[row][column];
        }
    }

    for (
        size_t column = 0;
        column <= kColumns;
        ++column) {
        lines_[
            kHistoryLines - 1][
            column] = '\0';
    }

    line_count_ =
        kHistoryLines;
}

void TerminalModel::new_line()
{
    if (
        line_count_ <
        kHistoryLines) {
        ++line_count_;

        for (
            size_t column = 0;
            column <= kColumns;
            ++column) {
            lines_[
                line_count_ - 1][
                column] = '\0';
        }
    } else {
        scroll();
    }

    cursor_column_ = 0;
}

void TerminalModel::put_char(
    char c)
{
    if (c == '\n') {
        new_line();
        return;
    }

    if (c == '\b') {
        if (cursor_column_ == 0) {
            return;
        }

        --cursor_column_;

        lines_[
            line_count_ - 1][
            cursor_column_] = '\0';

        return;
    }

    if (c == '\r') {
        return;
    }

    if (
        cursor_column_ >=
        kColumns) {
        new_line();
    }

    lines_[
        line_count_ - 1][
        cursor_column_] = c;

    ++cursor_column_;

    lines_[
        line_count_ - 1][
        cursor_column_] = '\0';
}

const char* TerminalModel::line(
    size_t visible_index) const
{
    if (
        visible_index >=
        line_count_) {
        return nullptr;
    }

    return lines_[visible_index];
}

size_t TerminalModel::line_count() const
{
    return line_count_;
}

size_t TerminalModel::cursor_column() const
{
    return cursor_column_;
}

terminal::Output make_output(
    TerminalModel& model)
{
    return terminal::Output{
        &model,
        output_put_char,
        output_clear,
        output_set_color,
    };
}

} // namespace linux95::gui
