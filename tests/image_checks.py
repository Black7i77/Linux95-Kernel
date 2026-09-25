#!/usr/bin/env python3

from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"

makefile_text = (ROOT / "Makefile").read_text()
kernel_sector_match = re.search(
    r"^KERNEL_SECTORS\s*:=\s*(\d+)\s*$",
    makefile_text,
    re.MULTILINE,
)

if kernel_sector_match is None:
    raise SystemExit("FAIL: unable to read KERNEL_SECTORS from Makefile")

kernel_sectors = int(kernel_sector_match.group(1))

if kernel_sectors % 64 != 0:
    raise SystemExit(
        f"FAIL: KERNEL_SECTORS={kernel_sectors} is not divisible by 64"
    )


makefile = (ROOT / "Makefile").read_text()
stage2_source = (ROOT / "boot/stage2.asm").read_text()

def make_number(name):
    match = re.search(rf"^{name}\s*:=\s*(\d+)\s*$", makefile, re.MULTILINE)
    if not match:
        raise SystemExit(f"FAIL: {name} missing from Makefile")
    return int(match.group(1))

sector = make_number("SECTOR")
stage2_sectors = make_number("STAGE2_SECTORS")
kernel_lba = make_number("KERNEL_LBA")
kernel_sectors = make_number("KERNEL_SECTORS")
image_sectors = make_number("IMAGE_SECTORS")

stage2_kernel = re.search(r"^KERNEL_SECTORS\s+equ\s+(\d+)\s*$", stage2_source, re.MULTILINE)
stage2_lba = re.search(r"^KERNEL_LBA\s+equ\s+(\d+)\s*$", stage2_source, re.MULTILINE)
if not stage2_kernel or int(stage2_kernel.group(1)) != kernel_sectors:
    raise SystemExit("FAIL: KERNEL_SECTORS differs between Makefile and boot/stage2.asm")
if not stage2_lba or int(stage2_lba.group(1)) != kernel_lba:
    raise SystemExit("FAIL: KERNEL_LBA differs between Makefile and boot/stage2.asm")

expected_image_sectors = kernel_lba + kernel_sectors
if image_sectors != expected_image_sectors:
    raise SystemExit(
        f"FAIL: IMAGE_SECTORS={image_sectors}, expected {expected_image_sectors}"
    )

stage1 = (BUILD / "stage1.bin").read_bytes()
stage2 = (BUILD / "stage2.bin").read_bytes()
kernel = (BUILD / "kernel.bin").read_bytes()
image = (BUILD / "linux95-kernel.img").read_bytes()

if len(stage1) != sector:
    raise SystemExit(f"FAIL: stage1 is {len(stage1)} bytes")
if stage1[510:512] != b"\x55\xAA":
    raise SystemExit("FAIL: stage1 boot signature is not 0x55AA")
if len(stage2) != stage2_sectors * sector:
    raise SystemExit(f"FAIL: stage2 is {len(stage2)} bytes")
if len(kernel) > kernel_sectors * sector:
    raise SystemExit(f"FAIL: kernel is {len(kernel)} bytes, budget is {kernel_sectors * sector}")
if len(image) != image_sectors * sector:
    raise SystemExit(f"FAIL: image is {len(image)} bytes")

result = subprocess.run(
    ["readelf", "-h", str(BUILD / "kernel.elf")],
    check=True,
    text=True,
    capture_output=True,
)
if "Entry point address:               0x100000" not in result.stdout:
    raise SystemExit("FAIL: kernel entry is not 0x100000")

symbols = subprocess.run(
    ["nm", "-n", str(BUILD / "kernel.elf")],
    check=True,
    text=True,
    capture_output=True,
).stdout
symbol_map = {}
for line in symbols.splitlines():
    parts = line.split()
    if len(parts) >= 3:
        symbol_map[parts[2]] = int(parts[0], 16)

for required in (
    "__kernel_phys_start",
    "__kernel_phys_end",
    "__bootstrap_pt_pool_start",
    "__bootstrap_pt_pool_end",
):
    if required not in symbol_map:
        raise SystemExit(f"FAIL: missing linker symbol {required}")

if symbol_map["__kernel_phys_start"] != 0x100000:
    raise SystemExit("FAIL: kernel physical start is not 0x100000")
if symbol_map["__kernel_phys_end"] > 0x200000:
    raise SystemExit("FAIL: kernel/bootstrap reservation exceeds initial 2 MiB mapping")
if symbol_map["__bootstrap_pt_pool_start"] & 0xFFF:
    raise SystemExit("FAIL: bootstrap page-table pool is not 4 KiB aligned")
if symbol_map["__bootstrap_pt_pool_end"] <= symbol_map["__bootstrap_pt_pool_start"]:
    raise SystemExit("FAIL: bootstrap page-table pool is empty")

user_image_base = 0x0000400000000000
user_image_limit = 0x0000400100000000

def check_user_elf(name):
    path = BUILD / "user" / name
    if not path.is_file():
        raise SystemExit(f"FAIL: missing user ELF: {path}")

    header = subprocess.run(
        ["readelf", "-h", str(path)],
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    required_header = (
        "Class:                             ELF64",
        "Data:                              2's complement, little endian",
        "Machine:                           Advanced Micro Devices X86-64",
        "Type:                              EXEC (Executable file)",
    )
    for required in required_header:
        if required not in header:
            raise SystemExit(f"FAIL: {name} missing ELF property: {required}")

    entry_match = re.search(r"Entry point address:\s+0x([0-9a-fA-F]+)", header)
    if entry_match is None:
        raise SystemExit(f"FAIL: {name} has no entry point")
    entry = int(entry_match.group(1), 16)
    if not user_image_base <= entry < user_image_limit:
        raise SystemExit(f"FAIL: {name} entry outside user image window")

    program_headers = subprocess.run(
        ["readelf", "-l", str(path)],
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    if "INTERP" in program_headers:
        raise SystemExit(f"FAIL: {name} contains an INTERP segment")

for user_elf in ("init.elf", "worker.elf"):
    check_user_elf(user_elf)

print("image checks: PASS")
