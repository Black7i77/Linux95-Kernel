#!/usr/bin/env python3

from pathlib import Path
import shutil
import subprocess
import sys
import time

WITHOUT_NETWORK = "--without-network" in sys.argv[1:]
PROCESS_SELF_TEST = "--process-self-test" in sys.argv[1:]

if any(argument not in ("--without-network", "--process-self-test")
       for argument in sys.argv[1:]):
    print("usage: qemu_smoke.py [--without-network] [--process-self-test]")
    sys.exit(2)

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "build" / (
    "linux95-kernel.img"
    if WITHOUT_NETWORK or PROCESS_SELF_TEST
    else "linux95-kernel-network-test.img"
)
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
    "-vga", "std",
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

if not WITHOUT_NETWORK and not PROCESS_SELF_TEST:
    cmd.extend([
        "-netdev", "user,id=net0",
        "-device", "rtl8139,netdev=net0",
    ])

proc = subprocess.Popen(
    cmd,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    text=True,
)

deadline = time.monotonic() + 12.0
saw_completion = False
early_exit = None
completion_marker = (
    "[PASS] desktop_online"
    if WITHOUT_NETWORK or PROCESS_SELF_TEST
    else "[PASS] icmp_echo_reply"
)

try:
    while time.monotonic() < deadline:
        rc = proc.poll()
        if rc is not None:
            early_exit = rc
            break

        if LOG.exists():
            content = LOG.read_text(errors="replace")
            if completion_marker in content:
                saw_completion = True
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

if early_exit is not None and not saw_completion:
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
    "[PASS] framebuffer_mapped",
    "[PASS] renderer_online",
    "[PASS] terminal_app_ready",
    "[PASS] system_info_app_ready",
    "[PASS] desktop_online",
]

if WITHOUT_NETWORK or PROCESS_SELF_TEST:
    if ("[WARN] pci_no_rtl8139" not in content and
            "[WARN] network_offline" not in content):
        required.append("[WARN] pci_no_rtl8139 or [WARN] network_offline")
else:
    required.extend([
        "[PASS] rtl8139_detected",
        "[PASS] rtl8139_initialized",
        "[PASS] ethernet_ready",
        "[PASS] arp_ready",
        "[PASS] ipv4_ready",
        "[PASS] icmp_ready",
        "[PASS] arp_gateway_resolved",
        "[PASS] icmp_echo_reply",
    ])

if PROCESS_SELF_TEST:
    required.extend([
        "[PASS] entered ring3",
        "[PASS] int80 syscall path",
    ])

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
print("[PASS] kernel initialization reached graphical desktop")
if WITHOUT_NETWORK or PROCESS_SELF_TEST:
    print("[PASS] missing RTL8139 remained non-fatal")
else:
    print("[PASS] RTL8139 network stack initialized")
    print("[PASS] ARP gateway resolved at 10.0.2.2")
    print("[PASS] ICMP echo reply received from 10.0.2.2")
print("QEMU smoke test: PASS")
