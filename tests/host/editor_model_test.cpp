#include "gui/editor_model.hpp"

#include <assert.h>
#include <stdint.h>

using namespace linux95::gui::editor;

static uint8_t storage[kTextCapacity];

static void test_empty_initialize_and_insertions()
{
    EditorModel model(storage, sizeof(storage));
    assert(model.initialize("NOTES.TXT", nullptr, 0, false));
    assert(model.length() == 0);
    assert(model.cursor() == 0);
    assert(model.line_count() == 1);
    size_t empty_begin = 9;
    size_t empty_end = 9;
    assert(model.line_bounds(0, empty_begin, empty_end));
    assert(empty_begin == 0 && empty_end == 0);
    assert(model.data() == storage);
    assert(model.path()[0] == 'N');
    assert(!model.file_exists());
    assert(!model.modified());

    assert(model.insert('a'));
    assert(model.insert('c'));
    assert(model.length() == 2 && model.cursor() == 2);
    assert(storage[0] == 'a' && storage[1] == 'c');
    model.move_left();
    assert(model.insert('b'));
    assert(model.cursor() == 2 && model.length() == 3);
    assert(storage[0] == 'a' && storage[1] == 'b' && storage[2] == 'c');
    assert(model.modified());
}

static void test_exact_capacity_and_refused_overflow()
{
    EditorModel model(storage, sizeof(storage));
    assert(model.initialize("FULL.TXT", nullptr, 0, false));
    for (size_t i = 0; i < kTextCapacity; ++i) assert(model.insert('x'));
    assert(model.length() == 65536);
    assert(model.cursor() == 65536);
    assert(!model.insert('y'));
    assert(model.length() == 65536 && model.cursor() == 65536);
    assert(storage[0] == 'x' && storage[65535] == 'x');
    assert(model.modified());
    assert(model.initialize("FULL.TXT", storage, kTextCapacity, true));
    assert(!model.modified());
    assert(!model.insert('y'));
    assert(model.length() == 65536 && model.cursor() == 65536);
    assert(!model.modified());
}

static void test_newline_and_backspace()
{
    EditorModel model(storage, sizeof(storage));
    assert(model.initialize("LINES.TXT", nullptr, 0, false));
    assert(!model.backspace());
    assert(model.cursor() == 0 && model.length() == 0 && !model.modified());
    assert(model.insert('a'));
    assert(model.insert_newline());
    assert(model.insert('b'));
    assert(model.length() == 3 && storage[1] == '\n');
    assert(model.backspace());
    assert(model.length() == 2 && model.cursor() == 2);
    assert(model.backspace());
    assert(model.length() == 1 && storage[0] == 'a');
}

static void test_invalid_storage_and_initialization_bounds()
{
    EditorModel too_small(storage, kTextCapacity - 1);
    assert(!too_small.initialize("BAD.TXT", nullptr, 0, false));
    EditorModel model(storage, sizeof(storage));
    assert(!model.initialize("BAD.TXT", storage, kTextCapacity + 1, true));
    assert(model.initialize("GOOD.TXT", nullptr, 0, false));
    assert(model.insert('\t') == false);
    assert(model.insert('\n') == false);
}

static void test_logical_lines_and_cursor_boundaries()
{
    static const uint8_t text[] = {'a', 'b', '\n', 'c', 'd', '\n'};
    EditorModel model(storage, sizeof(storage));
    assert(model.initialize("LINES.TXT", text, sizeof(text), true));
    assert(model.line_count() == 3);
    size_t begin = 99;
    size_t end = 99;
    assert(model.line_bounds(0, begin, end) && begin == 0 && end == 2);
    assert(model.line_bounds(1, begin, end) && begin == 3 && end == 5);
    assert(model.line_bounds(2, begin, end) && begin == 6 && end == 6);
    assert(!model.line_bounds(3, begin, end));
    assert(model.line_number() == 3 && model.column_number() == 1);

    model.move_left();
    assert(model.cursor() == 5);
    assert(model.line_number() == 2 && model.column_number() == 3);
    model.move_left();
    assert(model.cursor() == 4);
    assert(model.line_number() == 2 && model.column_number() == 2);
    model.move_left();
    assert(model.cursor() == 3);
    assert(model.line_number() == 2 && model.column_number() == 1);
    model.move_left();
    assert(model.cursor() == 2);
    assert(model.line_number() == 1 && model.column_number() == 3);
    model.move_left();
    model.move_left();
    assert(model.cursor() == 0);
    model.move_left();
    assert(model.cursor() == 0);
    model.move_right();
    assert(model.cursor() == 1);
}

