#include "arch/pic.hpp"

#include "arch/io.hpp"

namespace linux95::pic {
namespace {

constexpr uint16_t kMasterCommand = 0x20;
constexpr uint16_t kMasterData = 0x21;
constexpr uint16_t kSlaveCommand = 0xA0;
constexpr uint16_t kSlaveData = 0xA1;

constexpr uint8_t kIcw1Init = 0x10;
constexpr uint8_t kIcw1Icw4 = 0x01;
constexpr uint8_t kIcw4_8086 = 0x01;
constexpr uint8_t kEoi = 0x20;

} // namespace

void mask_all()
{
    io::outb(kMasterData, 0xFF);
    io::outb(kSlaveData, 0xFF);
}

void initialize()
{
    io::outb(kMasterCommand, kIcw1Init | kIcw1Icw4);
    io::io_wait();
    io::outb(kSlaveCommand, kIcw1Init | kIcw1Icw4);
    io::io_wait();

    io::outb(kMasterData, 0x20);
    io::io_wait();
    io::outb(kSlaveData, 0x28);
    io::io_wait();

    io::outb(kMasterData, 0x04);
    io::io_wait();
    io::outb(kSlaveData, 0x02);
    io::io_wait();

    io::outb(kMasterData, kIcw4_8086);
    io::io_wait();
    io::outb(kSlaveData, kIcw4_8086);
    io::io_wait();

    mask_all();
}

void unmask_irq(uint8_t irq)
{
    if (irq >= 16) {
        return;
    }

    const uint16_t port = irq < 8 ? kMasterData : kSlaveData;
    const uint8_t bit = irq < 8 ? irq : static_cast<uint8_t>(irq - 8);

    uint8_t mask = io::inb(port);
    mask = static_cast<uint8_t>(mask & ~(1u << bit));
    io::outb(port, mask);

    if (irq >= 8) {
        uint8_t master_mask = io::inb(kMasterData);
        master_mask = static_cast<uint8_t>(master_mask & ~(1u << 2));
        io::outb(kMasterData, master_mask);
    }
}

void send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        io::outb(kSlaveCommand, kEoi);
    }

    io::outb(kMasterCommand, kEoi);
}

} // namespace linux95::pic
