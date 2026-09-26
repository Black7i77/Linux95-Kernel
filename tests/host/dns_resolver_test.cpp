#include "arch/pit.hpp"
#include "net/dns.hpp"
#include "net/network.hpp"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace dns = linux95::net::dns;
namespace net = linux95::net;

namespace fake {
struct Sent {
    net::Ipv4Address destination;
    uint16_t source_port;
    uint16_t destination_port;
    std::vector<uint8_t> payload;
};

bool online = true;
bool bind_fails = false;
bool send_fails = false;
uint64_t now = 0;
uint32_t rate = 119;
unsigned binds = 0;
linux95::network::UdpReceiveCallback callback = nullptr;
std::vector<Sent> sent;

net::Ipv4Address ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return {{a, b, c, d}};
}

bool same(net::Ipv4Address a, net::Ipv4Address b)
{
    return memcmp(a.bytes, b.bytes, 4) == 0;
}

uint16_t id(const Sent& item)
{
    return static_cast<uint16_t>((item.payload[0] << 8) | item.payload[1]);
}

void put16(std::vector<uint8_t>& packet, size_t offset, uint16_t value)
{
    packet[offset] = static_cast<uint8_t>(value >> 8);
    packet[offset + 1] = static_cast<uint8_t>(value);
}

void add16(std::vector<uint8_t>& packet, uint16_t value)
{
    packet.push_back(static_cast<uint8_t>(value >> 8));
    packet.push_back(static_cast<uint8_t>(value));
}

std::vector<uint8_t> response(uint16_t flags = 0x8180, uint16_t answers = 0)
{
    std::vector<uint8_t> packet = sent.back().payload;
    put16(packet, 2, flags);
    put16(packet, 6, answers);
    return packet;
}

void add_a(std::vector<uint8_t>& packet, uint8_t last)
{
    add16(packet, 0xC00C);
    add16(packet, 1);
    add16(packet, 1);
    packet.insert(packet.end(), {0, 0, 0, 0});
    add16(packet, 4);
    packet.insert(packet.end(), {10, 20, 30, last});
}

void add_cname(std::vector<uint8_t>& packet, const char* target)
{
    dns::Name encoded{};
    assert(dns::encode_hostname(target, encoded));
    add16(packet, 0xC00C);
    add16(packet, 5);
    add16(packet, 1);
    packet.insert(packet.end(), {0, 0, 0, 0});
    add16(packet, encoded.length);
    packet.insert(packet.end(), encoded.wire, encoded.wire + encoded.length);
}

void deliver(std::vector<uint8_t>& packet,
             net::Ipv4Address source = ip(10, 0, 2, 3),
             uint16_t source_port = 53, uint16_t destination_port = 53000)
{
    assert(callback);
    callback(source, source_port, destination_port,
             packet.data(), static_cast<uint16_t>(packet.size()), nullptr);
    // A UDP callback owns no payload memory after it returns.
    for (uint8_t& byte : packet) byte = 0;
}

void begin(const char* hostname = "a.test")
{
    sent.clear();
    assert(dns::begin_lookup(hostname) == dns::Status::Pending);
    assert(dns::lookup_status() == dns::Status::Pending);
    assert(sent.size() == 1);
}

void finish_nxdomain()
{
    auto packet = response(0x8183);
    deliver(packet);
    assert(dns::lookup_status() == dns::Status::NotFound);
}
} // namespace fake

namespace linux95::network {
Status status() { return {fake::online, {}, {}, {}, {}}; }
bool bind_udp_port(uint16_t port, UdpReceiveCallback callback, void* context)
{
    assert(port == 53000 && context == nullptr);
    ++fake::binds;
    if (fake::bind_fails) return false;
    fake::callback = callback;
    return true;
}
bool send_udp(const net::Ipv4Address& destination, uint16_t source_port,
              uint16_t destination_port, const uint8_t* payload,
              uint16_t payload_length)
{
    if (fake::send_fails) return false;
    fake::sent.push_back({destination, source_port, destination_port,
                          {payload, payload + payload_length}});
    return true;
}
} // namespace linux95::network

namespace linux95::pit {
uint64_t ticks() { return fake::now; }
uint32_t ticks_per_second() { return fake::rate; }
} // namespace linux95::pit