static void test_vertical_desired_column_and_short_lines()
{
    static const uint8_t text[] = {
        '1','2','3','4','5','6','\n', 'x','\n', 'a','b','c','d','e','f'};
    EditorModel model(storage, sizeof(storage));
    assert(model.initialize("VERT.TXT", text, sizeof(text), true));
    model.move_up();
    assert(model.cursor() == 8); // line 1, clamped to its one-byte length
    assert(model.line_number() == 2 && model.column_number() == 2);
    model.move_down();
    assert(model.cursor() == 15); // desired column 6 restored on the long line
    assert(model.line_number() == 3 && model.column_number() == 7);
    model.move_down();
    assert(model.cursor() == 15); // last logical line boundary
    model.move_left();
    model.move_up();
    assert(model.line_number() == 2 && model.column_number() == 2);
    model.move_down();
    assert(model.line_number() == 3 && model.column_number() == 6);
}

static void test_viewport_scrolling_and_resize()
{
    static const uint8_t text[] = {
        'a','b','c','d','e','f','\n','x','\n',
        '0','1','2','3','4','5'};
    EditorModel model(storage, sizeof(storage));
    assert(model.initialize("VIEW.TXT", text, sizeof(text), true));
    const size_t saved_cursor = model.cursor();
    model.update_viewport(2, 3);
    assert(model.viewport_top_line() == 1);
    assert(model.viewport_left_column() == 4);
    model.move_up();
    model.update_viewport(2, 3);
    assert(model.viewport_top_line() <= 1);
    assert(model.viewport_left_column() == 1);
    const size_t before_zero_top = model.viewport_top_line();
    const size_t before_zero_left = model.viewport_left_column();
    const size_t before_zero_cursor = model.cursor();
    model.update_viewport(0, 0);
    assert(model.viewport_top_line() == before_zero_top);
    assert(model.viewport_left_column() == before_zero_left);
    assert(model.cursor() == before_zero_cursor);
    model.update_viewport(0, 3);
    assert(model.viewport_top_line() == before_zero_top);
    model.update_viewport(2, 0);
    assert(model.viewport_left_column() == before_zero_left);
    model.update_viewport(8, 20);
    assert(model.length() == sizeof(text));
    assert(model.cursor() == before_zero_cursor && saved_cursor == sizeof(text));
    assert(model.line_number() - 1 >= model.viewport_top_line());
    assert(model.line_number() - 1 < model.viewport_top_line() + 8);
    assert(model.column_number() - 1 >= model.viewport_left_column());
    assert(model.column_number() - 1 < model.viewport_left_column() + 20);
}

static void test_capacity_end_is_visible_without_reading_past_text()
{
    EditorModel model(storage, sizeof(storage));
    for (size_t i = 0; i < kTextCapacity; ++i) storage[i] = 'z';
    assert(model.initialize("FULL.TXT", storage, kTextCapacity, true));
    model.update_viewport(3, 80);
    assert(model.cursor() == kTextCapacity);
    assert(model.viewport_top_line() == 0);
    assert(model.viewport_left_column() == kTextCapacity - 80 + 1);
}

int main()
{
    test_empty_initialize_and_insertions();
    test_exact_capacity_and_refused_overflow();
    test_newline_and_backspace();
    test_invalid_storage_and_initialization_bounds();
    test_logical_lines_and_cursor_boundaries();
    test_vertical_desired_column_and_short_lines();
    test_viewport_scrolling_and_resize();
    test_capacity_end_is_visible_without_reading_past_text();
    return 0;
}
