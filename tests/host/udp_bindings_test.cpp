#include "net/udp_bindings.hpp"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

namespace {

using linux95::net::Ipv4Address;
namespace bindings = linux95::net::udp::bindings;

struct Received {
    unsigned calls = 0;
    Ipv4Address source{};
    uint16_t source_port = 0;
    uint16_t destination_port = 0;
    const uint8_t* expected_payload = nullptr;
    uint16_t payload_length = 0;
    uint8_t first_byte = 0;
};

void receive(const Ipv4Address& source, uint16_t source_port,
             uint16_t destination_port, const uint8_t* payload,
             uint16_t payload_length, void* context)
{
    auto& received = *static_cast<Received*>(context);
    ++received.calls;
    received.source = source;
    received.source_port = source_port;
    received.destination_port = destination_port;
    if (received.expected_payload != nullptr) {
        assert(payload == received.expected_payload);
    }
    received.payload_length = payload_length;
    received.first_byte = payload_length ? payload[0] : 0;
    // The payload pointer is valid only during this call; consumers copy data needed later.
}

void test_capacity_and_rejections()
{
    bindings::Table table{};
    Received received[bindings::kMaxUdpBindings]{};
    assert(bindings::kMaxUdpBindings == 8);
    assert(!table.bind(0, receive, &received[0]));
    assert(!table.bind(1, nullptr, &received[0]));
    for (uint16_t port = 1; port <= 8; ++port) {
        assert(table.bind(port, receive, &received[port - 1]));
    }
    assert(!table.bind(1, receive, &received[0]));
    assert(!table.bind(9, receive, &received[0]));
    assert(!table.unbind(9));

    const Ipv4Address source{{192, 0, 2, 7}};
    const uint8_t payload[] = {0x42};
    received[7].expected_payload = payload;
    table.dispatch(source, 3000, 8, payload, sizeof(payload));
    assert(received[7].calls == 1);
    assert(received[0].calls == 0);
    assert(table.unbind(4));
    assert(table.bind(9, receive, &received[0]));
    table.dispatch(source, 3000, 9, payload, sizeof(payload));
    assert(received[0].calls == 1);
    puts("[PASS] udp_bindings_capacity_and_rejections");
}

void test_unbind_rebind_and_dispatch()
{
    bindings::Table table{};
    Received first{};
    Received second{};
    assert(table.bind(53, receive, &first));
    assert(table.bind(54, receive, &second));

    const Ipv4Address source{{203, 0, 113, 9}};
    uint8_t payload[] = {0xA5, 0x5A};
    first.expected_payload = payload;
    second.expected_payload = payload;
    table.dispatch(source, 4040, 53, payload, sizeof(payload));
    assert(first.calls == 1 && second.calls == 0);
    assert(first.source.bytes[0] == 203 && first.source.bytes[1] == 0);
    assert(first.source.bytes[2] == 113 && first.source.bytes[3] == 9);
    assert(first.source_port == 4040 && first.destination_port == 53);
    assert(first.payload_length == 2);
    assert(first.first_byte == 0xA5);

    table.dispatch(source, 4040, 55, payload, sizeof(payload));
    assert(first.calls == 1 && second.calls == 0);
    assert(table.unbind(53));
    assert(!table.unbind(53));
    table.dispatch(source, 4040, 53, payload, sizeof(payload));
    assert(first.calls == 1);
    assert(table.bind(53, receive, &second));
    table.dispatch(source, 4040, 53, payload, sizeof(payload));
    assert(first.calls == 1 && second.calls == 1);
    assert(second.destination_port == 53);
    puts("[PASS] udp_bindings_unbind_rebind_dispatch");
}

} // namespace

int main()
{
    test_capacity_and_rejections();
    test_unbind_rebind_and_dispatch();
    return 0;
}
