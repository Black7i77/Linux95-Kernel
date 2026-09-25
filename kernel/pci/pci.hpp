#pragma once

#include <stdint.h>

namespace linux95::pci {

struct Address {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
};

constexpr uint32_t make_config_address(
    Address address,
    uint8_t offset)
{
    return
        0x80000000u |
        (static_cast<uint32_t>(address.bus) << 16) |
        (static_cast<uint32_t>(address.device & 0x1Fu) << 11) |
        (static_cast<uint32_t>(address.function & 0x07u) << 8) |
        (static_cast<uint32_t>(offset) & 0xFCu);
}

constexpr bool is_io_bar(uint32_t bar)
{
    return (bar & 0x1u) != 0;
}

constexpr uint32_t io_bar_base(uint32_t bar)
{
    return bar & ~0x3u;
}

uint32_t read32(Address address, uint8_t offset);
uint16_t read16(Address address, uint8_t offset);
void write16(Address address, uint8_t offset, uint16_t value);
uint32_t read_bar(Address address, uint8_t index);
bool find_device(
    uint16_t vendor_id,
    uint16_t device_id,
    Address& out);
bool enable_io_bus_master(Address address);

} // namespace linux95::pci
