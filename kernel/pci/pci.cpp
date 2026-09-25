#include "pci/pci.hpp"

namespace linux95::pci {
namespace {

constexpr uint16_t kConfigAddressPort = 0xCF8;
constexpr uint16_t kConfigDataPort = 0xCFC;
constexpr uint8_t kVendorDeviceOffset = 0x00;
constexpr uint8_t kCommandOffset = 0x04;
constexpr uint8_t kBar0Offset = 0x10;
constexpr uint8_t kBarCount = 6;
constexpr uint16_t kCommandIoSpace = 1u << 0;
constexpr uint16_t kCommandBusMaster = 1u << 2;

inline void out32(uint16_t port, uint32_t value)
{
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

inline uint32_t in32(uint16_t port)
{
    uint32_t value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void write32(Address address, uint8_t offset, uint32_t value)
{
    out32(kConfigAddressPort, make_config_address(address, offset));
    out32(kConfigDataPort, value);
}

} // namespace

uint32_t read32(Address address, uint8_t offset)
{
    out32(kConfigAddressPort, make_config_address(address, offset));
    return in32(kConfigDataPort);
}

uint16_t read16(Address address, uint8_t offset)
{
    const uint32_t value = read32(address, offset);
    const uint8_t shift = static_cast<uint8_t>((offset & 0x02u) * 8u);
    return static_cast<uint16_t>((value >> shift) & 0xFFFFu);
}

void write16(Address address, uint8_t offset, uint16_t value)
{
    const uint8_t shift = static_cast<uint8_t>((offset & 0x02u) * 8u);
    const uint32_t mask = 0xFFFFu << shift;
    const uint32_t current = read32(address, offset);
    const uint32_t updated =
        (current & ~mask) |
        (static_cast<uint32_t>(value) << shift);
    write32(address, offset, updated);
}

uint32_t read_bar(Address address, uint8_t index)
{
    if (index >= kBarCount) {
        return 0;
    }

    return read32(
        address,
        static_cast<uint8_t>(kBar0Offset + index * 4u));
}

bool find_device(
    uint16_t vendor_id,
    uint16_t device_id,
    Address& out)
{
    for (uint16_t bus = 0; bus <= 255; ++bus) {
        for (uint8_t device = 0; device < 32; ++device) {
            for (uint8_t function = 0; function < 8; ++function) {
                const Address address{
                    static_cast<uint8_t>(bus),
                    device,
                    function};
                const uint32_t identity =
                    read32(address, kVendorDeviceOffset);
                const uint16_t found_vendor =
                    static_cast<uint16_t>(identity & 0xFFFFu);

                if (found_vendor == 0xFFFFu) {
                    continue;
                }

                const uint16_t found_device =
                    static_cast<uint16_t>(identity >> 16);
                if (found_vendor == vendor_id &&
                    found_device == device_id) {
                    out = address;
                    return true;
                }
            }
        }
    }

    return false;
}

bool enable_io_bus_master(Address address)
{
    constexpr uint16_t required =
        kCommandIoSpace | kCommandBusMaster;
    const uint16_t command = read16(address, kCommandOffset);
    write16(
        address,
        kCommandOffset,
        static_cast<uint16_t>(command | required));
    return (read16(address, kCommandOffset) & required) == required;
}

} // namespace linux95::pci
