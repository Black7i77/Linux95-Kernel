#!/usr/bin/env python3

from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"

stage1 = (BUILD / "stage1.bin").read_bytes()
stage2 = (BUILD / "stage2.bin").read_bytes()
image = (BUILD / "linux95-kernel.img").read_bytes()

if len(stage1) != 512:
    raise SystemExit(f"FAIL: stage1 is {len(stage1)} bytes")

if stage1[510:512] != b"\x55\xAA":
    raise SystemExit("FAIL: stage1 boot signature is not 0x55AA")

if len(stage2) != 16 * 512:
    raise SystemExit(f"FAIL: stage2 is {len(stage2)} bytes")

if len(image) != 145 * 512:
    raise SystemExit(f"FAIL: image is {len(image)} bytes")

result = subprocess.run(
    ["readelf", "-h", str(BUILD / "kernel.elf")],
    check=True,
    text=True,
    capture_output=True,
)

if "Entry point address:               0x100000" not in result.stdout:
    raise SystemExit("FAIL: kernel entry is not 0x100000")

print("image checks: PASS")