void test_initialization_and_transport_failures()
{
    assert(fake::same(dns::server(), fake::ip(10, 0, 2, 3)));
    assert(dns::begin_lookup("a.test") == dns::Status::NetworkUnavailable);
    fake::bind_fails = true;
    assert(!dns::initialize());
    assert(fake::binds == 1);
    assert(dns::begin_lookup("a.test") == dns::Status::NetworkUnavailable);
    fake::bind_fails = false;
    assert(dns::initialize());
    assert(fake::binds == 2);
    assert(dns::initialize());
    assert(fake::binds == 2);
    fake::online = false;
    fake::sent.clear();
    assert(dns::begin_lookup("a.test") == dns::Status::NetworkUnavailable);
    assert(fake::sent.empty());
    fake::online = true;
    fake::send_fails = true;
    assert(dns::begin_lookup("a.test") == dns::Status::NetworkUnavailable);
    assert(fake::sent.empty());
    fake::send_fails = false;
    fake::begin();
    fake::now += 238;
    fake::send_fails = true;
    dns::poll();
    assert(dns::lookup_status() == dns::Status::NetworkUnavailable);
    assert(fake::sent.size() == 1);
    fake::send_fails = false;
    puts("[PASS] initialize_offline_bind_and_send_failures");
}

void test_query_busy_configuration_and_results()
{
    fake::begin("MiXeD.test.");
    const auto& first = fake::sent.back();
    assert(fake::same(first.destination, fake::ip(10, 0, 2, 3)));
    assert(first.source_port == 53000 && first.destination_port == 53);
    assert(first.payload[2] == 1 && first.payload[3] == 0);
    assert(first.payload[4] == 0 && first.payload[5] == 1);
    dns::Name expected{};
    assert(dns::encode_hostname("MiXeD.test", expected));
    assert(first.payload.size() == static_cast<size_t>(12 + expected.length + 4));
    assert(memcmp(first.payload.data() + 12, expected.wire, expected.length) == 0);
    assert(dns::begin_lookup("other.test") == dns::Status::Busy);
    assert(dns::set_server(fake::ip(1, 1, 1, 1)) == dns::Status::Busy);
    assert(fake::same(dns::server(), fake::ip(10, 0, 2, 3)));
    assert(fake::sent.size() == 1);
    auto packet = fake::response(0x8180, 2);
    fake::add_a(packet, 7);
    fake::add_a(packet, 7);
    fake::deliver(packet);
    assert(dns::lookup_status() == dns::Status::Success);
    assert(dns::result_count() == 1);
    net::Ipv4Address address{};
    assert(dns::result_address(0, address));
    assert(fake::same(address, fake::ip(10, 20, 30, 7)));
    assert(!dns::result_address(1, address));
    assert(dns::set_server(fake::ip(1, 1, 1, 1)) == dns::Status::Success);
    assert(fake::same(dns::server(), fake::ip(1, 1, 1, 1)));
    assert(dns::result_count() == 1);
    const size_t sent_before = fake::sent.size();
    assert(dns::begin_lookup("bad..test") == dns::Status::InvalidName);
    assert(fake::sent.size() == sent_before);
    assert(dns::result_count() == 1);
    assert(dns::result_address(0, address));
    assert(fake::same(address, fake::ip(10, 20, 30, 7)));
    assert(dns::set_server(fake::ip(10, 0, 2, 3)) == dns::Status::Success);
    fake::begin("new.test");
    assert(dns::result_count() == 0);
    fake::finish_nxdomain();
    puts("[PASS] query_busy_configuration_result_lifetime_and_reset");
}

