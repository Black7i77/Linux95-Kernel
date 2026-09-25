#include "net/ipv4.hpp"

#include <assert.h>
#include <stdint.h>

namespace {

void copy_header(uint8_t destination[20], const uint8_t source[20])
{
    for (uint8_t i = 0; i < 20; ++i) {
        destination[i] = source[i];
    }
}

bool ipv4_equals(
    const linux95::net::Ipv4Address& left,
    const linux95::net::Ipv4Address& right)
{
    for (uint8_t i = 0; i < 4; ++i) {
        if (left.bytes[i] != right.bytes[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    using namespace linux95::net::ipv4;

    uint8_t checksum_header[20] = {
        0x45, 0x00, 0x00, 0x73,
        0x00, 0x00, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00,
        0xC0, 0xA8, 0x00, 0x01,
        0xC0, 0xA8, 0x00, 0xC7,
    };
    assert(checksum(checksum_header, sizeof(checksum_header)) == 0xB861);

    checksum_header[10] = 0xB8;
    checksum_header[11] = 0x61;
    assert(checksum(checksum_header, sizeof(checksum_header)) == 0);

    const uint8_t valid_header[20] = {
        0x45, 0x00, 0x00, 0x14,
        0x12, 0x34, 0x00, 0x00,
        0x40, 0x01, 0x50, 0xA5,
        10, 0, 2, 15,
        10, 0, 2, 2,
    };
    PacketView view{};
    assert(parse(valid_header, sizeof(valid_header), view));
    assert(view.protocol == 1);
    assert(view.payload == valid_header + 20);
    assert(view.payload_length == 0);

    assert(!parse(valid_header, 19, view));

    uint8_t malformed[20];

    copy_header(malformed, valid_header);
    malformed[0] = 0x65;
    malformed[10] = 0x30;
    malformed[11] = 0xA5;
    assert(!parse(malformed, sizeof(malformed), view));

    copy_header(malformed, valid_header);
    malformed[0] = 0x44;
    malformed[10] = 0x51;
    malformed[11] = 0xA5;
    assert(!parse(malformed, sizeof(malformed), view));

    copy_header(malformed, valid_header);
    malformed[0] = 0x46;
    assert(!parse(malformed, sizeof(malformed), view));

    copy_header(malformed, valid_header);
    malformed[3] = 0x13;
    malformed[10] = 0x50;
    malformed[11] = 0xA6;
    assert(!parse(malformed, sizeof(malformed), view));

    copy_header(malformed, valid_header);
    malformed[3] = 0x15;
    malformed[10] = 0x50;
    malformed[11] = 0xA4;
    assert(!parse(malformed, sizeof(malformed), view));

    copy_header(malformed, valid_header);
    malformed[7] = 0x01;
    malformed[10] = 0x50;
    malformed[11] = 0xA4;
    assert(!parse(malformed, sizeof(malformed), view));

    copy_header(malformed, valid_header);
    malformed[6] = 0x20;
    malformed[10] = 0x30;
    malformed[11] = 0xA5;
    assert(!parse(malformed, sizeof(malformed), view));

    copy_header(malformed, valid_header);
    malformed[10] ^= 0x01;
    assert(!parse(malformed, sizeof(malformed), view));

    const linux95::net::Ipv4Address local_ip{{10, 0, 2, 15}};
    const linux95::net::Ipv4Address netmask{{255, 255, 255, 0}};
    const linux95::net::Ipv4Address gateway{{10, 0, 2, 2}};
    const linux95::net::Ipv4Address direct_gateway{{10, 0, 2, 2}};
    const linux95::net::Ipv4Address direct_host{{10, 0, 2, 99}};
    const linux95::net::Ipv4Address external{{1, 1, 1, 1}};

    assert(ipv4_equals(
        next_hop(direct_gateway, local_ip, netmask, gateway),
        direct_gateway));
    assert(ipv4_equals(
        next_hop(direct_host, local_ip, netmask, gateway),
        direct_host));
    assert(ipv4_equals(
        next_hop(external, local_ip, netmask, gateway),
        gateway));

    const uint8_t payload[3] = {0x01, 0x02, 0x03};
    uint8_t built[23] = {};
    uint16_t built_length = 0;
    assert(build(
        built,
        sizeof(built),
        local_ip,
        external,
        1,
        0xABCD,
        payload,
        sizeof(payload),
        built_length));
    assert(built_length == sizeof(built));
    const uint8_t expected_packet[23] = {
        0x45, 0x00, 0x00, 0x17,
        0xAB, 0xCD, 0x00, 0x00,
        0x40, 0x01, 0xC1, 0x08,
        10, 0, 2, 15,
        1, 1, 1, 1,
        0x01, 0x02, 0x03,
    };
    for (uint8_t i = 0; i < sizeof(expected_packet); ++i) {
        assert(built[i] == expected_packet[i]);
    }
    assert(checksum(built, 20) == 0);

    assert(!build(
        built,
        22,
        local_ip,
        external,
        1,
        0xABCD,
        payload,
        sizeof(payload),
        built_length));

    return 0;
}
