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
    Escape,
    Delete,
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

} // namespace linux95::keyboard
