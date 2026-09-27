#include "gui/editor_model.hpp"
#include "arch/keyboard.hpp"

#include <assert.h>
#include <string.h>

using namespace linux95;
using namespace linux95::gui::editor;

static uint8_t storage[kTextCapacity];

static keyboard::KeyEvent key(
    keyboard::KeyCode code,
    char character = 0,
    bool ctrl = false,
    bool pressed = true)
{
    return keyboard::KeyEvent{code, character, ctrl, false, pressed};
}

static void test_clean_and_modified_quit_sequence()
{
    EditorModel clean(storage, sizeof(storage));
    assert(clean.initialize("NEW.TXT", nullptr, 0, false));
    assert(clean.handle_key(key(keyboard::KeyCode::Character, 'q', true)) == EditorAction::Quit);

    EditorModel dirty(storage, sizeof(storage));
    assert(dirty.initialize("NEW.TXT", nullptr, 0, false));
    assert(dirty.insert('a'));
    assert(dirty.handle_key(key(keyboard::KeyCode::Character, 'q', true)) == EditorAction::None);
    assert(strcmp(dirty.status_message(),
        "Unsaved changes! Press Ctrl+Q again to quit without saving.") == 0);
    assert(dirty.handle_key(key(keyboard::KeyCode::Character, 'q', true, false)) == EditorAction::None);
    assert(dirty.modified());
    assert(dirty.handle_key(key(keyboard::KeyCode::Character, 'Q', true)) == EditorAction::Quit);
    assert(dirty.modified());
}

static void test_intervening_key_disarms_but_release_does_not()
{
    EditorModel model(storage, sizeof(storage));
    assert(model.initialize("A.TXT", nullptr, 0, false));
    assert(model.insert('x'));
    assert(model.handle_key(key(keyboard::KeyCode::Character, 'q', true)) == EditorAction::None);
    assert(model.handle_key(key(keyboard::KeyCode::ArrowLeft, 0, false, false)) == EditorAction::None);
    assert(model.handle_key(key(keyboard::KeyCode::Unknown, 0, false, false)) == EditorAction::None);
    assert(model.handle_key(key(keyboard::KeyCode::Character, 'q', true)) == EditorAction::Quit);

    assert(model.initialize("A.TXT", nullptr, 0, false));
    assert(model.insert('x'));
    assert(model.handle_key(key(keyboard::KeyCode::Character, 'q', true)) == EditorAction::None);
    assert(model.handle_key(key(keyboard::KeyCode::ArrowLeft)) == EditorAction::None);
    assert(model.handle_key(key(keyboard::KeyCode::Character, 'q', true)) == EditorAction::None);
    assert(strcmp(model.status_message(),
        "Unsaved changes! Press Ctrl+Q again to quit without saving.") == 0);
    assert(model.handle_key(key(keyboard::KeyCode::Unknown)) == EditorAction::None);
    assert(model.handle_key(key(keyboard::KeyCode::Character, 'q', true)) == EditorAction::None);
}

static void test_save_shortcut_and_status_mapping()
{
    EditorModel model(storage, sizeof(storage));
    assert(model.initialize("A.TXT", nullptr, 0, false));
    assert(model.insert('x'));
    assert(model.handle_key(key(keyboard::KeyCode::Character, 's', true)) == EditorAction::Save);
    model.save_failed(filesystem::Status::IoError);
    assert(model.modified() && !model.file_exists());
    assert(model.status_message()[0] != '\0');
    assert(model.handle_key(key(keyboard::KeyCode::Character, 'q', true)) == EditorAction::None);
    assert(model.handle_key(key(keyboard::KeyCode::Character, 's', true)) == EditorAction::Save);
    model.save_succeeded();
    assert(!model.modified() && model.file_exists());

    const filesystem::Status statuses[] = {
        filesystem::Status::NotMounted, filesystem::Status::IoError,
        filesystem::Status::InvalidFilesystem, filesystem::Status::NotFound,
        filesystem::Status::NotDirectory, filesystem::Status::IsDirectory,
        filesystem::Status::Corrupt, filesystem::Status::Unsupported,
        filesystem::Status::InvalidDescriptor, filesystem::Status::InvalidHandle,
        filesystem::Status::TooManyOpenFiles, filesystem::Status::TooManyOpenDirectories,
        filesystem::Status::AlreadyExists, filesystem::Status::InvalidName,
        filesystem::Status::NoSpace, filesystem::Status::DirectoryNotEmpty,
        filesystem::Status::ReadOnly,
    };
    for (filesystem::Status status : statuses) {
        model.save_failed(status);
        const char* message = model.status_message();
        assert(message[0] != '\0' && strlen(message) < kStatusCapacity);
    }
}

static void test_key_editing_and_ctrl_shortcuts()
{
    EditorModel model(storage, sizeof(storage));
    assert(model.initialize("A.TXT", nullptr, 0, false));
    assert(model.handle_key(key(keyboard::KeyCode::Character, 's', true)) == EditorAction::Save);
    assert(model.length() == 0);
    assert(model.handle_key(key(keyboard::KeyCode::Character, 'q', false)) == EditorAction::None);
    assert(model.length() == 1 && model.data()[0] == 'q');
    assert(model.handle_key(key(keyboard::KeyCode::Enter)) == EditorAction::None);
    assert(model.data()[1] == '\n');
}

int main()
{
    test_clean_and_modified_quit_sequence();
    test_intervening_key_disarms_but_release_does_not();
    test_save_shortcut_and_status_mapping();
    test_key_editing_and_ctrl_shortcuts();
    return 0;
}
