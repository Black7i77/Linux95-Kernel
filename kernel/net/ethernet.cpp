#include "net/ethernet.hpp"

#include <stdint.h>

namespace linux95::net {
namespace {

constexpr uint16_t kDestinationOffset = 0;
constexpr uint16_t kSourceOffset = 6;
constexpr uint16_t kEtherTypeOffset = 12;
constexpr uint16_t kPayloadOffset = 14;

bool supported_type(uint16_t value)
{
    return value == static_cast<uint16_t>(EtherType::Ipv4) ||
           value == static_cast<uint16_t>(EtherType::Arp);
}

} // namespace

bool parse_ethernet(
    const uint8_t* frame,
    uint16_t length,
    EthernetView& out)
{
    if (frame == nullptr || length < kPayloadOffset) {
        return false;
    }

    const uint16_t type_value = static_cast<uint16_t>(
        (static_cast<uint16_t>(frame[kEtherTypeOffset]) << 8) |
        frame[kEtherTypeOffset + 1]);
    if (!supported_type(type_value)) {
        return false;
    }

    for (uint8_t i = 0; i < 6; ++i) {
        out.destination.bytes[i] = frame[kDestinationOffset + i];
        out.source.bytes[i] = frame[kSourceOffset + i];
    }
    out.type = static_cast<EtherType>(type_value);
    out.payload = frame + kPayloadOffset;
    out.payload_length = static_cast<uint16_t>(length - kPayloadOffset);
    return true;
}

bool build_ethernet(
    uint8_t* frame,
    uint16_t capacity,
    const MacAddress& destination,
    const MacAddress& source,
    EtherType type,
    const uint8_t* payload,
    uint16_t payload_length,
    uint16_t& frame_length)
{
    frame_length = 0;
    const uint16_t type_value = static_cast<uint16_t>(type);
    const uint32_t required =
        static_cast<uint32_t>(kPayloadOffset) + payload_length;

    if (frame == nullptr ||
        (payload == nullptr && payload_length != 0) ||
        !supported_type(type_value) ||
        required > capacity ||
        required > UINT16_MAX) {
        return false;
    }

    for (uint8_t i = 0; i < 6; ++i) {
        frame[kDestinationOffset + i] = destination.bytes[i];
        frame[kSourceOffset + i] = source.bytes[i];
    }
    frame[kEtherTypeOffset] =
        static_cast<uint8_t>((type_value >> 8) & 0xFFu);
    frame[kEtherTypeOffset + 1] =
        static_cast<uint8_t>(type_value & 0xFFu);

    for (uint16_t i = 0; i < payload_length; ++i) {
        frame[kPayloadOffset + i] = payload[i];
    }

    frame_length = static_cast<uint16_t>(required);
    return true;
}

} // namespace linux95::net
