#include "net/ethernet.hpp"

#include <assert.h>
#include <stdint.h>

namespace {

bool mac_equals(
    const linux95::net::MacAddress& address,
    const uint8_t expected[6])
{
    for (uint8_t i = 0; i < 6; ++i) {
        if (address.bytes[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    using namespace linux95::net;

    const uint8_t short_frame[13] = {};
    EthernetView view{};
    assert(!parse_ethernet(short_frame, sizeof(short_frame), view));

    const uint8_t ipv4_header[14] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
        0x08, 0x00,
    };
    assert(parse_ethernet(ipv4_header, sizeof(ipv4_header), view));
    assert(view.type == EtherType::Ipv4);
    assert(view.payload == ipv4_header + 14);
    assert(view.payload_length == 0);

    const uint8_t arp_frame[16] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x52, 0x54, 0x00, 0xAB, 0xCD, 0xEF,
        0x08, 0x06,
        0xAA, 0xBB,
    };
    const uint8_t expected_destination[6] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    const uint8_t expected_source[6] = {
        0x52, 0x54, 0x00, 0xAB, 0xCD, 0xEF,
    };
    assert(parse_ethernet(arp_frame, sizeof(arp_frame), view));
    assert(view.type == EtherType::Arp);
    assert(mac_equals(view.destination, expected_destination));
    assert(mac_equals(view.source, expected_source));
    assert(view.payload == arp_frame + 14);
    assert(view.payload_length == 2);
    assert(view.payload[0] == 0xAA);
    assert(view.payload[1] == 0xBB);

    const MacAddress destination{{
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    }};
    const MacAddress source{{
        0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    }};
    const uint8_t payload[2] = {0xCC, 0xDD};
    uint8_t output[16] = {};
    uint16_t frame_length = 99;

    assert(!build_ethernet(
        output,
        15,
        destination,
        source,
        EtherType::Arp,
        payload,
        sizeof(payload),
        frame_length));

    assert(build_ethernet(
        output,
        sizeof(output),
        destination,
        source,
        EtherType::Arp,
        payload,
        sizeof(payload),
        frame_length));
    assert(frame_length == sizeof(output));

    const uint8_t expected_frame[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
        0x08, 0x06,
        0xCC, 0xDD,
    };
    for (uint16_t i = 0; i < sizeof(expected_frame); ++i) {
        assert(output[i] == expected_frame[i]);
    }

    return 0;
}
