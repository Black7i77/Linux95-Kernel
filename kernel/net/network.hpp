#pragma once

#include "net/icmp.hpp"
#include "net/net_types.hpp"

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

bool start_ping(net::Ipv4Address destination);

net::icmp::PingResult ping_result();
void clear_ping_result();

} // namespace linux95::network
