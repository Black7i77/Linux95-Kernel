#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

required_files = [
    "kernel/memory/address.hpp",
    "kernel/memory/page_bitmap.hpp",
    "kernel/memory/physical.hpp",
    "kernel/memory/physical.cpp",
    "kernel/memory/paging.hpp",
    "kernel/memory/paging.cpp",
    "kernel/memory/virtual.hpp",
    "kernel/memory/virtual.cpp",
    "kernel/memory/self_test.hpp",
    "kernel/memory/self_test.cpp",
    "kernel/arch/x86_64/control_regs.hpp",
    "kernel/arch/x86_64/paging_bootstrap.asm",
]

required_literals = {
    "kernel/memory/address.hpp": [
        "0x00100000",
        "0xFFFFFFFF80000000",
        "0xFFFFFFFF80100000",
        "0xFFFF800000000000",
        "4096",
        "align_down",
        "align_up",
        "physical_to_hhdm",
        "hhdm_to_physical",
    ],
    "kernel/kernel.cpp": [
        "[BOOT] low_kernel_entry",
        "[PASS] bootstrap_tables_created",
        "[PASS] higher_half_entry",
        "linux95_higher_half_entry",
        "linux95_reload_cr3_and_reenter",
        "[PASS] physical_allocator_online",
        "[PASS] virtual_memory_online",
        "[PASS] memory_self_test",
        "self_test::run",
    ],
    "kernel/memory/virtual.cpp": [
        "[PANIC] bootstrap_hhdm_limit",
    ],
    "kernel/memory/paging.hpp": [
        "map_page",
        "unmap_page",
        "translate",
        "is_mapped",
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

linker = (ROOT / "linker.ld").read_text(errors="replace")
for symbol in (
    "__kernel_phys_start",
    "__kernel_phys_end",
    "__bootstrap_pt_pool_start",
    "__bootstrap_pt_pool_end",
):
    if symbol not in linker:
        errors.append(f"linker.ld: missing {symbol}")

if errors:
    print("memory source checks: FAIL")
    for error in errors:
        print(error)
    sys.exit(1)

print("memory source checks: PASS")
