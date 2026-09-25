#include "drivers/rtl8139.hpp"

#include "drivers/rtl8139_helpers.hpp"
#include "memory/memory.hpp"
#include "pci/pci.hpp"

#include <stdint.h>

namespace linux95::rtl8139 {
namespace {

constexpr uint16_t kVendorId = 0x10EC;
constexpr uint16_t kDeviceId = 0x8139;

constexpr uint16_t kIdr0 = 0x00;
constexpr uint16_t kTxStatus0 = 0x10;
constexpr uint16_t kTxAddress0 = 0x20;
constexpr uint16_t kRxBufferStart = 0x30;
constexpr uint16_t kCommand = 0x37;
constexpr uint16_t kCurrentAddress = 0x38;
constexpr uint16_t kCurrentBuffer = 0x3A;
constexpr uint16_t kInterruptMask = 0x3C;
constexpr uint16_t kInterruptStatus = 0x3E;
constexpr uint16_t kReceiveConfig = 0x44;
constexpr uint16_t kConfig1 = 0x52;

constexpr uint8_t kCommandReset = 1u << 4;
constexpr uint8_t kCommandReceiverEnable = 1u << 3;
constexpr uint8_t kCommandTransmitterEnable = 1u << 2;
constexpr uint8_t kCommandRxBufferEmpty = 1u << 0;

constexpr uint16_t kRxOk = 1u << 0;
constexpr uint16_t kRxStatusMask = 0x0001u;
constexpr uint16_t kHandledInterrupts = 0xFFFFu;

constexpr uint32_t kTxAbort = 1u << 30;
constexpr uint32_t kTxOk = 1u << 15;
constexpr uint32_t kTxUnderrun = 1u << 14;

constexpr uint32_t kReceiveAcceptAll = 1u << 0;
constexpr uint32_t kReceivePhysicalMatch = 1u << 1;
constexpr uint32_t kReceiveMulticast = 1u << 2;
constexpr uint32_t kReceiveBroadcast = 1u << 3;
constexpr uint32_t kReceiveWrap = 1u << 7;

constexpr uint32_t kResetPollLimit = 100000;
constexpr uint32_t kTransmitPollLimit = 100000;
constexpr uint8_t kReceivePollLimit = 32;
constexpr uint16_t kRxHeaderSize = 4;
constexpr uint16_t kEthernetCrcSize = 4;
constexpr uint16_t kMaxFrameSize = 1518;

alignas(4096)
uint8_t g_rx_buffer[8192 + 16 + 1500];

alignas(4)
uint8_t g_tx_buffers[4][1536];

bool g_online = false;
uint16_t g_io_base = 0;
uint16_t g_rx_offset = 0;
uint8_t g_tx_slot = 0;
bool g_tx_slot_used[4] = {false, false, false, false};
uint32_t g_tx_physical[4] = {0, 0, 0, 0};
MacAddress g_mac{};

inline void out8(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t in8(uint16_t port)
{
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void out16(uint16_t port, uint16_t value)
{
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

inline uint16_t in16(uint16_t port)
{
    uint16_t value;
    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

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

uint16_t ring16(uint16_t offset)
{
    const uint16_t next =
        static_cast<uint16_t>((offset + 1u) % kRxRingSize);
    return static_cast<uint16_t>(
        g_rx_buffer[offset] |
        (static_cast<uint16_t>(g_rx_buffer[next]) << 8));
}

uint8_t ring8(uint16_t offset)
{
    return g_rx_buffer[offset % kRxRingSize];
}

void update_rx_offset(uint16_t packet_length)
{
    g_rx_offset = advance_rx_offset(g_rx_offset, packet_length);
    out16(
        static_cast<uint16_t>(g_io_base + kCurrentAddress),
        static_cast<uint16_t>(g_rx_offset - 16u));
}

void recover_rx_offset()
{
    g_rx_offset = static_cast<uint16_t>(
        in16(static_cast<uint16_t>(g_io_base + kCurrentBuffer)) %
        kRxRingSize);
    g_rx_offset = static_cast<uint16_t>(g_rx_offset & ~0x3u);
    out16(
        static_cast<uint16_t>(g_io_base + kCurrentAddress),
        static_cast<uint16_t>(g_rx_offset - 16u));
}

bool translate_dma_buffers(uint32_t& rx_physical)
{
    uint64_t physical = 0;
    if (!memory::kernel_virtual_to_physical(g_rx_buffer, physical) ||
        physical > UINT32_MAX) {
        return false;
    }
    rx_physical = static_cast<uint32_t>(physical);

    for (uint8_t slot = 0; slot < 4; ++slot) {
        if (!memory::kernel_virtual_to_physical(
                g_tx_buffers[slot], physical) ||
            physical > UINT32_MAX) {
            return false;
        }
        g_tx_physical[slot] = static_cast<uint32_t>(physical);
    }

    return true;
}

} // namespace

bool initialize()
{
    g_online = false;
    g_io_base = 0;
    g_rx_offset = 0;
    g_tx_slot = 0;
    for (uint8_t slot = 0; slot < 4; ++slot) {
        g_tx_slot_used[slot] = false;
        g_tx_physical[slot] = 0;
    }
    for (uint8_t i = 0; i < 6; ++i) {
        g_mac.bytes[i] = 0;
    }

    pci::Address address{};
    if (!pci::find_device(kVendorId, kDeviceId, address)) {
        return false;
    }

    const uint32_t bar0 = pci::read_bar(address, 0);
    if (!pci::is_io_bar(bar0)) {
        return false;
    }

    const uint32_t io_base = pci::io_bar_base(bar0);
    if (io_base == 0 || io_base > UINT16_MAX) {
        return false;
    }
    g_io_base = static_cast<uint16_t>(io_base);

    if (!pci::enable_io_bus_master(address)) {
        return false;
    }

    out8(static_cast<uint16_t>(g_io_base + kConfig1), 0x00);
    out8(static_cast<uint16_t>(g_io_base + kCommand), kCommandReset);

    bool reset_complete = false;
    for (uint32_t attempt = 0;
         attempt < kResetPollLimit;
         ++attempt) {
        if ((in8(static_cast<uint16_t>(g_io_base + kCommand)) &
             kCommandReset) == 0) {
            reset_complete = true;
            break;
        }
    }
    if (!reset_complete) {
        return false;
    }

    for (uint8_t i = 0; i < 6; ++i) {
        g_mac.bytes[i] =
            in8(static_cast<uint16_t>(g_io_base + kIdr0 + i));
    }

    uint32_t rx_physical = 0;
    if (!translate_dma_buffers(rx_physical)) {
        return false;
    }

    out32(
        static_cast<uint16_t>(g_io_base + kRxBufferStart),
        rx_physical);
    for (uint8_t slot = 0; slot < 4; ++slot) {
        out32(
            static_cast<uint16_t>(
                g_io_base + kTxAddress0 + slot * 4u),
            g_tx_physical[slot]);
    }

    out16(static_cast<uint16_t>(g_io_base + kInterruptMask), 0);
    out16(
        static_cast<uint16_t>(g_io_base + kInterruptStatus),
        kHandledInterrupts);
    out32(
        static_cast<uint16_t>(g_io_base + kReceiveConfig),
        kReceiveAcceptAll |
            kReceivePhysicalMatch |
            kReceiveMulticast |
            kReceiveBroadcast |
            kReceiveWrap);
    out16(static_cast<uint16_t>(g_io_base + kCurrentAddress), 0);
    out8(
        static_cast<uint16_t>(g_io_base + kCommand),
        kCommandReceiverEnable | kCommandTransmitterEnable);

    g_online = true;
    return true;
}

bool online()
{
    return g_online;
}

MacAddress mac_address()
{
    return g_mac;
}

bool transmit(const uint8_t* frame, uint16_t length)
{
    if (!g_online || frame == nullptr ||
        length == 0 || length > kMaxFrameSize) {
        return false;
    }

    const uint8_t slot = g_tx_slot;
    if (g_tx_slot_used[slot]) {
        bool completed = false;
        for (uint32_t attempt = 0;
             attempt < kTransmitPollLimit;
             ++attempt) {
            const uint32_t status = in32(
                static_cast<uint16_t>(
                    g_io_base + kTxStatus0 + slot * 4u));
            if ((status & (kTxAbort | kTxUnderrun)) != 0) {
                return false;
            }
            if ((status & kTxOk) != 0) {
                completed = true;
                break;
            }
        }
        if (!completed) {
            return false;
        }
    }

    for (uint16_t i = 0; i < length; ++i) {
        g_tx_buffers[slot][i] = frame[i];
    }

    out32(
        static_cast<uint16_t>(g_io_base + kTxAddress0 + slot * 4u),
        g_tx_physical[slot]);
    out32(
        static_cast<uint16_t>(g_io_base + kTxStatus0 + slot * 4u),
        length);

    g_tx_slot_used[slot] = true;
    g_tx_slot = next_tx_slot(slot);
    return true;
}

bool poll_receive(
    uint8_t* frame,
    uint16_t capacity,
    uint16_t& length)
{
    length = 0;
    if (!g_online || frame == nullptr || capacity == 0) {
        return false;
    }

    for (uint8_t attempt = 0;
         attempt < kReceivePollLimit;
         ++attempt) {
        if ((in8(static_cast<uint16_t>(g_io_base + kCommand)) &
             kCommandRxBufferEmpty) != 0) {
            return false;
        }

        const uint16_t status = ring16(g_rx_offset);
        const uint16_t packet_length = ring16(
            static_cast<uint16_t>((g_rx_offset + 2u) % kRxRingSize));

        if (!valid_rx_length(packet_length)) {
            recover_rx_offset();
            out16(
                static_cast<uint16_t>(g_io_base + kInterruptStatus),
                kHandledInterrupts);
            continue;
        }

        if ((status & kRxStatusMask) != kRxOk ||
            packet_length < kEthernetCrcSize) {
            update_rx_offset(packet_length);
            out16(
                static_cast<uint16_t>(g_io_base + kInterruptStatus),
                kHandledInterrupts);
            continue;
        }

        const uint16_t frame_length =
            static_cast<uint16_t>(packet_length - kEthernetCrcSize);
        if (frame_length > capacity) {
            update_rx_offset(packet_length);
            out16(
                static_cast<uint16_t>(g_io_base + kInterruptStatus),
                kHandledInterrupts);
            continue;
        }

        for (uint16_t i = 0; i < frame_length; ++i) {
            frame[i] = ring8(static_cast<uint16_t>(
                (g_rx_offset + kRxHeaderSize + i) % kRxRingSize));
        }

        length = frame_length;
        update_rx_offset(packet_length);
        out16(
            static_cast<uint16_t>(g_io_base + kInterruptStatus),
            kHandledInterrupts);
        return true;
    }

    return false;
}

} // namespace linux95::rtl8139
