#pragma once

#include "net/net_types.hpp"

#include <stdint.h>

namespace linux95::net::icmp {

enum class PingState : uint8_t {
    Idle,
    ResolvingArp,
    WaitingReply,
    ReplyReceived,
    HostUnreachable,
    TimedOut,
};

struct PingResult {
    PingState state;
    Ipv4Address address;
    uint16_t sequence;
    uint16_t payload_bytes;
};

uint16_t checksum(
    const uint8_t* data,
    uint16_t length);

bool build_echo_request(
    uint8_t* payload,
    uint16_t capacity,
    uint16_t identifier,
    uint16_t sequence,
    uint16_t& length);

bool accept_echo_reply(
    const uint8_t* payload,
    uint16_t length,
    uint16_t expected_identifier,
    uint16_t expected_sequence,
    uint16_t& payload_bytes);

} // namespace linux95::net::icmp
