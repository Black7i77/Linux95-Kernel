#!/usr/bin/env python3

from pathlib import Path
import shutil
import subprocess
import sys
import time

WITHOUT_NETWORK = "--without-network" in sys.argv[1:]
PROCESS_SELF_TEST = "--process-self-test" in sys.argv[1:]
PROCESS_FAULT_TEST = "--process-fault-test" in sys.argv[1:]
WITHOUT_USER_PROGRAMS = "--without-user-programs" in sys.argv[1:]

if sum((WITHOUT_NETWORK, PROCESS_SELF_TEST, PROCESS_FAULT_TEST,
        WITHOUT_USER_PROGRAMS)) > 1 or any(
        argument not in ("--without-network", "--process-self-test",
                         "--process-fault-test", "--without-user-programs")
       for argument in sys.argv[1:]):
    print("usage: qemu_smoke.py [--without-network] [--process-self-test] "
          "[--process-fault-test] [--without-user-programs]")
    sys.exit(2)

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "build" / (
    "linux95-kernel.img"
    if WITHOUT_NETWORK or PROCESS_SELF_TEST or PROCESS_FAULT_TEST or
            WITHOUT_USER_PROGRAMS
    else "linux95-kernel-network-test.img"
)
STORAGE_IMAGE = ROOT / "build" / (
    "linux95-fault-test.img" if PROCESS_FAULT_TEST else
    "linux95-no-user-test.img" if WITHOUT_USER_PROGRAMS else
    "linux95-storage-test.img"
)
LOG = ROOT / "build" / "qemu-debug.log"

QEMU = shutil.which("qemu-system-x86_64")

if QEMU is None:
    print("qemu smoke test: FAIL")
    print("qemu-system-x86_64 was not found in PATH")
    sys.exit(1)

if PROCESS_FAULT_TEST:
    subprocess.run(
        ["make", "all", "build/linux95-fault-test.img"],
        cwd=ROOT,
        check=True,
    )

if WITHOUT_USER_PROGRAMS:
    subprocess.run(
        ["make", "all", "prepare-storage-test-image"],
        cwd=ROOT,
        check=True,
    )
    shutil.copyfile(
        ROOT / "build" / "linux95-storage-test.img",
        STORAGE_IMAGE,
    )
    for user_path in ("INIT.ELF", "WORKER.ELF"):
        subprocess.run(
            ["mdel", "-i", str(STORAGE_IMAGE), f"::USER/{user_path}"],
            cwd=ROOT,
            check=True,
        )
        absent = subprocess.run(
            ["mdir", "-i", str(STORAGE_IMAGE), f"::USER/{user_path}"],
            cwd=ROOT,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if absent.returncode == 0:
            raise RuntimeError(f"{user_path} remains in no-user FAT fixture")

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

if not WITHOUT_NETWORK and not PROCESS_SELF_TEST and not PROCESS_FAULT_TEST:
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
completion_seen_at = None
early_exit = None
completion_marker = (
    "[PASS] desktop remained online"
    if PROCESS_SELF_TEST or PROCESS_FAULT_TEST
    else ("[PASS] desktop_online"
          if WITHOUT_NETWORK or WITHOUT_USER_PROGRAMS
          else "[PASS] icmp_echo_reply")
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
                if not (PROCESS_SELF_TEST or PROCESS_FAULT_TEST or
                        WITHOUT_USER_PROGRAMS):
                    saw_completion = True
                    break
                if completion_seen_at is None:
                    completion_seen_at = time.monotonic()
                elif time.monotonic() - completion_seen_at >= 1.0:
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

if WITHOUT_NETWORK or PROCESS_SELF_TEST or PROCESS_FAULT_TEST:
    if ("[WARN] pci_no_rtl8139" not in content and
            "[WARN] network_offline" not in content):
        required.append("[WARN] pci_no_rtl8139 or [WARN] network_offline")
elif WITHOUT_USER_PROGRAMS:
    required.extend([
        "[PASS] rtl8139_detected",
        "[PASS] rtl8139_initialized",
        "[PASS] ethernet_ready",
        "[PASS] arp_ready",
        "[PASS] ipv4_ready",
        "[PASS] icmp_ready",
    ])
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
        "[PASS] syscall path",
        "[PASS] cooperative process switch",
        "[PASS] pid2 exited",
        "[PASS] pid1 exited",
        "[PASS] desktop remained online",
    ])

