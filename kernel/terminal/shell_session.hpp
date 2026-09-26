#pragma once

#include "net/network.hpp"
#include "net/dns.hpp"
#include "terminal/output.hpp"

#include <stddef.h>

namespace linux95::terminal {

bool parse_ipv4(
    const char* text,
    net::Ipv4Address& out);

using ExecuteCallback =
    void (*)(
        void* context,
        Output& output,
        char* command);

struct NetworkCallbacks {
    void* context;
    network::Status (*status)(void* context);
    bool (*start_ping)(
        void* context,
        net::Ipv4Address destination);
    net::icmp::PingResult (*ping_result)(void* context);
    void (*clear_ping_result)(void* context);
};

struct DnsCallbacks {
    void* context;
    net::dns::Status (*begin_lookup)(void* context, const char* hostname);
    net::dns::Status (*lookup_status)(void* context);
    size_t (*result_count)(void* context);
    bool (*result_address)(void* context, size_t index, net::Ipv4Address& out);
    net::Ipv4Address (*server)(void* context);
    net::dns::Status (*set_server)(void* context, const net::Ipv4Address& address);
};

class ShellSession {
public:
    ShellSession(
        Output& output,
        void* context,
        ExecuteCallback execute,
        const NetworkCallbacks* network_callbacks = nullptr,
        const DnsCallbacks* dns_callbacks = nullptr);

    void begin();
    void on_char(char c);
    bool poll();

private:
    static constexpr size_t kCommandCapacity = 260;

    Output& output_;
    void* execute_context_;
    ExecuteCallback execute_;
    const NetworkCallbacks* network_callbacks_;
    const DnsCallbacks* dns_callbacks_;
    bool dns_pending_;

    char command_[kCommandCapacity];
    size_t length_;

    void prompt();
    bool execute_network_command();
    bool execute_dns_command();
};

} // namespace linux95::terminal