void test_peer_id_question_and_statuses()
{
    fake::now = 100;
    fake::begin();
    const uint16_t current = fake::id(fake::sent.back());
    auto wrong = fake::response();
    fake::deliver(wrong, fake::ip(10, 0, 2, 4));
    wrong = fake::response();
    fake::deliver(wrong, fake::ip(10, 0, 2, 3), 54);
    wrong = fake::response();
    fake::deliver(wrong, fake::ip(10, 0, 2, 3), 53, 53001);
    wrong = fake::response();
    fake::put16(wrong, 0, static_cast<uint16_t>(current - 1));
    fake::deliver(wrong);
    assert(dns::lookup_status() == dns::Status::Pending);
    fake::now = 337;
    dns::poll();
    assert(fake::sent.size() == 1);
    fake::now = 338;
    dns::poll();
    assert(fake::sent.size() == 2);
    assert(fake::id(fake::sent.back()) == static_cast<uint16_t>(current + 1));
    wrong = fake::response();
    fake::put16(wrong, 0, current);
    fake::deliver(wrong);
    assert(dns::lookup_status() == dns::Status::Pending);
    fake::finish_nxdomain();

    fake::begin();
    auto malformed = fake::response();
    malformed[13] = 'z';
    fake::deliver(malformed);
    assert(dns::lookup_status() == dns::Status::MalformedResponse);
    fake::begin();
    malformed = fake::response();
    malformed[malformed.size() - 3] = 28; // QTYPE AAAA
    fake::deliver(malformed);
    assert(dns::lookup_status() == dns::Status::MalformedResponse);
    fake::begin();
    malformed = fake::response();
    malformed[malformed.size() - 1] = 2; // QCLASS CH
    fake::deliver(malformed);
    assert(dns::lookup_status() == dns::Status::MalformedResponse);
    fake::begin();
    malformed = fake::response(0x0100); // QR unset
    fake::deliver(malformed);
    assert(dns::lookup_status() == dns::Status::MalformedResponse);
    fake::begin();
    malformed = fake::response(0x8980); // nonzero opcode
    fake::deliver(malformed);
    assert(dns::lookup_status() == dns::Status::MalformedResponse);
    fake::begin();
    malformed = fake::response();
    malformed.pop_back();
    fake::deliver(malformed);
    assert(dns::lookup_status() == dns::Status::MalformedResponse);
    fake::begin();
    malformed = fake::response(0x8380); // TC
    fake::deliver(malformed);
    assert(dns::lookup_status() == dns::Status::TruncatedResponse);
    fake::begin();
    malformed = fake::response(0x8182); // SERVFAIL
    fake::deliver(malformed);
    assert(dns::lookup_status() == dns::Status::ServerFailure);
    fake::begin();
    malformed = fake::response(); // NODATA
    fake::deliver(malformed);
    assert(dns::lookup_status() == dns::Status::NotFound);
    puts("[PASS] peer_ports_id_question_parser_status_and_deadline");
}

void test_retry_timeout_and_wraparound()
{
    fake::rate = 137;
    fake::now = UINT64_MAX - 100;
    fake::begin();
    const uint16_t original = fake::id(fake::sent.back());
    fake::now += 273;
    dns::poll();
    assert(fake::sent.size() == 1);
    fake::now += 1;
    dns::poll();
    assert(fake::sent.size() == 2);
    assert(fake::id(fake::sent.back()) == static_cast<uint16_t>(original + 1));
    fake::now += 273;
    dns::poll();
    assert(dns::lookup_status() == dns::Status::Pending);
    fake::now += 1;
    dns::poll();
    assert(dns::lookup_status() == dns::Status::TimedOut);
    assert(fake::sent.size() == 2);
    puts("[PASS] two_second_retry_final_timeout_and_tick_wraparound");
}

void test_cname_followup_budget()
{
    fake::rate = 119;
    fake::now = 0;
    fake::begin();
    const uint16_t first = fake::id(fake::sent.back());
    auto alias = fake::response(0x8180, 1);
    fake::add_cname(alias, "b.test");
    fake::deliver(alias);
    assert(dns::lookup_status() == dns::Status::Pending);
    assert(fake::sent.size() == 1);
    dns::poll();
    assert(fake::sent.size() == 2);
    assert(fake::id(fake::sent.back()) == static_cast<uint16_t>(first + 1));
    dns::Name expected{};
    assert(dns::encode_hostname("b.test", expected));
    assert(memcmp(fake::sent.back().payload.data() + 12,
                  expected.wire, expected.length) == 0);
    fake::now = 238;
    dns::poll();
    assert(fake::sent.size() == 3);
    assert(fake::id(fake::sent.back()) == static_cast<uint16_t>(first + 2));
    fake::now = 476;
    dns::poll();
    assert(dns::lookup_status() == dns::Status::TimedOut);
    assert(fake::sent.size() == 3);

    fake::begin();
    alias = fake::response(0x8180, 1);
    fake::add_cname(alias, "b.test");
    fake::deliver(alias);
    dns::poll();
    auto answer = fake::response(0x8180, 1);
    fake::add_a(answer, 19);
    fake::deliver(answer);
    assert(dns::lookup_status() == dns::Status::Success);
    net::Ipv4Address address{};
    assert(dns::result_address(0, address));
    assert(fake::same(address, fake::ip(10, 20, 30, 19)));
    puts("[PASS] cname_followup_fresh_attempt_budget_and_borrowed_payload");
}

int main()
{
    test_initialization_and_transport_failures();
    test_query_busy_configuration_and_results();
    test_peer_id_question_and_statuses();
    test_retry_timeout_and_wraparound();
    test_cname_followup_budget();
    return 0;
}
