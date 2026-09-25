#include "net/icmp.hpp"

#include <assert.h>
#include <stdint.h>

namespace {

constexpr uint16_t kIdentifier = 0x1234;
constexpr uint16_t kSequence = 0x0102;
constexpr uint16_t kEchoLength = 40;

void refresh_checksum(uint8_t* packet)
{
    packet[2] = 0;
    packet[3] = 0;
    const uint16_t packet_checksum =
        linux95::net::icmp::checksum(packet, kEchoLength);
    packet[2] = static_cast<uint8_t>(packet_checksum >> 8);
    packet[3] = static_cast<uint8_t>(packet_checksum & 0xFFu);
}

void make_valid_reply(uint8_t* reply)
{
    reply[0] = 0;
    reply[1] = 0;
    reply[2] = 0;
    reply[3] = 0;
    reply[4] = 0x12;
    reply[5] = 0x34;
    reply[6] = 0x01;
    reply[7] = 0x02;
    for (uint8_t i = 0; i < 32; ++i) {
        reply[8 + i] = i;
    }

    refresh_checksum(reply);
}

} // namespace

int main()
{
    using namespace linux95::net::icmp;

    uint8_t request[kEchoLength]{};
    uint16_t request_length = 0;
    assert(build_echo_request(
        request,
        sizeof(request),
        kIdentifier,
        kSequence,
        request_length));
    assert(request_length == kEchoLength);
    assert(request[0] == 8);
    assert(request[1] == 0);
    assert(request[2] == 0xF3);
    assert(request[3] == 0xC8);
    assert(checksum(request, request_length) == 0);
    assert(request[4] == 0x12);
    assert(request[5] == 0x34);
    assert(request[6] == 0x01);
    assert(request[7] == 0x02);
    for (uint8_t i = 0; i < 32; ++i) {
        assert(request[8 + i] == i);
    }

    uint8_t too_small[kEchoLength]{};
    uint16_t unchanged_length = 99;
    assert(!build_echo_request(
        too_small,
        static_cast<uint16_t>(kEchoLength - 1),
        kIdentifier,
        kSequence,
        unchanged_length));
    assert(unchanged_length == 0);

    uint8_t reply[kEchoLength]{};
    make_valid_reply(reply);
    uint16_t payload_bytes = 99;
    assert(!accept_echo_reply(
        reply, 7, kIdentifier, kSequence, payload_bytes));

    make_valid_reply(reply);
    reply[0] = 8;
    refresh_checksum(reply);
    assert(!accept_echo_reply(
        reply, kEchoLength, kIdentifier, kSequence, payload_bytes));

    make_valid_reply(reply);
    reply[1] = 1;
    refresh_checksum(reply);
    assert(!accept_echo_reply(
        reply, kEchoLength, kIdentifier, kSequence, payload_bytes));

    make_valid_reply(reply);
    reply[5] = 0x35;
    refresh_checksum(reply);
    assert(!accept_echo_reply(
        reply, kEchoLength, kIdentifier, kSequence, payload_bytes));

    make_valid_reply(reply);
    reply[7] = 0x03;
    refresh_checksum(reply);
    assert(!accept_echo_reply(
        reply, kEchoLength, kIdentifier, kSequence, payload_bytes));

    make_valid_reply(reply);
    reply[8] ^= 0xFFu;
    assert(!accept_echo_reply(
        reply, kEchoLength, kIdentifier, kSequence, payload_bytes));

    make_valid_reply(reply);
    assert(accept_echo_reply(
        reply, kEchoLength, kIdentifier, kSequence, payload_bytes));
    assert(payload_bytes == 32);

    return 0;
}
