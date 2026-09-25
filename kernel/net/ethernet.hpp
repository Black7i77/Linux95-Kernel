#pragma once

#include "net/net_types.hpp"

#include <stdint.h>

namespace linux95::net {

enum class EtherType : uint16_t {
    Ipv4 = 0x0800,
    Arp = 0x0806,
};

struct EthernetView {
    MacAddress destination;
    MacAddress source;
    EtherType type;
    const uint8_t* payload;
    uint16_t payload_length;
};

bool parse_ethernet(
    const uint8_t* frame,
    uint16_t length,
    EthernetView& out);

bool build_ethernet(
    uint8_t* frame,
    uint16_t capacity,
    const MacAddress& destination,
    const MacAddress& source,
    EtherType type,
    const uint8_t* payload,
    uint16_t payload_length,
    uint16_t& frame_length);

} // namespace linux95::net
