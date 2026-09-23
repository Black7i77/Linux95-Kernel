#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"

# High-alias execution requires kernel C++ objects to use address-relative
# references. The final static ELF has relocations already applied, so audit
# the relocatable C++ objects instead of the final ELF.
objects = sorted(BUILD.glob("*.o"))
objects = [p for p in objects if p.name not in {"entry.o", "interrupts_asm.o", "paging_bootstrap.o"}]

if not objects:
    print("relocation checks: FAIL - no kernel C++ objects found")
    sys.exit(1)

forbidden = ("R_X86_64_32 ", "R_X86_64_32S", "R_X86_64_64 ")
bad = []

for obj in objects:
    result = subprocess.run(
        ["readelf", "-rW", str(obj)],
        check=True,
        capture_output=True,
        text=True,
    )
    for line in result.stdout.splitlines():
        if any(kind in line for kind in forbidden):
            bad.append(f"{obj.name}: {line}")

if bad:
    print("relocation checks: FAIL")
    print("position-dependent relocations remain in C++ objects:")
    for line in bad:
        print(line)
    sys.exit(1)

print("relocation checks: PASS")
