#pragma once

#include <stdint.h>

namespace linux95::rtl8139 {

constexpr uint16_t kRxRingSize = 8192;

constexpr uint16_t advance_rx_offset(
    uint16_t offset,
    uint16_t packet_length)
{
    const uint32_t next =
        static_cast<uint32_t>(offset) + packet_length + 4u;
    const uint32_t aligned = (next + 3u) & ~0x3u;
    return static_cast<uint16_t>(aligned % kRxRingSize);
}

constexpr bool valid_rx_length(uint16_t packet_length)
{
    return packet_length >= 4 && packet_length <= 1518;
}

constexpr uint8_t next_tx_slot(uint8_t slot)
{
    return static_cast<uint8_t>((slot + 1u) % 4u);
}

} // namespace linux95::rtl8139
