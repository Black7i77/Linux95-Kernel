#pragma once

#include <stdint.h>

namespace linux95::keyboard {

enum class KeyCode : uint8_t {
    Unknown,
    Character,
    Enter,
    Backspace,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,
};

struct KeyEvent {
    KeyCode key;
    char character;
    bool ctrl;
    bool shift;
    bool pressed;
};

void initialize();
void on_irq();
bool has_event();
KeyEvent read_event();

// Transitional compatibility for existing callers; removed when all callers
// migrate to KeyEvent routing.
bool has_char();
char read_char();

} // namespace linux95::keyboard
