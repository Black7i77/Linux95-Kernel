#pragma once

#include "net/net_types.hpp"

#include <stdint.h>

namespace linux95::net::ipv4 {

struct PacketView {
    Ipv4Address source;
    Ipv4Address destination;
    uint8_t protocol;
    const uint8_t* payload;
    uint16_t payload_length;
};

uint16_t checksum(
    const uint8_t* data,
    uint16_t length);

bool parse(
    const uint8_t* packet,
    uint16_t length,
    PacketView& out);

bool build(
    uint8_t* packet,
    uint16_t capacity,
    const Ipv4Address& source,
    const Ipv4Address& destination,
    uint8_t protocol,
    uint16_t identification,
    const uint8_t* payload,
    uint16_t payload_length,
    uint16_t& packet_length);

Ipv4Address next_hop(
    const Ipv4Address& destination,
    const Ipv4Address& local_ip,
    const Ipv4Address& netmask,
    const Ipv4Address& gateway);

} // namespace linux95::net::ipv4
