#include "net/udp_bindings.hpp"

namespace linux95::net::udp::bindings {

bool Table::bind(uint16_t port, ReceiveCallback callback, void* context)
{
    if (port == 0 || callback == nullptr) {
        return false;
    }
    for (const Entry& entry : entries_) {
        if (entry.in_use && entry.port == port) {
            return false;
        }
    }
    for (Entry& entry : entries_) {
        if (!entry.in_use) {
            entry = {true, port, callback, context};
            return true;
        }
    }
    return false;
}

bool Table::unbind(uint16_t port)
{
    for (Entry& entry : entries_) {
        if (entry.in_use && entry.port == port) {
            entry = {};
            return true;
        }
    }
    return false;
}

bool Table::dispatch(const Ipv4Address& source, uint16_t source_port,
                     uint16_t destination_port, const uint8_t* payload,
                     uint16_t payload_length)
{
    for (const Entry& entry : entries_) {
        if (entry.in_use && entry.port == destination_port) {
            entry.callback(source, source_port, destination_port, payload,
                           payload_length, entry.context);
            return true;
        }
    }
    return false;
}

} // namespace linux95::net::udp::bindings
