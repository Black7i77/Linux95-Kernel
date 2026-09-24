#include "arch/interrupts.hpp"

#include "arch/keyboard.hpp"
#include "arch/mouse.hpp"
#include "arch/pic.hpp"
#include "arch/pit.hpp"
#include "panic/panic.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::interrupts {
namespace {

struct __attribute__((packed)) IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
};

struct __attribute__((packed)) Idtr {
    uint16_t limit;
    uint64_t base;
};

IdtEntry g_idt[256];

extern "C" void (*isr_stub_table[])();
extern "C" void isr_default();

void set_gate(uint8_t vector, void (*handler)())
{
    const uint64_t address =
        reinterpret_cast<uint64_t>(handler);

    IdtEntry& entry = g_idt[vector];
    entry.offset_low = static_cast<uint16_t>(address & 0xFFFFu);
    entry.selector = 0x18;
    entry.ist = 0;
    entry.type_attributes = 0x8E;
    entry.offset_mid =
        static_cast<uint16_t>((address >> 16) & 0xFFFFu);
    entry.offset_high =
        static_cast<uint32_t>((address >> 32) & 0xFFFFFFFFu);
    entry.zero = 0;
}

} // namespace

void initialize()
{
    for (size_t i = 0; i < 256; ++i) {
        set_gate(static_cast<uint8_t>(i), isr_default);
    }

    for (size_t i = 0; i < 48; ++i) {
        set_gate(
            static_cast<uint8_t>(i),
            isr_stub_table[i]);
    }

    const Idtr idtr{
        static_cast<uint16_t>(sizeof(g_idt) - 1),
        reinterpret_cast<uint64_t>(&g_idt[0])
    };

    asm volatile("lidt %0" : : "m"(idtr) : "memory");
}

} // namespace linux95::interrupts

extern "C" void interrupt_dispatch(
    linux95::interrupts::InterruptFrame* frame)
{
    using namespace linux95;

    if (frame == nullptr) {
        panic::halt("Null interrupt frame");
    }

    if (frame->vector < 32) {
        panic::exception(frame->vector, frame->error_code);
    }

    if (frame->vector == 32) {
        pit::on_irq();
        pic::send_eoi(0);
        return;
    }

    if (frame->vector == 33) {
        keyboard::on_irq();
        pic::send_eoi(1);
        return;
    }

    if (frame->vector == 44) {
        mouse::on_irq();
        pic::send_eoi(12);
        return;
    }

    if (frame->vector >= 32 && frame->vector < 48) {
        pic::send_eoi(
            static_cast<uint8_t>(frame->vector - 32));
    }
}
