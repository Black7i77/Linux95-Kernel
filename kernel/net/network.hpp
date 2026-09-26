#pragma once

#include "net/icmp.hpp"
#include "net/net_types.hpp"
#include "net/udp_bindings.hpp"

namespace linux95::network {

struct Status {
    bool online;
    net::MacAddress mac;
    net::Ipv4Address ip;
    net::Ipv4Address netmask;
    net::Ipv4Address gateway;
};

bool initialize();
void poll();
Status status();

using UdpReceiveCallback = net::udp::bindings::ReceiveCallback;
bool bind_udp_port(uint16_t port, UdpReceiveCallback callback, void* context);
bool unbind_udp_port(uint16_t port);

bool start_ping(net::Ipv4Address destination);

net::icmp::PingResult ping_result();
void clear_ping_result();

} // namespace linux95::network
