#include "net/udp.hpp"

#include <stdint.h>

namespace linux95::net::udp {
namespace {

constexpr uint16_t kHeaderLength = 8;
constexpr uint16_t kMaximumPayloadLength = 1472;
constexpr uint16_t kLengthOffset = 4;
constexpr uint16_t kChecksumOffset = 6;
constexpr uint8_t kProtocol = 17;

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

uint16_t checksum(
    const Ipv4Address& source,
    const Ipv4Address& destination,
    const uint8_t* datagram,
    uint16_t length)
{
    if (datagram == nullptr) {
        return 0xFFFFu;
    }

    uint32_t sum = 0;
    for (uint8_t i = 0; i < 4; i += 2) {
        sum += static_cast<uint16_t>(
            (static_cast<uint16_t>(source.bytes[i]) << 8) |
            source.bytes[i + 1]);
        sum += static_cast<uint16_t>(
            (static_cast<uint16_t>(destination.bytes[i]) << 8) |
            destination.bytes[i + 1]);
    }
    sum += kProtocol;
    sum += length;

    uint16_t offset = 0;
    while (static_cast<uint16_t>(length - offset) >= 2) {
        sum += read_be16(datagram, offset);
        offset = static_cast<uint16_t>(offset + 2u);
    }
    if (offset < length) {
        sum += static_cast<uint16_t>(
            static_cast<uint16_t>(datagram[offset]) << 8);
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

bool build(
    uint8_t* datagram,
    uint16_t capacity,
    const Ipv4Address& source,
    const Ipv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    uint16_t payload_length,
    uint16_t& datagram_length)
{
    datagram_length = 0;
    const uint32_t required =
        static_cast<uint32_t>(kHeaderLength) + payload_length;
    if (datagram == nullptr ||
        (payload == nullptr && payload_length != 0) ||
        payload_length > kMaximumPayloadLength ||
        required > capacity) {
        return false;
    }

    write_be16(datagram, 0, source_port);
    write_be16(datagram, 2, destination_port);
    write_be16(datagram, kLengthOffset, static_cast<uint16_t>(required));
    write_be16(datagram, kChecksumOffset, 0);
    for (uint16_t i = 0; i < payload_length; ++i) {
        datagram[kHeaderLength + i] = payload[i];
    }

    uint16_t packet_checksum = checksum(
        source, destination, datagram, static_cast<uint16_t>(required));
    if (packet_checksum == 0) {
        packet_checksum = 0xFFFFu;
    }
    write_be16(datagram, kChecksumOffset, packet_checksum);
    datagram_length = static_cast<uint16_t>(required);
    return true;
}

bool parse(
    const Ipv4Address& source,
    const Ipv4Address& destination,
    const uint8_t* datagram,
    uint16_t enclosing_length,
    DatagramView& out)
{
    if (datagram == nullptr || enclosing_length < kHeaderLength) {
        return false;
    }
    const uint16_t length = read_be16(datagram, kLengthOffset);
    if (length < kHeaderLength || length > enclosing_length) {
        return false;
    }
    if (read_be16(datagram, kChecksumOffset) != 0 &&
        checksum(source, destination, datagram, length) != 0) {
        return false;
    }

    out.source_port = read_be16(datagram, 0);
    out.destination_port = read_be16(datagram, 2);
    out.payload = datagram + kHeaderLength;
    out.payload_length = static_cast<uint16_t>(length - kHeaderLength);
    return true;
}

} // namespace linux95::net::udp
