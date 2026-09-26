#include "net/udp.hpp"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

namespace {

using linux95::net::Ipv4Address;

static_assert(linux95::net::udp::kHeaderLength == 8);
static_assert(linux95::net::udp::kMaxPayloadLength == 1472);

constexpr Ipv4Address kSource{{192, 0, 2, 1}};
constexpr Ipv4Address kDestination{{198, 51, 100, 2}};

void passed(const char* name)
{
    printf("[PASS] %s\n", name);
}

void test_build()
{
    uint8_t datagram[1500]{};
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    uint16_t length = 99;
    assert(linux95::net::udp::build(
        datagram, sizeof(datagram), kSource, kDestination,
        0x1234, 0xABCD, payload, sizeof(payload), length));
    assert(length == 11);
    assert(datagram[0] == 0x12 && datagram[1] == 0x34);
    assert(datagram[2] == 0xAB && datagram[3] == 0xCD);
    assert(datagram[4] == 0 && datagram[5] == 11);
    assert(datagram[8] == 0xAA && datagram[10] == 0xCC);
    assert(linux95::net::udp::checksum(
        kSource, kDestination, datagram, length) == 0);

    uint8_t empty[linux95::net::udp::kHeaderLength]{};
    assert(linux95::net::udp::build(
        empty, sizeof(empty), kSource, kDestination,
        1, 2, nullptr, 0, length));
    assert(length == 8 && empty[4] == 0 && empty[5] == 8);

    uint8_t maximum[linux95::net::udp::kHeaderLength +
                    linux95::net::udp::kMaxPayloadLength]{};
    uint8_t maximum_payload[linux95::net::udp::kMaxPayloadLength]{};
    assert(linux95::net::udp::build(
        maximum, sizeof(maximum), kSource, kDestination,
        1, 2, maximum_payload, sizeof(maximum_payload), length));
    assert(length == 1480);
    assert(!linux95::net::udp::build(
        maximum, sizeof(maximum), kSource, kDestination,
        1, 2, maximum_payload,
        linux95::net::udp::kMaxPayloadLength + 1, length));
    assert(!linux95::net::udp::build(
        maximum, sizeof(maximum), kSource, kDestination,
        1, 2, nullptr, 1, length));
    assert(!linux95::net::udp::build(
        nullptr, sizeof(maximum), kSource, kDestination,
        1, 2, nullptr, 0, length));
    passed("udp_build");
}

void test_parse()
{
    uint8_t datagram[16]{};
    const uint8_t payload[] = {1, 2, 3};
    uint16_t length = 0;
    assert(linux95::net::udp::build(
        datagram, sizeof(datagram), kSource, kDestination,
        1000, 2000, payload, sizeof(payload), length));
    linux95::net::udp::DatagramView view{};
    assert(linux95::net::udp::parse(
        datagram, length + 2, kSource, kDestination, view));
    assert(view.source_port == 1000 && view.destination_port == 2000);
    assert(view.payload_length == 3 && view.payload[2] == 3);
    assert(!linux95::net::udp::parse(
        nullptr, length, kSource, kDestination, view));
    assert(!linux95::net::udp::parse(
        datagram, linux95::net::udp::kHeaderLength - 1,
        kSource, kDestination, view));
    datagram[4] = 0;
    datagram[5] = 7;
    assert(!linux95::net::udp::parse(
        datagram, length, kSource, kDestination, view));
    datagram[4] = 0;
    datagram[5] = 17;
    assert(!linux95::net::udp::parse(
        datagram, length, kSource, kDestination, view));
    passed("udp_parse");
}

void test_checksum()
{
    uint8_t vector[] = {
        0x30, 0x39, 0x00, 0x35, 0x00, 0x0D, 0x00, 0x00,
        'a', 'b', 'c', 'd', 'e'};
    assert(linux95::net::udp::checksum(
        kSource, kDestination, vector, sizeof(vector)) == 0xB967);
    uint8_t odd[11] = {0, 1, 0, 2, 0, 11, 0, 0, 1, 2, 3};
    const uint16_t odd_sum = linux95::net::udp::checksum(
        kSource, kDestination, odd, sizeof(odd));
    assert(odd_sum != 0);
    odd[6] = static_cast<uint8_t>(odd_sum >> 8);
    odd[7] = static_cast<uint8_t>(odd_sum);
    assert(linux95::net::udp::checksum(
        kSource, kDestination, odd, sizeof(odd)) == 0);
    passed("udp_checksum");
}

void test_zero_checksum_encoding()
{
    uint8_t datagram[10]{};
    uint8_t payload[2]{};
    uint16_t length = 0;
    bool found = false;
    for (uint32_t value = 0; value <= 0xFFFFu; ++value) {
        payload[0] = static_cast<uint8_t>(value >> 8);
        payload[1] = static_cast<uint8_t>(value);
        assert(linux95::net::udp::build(
            datagram, sizeof(datagram), kSource, kDestination,
            1, 2, payload, sizeof(payload), length));
        if (datagram[6] == 0xFF && datagram[7] == 0xFF) {
            found = true;
            assert(linux95::net::udp::checksum(
                kSource, kDestination, datagram, length) == 0);
            break;
        }
    }
    assert(found);
    passed("udp_checksum_zero_encoded_ffff");
}

void test_parse_checksum_policy()
{
    uint8_t datagram[9]{};
    uint16_t length = 0;
    const uint8_t payload[] = {0x7F};
    assert(linux95::net::udp::build(
        datagram, sizeof(datagram), kSource, kDestination,
        10, 20, payload, sizeof(payload), length));
    linux95::net::udp::DatagramView view{};
    assert(linux95::net::udp::parse(
        datagram, length, kSource, kDestination, view));
    datagram[6] = 0;
    datagram[7] = 0;
    assert(linux95::net::udp::parse(
        datagram, length, kSource, kDestination, view));
    passed("udp_zero_checksum_ipv4");

    datagram[6] = 0x12;
    datagram[7] = 0x34;
    assert(!linux95::net::udp::parse(
        datagram, length, kSource, kDestination, view));
    passed("udp_bad_checksum_rejected");
}

} // namespace

int main()
{
    test_build();
    test_parse();
    test_checksum();
    test_zero_checksum_encoding();
    test_parse_checksum_policy();
    return 0;
}
