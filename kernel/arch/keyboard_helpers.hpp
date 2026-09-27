#pragma once

#include "arch/keyboard.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::keyboard {

class Set1Decoder {
public:
    void reset()
    {
        extended_ = left_shift_ = right_shift_ = left_ctrl_ = right_ctrl_ = false;
    }

    __attribute__((noinline)) bool feed(uint8_t scancode, KeyEvent& out)
    {
        if (scancode == 0xE0) {
            extended_ = true;
            return false;
        }

        const bool extended = extended_;
        extended_ = false;
        const bool pressed = (scancode & 0x80u) == 0;
        const uint8_t code = static_cast<uint8_t>(scancode & 0x7Fu);

        if (!extended && (code == 0x2A || code == 0x36)) {
            if (code == 0x2A) left_shift_ = pressed;
            else right_shift_ = pressed;
            return false;
        }
        if (code == 0x1D) {
            if (extended) right_ctrl_ = pressed;
            else left_ctrl_ = pressed;
            return false;
        }

        KeyCode key = KeyCode::Unknown;
        char character = 0;
        if (extended) {
            switch (code) {
            case 0x4B: key = KeyCode::ArrowLeft; break;
            case 0x4D: key = KeyCode::ArrowRight; break;
            case 0x48: key = KeyCode::ArrowUp; break;
            case 0x50: key = KeyCode::ArrowDown; break;
            default: return false;
            }
        } else if (code == 0x1C) {
            key = KeyCode::Enter;
        } else if (code == 0x0E) {
            key = KeyCode::Backspace;
        } else {
            character = translate(code, left_shift_ || right_shift_);
            if (character == 0) return false;
            key = KeyCode::Character;
        }

        out = KeyEvent{key, character, left_ctrl_ || right_ctrl_,
            left_shift_ || right_shift_, pressed};
        return true;
    }

private:
    static char translate(uint8_t code, bool shift)
    {
        static constexpr char unshifted[0x3A] = {
            0, 0, '1', '2', '3', '4', '5', '6', '7', '8',
            '9', '0', '-', '=', 0, 0, 'q', 'w', 'e', 'r',
            't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
            'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
            '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
            'm', ',', '.', '/', 0, 0, 0, ' '};
        static constexpr char shifted[0x3A] = {
            0, 0, '!', '@', '#', '$', '%', '^', '&', '*',
            '(', ')', '_', '+', 0, 0, 'Q', 'W', 'E', 'R',
            'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0,
            'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
            '"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N',
            'M', '<', '>', '?', 0, 0, 0, ' '};
        if (code >= sizeof(unshifted)) return 0;
        const char value = unshifted[code];
        if (!shift || value == 0) return value;
        return shifted[code] != 0 ? shifted[code] : value;
    }

    bool extended_ = false;
    bool left_shift_ = false;
    bool right_shift_ = false;
    bool left_ctrl_ = false;
    bool right_ctrl_ = false;
};

class EventQueue {
public:
    static constexpr size_t kCapacity = 128;

    bool push(const KeyEvent& event)
    {
        if (count_ == kCapacity) return false;
        events_[head_] = event;
        head_ = (head_ + 1) % kCapacity;
        ++count_;
        return true;
    }

    bool empty() const { return count_ == 0; }

    void reset()
    {
        head_ = tail_ = count_ = 0;
    }

    KeyEvent pop()
    {
        if (empty()) return KeyEvent{};
        const KeyEvent event = events_[tail_];
        tail_ = (tail_ + 1) % kCapacity;
        --count_;
        return event;
    }

private:
    KeyEvent events_[kCapacity]{};
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
};

inline void decode_and_queue(Set1Decoder& decoder, EventQueue& queue, uint8_t scancode)
{
    KeyEvent event{};
    if (decoder.feed(scancode, event)) static_cast<void>(queue.push(event));
}

} // namespace linux95::keyboard