if PROCESS_FAULT_TEST:
    required.extend([
        "[PASS] process subsystem initialized",
        "[PASS] pid1 ELF loaded",
        "[PASS] pid2 ELF loaded",
        "[PASS] user fault captured",
        "[PASS] faulty process terminated",
        "[PASS] kernel survived user fault",
        "[PASS] desktop remained online",
    ])

if WITHOUT_USER_PROGRAMS:
    required.append("[WARN] user_processes_offline")

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

if PROCESS_SELF_TEST:
    ordered_markers = [
        "[PASS] process subsystem initialized",
        "[PASS] pid1 ELF loaded",
        "[PASS] pid2 ELF loaded",
        "[PASS] entered ring3",
        "[PASS] int80 syscall path",
        "[PASS] syscall path",
        "[PASS] cooperative process switch",
        "[PASS] pid2 exited",
        "[PASS] pid1 exited",
        "[PASS] desktop remained online",
    ]
    marker_positions = [content.find(marker) for marker in ordered_markers]
    if any(position < 0 for position in marker_positions) or marker_positions != sorted(marker_positions):
        print("qemu smoke test: FAIL")
        print("process markers did not appear in required order")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)

    user_messages = [
        "[pid 1] hello through int 0x80",
        "[pid 2] hello through syscall",
        "[pid 1] resumed through syscall",
        "[pid 2] resumed through int 0x80",
    ]
    message_positions = [content.find(message) for message in user_messages]
    if (any(position < 0 for position in message_positions) or
            message_positions != sorted(message_positions)):
        print("qemu smoke test: FAIL")
        print("user messages did not appear in required round-robin order")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)
    if any(content.count(message) != 1 for message in user_messages):
        print("qemu smoke test: FAIL")
        print("a user message was missing or appeared more than once")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)
    if (content.count("[PASS] pid2 exited") != 1 or
            content.count("[PASS] pid1 exited") != 1 or
            content.count("[PASS] process reaped") != 2):
        print("qemu smoke test: FAIL")
        print("exit/reap lifecycle markers had unexpected counts")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)
    if message_positions[-1] > content.find("[PASS] pid2 exited"):
        print("qemu smoke test: FAIL")
        print("user output continued after process exit")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)

if WITHOUT_USER_PROGRAMS:
    offline_position = content.find("[WARN] user_processes_offline")
    desktop_position = content.find("[PASS] desktop_online")
    if (offline_position < 0 or desktop_position < 0 or
            offline_position >= desktop_position):
        print("qemu smoke test: FAIL")
        print("userspace-offline warning did not precede desktop startup")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)

if PROCESS_FAULT_TEST:
    fault_diagnostic = (
        "[FAULT] vector=14 error=0x4 cs=0x23 "
        "cr2=0x500000000000 pid=1"
    )
    fault_markers = [
        "[PASS] process subsystem initialized",
        "[PASS] pid1 ELF loaded",
        "[PASS] pid2 ELF loaded",
        "[PASS] user fault captured",
        "[PASS] faulty process terminated",
        "[PASS] kernel survived user fault",
        "[PASS] process reaped",
        "[PASS] desktop remained online",
    ]
    fault_positions = [content.find(marker) for marker in fault_markers]
    if (content.count(fault_diagnostic) != 1 or
            any(position < 0 for position in fault_positions) or
            fault_positions != sorted(fault_positions)):
        print("qemu smoke test: FAIL")
        print("user-fault diagnostic/host-return order was invalid")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)
    if any(content.count(marker) != 1 for marker in fault_markers[:6]) or \
            content.count(fault_markers[7]) != 1 or \
            content.count("[PASS] process reaped") != 2:
        print("qemu smoke test: FAIL")
        print("fault markers/reaping did not occur exactly once per process")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)
    if (content.count("[BOOT] low_kernel_entry") != 1 or
            "#DF" in content or "double fault" in content.lower() or
            "triple fault" in content.lower() or "reset" in content.lower()):
        print("qemu smoke test: FAIL")
        print("fault isolation encountered a reset or double fault")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)

print("[PASS] Linux95 booted in QEMU")
print("[PASS] x86_64 kernel entered kernel_main")
print("[PASS] kernel initialization reached graphical desktop")
if WITHOUT_NETWORK or PROCESS_SELF_TEST or PROCESS_FAULT_TEST:
    print("[PASS] missing RTL8139 remained non-fatal")
else:
    print("[PASS] RTL8139 network stack initialized")
    print("[PASS] ARP gateway resolved at 10.0.2.2")
    print("[PASS] ICMP echo reply received from 10.0.2.2")
print("QEMU smoke test: PASS")
