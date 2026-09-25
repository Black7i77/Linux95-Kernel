#include "net/ipv4.hpp"

#include <stdint.h>

namespace linux95::net::ipv4 {
namespace {

constexpr uint16_t kMinimumHeaderLength = 20;
constexpr uint8_t kVersion = 4;
constexpr uint8_t kMinimumIhl = 5;
constexpr uint8_t kVersionIhl = 0x45;
constexpr uint8_t kDefaultTtl = 64;

constexpr uint16_t kVersionIhlOffset = 0;
constexpr uint16_t kDscpEcnOffset = 1;
constexpr uint16_t kTotalLengthOffset = 2;
constexpr uint16_t kIdentificationOffset = 4;
constexpr uint16_t kFragmentOffset = 6;
constexpr uint16_t kTtlOffset = 8;
constexpr uint16_t kProtocolOffset = 9;
constexpr uint16_t kChecksumOffset = 10;
constexpr uint16_t kSourceOffset = 12;
constexpr uint16_t kDestinationOffset = 16;

constexpr uint16_t kMoreFragmentsOrOffsetMask = 0x3FFF;

uint16_t read_be16(const uint8_t* bytes, uint16_t offset)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(bytes[offset]) << 8) |
        bytes[offset + 1]);
}

void write_be16(uint8_t* bytes, uint16_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    bytes[offset + 1] = static_cast<uint8_t>(value & 0xFFu);
}

} // namespace

uint16_t checksum(const uint8_t* data, uint16_t length)
{
    if (data == nullptr) {
        return 0xFFFF;
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

bool parse(
    const uint8_t* packet,
    uint16_t length,
    PacketView& out)
{
    if (packet == nullptr || length < kMinimumHeaderLength) {
        return false;
    }

    const uint8_t version =
        static_cast<uint8_t>(packet[kVersionIhlOffset] >> 4);
    const uint8_t ihl =
        static_cast<uint8_t>(packet[kVersionIhlOffset] & 0x0Fu);
    if (version != kVersion || ihl < kMinimumIhl) {
        return false;
    }

    const uint16_t header_length =
        static_cast<uint16_t>(ihl * 4u);
    if (header_length > length) {
        return false;
    }

    const uint16_t total_length =
        read_be16(packet, kTotalLengthOffset);
    if (total_length < header_length || total_length > length) {
        return false;
    }

    const uint16_t fragment = read_be16(packet, kFragmentOffset);
    if ((fragment & kMoreFragmentsOrOffsetMask) != 0) {
        return false;
    }

    if (checksum(packet, header_length) != 0) {
        return false;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        out.source.bytes[i] = packet[kSourceOffset + i];
        out.destination.bytes[i] = packet[kDestinationOffset + i];
    }
    out.protocol = packet[kProtocolOffset];
    out.payload = packet + header_length;
    out.payload_length =
        static_cast<uint16_t>(total_length - header_length);
    return true;
}

bool build(
    uint8_t* packet,
    uint16_t capacity,
    const Ipv4Address& source,
    const Ipv4Address& destination,
    uint8_t protocol,
    uint16_t identification,
    const uint8_t* payload,
    uint16_t payload_length,
    uint16_t& packet_length)
{
    packet_length = 0;
    const uint32_t required =
        static_cast<uint32_t>(kMinimumHeaderLength) + payload_length;
    if (packet == nullptr ||
        (payload == nullptr && payload_length != 0) ||
        required > capacity ||
        required > UINT16_MAX) {
        return false;
    }

    packet[kVersionIhlOffset] = kVersionIhl;
    packet[kDscpEcnOffset] = 0;
    write_be16(
        packet,
        kTotalLengthOffset,
        static_cast<uint16_t>(required));
    write_be16(packet, kIdentificationOffset, identification);
    write_be16(packet, kFragmentOffset, 0);
    packet[kTtlOffset] = kDefaultTtl;
    packet[kProtocolOffset] = protocol;
    write_be16(packet, kChecksumOffset, 0);
    for (uint8_t i = 0; i < 4; ++i) {
        packet[kSourceOffset + i] = source.bytes[i];
        packet[kDestinationOffset + i] = destination.bytes[i];
    }

    const uint16_t header_checksum =
        checksum(packet, kMinimumHeaderLength);
    write_be16(packet, kChecksumOffset, header_checksum);

    for (uint16_t i = 0; i < payload_length; ++i) {
        packet[kMinimumHeaderLength + i] = payload[i];
    }

    packet_length = static_cast<uint16_t>(required);
    return true;
}

Ipv4Address next_hop(
    const Ipv4Address& destination,
    const Ipv4Address& local_ip,
    const Ipv4Address& netmask,
    const Ipv4Address& gateway)
{
    bool same_subnet = true;
    for (uint8_t i = 0; i < 4; ++i) {
        if ((destination.bytes[i] & netmask.bytes[i]) !=
            (local_ip.bytes[i] & netmask.bytes[i])) {
            same_subnet = false;
            break;
        }
    }
    return same_subnet ? destination : gateway;
}

} // namespace linux95::net::ipv4
