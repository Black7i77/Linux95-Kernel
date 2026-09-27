#pragma once

#include "arch/keyboard.hpp"

namespace linux95::terminal {

inline bool shell_character_for_key(
    const keyboard::KeyEvent& event,
    char& out)
{
    if (!event.pressed || event.ctrl) return false;
    switch (event.key) {
    case keyboard::KeyCode::Character:
        out = event.character;
        return true;
    case keyboard::KeyCode::Enter:
        out = '\n';
        return true;
    case keyboard::KeyCode::Backspace:
        out = '\b';
        return true;
    default:
        return false;
    }
}

} // namespace linux95::terminal
