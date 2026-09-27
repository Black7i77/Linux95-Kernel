#include "arch/keyboard_helpers.hpp"

#include <assert.h>
#include <stdint.h>

using namespace linux95::keyboard;

static KeyEvent decode(Set1Decoder& decoder, uint8_t code)
{
    KeyEvent event{};
    assert(decoder.feed(code, event));
    return event;
}

static void test_character_shift_and_punctuation()
{
    Set1Decoder decoder;
    KeyEvent event = decode(decoder, 0x1E);
    assert(event.key == KeyCode::Character && event.character == 'a');
    assert(event.pressed && !event.shift && !event.ctrl);

    assert(!decoder.feed(0x2A, event));
    event = decode(decoder, 0x1E);
    assert(event.character == 'A' && event.shift);
    event = decode(decoder, 0x03);
    assert(event.character == '@' && event.shift);
}

static void test_independent_shift_and_control_modifiers()
{
    Set1Decoder decoder;
    KeyEvent event{};
    assert(!decoder.feed(0x2A, event)); // left Shift down
    assert(!decoder.feed(0x36, event)); // right Shift down
    assert(!decoder.feed(0xAA, event)); // left Shift up
    event = decode(decoder, 0x1E);
    assert(event.character == 'A' && event.shift);
    assert(!decoder.feed(0xB6, event));
    event = decode(decoder, 0x1E);
    assert(event.character == 'a' && !event.shift);

    assert(!decoder.feed(0x1D, event)); // left Ctrl down
    event = decode(decoder, 0x1F);
    assert(event.character == 's' && event.ctrl);
    assert(!decoder.feed(0x9D, event));
    event = decode(decoder, 0x1F);
    assert(!event.ctrl);

    assert(!decoder.feed(0xE0, event));
    assert(!decoder.feed(0x1D, event)); // right Ctrl down
    event = decode(decoder, 0x1F);
    assert(event.ctrl);
    assert(!decoder.feed(0xE0, event));
    assert(!decoder.feed(0x9D, event)); // right Ctrl up
    event = decode(decoder, 0x1F);
    assert(!event.ctrl);
}

static void test_extended_arrows_and_breaks()
{
    const uint8_t codes[] = {0x4B, 0x4D, 0x48, 0x50};
    const KeyCode keys[] = {
        KeyCode::ArrowLeft, KeyCode::ArrowRight,
        KeyCode::ArrowUp, KeyCode::ArrowDown};
    Set1Decoder decoder;
    KeyEvent event{};
    for (unsigned i = 0; i < 4; ++i) {
        assert(!decoder.feed(0xE0, event));
        event = decode(decoder, codes[i]);
        assert(event.key == keys[i] && event.pressed);
        assert(!decoder.feed(0xE0, event));
        event = decode(decoder, static_cast<uint8_t>(codes[i] | 0x80));
        assert(event.key == keys[i] && !event.pressed);
    }
    assert(!decoder.feed(0xE0, event));
    assert(decoder.feed(0x1C, event)); // unsupported E0 key is observable
    assert(event.key == KeyCode::Unknown && event.pressed);
    assert(!decoder.feed(0xE0, event));
    assert(decoder.feed(0x9C, event));
    assert(event.key == KeyCode::Unknown && !event.pressed);
    decoder.reset();
    assert(!decoder.feed(0xE0, event)); // prefix alone is never an event
    decoder.reset();
    assert(!decoder.feed(0x2A, event)); // modifier alone is never an event
}

static void test_queue_drops_newest()
{
    EventQueue queue;
    for (size_t i = 0; i < EventQueue::kCapacity; ++i) {
        const KeyEvent event{KeyCode::Character,
            static_cast<char>('a' + (i % 26)), false, false, true};
        assert(queue.push(event));
    }
    assert(!queue.push(KeyEvent{KeyCode::Character, 'z', false, false, true}));
    for (size_t i = 0; i < EventQueue::kCapacity; ++i) {
        const KeyEvent event = queue.pop();
        assert(event.character == static_cast<char>('a' + (i % 26)));
    }
    assert(queue.empty());
}

static void test_modifiers_update_when_queue_is_full()
{
    Set1Decoder decoder;
    EventQueue queue;
    for (size_t i = 0; i < EventQueue::kCapacity; ++i) {
        assert(queue.push(KeyEvent{KeyCode::Character, 'x', false, false, true}));
    }

    decode_and_queue(decoder, queue, 0x2A); // Shift make is processed despite full queue
    decode_and_queue(decoder, queue, 0x1D); // Ctrl make
    decode_and_queue(decoder, queue, 0xAA); // Shift break
    decode_and_queue(decoder, queue, 0x9D); // Ctrl break
    while (!queue.empty()) {
        static_cast<void>(queue.pop());
    }
    decode_and_queue(decoder, queue, 0x1E);
    const KeyEvent event = queue.pop();
    assert(event.character == 'a' && !event.shift && !event.ctrl);
}

int main()
{
    test_character_shift_and_punctuation();
    test_independent_shift_and_control_modifiers();
    test_extended_arrows_and_breaks();
    test_queue_drops_newest();
    test_modifiers_update_when_queue_is_full();
    return 0;
}
