#include "net/icmp.hpp"

#include <stdint.h>

namespace linux95::net::icmp {
namespace {

constexpr uint8_t kEchoReplyType = 0;
constexpr uint8_t kEchoRequestType = 8;
constexpr uint8_t kEchoCode = 0;
constexpr uint16_t kHeaderLength = 8;
constexpr uint16_t kPayloadLength = 32;
constexpr uint16_t kEchoRequestLength = kHeaderLength + kPayloadLength;

uint16_t read_be16(const uint8_t* bytes, uint16_t offset)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(bytes[offset]) << 8) |
        bytes[offset + 1]);
}

void write_be16(uint8_t* bytes, uint16_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 1] = static_cast<uint8_t>(value & 0xFFu);
}

} // namespace

uint16_t checksum(const uint8_t* data, uint16_t length)
{
    if (data == nullptr) {
        return 0xFFFFu;
    }

    uint32_t sum = 0;
    uint16_t offset = 0;
    while (static_cast<uint16_t>(length - offset) >= 2) {
        sum += static_cast<uint16_t>(
            (static_cast<uint16_t>(data[offset]) << 8) |
            data[offset + 1]);
        offset = static_cast<uint16_t>(offset + 2u);
    }
    if (offset < length) {
        sum += static_cast<uint16_t>(
            static_cast<uint16_t>(data[offset]) << 8);
    }

    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

bool build_echo_request(
    uint8_t* payload,
    uint16_t capacity,
    uint16_t identifier,
    uint16_t sequence,
    uint16_t& length)
{
    length = 0;
    if (payload == nullptr || capacity < kEchoRequestLength) {
        return false;
    }

    payload[0] = kEchoRequestType;
    payload[1] = kEchoCode;
    write_be16(payload, 2, 0);
    write_be16(payload, 4, identifier);
    write_be16(payload, 6, sequence);
    for (uint8_t i = 0; i < kPayloadLength; ++i) {
        payload[kHeaderLength + i] = i;
    }

    write_be16(payload, 2, checksum(payload, kEchoRequestLength));
    length = kEchoRequestLength;
    return true;
}

bool accept_echo_reply(
    const uint8_t* payload,
    uint16_t length,
    uint16_t expected_identifier,
    uint16_t expected_sequence,
    uint16_t& payload_bytes)
{
    payload_bytes = 0;
    if (payload == nullptr || length < kHeaderLength) {
        return false;
    }
    if (payload[0] != kEchoReplyType || payload[1] != kEchoCode) {
        return false;
    }
    if (checksum(payload, length) != 0) {
        return false;
    }
    if (read_be16(payload, 4) != expected_identifier ||
        read_be16(payload, 6) != expected_sequence) {
        return false;
    }

    payload_bytes = static_cast<uint16_t>(length - kHeaderLength);
    return true;
}

} // namespace linux95::net::icmp
