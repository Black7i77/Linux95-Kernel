#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]

interrupts_cpp = (root / "kernel/arch/interrupts.cpp").read_text()
interrupts_asm = (root / "kernel/arch/interrupts.asm").read_text()
context_asm = (root / "kernel/process/context.asm").read_text()
syscall_asm = (root / "kernel/syscall/syscall_entry.asm").read_text()

assert "scheduler::on_timer_tick" in interrupts_cpp
assert "process::handle_user_preempt" in interrupts_cpp

irq32 = interrupts_cpp[interrupts_cpp.index("if (frame->vector == 32)"):]
irq32 = irq32[:irq32.index("if (frame->vector == 33)")]

assert (
    irq32.index("pic::send_eoi(0)")
    < irq32.index("process::handle_user_preempt")
)

assert "swapgs" not in interrupts_asm
assert "iretq" in context_asm
assert "o64 sysret" in context_asm
assert syscall_asm.count("swapgs") == 2

print("preemption source checks: PASS")
