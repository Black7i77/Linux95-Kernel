#!/usr/bin/env python3

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FS = ROOT / "kernel" / "filesystem"

fat32 = (FS / "fat32.cpp").read_text()
shell = (ROOT / "kernel" / "terminal" / "shell.cpp").read_text()
facade = (FS / "filesystem.hpp").read_text()

filesystem_sources = ""
for path in sorted(FS.glob("*")):
    if path.suffix in {".cpp", ".hpp"}:
        text = path.read_text()
        filesystem_sources += "\n" + text

        if "storage::write_sector" in text:
            raise SystemExit(
                f"FAIL: filesystem write call found in {path}"
            )

if "storage::read_sector" not in fat32:
    raise SystemExit(
        "FAIL: FAT32 backend does not use storage::read_sector"
    )

if "0x0FFFFFFF" not in filesystem_sources:
    raise SystemExit(
        "FAIL: FAT32 28-bit mask missing"
    )

if "0x0F" not in filesystem_sources:
    raise SystemExit(
        "FAIL: FAT32 LFN handling marker missing"
    )

if "cluster_count" not in fat32:
    raise SystemExit(
        "FAIL: bounded cluster traversal marker missing"
    )

for command in ("fsinfo", "ls", "cat"):
    if f'"{command}"' not in shell:
        raise SystemExit(
            f"FAIL: shell command {command!r} missing"
        )

forbidden_api = re.compile(
    r"\b(create|delete|rename|truncate|mkdir|write)\s*\("
)

match = forbidden_api.search(facade)
if match:
    raise SystemExit(
        f"FAIL: writable filesystem API exposed: "
        f"{match.group(1)}"
    )

print("filesystem source checks: PASS")
