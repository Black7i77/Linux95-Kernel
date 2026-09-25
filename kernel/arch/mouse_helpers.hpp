#pragma once

#include <stdint.h>

namespace linux95::mouse {

struct MouseEvent {
    int16_t dx;
    int16_t dy;
    bool left;
    bool right;
    bool middle;
};

class PacketDecoder {
public:
    constexpr PacketDecoder()
        : packet_{0, 0, 0},
          index_(0)
    {
    }

    void reset()
    {
        packet_[0] = 0;
        packet_[1] = 0;
        packet_[2] = 0;
        index_ = 0;
    }

    bool feed(
        uint8_t byte,
        MouseEvent& event)
    {
        if (index_ == 0) {
            // A valid PS/2 mouse packet always has bit 3 set.
            if ((byte & 0x08u) == 0) {
                return false;
            }

            packet_[0] = byte;
            index_ = 1;
            return false;
        }

        if (index_ == 1) {
            packet_[1] = byte;
            index_ = 2;
            return false;
        }

        packet_[2] = byte;

        const uint8_t flags = packet_[0];
        const uint8_t raw_x = packet_[1];
        const uint8_t raw_y = packet_[2];

        index_ = 0;

        // Overflow packets are fully consumed but never emitted.
        if ((flags & 0xC0u) != 0) {
            return false;
        }

        event.dx =
            decode_axis(
                raw_x,
                (flags & 0x10u) != 0);

        event.dy =
            decode_axis(
                raw_y,
                (flags & 0x20u) != 0);

        event.left =
            (flags & 0x01u) != 0;

        event.right =
            (flags & 0x02u) != 0;

        event.middle =
            (flags & 0x04u) != 0;

        return true;
    }

private:
    static int16_t decode_axis(
        uint8_t raw,
        bool negative)
    {
        // PS/2 sign extension is controlled by the sign flag
        // in the packet's first byte.
        if (negative) {
            return static_cast<int16_t>(
                static_cast<uint16_t>(raw) |
                0xFF00u);
        }

        return static_cast<int16_t>(raw);
    }

    uint8_t packet_[3];
    uint8_t index_;
};

} // namespace linux95::mouse
