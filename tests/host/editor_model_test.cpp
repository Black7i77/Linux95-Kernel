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

int main()
{
    test_empty_initialize_and_insertions();
    test_exact_capacity_and_refused_overflow();
    test_newline_and_backspace();
    test_invalid_storage_and_initialization_bounds();
    return 0;
}
