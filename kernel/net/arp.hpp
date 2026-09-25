#pragma once

#include "net/net_types.hpp"

#include <stdint.h>

namespace linux95::net::arp {

struct CacheEntry {
    Ipv4Address ip;
    MacAddress mac;
    bool valid;
};

void reset();

bool parse_and_learn(
    const uint8_t* payload,
    uint16_t length,
    const Ipv4Address& local_ip,
    const MacAddress& local_mac,
    bool& should_reply,
    MacAddress& sender_mac,
    Ipv4Address& sender_ip);

bool lookup(
    const Ipv4Address& ip,
    MacAddress& mac);

bool build_request(
    uint8_t* payload,
    uint16_t capacity,
    const MacAddress& local_mac,
    const Ipv4Address& local_ip,
    const Ipv4Address& target_ip,
    uint16_t& length);

bool build_reply(
    uint8_t* payload,
    uint16_t capacity,
    const MacAddress& local_mac,
    const Ipv4Address& local_ip,
    const MacAddress& target_mac,
    const Ipv4Address& target_ip,
    uint16_t& length);

} // namespace linux95::net::arp
