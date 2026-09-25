#!/usr/bin/env python3

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def text(path):
    return (ROOT / path).read_text()


def require(path, needle):
    data = text(path)
    if needle not in data:
        raise SystemExit(f"FAIL: {needle!r} missing from {path}")


stage2 = "boot/stage2.asm"

require(stage2, "0x4F00")
require(stage2, "0x4F01")
require(stage2, "0x4F02")
require(stage2, "0x4000")
require(stage2, "1280")
require(stage2, "720")
require(stage2, "32")

require(
    "kernel/boot_info.hpp",
    "FramebufferInfo framebuffer;"
)

# BIOS interrupts must remain in the real-mode bootloader.
for path in (ROOT / "kernel").rglob("*"):
    if path.suffix not in {".cpp", ".hpp", ".asm"}:
        continue

    data = path.read_text(errors="ignore").lower()

    if "int 0x10" in data or "int 10h" in data:
        raise SystemExit(
            f"FAIL: BIOS video interrupt found in long-mode kernel code: "
            f"{path.relative_to(ROOT)}"
        )

print("graphics source checks: PASS")

# Task 6: optional PS/2 mouse IRQ12 wiring.
interrupts = (ROOT / "kernel/arch/interrupts.cpp").read_text()
kernel = (ROOT / "kernel/kernel.cpp").read_text()

if "frame->vector == 44" not in interrupts:
    raise SystemExit("FAIL: IRQ12 vector 44 is not dispatched")

if "mouse::on_irq()" not in interrupts:
    raise SystemExit("FAIL: mouse IRQ handler is not called")

if "pic::send_eoi(12)" not in interrupts:
    raise SystemExit("FAIL: IRQ12 EOI missing")

if "mouse::initialize()" not in kernel:
    raise SystemExit("FAIL: PS/2 mouse is never initialized")

if "pic::unmask_irq(12)" not in kernel:
    raise SystemExit("FAIL: IRQ12 is never unmasked")

if "[PASS] mouse_initialized" not in kernel:
    raise SystemExit("FAIL: mouse success diagnostic missing")

if "[INFO] mouse_unavailable" not in kernel:
    raise SystemExit("FAIL: optional mouse fallback diagnostic missing")


# Task 13 graphical desktop boot contract
_task13_kernel = (ROOT / "kernel/kernel.cpp").read_text()
_task13_framebuffer = (ROOT / "kernel/graphics/framebuffer.cpp").read_text()
_task13_qemu = (ROOT / "tests/qemu_smoke.py").read_text()

for _required in [
    "[INFO] graphics_fallback_vga",
    "shell::run_vga()",
    "[PASS] renderer_online",
    "desktop::run(",
]:
    if _required not in _task13_kernel:
        raise SystemExit("FAIL: graphical boot marker missing: " + _required)

if "memory::paging::kPageNoExecute" in _task13_framebuffer:
    raise SystemExit("FAIL: framebuffer uses NX while EFER.NXE is disabled")

if "[PASS] desktop_online" not in _task13_qemu:
    raise SystemExit("FAIL: QEMU smoke does not require desktop_online")

if "\"-vga\", \"std\"" not in _task13_qemu:
    raise SystemExit("FAIL: QEMU smoke does not use standard VGA")
