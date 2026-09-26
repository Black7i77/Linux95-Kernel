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
constexpr Ipv4Address kRemoteIp{{203, 0, 113, 9}};
constexpr MacAddress kLocalMac{{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
constexpr MacAddress kPeerMac{{0x52, 0x54, 0x00, 0xAA, 0xBB, 0xCC}};
constexpr MacAddress kOtherMac{{0x52, 0x54, 0x00, 0x11, 0x22, 0x33}};

struct Frame {
    uint8_t bytes[1518]{};
    uint16_t length = 0;
};
Frame g_frames[64]{};
unsigned g_read = 0;
unsigned g_write = 0;
bool g_device_available = true;
uint64_t g_now = 0;
Frame g_sent[32]{};
unsigned g_sent_count = 0;
bool g_fail_next_transmit = false;

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
    assert(g_write < 64);
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

void reset_case()
{
    g_now = 0;
    g_read = g_write;
    g_sent_count = 0;
    g_fail_next_transmit = false;
    assert(linux95::network::initialize());
}

void enqueue_arp_reply(const Ipv4Address& sender_ip,
                       const MacAddress& sender_mac)
{
    uint8_t arp_reply[28]{};
    uint16_t arp_length = 0;
    assert(linux95::net::arp::build_reply(
        arp_reply, sizeof(arp_reply), sender_mac, sender_ip,
        kLocalMac, kLocalIp, arp_length));
    enqueue_ethernet(linux95::net::EtherType::Arp, arp_reply, arp_length);
}

linux95::net::EthernetView sent_frame(unsigned index,
                                      linux95::net::EtherType type)
{
    assert(index < g_sent_count);
    linux95::net::EthernetView frame{};
    assert(linux95::net::parse_ethernet(
        g_sent[index].bytes, g_sent[index].length, frame));
    assert(frame.type == type);
    return frame;
}

void check_arp_target(unsigned index, const Ipv4Address& target)
{
    const auto frame = sent_frame(index, linux95::net::EtherType::Arp);
    assert(frame.payload_length == 28);
    assert(frame.payload[6] == 0 && frame.payload[7] == 1);
    assert(memcmp(frame.payload + 24, target.bytes, 4) == 0);
}

linux95::net::ipv4::PacketView sent_ipv4(
    unsigned index, const MacAddress& destination_mac,
    const Ipv4Address& destination_ip)
{
    const auto frame = sent_frame(index, linux95::net::EtherType::Ipv4);
    assert(memcmp(frame.destination.bytes, destination_mac.bytes, 6) == 0);
    linux95::net::ipv4::PacketView packet{};
    assert(linux95::net::ipv4::parse(frame.payload, frame.payload_length,
                                     packet));
    assert(memcmp(packet.source.bytes, kLocalIp.bytes, 4) == 0);
    assert(memcmp(packet.destination.bytes, destination_ip.bytes, 4) == 0);
    return packet;
}

void check_sent_udp(unsigned index, const MacAddress& destination_mac,
                    const Ipv4Address& destination_ip,
                    uint16_t source_port, uint16_t destination_port,
                    const uint8_t* payload, uint16_t payload_length)
{
    const auto packet = sent_ipv4(index, destination_mac, destination_ip);
    assert(packet.protocol == 17);
    assert(packet.payload_length == static_cast<uint16_t>(payload_length + 8));
    assert(packet.payload[6] != 0 || packet.payload[7] != 0);
    assert(linux95::net::udp::checksum(kLocalIp, destination_ip,
                                       packet.payload, packet.payload_length) == 0);
    linux95::net::udp::DatagramView datagram{};
    assert(linux95::net::udp::parse(packet.payload, packet.payload_length,
                                    kLocalIp, destination_ip, datagram));
    assert(datagram.source_port == source_port);
    assert(datagram.destination_port == destination_port);
    assert(datagram.payload_length == payload_length);
    if (payload_length != 0) {
        assert(memcmp(datagram.payload, payload, payload_length) == 0);
    }
}

void check_send_validation()
{
    using namespace linux95;
    const uint8_t byte = 0x5A;
    uint8_t maximum[1473]{};
    g_sent_count = 0;
    g_device_available = false;
    assert(!network::initialize());
    assert(!network::send_udp(kPeerIp, 40001, 40000, &byte, 1));
    assert(g_sent_count == 0);
    g_device_available = true;
    reset_case();
    assert(!network::send_udp(kPeerIp, 0, 40000, &byte, 1));
    assert(!network::send_udp(kPeerIp, 40001, 0, &byte, 1));
    assert(!network::send_udp(kPeerIp, 40001, 40000, nullptr, 1));
    assert(!network::send_udp(kPeerIp, 40001, 40000, maximum, 1473));
    assert(g_sent_count == 0);
    enqueue_arp_reply(kPeerIp, kPeerMac);
    network::poll();
    assert(network::send_udp(kPeerIp, 40001, 40000, maximum, 1472));
    assert(g_sent_count == 1 && g_sent[0].length == 1514);
    check_sent_udp(0, kPeerMac, kPeerIp, 40001, 40000, maximum, 1472);
    puts("[PASS] network_udp_send_validation_and_maximum");
}

void check_cached_and_gateway_routing()
{
    using namespace linux95;
    reset_case();
    enqueue_arp_reply(kPeerIp, kPeerMac);
    enqueue_arp_reply(kOtherIp, kOtherMac);
    network::poll();
    const uint8_t body[] = {'r', 'o', 'u', 't', 'e'};
    assert(network::send_udp(kOtherIp, 1234, 5678, body, sizeof(body)));
    check_sent_udp(0, kOtherMac, kOtherIp, 1234, 5678, body, sizeof(body));
    assert(network::send_udp(kRemoteIp, 1234, 5678, nullptr, 0));
    check_sent_udp(1, kPeerMac, kRemoteIp, 1234, 5678, nullptr, 0);
    assert(g_sent_count == 2);
    puts("[PASS] network_udp_cached_next_hop_and_checksum");
}

void check_pending_slot_and_unrelated_arp()
{
    using namespace linux95;
    reset_case();
    uint8_t first[] = {'f', 'i', 'r', 's', 't'};
    const uint8_t second[] = {'s', 'e', 'c', 'o', 'n', 'd'};
    assert(network::send_udp(kRemoteIp, 1234, 5678, first, sizeof(first)));
    check_arp_target(0, kPeerIp);
    first[0] = 'X';
    assert(!network::send_udp(kOtherIp, 1234, 5678, second, sizeof(second)));
    assert(g_sent_count == 1);
    enqueue_arp_reply(kOtherIp, kOtherMac);
    network::poll();
    assert(g_sent_count == 1);
    assert(network::send_udp(kOtherIp, 1234, 5678, second, sizeof(second)));
    check_sent_udp(1, kOtherMac, kOtherIp, 1234, 5678,
                   second, sizeof(second));
    enqueue_arp_reply(kPeerIp, kPeerMac);
    network::poll();
    const uint8_t original[] = {'f', 'i', 'r', 's', 't'};
    check_sent_udp(2, kPeerMac, kRemoteIp, 1234, 5678,
                   original, sizeof(original));
    enqueue_arp_reply(kPeerIp, kPeerMac);
    network::poll();
    assert(g_sent_count == 3);
    puts("[PASS] network_udp_one_pending_slot_and_matching_arp");
}

void check_pending_timeout_and_reuse()
{
    using namespace linux95;
    reset_case();
    const uint8_t old_body[] = {'o', 'l', 'd'};
    const uint8_t new_body[] = {'n', 'e', 'w'};
    assert(network::send_udp(kOtherIp, 1234, 5678, old_body, sizeof(old_body)));
    check_arp_target(0, kOtherIp);
    g_now = 1;
    network::poll();
    assert(network::send_udp(kRemoteIp, 1234, 5678, new_body, sizeof(new_body)));
    check_arp_target(1, kPeerIp);
    enqueue_arp_reply(kOtherIp, kOtherMac);
    network::poll();
    assert(g_sent_count == 2);
    enqueue_arp_reply(kPeerIp, kPeerMac);
    network::poll();
    check_sent_udp(2, kPeerMac, kRemoteIp, 1234, 5678,
                   new_body, sizeof(new_body));
    assert(g_sent_count == 3);
    puts("[PASS] network_udp_pending_timeout_and_reuse");
}

void check_failed_transmit_cleanup()
{
    using namespace linux95;
    const uint8_t body[] = {'f'};
    reset_case();
    g_fail_next_transmit = true;
    assert(!network::send_udp(kOtherIp, 1234, 5678, body, sizeof(body)));
    assert(g_sent_count == 0);
    assert(network::send_udp(kRemoteIp, 1234, 5678, body, sizeof(body)));
    check_arp_target(0, kPeerIp);
    enqueue_arp_reply(kPeerIp, kPeerMac);
    g_fail_next_transmit = true;
    network::poll();
    assert(g_sent_count == 1);
    assert(network::send_udp(kOtherIp, 1234, 5678, body, sizeof(body)));
    check_arp_target(1, kOtherIp);
    enqueue_arp_reply(kOtherIp, kOtherMac);
    network::poll();
    check_sent_udp(2, kOtherMac, kOtherIp, 1234, 5678, body, sizeof(body));
    assert(g_sent_count == 3);
    puts("[PASS] network_udp_failed_transmit_cleanup");
}

void check_icmp_udp_arp_coexistence()
{
    using namespace linux95;
    reset_case();
    const uint8_t body[] = {'b', 'o', 't', 'h'};
    assert(network::start_ping(kRemoteIp));
    assert(network::ping_result().state == net::icmp::PingState::ResolvingArp);
    assert(network::send_udp(kRemoteIp, 1234, 5678, body, sizeof(body)));
    assert(g_sent_count == 2);
    check_arp_target(0, kPeerIp);
    check_arp_target(1, kPeerIp);
    enqueue_arp_reply(kPeerIp, kPeerMac);
    network::poll();
    assert(network::ping_result().state == net::icmp::PingState::WaitingReply);
    assert(g_sent_count == 4);
    unsigned udp_count = 0;
    unsigned icmp_count = 0;
    for (unsigned i = 2; i < 4; ++i) {
        const auto packet = sent_ipv4(i, kPeerMac, kRemoteIp);
        if (packet.protocol == 17) {
            ++udp_count;
            check_sent_udp(i, kPeerMac, kRemoteIp, 1234, 5678,
                           body, sizeof(body));
        } else {
            assert(packet.protocol == 1);
            ++icmp_count;
        }
    }
    assert(udp_count == 1 && icmp_count == 1);
    puts("[PASS] network_udp_icmp_pending_coexistence");
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
bool transmit(const uint8_t* bytes, uint16_t length)
{
    if (g_fail_next_transmit) {
        g_fail_next_transmit = false;
        return false;
    }
    assert(g_sent_count < 32 && length <= sizeof(g_sent[0].bytes));
    Frame& frame = g_sent[g_sent_count++];
    memcpy(frame.bytes, bytes, length);
    frame.length = length;
    return true;
}
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
    check_send_validation();
    check_cached_and_gateway_routing();
    check_pending_slot_and_unrelated_arp();
    check_pending_timeout_and_reuse();
    check_failed_transmit_cleanup();
    check_icmp_udp_arp_coexistence();
    return 0;
}
