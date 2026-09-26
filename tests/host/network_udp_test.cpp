#include "arch/pit.hpp"
#include "drivers/rtl8139.hpp"
#include "net/arp.hpp"
#include "net/ethernet.hpp"
#include "net/icmp.hpp"
#include "net/ipv4.hpp"
#include "net/network.hpp"
#include "net/udp.hpp"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

using linux95::net::Ipv4Address;
using linux95::net::MacAddress;
constexpr Ipv4Address kLocalIp{{10, 0, 2, 15}};
constexpr Ipv4Address kPeerIp{{10, 0, 2, 2}};
constexpr Ipv4Address kOtherIp{{10, 0, 2, 99}};
constexpr MacAddress kLocalMac{{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
constexpr MacAddress kPeerMac{{0x52, 0x54, 0x00, 0xAA, 0xBB, 0xCC}};

struct Frame {
    uint8_t bytes[1518]{};
    uint16_t length = 0;
};
Frame g_frames[16]{};
unsigned g_read = 0;
unsigned g_write = 0;
bool g_device_available = true;
uint64_t g_now = 0;

struct Capture {
    unsigned calls = 0;
    Ipv4Address source{};
    uint16_t source_port = 0;
    uint16_t destination_port = 0;
    uint8_t payload[32]{};
    uint16_t payload_length = 0;
};

void receive(const Ipv4Address& source, uint16_t source_port,
             uint16_t destination_port, const uint8_t* payload,
             uint16_t payload_length, void* context)
{
    auto& capture = *static_cast<Capture*>(context);
    assert(payload_length <= sizeof(capture.payload));
    ++capture.calls;
    capture.source = source;
    capture.source_port = source_port;
    capture.destination_port = destination_port;
    capture.payload_length = payload_length;
    memcpy(capture.payload, payload, payload_length);
}

void enqueue_ethernet(linux95::net::EtherType type,
                      const uint8_t* payload, uint16_t payload_length)
{
    assert(g_write < 16);
    Frame& frame = g_frames[g_write++];
    assert(linux95::net::build_ethernet(
        frame.bytes, sizeof(frame.bytes), kLocalMac, kPeerMac, type,
        payload, payload_length, frame.length));
}

void enqueue_ipv4(uint8_t protocol, const Ipv4Address& destination,
                  const uint8_t* payload, uint16_t payload_length)
{
    uint8_t packet[1500]{};
    uint16_t length = 0;
    assert(linux95::net::ipv4::build(
        packet, sizeof(packet), kPeerIp, destination, protocol, 7,
        payload, payload_length, length));
    enqueue_ethernet(linux95::net::EtherType::Ipv4, packet, length);
}

void enqueue_udp(uint16_t destination_port, const Ipv4Address& destination,
                 bool bad_checksum = false, bool zero_checksum = false)
{
    const uint8_t body[] = {'u', 'd', 'p'};
    uint8_t datagram[32]{};
    uint16_t length = 0;
    assert(linux95::net::udp::build(
        datagram, sizeof(datagram), kPeerIp, destination,
        4321, destination_port, body, sizeof(body), length));
    if (bad_checksum) {
        datagram[8] ^= 0x01;
    }
    if (zero_checksum) {
        datagram[6] = 0;
        datagram[7] = 0;
    }
    enqueue_ipv4(17, destination, datagram, length);
}

void check_udp_dispatch()
{
    using namespace linux95;
    assert(network::initialize());
    Capture selected{};
    Capture other{};
    assert(!network::bind_udp_port(0, receive, &selected));
    assert(!network::bind_udp_port(9000, nullptr, &selected));
    assert(network::bind_udp_port(9000, receive, &selected));
    assert(!network::bind_udp_port(9000, receive, &other));
    assert(network::bind_udp_port(9001, receive, &other));

    enqueue_udp(9000, kLocalIp);
    network::poll();
    assert(selected.calls == 1 && other.calls == 0);
    assert(memcmp(selected.source.bytes, kPeerIp.bytes, 4) == 0);
    assert(selected.source_port == 4321 && selected.destination_port == 9000);
    assert(selected.payload_length == 3);
    assert(memcmp(selected.payload, "udp", 3) == 0);

    enqueue_udp(9999, kLocalIp);
    enqueue_udp(9000, kLocalIp, true);
    enqueue_udp(9000, kOtherIp);
    const uint8_t malformed_udp[] = {0x10, 0xE1, 0x23, 0x28, 0, 7, 0, 0};
    enqueue_ipv4(17, kLocalIp, malformed_udp, sizeof(malformed_udp));
    const uint8_t unrelated[] = {1, 2, 3};
    enqueue_ipv4(6, kLocalIp, unrelated, sizeof(unrelated));
    network::poll();
    assert(selected.calls == 1 && other.calls == 0);

    enqueue_udp(9000, kLocalIp, false, true);
    network::poll();
    assert(selected.calls == 2 && other.calls == 0);
    assert(network::unbind_udp_port(9000));
    assert(!network::unbind_udp_port(9000));
    enqueue_udp(9000, kLocalIp);
    network::poll();
    assert(selected.calls == 2);
    puts("[PASS] network_udp_dispatch_and_filtering");

    assert(network::initialize());
    enqueue_udp(9001, kLocalIp);
    network::poll();
    assert(other.calls == 0);
    puts("[PASS] network_udp_bindings_reset");
}

void check_icmp_reply()
{
    using namespace linux95;
    assert(network::initialize());
    assert(network::start_ping(kPeerIp));
    assert(network::ping_result().state == net::icmp::PingState::ResolvingArp);

    uint8_t arp_reply[28]{};
    uint16_t arp_length = 0;
    assert(net::arp::build_reply(
        arp_reply, sizeof(arp_reply), kPeerMac, kPeerIp,
        kLocalMac, kLocalIp, arp_length));
    enqueue_ethernet(net::EtherType::Arp, arp_reply, arp_length);
    network::poll();
    assert(network::ping_result().state == net::icmp::PingState::WaitingReply);

    uint8_t reply[40]{};
    reply[0] = 0;
    reply[4] = 0x4C;
    reply[5] = 0x95;
    reply[7] = 1;
    for (uint8_t i = 0; i < 32; ++i) {
        reply[8 + i] = i;
    }
    const uint16_t checksum = net::icmp::checksum(reply, sizeof(reply));
    reply[2] = static_cast<uint8_t>(checksum >> 8);
    reply[3] = static_cast<uint8_t>(checksum);
    enqueue_ipv4(1, kLocalIp, reply, sizeof(reply));
    network::poll();
    const auto result = network::ping_result();
    assert(result.state == net::icmp::PingState::ReplyReceived);
    assert(result.payload_bytes == 32);
    puts("[PASS] network_icmp_echo_reply_retained");
}

void check_failed_initialization()
{
    using namespace linux95;
    g_device_available = false;
    assert(!network::initialize());
    assert(!network::status().online);
    enqueue_udp(9000, kLocalIp);
    network::poll();
    assert(g_read < g_write);
    g_read = g_write;
    g_device_available = true;
    puts("[PASS] network_offline_poll_ignored");
}

} // namespace

namespace linux95::rtl8139 {

bool initialize() { return g_device_available; }
bool online() { return g_device_available; }
MacAddress mac_address()
{
    MacAddress mac{};
    memcpy(mac.bytes, kLocalMac.bytes, sizeof(mac.bytes));
    return mac;
}
bool transmit(const uint8_t*, uint16_t) { return true; }
bool poll_receive(uint8_t* frame, uint16_t capacity, uint16_t& length)
{
    if (g_read == g_write) {
        return false;
    }
    const Frame& queued = g_frames[g_read++];
    assert(queued.length <= capacity);
    memcpy(frame, queued.bytes, queued.length);
    length = queued.length;
    return true;
}

} // namespace linux95::rtl8139

namespace linux95::pit {
uint64_t uptime_seconds() { return g_now; }
} // namespace linux95::pit

int main()
{
    check_failed_initialization();
    check_udp_dispatch();
    check_icmp_reply();
    return 0;
}
