#pragma once

#include <stdint.h>

namespace linux95::net {

struct MacAddress {
    uint8_t bytes[6];
};

struct Ipv4Address {
    uint8_t bytes[4];
};

constexpr uint16_t host_to_be16(uint16_t value)
{
    return static_cast<uint16_t>(
        (value << 8) |
        (value >> 8));
}

constexpr uint16_t be16_to_host(uint16_t value)
{
    return host_to_be16(value);
}

} // namespace linux95::net
