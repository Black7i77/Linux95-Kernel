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
