#include "net/arp.hpp"

#include <stdint.h>

namespace linux95::net::arp {
namespace {

constexpr uint16_t kPacketLength = 28;
constexpr uint16_t kHardwareTypeOffset = 0;
constexpr uint16_t kProtocolTypeOffset = 2;
constexpr uint16_t kHardwareLengthOffset = 4;
constexpr uint16_t kProtocolLengthOffset = 5;
constexpr uint16_t kOperationOffset = 6;
constexpr uint16_t kSenderMacOffset = 8;
constexpr uint16_t kSenderIpOffset = 14;
constexpr uint16_t kTargetMacOffset = 18;
constexpr uint16_t kTargetIpOffset = 24;

constexpr uint16_t kHardwareEthernet = 1;
constexpr uint16_t kProtocolIpv4 = 0x0800;
constexpr uint8_t kMacLength = 6;
constexpr uint8_t kIpv4Length = 4;
constexpr uint16_t kOperationRequest = 1;
constexpr uint16_t kOperationReply = 2;
constexpr uint8_t kCacheSize = 8;

CacheEntry g_cache[kCacheSize]{};
uint8_t g_replace_index = 0;

uint16_t read_be16(const uint8_t* bytes, uint16_t offset)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(bytes[offset]) << 8) |
        bytes[offset + 1]);
}

void write_be16(uint8_t* bytes, uint16_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    bytes[offset + 1] = static_cast<uint8_t>(value & 0xFFu);
}

bool ipv4_equals(const Ipv4Address& left, const Ipv4Address& right)
{
    for (uint8_t i = 0; i < 4; ++i) {
        if (left.bytes[i] != right.bytes[i]) {
            return false;
        }
    }
    return true;
}

void copy_mac(MacAddress& destination, const MacAddress& source)
{
    for (uint8_t i = 0; i < 6; ++i) {
        destination.bytes[i] = source.bytes[i];
    }
}

void copy_ipv4(Ipv4Address& destination, const Ipv4Address& source)
{
    for (uint8_t i = 0; i < 4; ++i) {
        destination.bytes[i] = source.bytes[i];
    }
}

void learn(const Ipv4Address& ip, const MacAddress& mac)
{
    for (uint8_t i = 0; i < kCacheSize; ++i) {
        if (g_cache[i].valid && ipv4_equals(g_cache[i].ip, ip)) {
            copy_mac(g_cache[i].mac, mac);
            return;
        }
    }

    for (uint8_t i = 0; i < kCacheSize; ++i) {
        if (!g_cache[i].valid) {
            copy_ipv4(g_cache[i].ip, ip);
            copy_mac(g_cache[i].mac, mac);
            g_cache[i].valid = true;
            return;
        }
    }

    CacheEntry& replacement = g_cache[g_replace_index];
    copy_ipv4(replacement.ip, ip);
    copy_mac(replacement.mac, mac);
    replacement.valid = true;
    g_replace_index = static_cast<uint8_t>(
        (g_replace_index + 1u) % kCacheSize);
}

bool build_packet(
    uint8_t* payload,
    uint16_t capacity,
    uint16_t operation,
    const MacAddress& local_mac,
    const Ipv4Address& local_ip,
    const MacAddress& target_mac,
    const Ipv4Address& target_ip,
    uint16_t& length)
{
    length = 0;
    if (payload == nullptr || capacity < kPacketLength) {
        return false;
    }

    write_be16(payload, kHardwareTypeOffset, kHardwareEthernet);
    write_be16(payload, kProtocolTypeOffset, kProtocolIpv4);
    payload[kHardwareLengthOffset] = kMacLength;
    payload[kProtocolLengthOffset] = kIpv4Length;
    write_be16(payload, kOperationOffset, operation);

    for (uint8_t i = 0; i < 6; ++i) {
        payload[kSenderMacOffset + i] = local_mac.bytes[i];
        payload[kTargetMacOffset + i] = target_mac.bytes[i];
    }
    for (uint8_t i = 0; i < 4; ++i) {
        payload[kSenderIpOffset + i] = local_ip.bytes[i];
        payload[kTargetIpOffset + i] = target_ip.bytes[i];
    }

    length = kPacketLength;
    return true;
}

} // namespace

void reset()
{
    for (uint8_t i = 0; i < kCacheSize; ++i) {
        g_cache[i].valid = false;
        for (uint8_t j = 0; j < 4; ++j) {
            g_cache[i].ip.bytes[j] = 0;
        }
        for (uint8_t j = 0; j < 6; ++j) {
            g_cache[i].mac.bytes[j] = 0;
        }
    }
    g_replace_index = 0;
}

bool parse_and_learn(
    const uint8_t* payload,
    uint16_t length,
    const Ipv4Address& local_ip,
    const MacAddress& local_mac,
    bool& should_reply,
    MacAddress& sender_mac,
    Ipv4Address& sender_ip)
{
    should_reply = false;
    (void)local_mac;

    if (payload == nullptr || length < kPacketLength) {
        return false;
    }

    if (read_be16(payload, kHardwareTypeOffset) != kHardwareEthernet ||
        read_be16(payload, kProtocolTypeOffset) != kProtocolIpv4 ||
        payload[kHardwareLengthOffset] != kMacLength ||
        payload[kProtocolLengthOffset] != kIpv4Length) {
        return false;
    }

    const uint16_t operation = read_be16(payload, kOperationOffset);
    if (operation != kOperationRequest && operation != kOperationReply) {
        return false;
    }

    for (uint8_t i = 0; i < 6; ++i) {
        sender_mac.bytes[i] = payload[kSenderMacOffset + i];
    }
    for (uint8_t i = 0; i < 4; ++i) {
        sender_ip.bytes[i] = payload[kSenderIpOffset + i];
    }

    learn(sender_ip, sender_mac);

    if (operation == kOperationRequest) {
        Ipv4Address target_ip{};
        for (uint8_t i = 0; i < 4; ++i) {
            target_ip.bytes[i] = payload[kTargetIpOffset + i];
        }
        should_reply = ipv4_equals(target_ip, local_ip);
    }

    return true;
}

bool lookup(const Ipv4Address& ip, MacAddress& mac)
{
    for (uint8_t i = 0; i < kCacheSize; ++i) {
        if (g_cache[i].valid && ipv4_equals(g_cache[i].ip, ip)) {
            copy_mac(mac, g_cache[i].mac);
            return true;
        }
    }
    return false;
}

bool build_request(
    uint8_t* payload,
    uint16_t capacity,
    const MacAddress& local_mac,
    const Ipv4Address& local_ip,
    const Ipv4Address& target_ip,
    uint16_t& length)
{
    const MacAddress unknown_target{{0, 0, 0, 0, 0, 0}};
    return build_packet(
        payload,
        capacity,
        kOperationRequest,
        local_mac,
        local_ip,
        unknown_target,
        target_ip,
        length);
}

bool build_reply(
    uint8_t* payload,
    uint16_t capacity,
    const MacAddress& local_mac,
    const Ipv4Address& local_ip,
    const MacAddress& target_mac,
    const Ipv4Address& target_ip,
    uint16_t& length)
{
    return build_packet(
        payload,
        capacity,
        kOperationReply,
        local_mac,
        local_ip,
        target_mac,
        target_ip,
        length);
}

} // namespace linux95::net::arp
