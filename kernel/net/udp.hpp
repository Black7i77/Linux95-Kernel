#pragma once

#include "net/net_types.hpp"

#include <stdint.h>

namespace linux95::net::udp {

constexpr uint16_t kHeaderLength = 8;
constexpr uint16_t kMaxPayloadLength = 1472;

struct DatagramView {
    uint16_t source_port;
    uint16_t destination_port;
    const uint8_t* payload;
    uint16_t payload_length;
};

uint16_t checksum(
    const Ipv4Address& source,
    const Ipv4Address& destination,
    const uint8_t* datagram,
    uint16_t length);

bool build(
    uint8_t* datagram,
    uint16_t capacity,
    const Ipv4Address& source,
    const Ipv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    uint16_t payload_length,
    uint16_t& datagram_length);

bool parse(
    const uint8_t* datagram,
    uint16_t enclosing_length,
    const Ipv4Address& source,
    const Ipv4Address& destination,
    DatagramView& out);

} // namespace linux95::net::udp
