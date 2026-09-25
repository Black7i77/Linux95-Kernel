#include "net/arp.hpp"

#include <assert.h>
#include <stdint.h>

namespace {

using linux95::net::Ipv4Address;
using linux95::net::MacAddress;

bool mac_equals(const MacAddress& left, const MacAddress& right)
{
    for (uint8_t i = 0; i < 6; ++i) {
        if (left.bytes[i] != right.bytes[i]) {
            return false;
        }
    }
    return true;
}

bool parse(
    const uint8_t* packet,
    uint16_t length,
    bool& should_reply,
    MacAddress& sender_mac,
    Ipv4Address& sender_ip)
{
    const Ipv4Address local_ip{{10, 0, 2, 15}};
    const MacAddress local_mac{{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
    return linux95::net::arp::parse_and_learn(
        packet,
        length,
        local_ip,
        local_mac,
        should_reply,
        sender_mac,
        sender_ip);
}

} // namespace

int main()
{
    using namespace linux95::net;
    using namespace linux95::net::arp;

    const MacAddress local_mac{{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
    const Ipv4Address local_ip{{10, 0, 2, 15}};
    const Ipv4Address gateway_ip{{10, 0, 2, 2}};
    const MacAddress gateway_mac{{0x52, 0x55, 0x0A, 0x00, 0x02, 0x02}};

    uint8_t reply[28] = {
        0x00, 0x01,
        0x08, 0x00,
        0x06,
        0x04,
        0x00, 0x02,
        0x52, 0x55, 0x0A, 0x00, 0x02, 0x02,
        10, 0, 2, 2,
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        10, 0, 2, 15,
    };

    bool should_reply = true;
    MacAddress sender_mac{};
    Ipv4Address sender_ip{};

    reset();
    assert(!parse(reply, 27, should_reply, sender_mac, sender_ip));

    reply[1] = 0x02;
    assert(!parse(reply, sizeof(reply), should_reply, sender_mac, sender_ip));
    reply[1] = 0x01;

    reply[2] = 0x86;
    reply[3] = 0xDD;
    assert(!parse(reply, sizeof(reply), should_reply, sender_mac, sender_ip));
    reply[2] = 0x08;
    reply[3] = 0x00;

    reply[4] = 5;
    assert(!parse(reply, sizeof(reply), should_reply, sender_mac, sender_ip));
    reply[4] = 6;

    reply[5] = 16;
    assert(!parse(reply, sizeof(reply), should_reply, sender_mac, sender_ip));
    reply[5] = 4;

    assert(parse(reply, sizeof(reply), should_reply, sender_mac, sender_ip));
    assert(!should_reply);
    assert(mac_equals(sender_mac, gateway_mac));
    MacAddress cached_mac{};
    assert(lookup(gateway_ip, cached_mac));
    assert(mac_equals(cached_mac, gateway_mac));

    const MacAddress updated_gateway_mac{{
        0x52, 0x55, 0x0A, 0x00, 0x02, 0x99,
    }};
    reply[13] = 0x99;
    assert(parse(reply, sizeof(reply), should_reply, sender_mac, sender_ip));
    assert(lookup(gateway_ip, cached_mac));
    assert(mac_equals(cached_mac, updated_gateway_mac));

    for (uint8_t host = 3; host <= 9; ++host) {
        reply[13] = host;
        reply[17] = host;
        assert(parse(reply, sizeof(reply), should_reply, sender_mac, sender_ip));
    }

    for (uint8_t host = 2; host <= 9; ++host) {
        const Ipv4Address ip{{10, 0, 2, host}};
        assert(lookup(ip, cached_mac));
    }

    reply[13] = 10;
    reply[17] = 10;
    assert(parse(reply, sizeof(reply), should_reply, sender_mac, sender_ip));
    assert(!lookup(gateway_ip, cached_mac));
    const Ipv4Address host_three{{10, 0, 2, 3}};
    const Ipv4Address host_ten{{10, 0, 2, 10}};
    assert(lookup(host_three, cached_mac));
    assert(lookup(host_ten, cached_mac));

    const uint8_t request_for_local[28] = {
        0x00, 0x01,
        0x08, 0x00,
        0x06,
        0x04,
        0x00, 0x01,
        0x52, 0x55, 0x0A, 0x00, 0x02, 0x02,
        10, 0, 2, 2,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        10, 0, 2, 15,
    };
    assert(parse(
        request_for_local,
        sizeof(request_for_local),
        should_reply,
        sender_mac,
        sender_ip));
    assert(should_reply);

    uint8_t built[28] = {};
    uint16_t built_length = 0;
    assert(build_request(
        built,
        sizeof(built),
        local_mac,
        local_ip,
        gateway_ip,
        built_length));
    assert(built_length == sizeof(built));
    const uint8_t expected_request[28] = {
        0x00, 0x01,
        0x08, 0x00,
        0x06,
        0x04,
        0x00, 0x01,
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        10, 0, 2, 15,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        10, 0, 2, 2,
    };
    for (uint8_t i = 0; i < sizeof(expected_request); ++i) {
        assert(built[i] == expected_request[i]);
    }

    assert(build_reply(
        built,
        sizeof(built),
        local_mac,
        local_ip,
        gateway_mac,
        gateway_ip,
        built_length));
    const uint8_t expected_reply[28] = {
        0x00, 0x01,
        0x08, 0x00,
        0x06,
        0x04,
        0x00, 0x02,
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        10, 0, 2, 15,
        0x52, 0x55, 0x0A, 0x00, 0x02, 0x02,
        10, 0, 2, 2,
    };
    for (uint8_t i = 0; i < sizeof(expected_reply); ++i) {
        assert(built[i] == expected_reply[i]);
    }

    assert(!build_request(
        built,
        27,
        local_mac,
        local_ip,
        gateway_ip,
        built_length));

    return 0;
}
