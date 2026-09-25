#pragma once

#include <stdint.h>

namespace linux95::rtl8139 {

struct MacAddress {
    uint8_t bytes[6];
};

bool initialize();
bool online();
MacAddress mac_address();

bool transmit(
    const uint8_t* frame,
    uint16_t length);

bool poll_receive(
    uint8_t* frame,
    uint16_t capacity,
    uint16_t& length);

} // namespace linux95::rtl8139
