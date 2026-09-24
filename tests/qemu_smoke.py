#!/usr/bin/env python3

from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "build" / "linux95-kernel.img"
STORAGE_IMAGE = ROOT / "build" / "linux95-storage-test.img"
LOG = ROOT / "build" / "qemu-debug.log"

QEMU = shutil.which("qemu-system-x86_64")

if QEMU is None:
    print("qemu smoke test: FAIL")
    print("qemu-system-x86_64 was not found in PATH")
    sys.exit(1)

if not IMAGE.is_file():
    print("qemu smoke test: FAIL")
    print(f"missing image: {IMAGE}")
    sys.exit(1)

if not STORAGE_IMAGE.is_file():
    print("qemu smoke test: FAIL")
    print(f"missing storage test image: {STORAGE_IMAGE}")
    sys.exit(1)

LOG.unlink(missing_ok=True)

cmd = [
    QEMU,
    "-machine", "pc",
    "-m", "128M",
    "-boot", "c",
    "-drive", f"if=ide,index=0,media=disk,format=raw,file={IMAGE}",
    "-drive", f"if=ide,index=1,media=disk,format=raw,file={STORAGE_IMAGE}",
    "-display", "none",
    "-serial", "none",
    "-monitor", "none",
    "-no-reboot",
    "-no-shutdown",
    "-debugcon", f"file:{LOG}",
    "-global", "isa-debugcon.iobase=0xe9",
]

proc = subprocess.Popen(
    cmd,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    text=True,
)

deadline = time.monotonic() + 12.0
saw_shell = False
early_exit = None

try:
    while time.monotonic() < deadline:
        rc = proc.poll()
        if rc is not None:
            early_exit = rc
            break

        if LOG.exists():
            content = LOG.read_text(errors="replace")
            if "[PASS] shell_ready" in content:
                saw_shell = True
                break

        time.sleep(0.05)
finally:
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2)

stderr = ""
if proc.stderr is not None:
    stderr = proc.stderr.read()

content = LOG.read_text(errors="replace") if LOG.exists() else ""

if early_exit is not None and not saw_shell:
    print("qemu smoke test: FAIL")
    print(f"QEMU exited early with status {early_exit}")
    if stderr.strip():
        print(stderr.strip())
    print("--- debug log ---")
    print(content or "(empty)")
    sys.exit(1)

if "[PANIC]" in content:
    print("qemu smoke test: FAIL")
    print("kernel panic marker found")
    print("--- debug log ---")
    print(content)
    sys.exit(1)

required = [
    "[BOOT] low_kernel_entry",
    "[PASS] bootstrap_tables_created",
    "[PASS] cr3_reloaded",
    "[PASS] higher_half_entry",
    "[PASS] hhdm_online",
    "[PASS] physical_allocator_online",
    "[PASS] virtual_memory_online",
    "[PASS] memory_self_test",
    "[PASS] ata_master_identify",
    "[PASS] ata_slave_identify",
    "[PASS] ata_read",
    "[PASS] ata_write",
    "[PASS] ata_restore",
    "[PASS] master_write_guard",
    "[PASS] storage_self_test",
    "[PASS] fat32_mount",
    "[PASS] fat32_root_list",
    "[PASS] fat32_file_lookup",
    "[PASS] fat32_file_read",
    "[PASS] fat32_subdirectory",
    "[PASS] fat32_cluster_chain",
    "[PASS] filesystem_self_test",
    "[PASS] vfs_initialize",
    "[PASS] vfs_file_open",
    "[PASS] vfs_file_read",
    "[PASS] vfs_stat",
    "[PASS] vfs_directory_open",
    "[PASS] vfs_readdir",
    "[PASS] vfs_self_test",
    "[PASS] shell_ready",
]

missing = [marker for marker in required if marker not in content]

if missing:
    print("qemu smoke test: FAIL")
    seen = [marker for marker in required if marker in content]
    print("last checkpoint:", seen[-1] if seen else "(none)")
    print("missing markers:")
    for marker in missing:
        print(f"  {marker}")
    print("--- debug log ---")
    print(content or "(empty)")
    if stderr.strip():
        print("--- qemu stderr ---")
        print(stderr.strip())
    sys.exit(1)

print("[PASS] Linux95 booted in QEMU")
print("[PASS] x86_64 kernel entered kernel_main")
print("[PASS] kernel initialization reached shell")
print("QEMU smoke test: PASS")
