#include "gui/terminal_model.hpp"
#include "terminal/output.hpp"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

using namespace linux95;
using namespace linux95::gui;

namespace {

void put(
    terminal::Output& output,
    char c)
{
    assert(output.put_char != nullptr);

    output.put_char(
        output.context,
        c);
}

void test_chars_append_at_cursor()
{
    TerminalModel model;

    terminal::Output output =
        make_output(model);

    terminal::write(
        output,
        "abc");

    assert(model.line_count() == 1);
    assert(strcmp(model.line(0), "abc") == 0);
    assert(model.cursor_column() == 3);
}

void test_newline_advances_row()
{
    TerminalModel model;

    terminal::Output output =
        make_output(model);

    terminal::write(
        output,
        "first");

    put(output, '\n');

    terminal::write(
        output,
        "second");

    assert(model.line_count() == 2);
    assert(strcmp(model.line(0), "first") == 0);
    assert(strcmp(model.line(1), "second") == 0);
    assert(model.cursor_column() == 6);
}

void test_backspace_removes_visible_char()
{
    TerminalModel model;

    terminal::Output output =
        make_output(model);

    terminal::write(
        output,
        "abc");

    put(output, '\b');

    assert(strcmp(model.line(0), "ab") == 0);
    assert(model.cursor_column() == 2);

    put(output, '\b');
    put(output, '\b');

    assert(strcmp(model.line(0), "") == 0);
    assert(model.cursor_column() == 0);

    // Must not underflow.
    put(output, '\b');

    assert(strcmp(model.line(0), "") == 0);
    assert(model.cursor_column() == 0);
}

void test_long_line_wraps_at_120_columns()
{
    TerminalModel model;

    terminal::Output output =
        make_output(model);

    for (
        size_t i = 0;
        i < TerminalModel::kColumns;
        ++i) {
        put(output, 'x');
    }

    put(output, 'y');

    assert(model.line_count() == 2);

    const char* first =
        model.line(0);

    assert(first != nullptr);
    assert(strlen(first) == TerminalModel::kColumns);

    for (
        size_t i = 0;
        i < TerminalModel::kColumns;
        ++i) {
        assert(first[i] == 'x');
    }

    assert(strcmp(model.line(1), "y") == 0);
    assert(model.cursor_column() == 1);
}

void test_history_keeps_newest_64_lines()
{
    TerminalModel model;

    terminal::Output output =
        make_output(model);

    for (
        uint64_t i = 0;
        i < 70;
        ++i) {
        if (i != 0) {
            put(output, '\n');
        }

        terminal::write_uint(
            output,
            i);
    }

    assert(
        model.line_count() ==
        TerminalModel::kHistoryLines);

    // 70 total lines minus 64 retained = first visible is 6.
    assert(strcmp(model.line(0), "6") == 0);

    assert(
        strcmp(
            model.line(
                TerminalModel::kHistoryLines - 1),
            "69") == 0);

    assert(model.cursor_column() == 2);
}

void test_clear_resets_history_and_cursor()
{
    TerminalModel model;

    terminal::Output output =
        make_output(model);

    terminal::write(
        output,
        "one\ntwo\nthree");

    assert(model.line_count() == 3);

    assert(output.clear != nullptr);

    output.clear(
        output.context);

    assert(model.line_count() == 1);
    assert(strcmp(model.line(0), "") == 0);
    assert(model.cursor_column() == 0);

    terminal::write(
        output,
        "fresh");

    assert(model.line_count() == 1);
    assert(strcmp(model.line(0), "fresh") == 0);
}

void test_wrap_scroll_stress_preserves_canaries()
{
    constexpr uint64_t kBefore =
        0x1122334455667788ULL;

    constexpr uint64_t kAfter =
        0x8877665544332211ULL;

    struct Guarded {
        uint64_t before;
        TerminalModel model;
        uint64_t after;
    };

    Guarded guarded{
        kBefore,
        TerminalModel{},
        kAfter,
    };

    terminal::Output output =
        make_output(
            guarded.model);

    for (
        size_t i = 0;
        i < 20000;
        ++i) {
        put(
            output,
            static_cast<char>(
                'A' + (i % 26)));

        if ((i % 131) == 0) {
            put(output, '\n');
        }

        if ((i % 197) == 0) {
            put(output, '\b');
        }
    }

    assert(guarded.before == kBefore);
    assert(guarded.after == kAfter);

    assert(
        guarded.model.line_count() <=
        TerminalModel::kHistoryLines);

    assert(
        guarded.model.cursor_column() <=
        TerminalModel::kColumns);
}

} // namespace

int main()
{
    test_chars_append_at_cursor();
    test_newline_advances_row();
    test_backspace_removes_visible_char();
    test_long_line_wraps_at_120_columns();
    test_history_keeps_newest_64_lines();
    test_clear_resets_history_and_cursor();
    test_wrap_scroll_stress_preserves_canaries();

    return 0;
}
