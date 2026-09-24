#include "arch/mouse.hpp"

#include "arch/io.hpp"
#include "arch/ps2.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::mouse {

namespace {

constexpr uint16_t kDataPort = 0x60;
constexpr uint16_t kCommandPort = 0x64;

constexpr uint8_t kEnableAuxiliary = 0xA8;
constexpr uint8_t kReadConfig = 0x20;
constexpr uint8_t kWriteConfig = 0x60;
constexpr uint8_t kWriteMouse = 0xD4;

constexpr uint8_t kMouseSetDefaults = 0xF6;
constexpr uint8_t kMouseEnableStreaming = 0xF4;
constexpr uint8_t kMouseAck = 0xFA;

constexpr uint8_t kConfigIrq12 = 1u << 1;
constexpr uint8_t kConfigAuxClockDisabled = 1u << 5;

constexpr uint32_t kWaitLimit = 1000000;

constexpr size_t kEventCapacity = 64;

PacketDecoder g_decoder{};

MouseEvent g_events[kEventCapacity]{};

volatile uint8_t g_head = 0;
volatile uint8_t g_tail = 0;
volatile uint8_t g_count = 0;

bool write_controller_command(uint8_t command)
{
    if (!ps2::wait_input_clear(kWaitLimit)) {
        return false;
    }

    io::outb(kCommandPort, command);
    return true;
}

bool write_data(uint8_t value)
{
    if (!ps2::wait_input_clear(kWaitLimit)) {
        return false;
    }

    io::outb(kDataPort, value);
    return true;
}

bool read_data(uint8_t& value)
{
    if (!ps2::wait_output_full(kWaitLimit)) {
        return false;
    }

    value = io::inb(kDataPort);
    return true;
}

bool send_mouse_command(uint8_t command)
{
    if (!write_controller_command(kWriteMouse)) {
        return false;
    }

    if (!write_data(command)) {
        return false;
    }

    uint8_t response = 0;

    if (!read_data(response)) {
        return false;
    }

    return response == kMouseAck;
}

void reset_queue()
{
    g_head = 0;
    g_tail = 0;
    g_count = 0;
}

void push_event(const MouseEvent& event)
{
    // Keep all existing queued input when full.
    // The newest incoming event is discarded.
    if (g_count == kEventCapacity) {
        return;
    }

    g_events[g_head] = event;

    g_head =
        static_cast<uint8_t>(
            (g_head + 1) % kEventCapacity);

    ++g_count;
}

} // namespace

bool initialize()
{
    g_decoder.reset();
    reset_queue();

    // 1. Enable the PS/2 auxiliary device.
    if (!write_controller_command(kEnableAuxiliary)) {
        return false;
    }

    // 2. Read controller configuration.
    if (!write_controller_command(kReadConfig)) {
        return false;
    }

    uint8_t config = 0;

    if (!read_data(config)) {
        return false;
    }

    // 3. Enable IRQ12.
    config =
        static_cast<uint8_t>(
            config | kConfigIrq12);

    // 4. Enable the auxiliary clock.
    config =
        static_cast<uint8_t>(
            config & ~kConfigAuxClockDisabled);

    // 5. Write configuration back.
    if (!write_controller_command(kWriteConfig)) {
        return false;
    }

    if (!write_data(config)) {
        return false;
    }

    // 6. Restore mouse defaults.
    if (!send_mouse_command(kMouseSetDefaults)) {
        return false;
    }

    // 7. Enable mouse streaming.
    if (!send_mouse_command(kMouseEnableStreaming)) {
        return false;
    }

    return true;
}

void on_irq()
{
    const uint8_t byte =
        io::inb(kDataPort);

    MouseEvent event{
        0,
        0,
        false,
        false,
        false,
    };

    if (g_decoder.feed(byte, event)) {
        push_event(event);
    }
}

bool has_event()
{
    return g_count != 0;
}

MouseEvent read_event()
{
    if (g_count == 0) {
        return MouseEvent{
            0,
            0,
            false,
            false,
            false,
        };
    }

    const MouseEvent event =
        g_events[g_tail];

    g_tail =
        static_cast<uint8_t>(
            (g_tail + 1) % kEventCapacity);

    --g_count;

    return event;
}

} // namespace linux95::mouse
