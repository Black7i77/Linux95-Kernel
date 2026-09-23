#!/usr/bin/env python3

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def require(path, needle):
    text = (ROOT / path).read_text()
    if needle not in text:
        raise SystemExit(f"FAIL: {needle!r} missing from {path}")

require("kernel/boot_info.hpp", "static_assert(sizeof(BootInfo) == 17")
require("kernel/boot_info.hpp", "static_assert(sizeof(E820Entry) == 24")
require("kernel/entry.asm", "extern _bss_start")
require("kernel/entry.asm", "rep stosb")
require("kernel/arch/interrupts.asm", "iretq")
require("kernel/arch/interrupts.cpp", "set_gate")
require("kernel/arch/pic.cpp", "0x20")
require("kernel/arch/pit.cpp", "1193182")
require("kernel/arch/keyboard.cpp", "kBufferSize = 128")
require("kernel/terminal/shell.cpp", "\"help\"")
require("kernel/terminal/shell.cpp", "\"reboot\"")
require("linker.ld", ". = 0x100000;")

print("source checks: PASS")
