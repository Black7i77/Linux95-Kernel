#pragma once

#include "net/net_types.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::net::udp::bindings {

constexpr size_t kMaxUdpBindings = 8;

// payload is valid only until the callback returns; copy data needed afterward.
using ReceiveCallback = void (*)(const Ipv4Address& source,
                                 uint16_t source_port,
                                 uint16_t destination_port,
                                 const uint8_t* payload,
                                 uint16_t payload_length,
                                 void* context);

class Table {
public:
    bool bind(uint16_t port, ReceiveCallback callback, void* context);
    bool unbind(uint16_t port);
    void dispatch(const Ipv4Address& source, uint16_t source_port,
                  uint16_t destination_port, const uint8_t* payload,
                  uint16_t payload_length);

private:
    struct Entry {
        bool in_use;
        uint16_t port;
        ReceiveCallback callback;
        void* context;
    };

    Entry entries_[kMaxUdpBindings]{};
};

} // namespace linux95::net::udp::bindings
