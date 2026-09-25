#include "arch/interrupts.hpp"

#include "arch/keyboard.hpp"
#include "arch/mouse.hpp"
#include "arch/pic.hpp"
#include "arch/pit.hpp"
#include "arch/x86_64/segments.hpp"
#include "panic/panic.hpp"
#include "process/process.hpp"
#include "process/scheduler.hpp"

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
extern "C" void int80_entry();

void set_gate(uint8_t vector,
              void (*handler)(),
              uint8_t dpl = 0,
              uint8_t ist = 0)
{
    const uint64_t address =
        reinterpret_cast<uint64_t>(handler);

    IdtEntry& entry = g_idt[vector];
    entry.offset_low = static_cast<uint16_t>(address & 0xFFFFu);
    entry.selector = arch::x86_64::kKernelCodeSelector;
    entry.ist = static_cast<uint8_t>(ist & 0x7U);
    entry.type_attributes = static_cast<uint8_t>(0x8E | (dpl << 5));
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

    set_gate(2, isr_stub_table[2], 0, 1);
    set_gate(8, isr_stub_table[8], 0, 2);
    set_gate(18, isr_stub_table[18], 0, 3);
    set_gate(0x80, int80_entry, 3);

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
        const bool from_user = (frame->cs & 0x3U) == 0x3U;
        const bool user_fatal_exception =
            frame->vector != 2 &&
            frame->vector != 8 &&
            frame->vector != 18;
        if (from_user && user_fatal_exception &&
            process::handle_user_fault(
                static_cast<uint8_t>(frame->vector),
                frame->error_code,
                *frame)) {
            return;
        }
        panic::exception(frame->vector, frame->error_code);
    }

    if (frame->vector == 32) {
        pit::on_irq();

        const bool from_user =
            (frame->cs & 0x3U) == 0x3U;
        const bool expired =
            scheduler::on_timer_tick(from_user);

        pic::send_eoi(0);

        if (expired) {
            process::handle_user_preempt(*frame);
        }
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
