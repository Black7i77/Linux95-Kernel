#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

required_files = [
    "kernel/storage/ata.hpp",
    "kernel/storage/ata.cpp",
    "kernel/storage/ata_helpers.hpp",
    "kernel/storage/disk.hpp",
    "kernel/storage/disk.cpp",
    "kernel/storage/storage_self_test.hpp",
    "kernel/storage/storage_self_test.cpp",
]

required_literals = {
    "kernel/storage/ata.hpp": [
        "enum class Drive",
        "Master",
        "Slave",
        "DeviceInfo",
        "identify",
        "read_sector",
        "write_sector",
        "flush_cache",
    ],
    "kernel/storage/storage_self_test.cpp": [
        "[PASS] ata_master_identify",
        "[PASS] ata_slave_identify",
        "[PASS] ata_read",
        "[PASS] ata_write",
        "[PASS] ata_restore",
        "[PASS] master_write_guard",
        "[PASS] storage_self_test",
    ],
    "kernel/kernel.cpp": [
        "[PANIC] storage_self_test",
    ],
    "kernel/terminal/shell.cpp": [
        "diskinfo",
    ],
}

errors = []
for relative in required_files:
    if not (ROOT / relative).is_file():
        errors.append(f"missing file: {relative}")

for relative, literals in required_literals.items():
    path = ROOT / relative
    if not path.is_file():
        errors.append(f"missing file: {relative}")
        continue
    text = path.read_text(errors="replace")
    for literal in literals:
        if literal not in text:
            errors.append(f"{relative}: missing {literal}")



makefile = ROOT / "Makefile"
if makefile.is_file():
    make_text = makefile.read_text(errors="replace")
    for literal in (
        "STORAGE_TEST_IMAGE",
        "linux95-storage-test.img",
        "if=ide,index=0",
        "if=ide,index=1",
    ):
        if literal not in make_text:
            errors.append(f"Makefile: missing {literal}")
else:
    errors.append("missing file: Makefile")


qemu_test = ROOT / "tests/qemu_smoke.py"
if qemu_test.is_file():
    qemu_text = qemu_test.read_text(errors="replace")
    for literal in (
        "linux95-storage-test.img",
        "if=ide,index=0",
        "if=ide,index=1",
        "[PASS] ata_master_identify",
        "[PASS] ata_slave_identify",
        "[PASS] ata_read",
        "[PASS] ata_write",
        "[PASS] ata_restore",
        "[PASS] master_write_guard",
        "[PASS] storage_self_test",
    ):
        if literal not in qemu_text:
            errors.append(f"tests/qemu_smoke.py: missing {literal}")
else:
    errors.append("missing file: tests/qemu_smoke.py")


if makefile.is_file():
    make_text = makefile.read_text(errors="replace")
    for literal in (
        "test: all test-host-memory test-host-storage",
        "$(PYTHON) tests/storage_source_checks.py",
    ):
        if literal not in make_text:
            errors.append(f"Makefile test integration: missing {literal}")


readme = ROOT / "README.md"
if readme.is_file():
    readme_text = readme.read_text(errors="replace")
    for literal in (
        "v1.0 Storage Foundation",
        "ATA PIO",
        "diskinfo",
        "FAT/filesystem",
    ):
        if literal not in readme_text:
            errors.append(f"README.md: missing {literal}")

# The ATA PIO data register transfers 16-bit words.
io_header = ROOT / "kernel/arch/io.hpp"
if io_header.is_file():
    io_text = io_header.read_text(errors="replace")
    for literal in ("inline uint16_t inw", "inline void outw"):
        if literal not in io_text:
            errors.append(f"kernel/arch/io.hpp: missing {literal}")
else:
    errors.append("missing file: kernel/arch/io.hpp")

if errors:
    print("storage source checks: FAIL")
    for error in errors:
        print(error)
    sys.exit(1)

print("storage source checks: PASS")
