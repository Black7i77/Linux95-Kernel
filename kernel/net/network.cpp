#include "net/network.hpp"

#include "arch/debug.hpp"
#include "arch/pit.hpp"
#include "drivers/rtl8139.hpp"
#include "net/arp.hpp"
#include "net/ethernet.hpp"
#include "net/ipv4.hpp"
#include "net/udp.hpp"

#include <stdint.h>

namespace linux95::network {
namespace {

constexpr net::Ipv4Address kLocalIp =
    net::Ipv4Address{{10, 0, 2, 15}};
constexpr net::Ipv4Address kNetmask =
    net::Ipv4Address{{255, 255, 255, 0}};
constexpr net::Ipv4Address kGateway =
    net::Ipv4Address{{10, 0, 2, 2}};
constexpr net::MacAddress kBroadcastMac =
    net::MacAddress{{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

constexpr uint8_t kMaxFramesPerPoll = 8;
constexpr uint16_t kMaxFrameLength = 1518;
constexpr uint16_t kMaxIpv4Length = 1500;
constexpr uint8_t kIcmpProtocol = 1;
constexpr uint8_t kUdpProtocol = 17;
constexpr uint16_t kPingIdentifier = 0x4C95;
constexpr uint64_t kArpTimeoutSeconds = 1;
constexpr uint64_t kReplyTimeoutSeconds = 2;

Status g_status{false, {}, kLocalIp, kNetmask, kGateway};
net::icmp::PingResult g_ping{
    net::icmp::PingState::Idle,
    {},
    0,
    0,
};
net::Ipv4Address g_next_hop{};
uint64_t g_deadline = 0;
uint16_t g_next_sequence = 1;
uint16_t g_next_identification = 1;
net::udp::bindings::Table g_udp_bindings{};
struct PendingUdp {
    bool occupied;
    net::Ipv4Address next_hop;
    uint16_t packet_length;
    uint64_t deadline;
    uint8_t packet[kMaxIpv4Length];
};
PendingUdp g_pending_udp{};

void diagnostic(const char* message)
{
#ifndef LINUX95_NETWORK_HOST_TEST
    debug::write(message);
#else
    (void)message;
#endif
}

bool mac_equals(
    const net::MacAddress& left,
    const net::MacAddress& right)
{
    for (uint8_t i = 0; i < 6; ++i) {
        if (left.bytes[i] != right.bytes[i]) {
            return false;
        }
    }
    return true;
}

bool ipv4_equals(
    const net::Ipv4Address& left,
    const net::Ipv4Address& right)
{
    for (uint8_t i = 0; i < 4; ++i) {
        if (left.bytes[i] != right.bytes[i]) {
            return false;
        }
    }
    return true;
}

bool transmit_ethernet(
    const net::MacAddress& destination,
    net::EtherType type,
    const uint8_t* payload,
    uint16_t payload_length)
{
    uint8_t frame[kMaxFrameLength];
    uint16_t frame_length = 0;
    if (!net::build_ethernet(
            frame,
            sizeof(frame),
            destination,
            g_status.mac,
            type,
            payload,
            payload_length,
            frame_length)) {
        return false;
    }
    return rtl8139::transmit(frame, frame_length);
}

bool transmit_arp_request(const net::Ipv4Address& target)
{
    uint8_t payload[28];
    uint16_t payload_length = 0;
    if (!net::arp::build_request(
            payload,
            sizeof(payload),
            g_status.mac,
            g_status.ip,
            target,
            payload_length)) {
        return false;
    }
    return transmit_ethernet(
        kBroadcastMac,
        net::EtherType::Arp,
        payload,
        payload_length);
}

bool transmit_arp_reply(
    const net::MacAddress& target_mac,
    const net::Ipv4Address& target_ip)
{
    uint8_t payload[28];
    uint16_t payload_length = 0;
    if (!net::arp::build_reply(
            payload,
            sizeof(payload),
            g_status.mac,
            g_status.ip,
            target_mac,
            target_ip,
            payload_length)) {
        return false;
    }
    return transmit_ethernet(
        target_mac,
        net::EtherType::Arp,
        payload,
        payload_length);
}

bool transmit_echo_request(const net::MacAddress& destination_mac)
{
    uint8_t icmp_payload[40];
    uint16_t icmp_length = 0;
    if (!net::icmp::build_echo_request(
            icmp_payload,
            sizeof(icmp_payload),
            kPingIdentifier,
            g_ping.sequence,
            icmp_length)) {
        return false;
    }

    uint8_t ipv4_packet[kMaxIpv4Length];
    uint16_t ipv4_length = 0;
    if (!net::ipv4::build(
            ipv4_packet,
            sizeof(ipv4_packet),
            g_status.ip,
            g_ping.address,
            kIcmpProtocol,
            g_next_identification++,
            icmp_payload,
            icmp_length,
            ipv4_length)) {
        return false;
    }

    if (!transmit_ethernet(
            destination_mac,
            net::EtherType::Ipv4,
            ipv4_packet,
            ipv4_length)) {
        return false;
    }

    g_ping.state = net::icmp::PingState::WaitingReply;
    g_deadline = pit::uptime_seconds() + kReplyTimeoutSeconds;
    return true;
}

void handle_arp(const net::EthernetView& frame)
{
    bool should_reply = false;
    net::MacAddress sender_mac{};
    net::Ipv4Address sender_ip{};
    if (!net::arp::parse_and_learn(
            frame.payload,
            frame.payload_length,
            g_status.ip,
            g_status.mac,
            should_reply,
            sender_mac,
            sender_ip)) {
        return;
    }

    if (should_reply) {
        (void)transmit_arp_reply(sender_mac, sender_ip);
    }

    if (g_ping.state == net::icmp::PingState::ResolvingArp) {
        net::MacAddress next_hop_mac{};
        if (net::arp::lookup(g_next_hop, next_hop_mac)) {
            if (transmit_echo_request(next_hop_mac)) {
                diagnostic("[PASS] arp_gateway_resolved\n");
            } else {
                g_ping.state = net::icmp::PingState::HostUnreachable;
            }
        }
    }

    if (g_pending_udp.occupied) {
        if (pit::uptime_seconds() >= g_pending_udp.deadline) {
            g_pending_udp.occupied = false;
        } else if (ipv4_equals(sender_ip, g_pending_udp.next_hop)) {
            g_pending_udp.occupied = false;
            (void)transmit_ethernet(sender_mac, net::EtherType::Ipv4,
                                    g_pending_udp.packet,
                                    g_pending_udp.packet_length);
        }
    }
}

void handle_ipv4(const net::EthernetView& frame)
{
    net::ipv4::PacketView packet{};
    if (!net::ipv4::parse(
            frame.payload,
            frame.payload_length,
            packet) ||
        !ipv4_equals(packet.destination, g_status.ip)) {
        return;
    }

    if (packet.protocol == kIcmpProtocol) {
        if (g_ping.state != net::icmp::PingState::WaitingReply ||
            !ipv4_equals(packet.source, g_ping.address)) {
            return;
        }
        uint16_t payload_bytes = 0;
        if (net::icmp::accept_echo_reply(
                packet.payload,
                packet.payload_length,
                kPingIdentifier,
                g_ping.sequence,
                payload_bytes)) {
            g_ping.payload_bytes = payload_bytes;
            g_ping.state = net::icmp::PingState::ReplyReceived;
            diagnostic("[PASS] icmp_echo_reply\n");
        }
    } else if (packet.protocol == kUdpProtocol) {
        net::udp::DatagramView datagram{};
        if (net::udp::parse(packet.payload, packet.payload_length,
                            packet.source, packet.destination, datagram)) {
            g_udp_bindings.dispatch(packet.source, datagram.source_port,
                                    datagram.destination_port, datagram.payload,
                                    datagram.payload_length);
        }
    }
}

void dispatch_frame(const uint8_t* bytes, uint16_t length)
{
    net::EthernetView frame{};
    if (!net::parse_ethernet(bytes, length, frame) ||
        (!mac_equals(frame.destination, g_status.mac) &&
         !mac_equals(frame.destination, kBroadcastMac))) {
        return;
    }

    if (frame.type == net::EtherType::Arp) {
        handle_arp(frame);
    } else if (frame.type == net::EtherType::Ipv4) {
        handle_ipv4(frame);
    }
}

void advance_timeout()
{
    const uint64_t now = pit::uptime_seconds();
    if (g_pending_udp.occupied && now >= g_pending_udp.deadline) {
        g_pending_udp.occupied = false;
    }
    if (g_ping.state == net::icmp::PingState::ResolvingArp &&
        now >= g_deadline) {
        g_ping.state = net::icmp::PingState::HostUnreachable;
    } else if (g_ping.state == net::icmp::PingState::WaitingReply &&
               now >= g_deadline) {
        g_ping.state = net::icmp::PingState::TimedOut;
    }
}

} // namespace

bool initialize()
{
    g_status = Status{false, {}, kLocalIp, kNetmask, kGateway};
    g_ping = net::icmp::PingResult{
        net::icmp::PingState::Idle,
        {},
        0,
        0,
    };
    g_next_hop = net::Ipv4Address{};
    g_deadline = 0;
    g_next_sequence = 1;
    g_next_identification = 1;
    g_pending_udp.occupied = false;
    net::arp::reset();
    g_udp_bindings = net::udp::bindings::Table{};

    if (!rtl8139::initialize()) {
        return false;
    }

    const rtl8139::MacAddress hardware_mac = rtl8139::mac_address();
    for (uint8_t i = 0; i < 6; ++i) {
        g_status.mac.bytes[i] = hardware_mac.bytes[i];
    }
    g_status.online = true;

    diagnostic("[PASS] rtl8139_detected\n");
    diagnostic("[PASS] rtl8139_initialized\n");
    diagnostic("[PASS] ethernet_ready\n");
    diagnostic("[PASS] arp_ready\n");
    diagnostic("[PASS] ipv4_ready\n");
    diagnostic("[PASS] icmp_ready\n");
    diagnostic("[PASS] udp_ready\n");
    return true;
}

bool bind_udp_port(uint16_t port, UdpReceiveCallback callback, void* context)
{
    return g_udp_bindings.bind(port, callback, context);
}

bool unbind_udp_port(uint16_t port)
{
    return g_udp_bindings.unbind(port);
}

bool send_udp(const net::Ipv4Address& destination,
              uint16_t source_port,
              uint16_t destination_port,
              const uint8_t* payload,
              uint16_t payload_length)
{
    if (!g_status.online || source_port == 0 || destination_port == 0 ||
        (payload == nullptr && payload_length != 0) ||
        payload_length > net::udp::kMaxPayloadLength) {
        return false;
    }

    const net::Ipv4Address next_hop = net::ipv4::next_hop(
        destination, g_status.ip, g_status.netmask, g_status.gateway);
    net::MacAddress next_hop_mac{};
    const bool cached = net::arp::lookup(next_hop, next_hop_mac);
    if (!cached && g_pending_udp.occupied &&
        pit::uptime_seconds() >= g_pending_udp.deadline) {
        g_pending_udp.occupied = false;
    }
    if (!cached && g_pending_udp.occupied) {
        return false;
    }

    uint8_t datagram[net::udp::kHeaderLength + net::udp::kMaxPayloadLength];
    uint16_t datagram_length = 0;
    if (!net::udp::build(datagram, sizeof(datagram), g_status.ip,
                         destination, source_port, destination_port,
                         payload, payload_length, datagram_length)) {
        return false;
    }
    uint8_t packet[kMaxIpv4Length];
    uint16_t packet_length = 0;
    if (!net::ipv4::build(packet, sizeof(packet), g_status.ip, destination,
                          kUdpProtocol, g_next_identification++, datagram,
                          datagram_length, packet_length)) {
        return false;
    }

    if (cached) {
        return transmit_ethernet(next_hop_mac, net::EtherType::Ipv4,
                                 packet, packet_length);
    }

    for (uint16_t i = 0; i < packet_length; ++i) {
        g_pending_udp.packet[i] = packet[i];
    }
    g_pending_udp.next_hop = next_hop;
    g_pending_udp.packet_length = packet_length;
    g_pending_udp.deadline = pit::uptime_seconds() + kArpTimeoutSeconds;
    g_pending_udp.occupied = true;
    if (!transmit_arp_request(next_hop)) {
        g_pending_udp.occupied = false;
        return false;
    }
    return true;
}

void poll()
{
    if (!g_status.online) {
        return;
    }

    for (uint8_t frame_count = 0;
         frame_count < kMaxFramesPerPoll;
         ++frame_count) {
        uint8_t frame[kMaxFrameLength];
        uint16_t frame_length = 0;
        if (!rtl8139::poll_receive(
                frame,
                sizeof(frame),
                frame_length)) {
            break;
        }
        dispatch_frame(frame, frame_length);
    }

    advance_timeout();
}

Status status()
{
    return g_status;
}

bool start_ping(net::Ipv4Address destination)
{
    if (!g_status.online ||
        g_ping.state != net::icmp::PingState::Idle) {
        return false;
    }

    g_ping.address = destination;
    g_ping.sequence = g_next_sequence++;
    g_ping.payload_bytes = 0;
    g_next_hop = net::ipv4::next_hop(
        destination,
        g_status.ip,
        g_status.netmask,
        g_status.gateway);

    net::MacAddress next_hop_mac{};
    if (net::arp::lookup(g_next_hop, next_hop_mac)) {
        if (!transmit_echo_request(next_hop_mac)) {
            g_ping.state = net::icmp::PingState::HostUnreachable;
        }
        return true;
    }

    g_ping.state = net::icmp::PingState::ResolvingArp;
    g_deadline = pit::uptime_seconds() + kArpTimeoutSeconds;
    if (!transmit_arp_request(g_next_hop)) {
        g_ping.state = net::icmp::PingState::HostUnreachable;
    }
    return true;
}

net::icmp::PingResult ping_result()
{
    return g_ping;
}

void clear_ping_result()
{
    g_ping = net::icmp::PingResult{
        net::icmp::PingState::Idle,
        {},
        0,
        0,
    };
}

} // namespace linux95::network
