#!/usr/bin/env python3

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FS = ROOT / "kernel" / "filesystem"

fat32 = (FS / "fat32.cpp").read_text()
shell = (ROOT / "kernel" / "terminal" / "shell.cpp").read_text()
facade = (FS / "filesystem.hpp").read_text()
vfs = (FS / "vfs.hpp").read_text()

public_signatures = (
    r"Status\s+touch\s*\(\s*const char\s*\*\s*path\s*\)\s*;",
    r"Status\s+write_file\s*\(\s*const char\s*\*\s*path\s*,\s*const uint8_t\s*\*\s*data\s*,\s*size_t\s+size\s*\)\s*;",
    r"Status\s+mkdir\s*\(\s*const char\s*\*\s*path\s*\)\s*;",
    r"Status\s+remove\s*\(\s*const char\s*\*\s*path\s*\)\s*;",
    r"Status\s+copy_file\s*\(\s*const char\s*\*\s*source\s*,\s*const char\s*\*\s*destination\s*\)\s*;",
    r"Status\s+move\s*\(\s*const char\s*\*\s*source\s*,\s*const char\s*\*\s*destination\s*\)\s*;",
)

for name, header in (("filesystem", facade), ("VFS", vfs)):
    for signature in public_signatures:
        if not re.search(signature, header):
            raise SystemExit(f"FAIL: {name} missing public writable API: {signature}")
    for forbidden in ("storage::DiskId", "fat32::", "DirectorySlot", "ResolvedPath", "fat_begin_lba", "first_data_lba"):
        if forbidden in header:
            raise SystemExit(f"FAIL: {name} public header exposes FAT/disk internals: {forbidden}")

filesystem_sources = ""
for path in sorted(FS.glob("*")):
    if path.suffix in {".cpp", ".hpp"}:
        text = path.read_text()
        filesystem_sources += "\n" + text

        if "storage::write_sector" in text and path.name != "fat32_write.cpp":
            raise SystemExit(
                f"FAIL: filesystem write call found in {path}"
            )

raw_write_exceptions = {
    FS / "fat32_write.cpp",
    ROOT / "kernel" / "storage" / "storage_self_test.cpp",
    FS / "fat32_write_self_test.cpp",
}
for path in sorted((ROOT / "kernel").rglob("*.cpp")):
    text = path.read_text()
    if path not in raw_write_exceptions and re.search(r"\bstorage::write_sector\s*\(", text):
        raise SystemExit(f"FAIL: raw sector write outside FAT32 writer: {path}")
    if path == FS / "fat32_write_self_test.cpp":
        for call in re.finditer(r"storage::write_sector\s*\(", text):
            if not re.match(r"\s*storage::DiskId::Boot\s*,", text[call.end():]):
                raise SystemExit("FAIL: writable self-test raw write is not a Boot denial probe")
    if path != ROOT / "kernel" / "storage" / "disk.cpp" and re.search(r"\bata::write_sector\s*\(", text):
        raise SystemExit(f"FAIL: ATA write outside storage boundary: {path}")
    if path.parent.name != "terminal":
        continue
    if re.search(r"\bfat32(?:::|_write)", text):
        raise SystemExit(f"FAIL: FAT32 internals in terminal production: {path}")
    if re.search(r"\bfilesystem::(?:touch|write_file|mkdir|remove|copy_file|move)\s*\(", text):
        raise SystemExit(f"FAIL: terminal mutation bypasses VFS: {path}")

command_source = ROOT / "kernel" / "terminal" / "filesystem_commands.cpp"
if command_source.exists():
    command_text = command_source.read_text()
    for operation in ("touch", "write_file", "mkdir", "remove", "copy_file", "move"):
        if f"filesystem::vfs::{operation}(" not in command_text:
            raise SystemExit(f"FAIL: terminal command missing VFS dispatch: {operation}")

writer = (FS / "fat32_write.cpp").read_text()
write_calls = list(re.finditer(r"storage::write_sector\s*\(", writer))
if not write_calls:
    raise SystemExit("FAIL: FAT32 writer has no sector-write call")
for call in write_calls:
    if not re.match(r"\s*storage::DiskId::Test\s*,", writer[call.end():]):
        raise SystemExit("FAIL: FAT32 writer sector-write call does not target Test disk")

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

# Shell file/directory access must go through the VFS layer.
if '#include "filesystem/vfs.hpp"' not in shell:
    raise SystemExit(
        "FAIL: shell does not include filesystem/vfs.hpp"
    )

required_vfs_calls = (
    "filesystem::vfs::opendir",
    "filesystem::vfs::readdir",
    "filesystem::vfs::closedir",
    "filesystem::vfs::open",
    "filesystem::vfs::read",
    "filesystem::vfs::close",
)

for call in required_vfs_calls:
    if call not in shell:
        raise SystemExit(
            f"FAIL: shell missing VFS call: {call}"
        )

for raw_call in (
    "filesystem::list_directory(",
    "filesystem::read_file(",
):
    if raw_call in shell:
        raise SystemExit(
            f"FAIL: shell bypasses VFS: {raw_call}"
        )

print("filesystem source checks: PASS")
