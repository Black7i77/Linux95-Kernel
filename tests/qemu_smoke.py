#!/usr/bin/env python3

from pathlib import Path
import hashlib
import json
import socket
import shutil
import subprocess
import sys
import tempfile
import threading
import time

from qemu_smoke_policy import grace_complete
from file_manager_screen_checks import wait_for_text
from qemu_cursor import locate_cursor, movement_steps, wait_for_cursor

WITHOUT_NETWORK = "--without-network" in sys.argv[1:]
PROCESS_SELF_TEST = "--process-self-test" in sys.argv[1:]
PROCESS_FAULT_TEST = "--process-fault-test" in sys.argv[1:]
PROCESS_PREEMPTION_TEST = "--process-preemption-test" in sys.argv[1:]
WITHOUT_USER_PROGRAMS = "--without-user-programs" in sys.argv[1:]
UDP_NETWORK_TEST = "--udp-network-test" in sys.argv[1:]
DNS_NETWORK_TEST = "--dns-network-test" in sys.argv[1:]
FAT32_WRITE_TEST = "--fat32-write-test" in sys.argv[1:]
EDITOR_TEST = "--editor-test" in sys.argv[1:]
FILE_MANAGER_TEST = "--file-manager-test" in sys.argv[1:]
FAT32_WRITE_BYTES = b"Linux95 FAT32 write proof\x00\xff\n"
UDP_PAYLOAD = b"linux95-udp-echo"
UDP_INVALID_REPLY = b"linux95-udp-evil"

if sum((WITHOUT_NETWORK, PROCESS_SELF_TEST, PROCESS_FAULT_TEST,
        PROCESS_PREEMPTION_TEST, WITHOUT_USER_PROGRAMS,
        UDP_NETWORK_TEST, DNS_NETWORK_TEST, FAT32_WRITE_TEST,
        EDITOR_TEST, FILE_MANAGER_TEST)) > 1 or any(
        argument not in ("--without-network", "--process-self-test",
                         "--process-fault-test", "--process-preemption-test",
                         "--without-user-programs", "--udp-network-test",
                         "--dns-network-test", "--fat32-write-test",
                         "--editor-test", "--file-manager-test")
       for argument in sys.argv[1:]):
    print("usage: qemu_smoke.py [--without-network] [--process-self-test] "
          "[--process-fault-test] [--process-preemption-test] "
          "[--without-user-programs] [--udp-network-test] "
          "[--dns-network-test] [--fat32-write-test] [--editor-test] "
          "[--file-manager-test]")
    sys.exit(2)

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "build" / (
    "linux95-kernel.img" if FILE_MANAGER_TEST else
    "linux95-editor-test.img" if EDITOR_TEST else
    "linux95-fat32-write-test.img" if FAT32_WRITE_TEST else
    "linux95-kernel.img"
    if WITHOUT_NETWORK or PROCESS_SELF_TEST or PROCESS_FAULT_TEST or
            PROCESS_PREEMPTION_TEST or WITHOUT_USER_PROGRAMS
    else "linux95-udp-network-test.img"
    if UDP_NETWORK_TEST
    else "linux95-dns-network-test.img" if DNS_NETWORK_TEST
    else "linux95-kernel-network-test.img"
)
STORAGE_IMAGE = ROOT / "build" / (
    "linux95-file-manager-test-fat.img" if FILE_MANAGER_TEST else
    "linux95-editor-test-fat.img" if EDITOR_TEST else
    "linux95-fat32-write-test-fat.img" if FAT32_WRITE_TEST else
    "linux95-preemption-test.img" if PROCESS_PREEMPTION_TEST else
    "linux95-fault-test.img" if PROCESS_FAULT_TEST else
    "linux95-no-user-test.img" if WITHOUT_USER_PROGRAMS else
    "linux95-storage-test.img"
)
LOG = ROOT / "build" / "qemu-debug.log"
CANONICAL_STORAGE_IMAGE = ROOT / "build" / "linux95-storage-test.img"

def image_digest(path):
    digest = hashlib.sha256()
    with path.open("rb") as image_file:
        for block in iter(lambda: image_file.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


editor_boot_digest = None
if EDITOR_TEST:
    subprocess.run(["make", "all", "build/linux95-editor-test.img",
                    "prepare-editor-test-image"],
                   cwd=ROOT, check=True)
    editor_boot_digest = image_digest(IMAGE)
    for name in ("NOSAVE.TXT", "SAVED.TXT"):
        if subprocess.run(
                ["mdir", "-i", str(STORAGE_IMAGE), f"::{name}"],
                cwd=ROOT, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, check=False).returncode == 0:
            raise RuntimeError(f"editor fixture unexpectedly contains {name}")

file_manager_boot_digest = None
file_manager_canonical_digest = None
if FILE_MANAGER_TEST:
    if STORAGE_IMAGE.resolve() == CANONICAL_STORAGE_IMAGE.resolve():
        raise RuntimeError("File Manager test must use a disposable Test image")
    if CANONICAL_STORAGE_IMAGE.exists():
        file_manager_canonical_digest = image_digest(CANONICAL_STORAGE_IMAGE)
    subprocess.run(
        ["make", "all", "prepare-file-manager-test-image"],
        cwd=ROOT,
        check=True,
    )
    if not STORAGE_IMAGE.is_file():
        raise RuntimeError("File Manager disposable Test image was not created")
    if subprocess.run(
            ["mdir", "-i", str(STORAGE_IMAGE), "::FMTEST"],
            cwd=ROOT, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, check=False).returncode == 0:
        raise RuntimeError("File Manager fixture unexpectedly contains FMTEST")
    if (file_manager_canonical_digest is not None and
            image_digest(CANONICAL_STORAGE_IMAGE) != file_manager_canonical_digest):
        raise RuntimeError("canonical Test image changed during fixture preparation")
    file_manager_boot_digest = image_digest(IMAGE)


canonical_digest = None
if FAT32_WRITE_TEST:
    if (STORAGE_IMAGE.resolve() == CANONICAL_STORAGE_IMAGE.resolve() or
            IMAGE.resolve() == STORAGE_IMAGE.resolve() or
            IMAGE.resolve() == CANONICAL_STORAGE_IMAGE.resolve()):
        print("qemu smoke test: FAIL")
        print("FAT32 write test must use separate boot, disposable Test, and canonical paths")
        sys.exit(1)
    if CANONICAL_STORAGE_IMAGE.exists():
        canonical_digest = image_digest(CANONICAL_STORAGE_IMAGE)
    build = subprocess.run(
        ["make", "build/linux95-fat32-write-test.img",
         "prepare-fat32-write-test-image"], cwd=ROOT, check=False)
    if build.returncode != 0:
        print("qemu smoke test: FAIL")
        print("dedicated FAT32 write image/fixture build failed")
        sys.exit(1)
    if not STORAGE_IMAGE.is_file():
        print("qemu smoke test: FAIL")
        print("disposable FAT32 Test image was not created")
        sys.exit(1)
    if subprocess.run(
            ["mdir", "-i", str(STORAGE_IMAGE), "::WRPROOF"],
            cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            check=False).returncode == 0:
        print("qemu smoke test: FAIL")
        print("disposable FAT32 fixture was reused instead of freshly formatted")
        sys.exit(1)
    if (canonical_digest is not None and
            image_digest(CANONICAL_STORAGE_IMAGE) != canonical_digest):
        print("qemu smoke test: FAIL")
        print("canonical FAT32 source image changed during fixture preparation")
        sys.exit(1)

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

if PROCESS_PREEMPTION_TEST:
    subprocess.run(
        ["make", "all", "build/linux95-preemption-test.img"],
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

editor_qmp_directory = None
editor_qmp_socket = None
if EDITOR_TEST or FILE_MANAGER_TEST:
    editor_qmp_directory = tempfile.TemporaryDirectory(
        prefix="editor-qmp-", dir=ROOT / "build")
    editor_qmp_socket = str(Path(editor_qmp_directory.name) / "qmp.sock")

LOG.unlink(missing_ok=True)

echo_socket = None
echo_thread = None
echo_result = {"received": False, "replies_sent": 0, "error": None}
if UDP_NETWORK_TEST:
    try:
        echo_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        echo_socket.bind(("127.0.0.1", 40000))
        echo_socket.settimeout(12.0)
    except OSError as error:
        print("qemu smoke test: FAIL")
        print(f"UDP echo responder setup failed: {error}")
        sys.exit(1)

    def echo_once():
        try:
            payload, peer = echo_socket.recvfrom(2048)
            if payload != UDP_PAYLOAD:
                echo_result["error"] = f"unexpected UDP payload: {payload!r}"
                return
            echo_result["received"] = True
            for reply in (UDP_INVALID_REPLY, UDP_PAYLOAD, UDP_PAYLOAD):
                echo_socket.sendto(reply, peer)
                echo_result["replies_sent"] += 1
                time.sleep(0.15)
        except OSError as error:
            echo_result["error"] = str(error)

    echo_thread = threading.Thread(target=echo_once, daemon=True)
    echo_thread.start()

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
    "-debugcon", f"file:{LOG}",
    "-global", "isa-debugcon.iobase=0xe9",
]

if (not WITHOUT_NETWORK and not PROCESS_SELF_TEST and
        not PROCESS_FAULT_TEST and not PROCESS_PREEMPTION_TEST):
    cmd.extend([
        "-netdev", "user,id=net0",
        "-device", "rtl8139,netdev=net0",
    ])

if EDITOR_TEST or FILE_MANAGER_TEST:
    cmd.extend(["-qmp", f"unix:{editor_qmp_socket},server=on,wait=off"])

proc = subprocess.Popen(
    cmd,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    text=True,
)

deadline = time.monotonic() + (60.0 if FILE_MANAGER_TEST else
                               30.0 if EDITOR_TEST else
                               25.0 if DNS_NETWORK_TEST else 12.0)
saw_completion = False
completion_seen_at = None
early_exit = None
editor_failure = None
completion_marker = (
    "[PASS] shell accepted input"
    if EDITOR_TEST
    else "[PASS] desktop_online"
    if FILE_MANAGER_TEST
    else "[PASS] fat32_write_complete"
    if FAT32_WRITE_TEST
    else
    "[PASS] preemptive round robin"
    if PROCESS_PREEMPTION_TEST
    else "[PASS] udp_rx"
    if UDP_NETWORK_TEST
    else "[PASS] dns_lookup"
    if DNS_NETWORK_TEST
    else "[PASS] desktop remained online"
    if PROCESS_SELF_TEST or PROCESS_FAULT_TEST
    else ("[PASS] desktop_online"
          if WITHOUT_NETWORK or WITHOUT_USER_PROGRAMS
          else "[PASS] icmp_echo_reply")
)

try:
    if EDITOR_TEST or FILE_MANAGER_TEST:
        qmp = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        qmp.settimeout(3)
        qmp_deadline = time.monotonic() + 8.0
        while True:
            try:
                qmp.connect(editor_qmp_socket)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() >= qmp_deadline:
                    editor_failure = "QMP socket did not become ready"
                    break
                time.sleep(0.05)

        if editor_failure is None:
            qmp_file = qmp.makefile("rwb", buffering=0)
            qmp_file.readline()  # QMP greeting.

            def qmp_command(command):
                qmp_file.write((json.dumps(command) + "\n").encode())
                while True:
                    response = json.loads(qmp_file.readline().decode())
                    if "return" in response or "error" in response:
                        if "error" in response:
                            raise RuntimeError(
                                f"QMP command failed: {response['error']}")
                        return response

            qmp_command({"execute": "qmp_capabilities"})

            def send_key(name):
                qmp_command({
                    "execute": "human-monitor-command",
                    "arguments": {
                        "command-line": f"sendkey {name} 50"},
                })
                time.sleep(0.06)

            def send_text(text):
                for character in text:
                    key_name = "spc" if character == " " else (
                        "dot" if character == "." else character)
                    send_key(key_name)

            def wait_for_marker(marker, count=1, timeout=6.0):
                marker_deadline = time.monotonic() + timeout
                while time.monotonic() < marker_deadline:
                    current = LOG.read_text(errors="replace") if LOG.exists() else ""
                    if current.count(marker) >= count:
                        return True
                    if proc.poll() is not None:
                        return False
                    time.sleep(0.05)
                return False

            def capture_screen():
                screenshot = ROOT / "build" / "file-manager-current.ppm" if FILE_MANAGER_TEST else Path(editor_qmp_directory.name) / "pointer.ppm"
                result = qmp_command({
                    "execute": "human-monitor-command",
                    "arguments": {"command-line": f"screendump {screenshot}"},
                })["return"]
                if isinstance(result, str) and "error" in result.lower():
                    raise RuntimeError(f"QEMU screendump failed: {result}")
                return screenshot.read_bytes()

            def save_stage(name, delay=0.35):
                time.sleep(delay)
                (ROOT / "build" / f"file-manager-stage-{name}.ppm").write_bytes(capture_screen())

            def require_screen_text(text, present=True, foreground=None, region=None):
                if not wait_for_text(capture_screen, text, present=present,
                                     timeout=4.0, foreground=foreground,
                                     region=region):
                    state = "visible" if present else "absent"
                    raise RuntimeError(f"File Manager UI did not show {text!r} {state}")

            status_region = (250, 560, 1010, 590)
            dialog_text_region = (250, 184, 1010, 228)
            entries_region = (250, 220, 1010, 550)

            if not wait_for_marker("[PASS] desktop_online", timeout=10.0):
                editor_failure = "Linux95 did not reach desktop before QMP input"
            elif FILE_MANAGER_TEST:
                mouse_position = list(wait_for_cursor(
                    capture_screen, expected=(640, 360)))

                def mouse_move(x, y):
                    if mouse_position == [x, y]:
                        return
                    for dx, dy in movement_steps(mouse_position, (x, y)):
                        result = qmp_command({
                            "execute": "human-monitor-command",
                            "arguments": {"command-line": f"mouse_move {dx} {dy} 0"},
                        })["return"]
                        if isinstance(result, str) and result.strip():
                            raise RuntimeError(f"QEMU mouse_move failed: {result}")
                        time.sleep(0.05)
                    mouse_position[:] = wait_for_cursor(
                        capture_screen, timeout=3.0, expected=(x, y))

                def mouse_button(value):
                    result = qmp_command({
                        "execute": "human-monitor-command",
                        "arguments": {"command-line": f"mouse_button {value}"},
                    })["return"]
                    if isinstance(result, str) and "unknown command" in result.lower():
                        raise RuntimeError(f"QEMU mouse_button unavailable: {result}")
                    if isinstance(result, str) and result.strip():
                        print("QMP mouse_button:", result.strip())
                    time.sleep(0.12)

                def click_screen(x, y):
                    mouse_move(x, y)
                    mouse_button(1)
                    mouse_button(0)

                def create_folder(name):
                    send_key("ctrl-n")
                    require_screen_text("new folder name", region=status_region)
                    save_stage(f"{name}-dialog")
                    send_text(name)
                    require_screen_text(name, region=dialog_text_region)
                    save_stage(f"{name}-typed")
                    send_key("ret")
                    require_screen_text(name.upper(), region=entries_region)
                    save_stage(f"{name}-confirmed")

                def create_file(name):
                    send_key("ctrl-f")
                    require_screen_text("new file name", region=status_region)
                    save_stage(f"{name}-dialog")
                    (ROOT / "build" / "file-manager-create-file-open.ppm").write_bytes(capture_screen())
                    send_text(name)
                    require_screen_text(name, region=dialog_text_region)
                    save_stage(f"{name}-typed")
                    (ROOT / "build" / "file-manager-create-file-typed.ppm").write_bytes(capture_screen())
                    send_key("ret")
                    require_screen_text(name.split(".", 1)[0].upper(),
                                        region=entries_region)
                    save_stage(f"{name}-confirmed")
                    (ROOT / "build" / "file-manager-create-file-confirmed.ppm").write_bytes(capture_screen())

                def confirm_delete(target, expect_error=None):
                    send_key("delete")
                    require_screen_text(f"Delete {target}?",
                                        region=dialog_text_region)
                    send_key("ret")
                    if expect_error:
                        require_screen_text(expect_error, region=status_region)
                    else:
                        require_screen_text(target.split(".", 1)[0], present=False,
                                            region=entries_region)

                try:
                    phase = "Applications button"
                    print(f"File Manager phase: {phase}", flush=True)
                    click_screen(60, 14)      # Applications panel button
                    phase = "File Manager menu item"
                    print(f"File Manager phase: {phase}", flush=True)
                    click_screen(70, 88)      # File Manager menu item
                    save_stage("opened")
                    (ROOT / "build" / "file-manager-open.ppm").write_bytes(capture_screen())
                    phase = "create FMTEST"
                    print(f"File Manager phase: {phase}", flush=True)
                    create_folder("fmtest")
                    phase = "enter FMTEST"
                    print(f"File Manager phase: {phase}", flush=True)
                    send_key("ret")          # Enter newly selected folder.
                    save_stage("entered-fmtest")

                    phase = "create PROTECT"
                    print(f"File Manager phase: {phase}", flush=True)
                    create_folder("protect")
                    phase = "enter PROTECT"
                    print(f"File Manager phase: {phase}", flush=True)
                    send_key("ret")
                    save_stage("entered-protect")
                    phase = "create CHILD.TXT"
                    print(f"File Manager phase: {phase}", flush=True)
                    create_file("child.txt")
                    phase = "return to FMTEST"
                    send_key("backspace")    # Return to FMTEST.
                    phase = "select PROTECT"
                    send_key("down")          # Select its only child, PROTECT.
                    phase = "refuse PROTECT deletion"
                    confirm_delete("PROTECT", "delete: directory is not empty")
                    require_screen_text("PROTECT", region=entries_region)

                    phase = "create EMPTYDIR"
                    create_folder("emptydir")
                    phase = "delete EMPTYDIR"
                    confirm_delete("EMPTYDIR")

                    phase = "create TEMP.TXT"
                    create_file("temp.txt")
                    phase = "rename TEMP.TXT"
                    send_key("ctrl-r")      # Rename the selected file.
                    require_screen_text("rename entry", region=status_region)
                    (ROOT / "build" / "file-manager-rename-open.ppm").write_bytes(capture_screen())
                    for _ in range(len("temp.txt")):
                        send_key("backspace")
                    (ROOT / "build" / "file-manager-rename-cleared.ppm").write_bytes(capture_screen())
                    send_text("keep.txt")
                    (ROOT / "build" / "file-manager-rename-typed.ppm").write_bytes(capture_screen())
                    send_key("ret")
                    time.sleep(0.2)
                    (ROOT / "build" / "file-manager-rename-confirmed.ppm").write_bytes(capture_screen())

                    phase = "create REMOVE.TXT"
                    create_file("remove.txt")
                    phase = "delete REMOVE.TXT"
                    confirm_delete("REMOVE.TXT")
                    time.sleep(1.0)
                    (ROOT / "build" / "file-manager-completed.ppm").write_bytes(capture_screen())
                    saw_completion = True
                    completion_seen_at = time.monotonic()
                except (RuntimeError, OSError, ValueError) as error:
                    editor_failure = f"QMP File Manager input failed at {phase}: {error}"
            else:
                send_text("edit nosave.txt")
                send_key("ret")
                if not wait_for_marker("[PASS] editor opened"):
                    editor_failure = "missing [PASS] editor opened after first edit"
                else:
                    send_key("ctrl-q")
                    if not wait_for_marker("[PASS] editor returned to shell"):
                        editor_failure = "missing editor return marker after clean quit"
                    else:
                        send_text("edit saved.txt")
                        send_key("ret")
                        if not wait_for_marker("[PASS] editor opened", count=2):
                            editor_failure = "missing second editor-open marker"
                        else:
                            send_text("editor saved")
                            send_key("ctrl-s")
                            if not wait_for_marker("[PASS] editor saved"):
                                editor_failure = "missing editor-save marker"
                            else:
                                send_key("ctrl-q")
                                if not wait_for_marker(
                                        "[PASS] editor returned to shell", count=2):
                                    editor_failure = "missing editor return marker after save"
                                else:
                                    send_text("version")
                                    send_key("ret")
                                    if not wait_for_marker(
                                            "[PASS] shell accepted input"):
                                        editor_failure = "missing shell-input marker after editor exit"
                                    else:
                                        completion_seen_at = time.monotonic()
                                        time.sleep(1.0)
                                        saw_completion = True
            qmp_file.close()
        qmp.close()

    while not (EDITOR_TEST or FILE_MANAGER_TEST) and time.monotonic() < deadline:
        rc = proc.poll()
        if rc is not None:
            early_exit = rc
            break

        if LOG.exists():
            content = LOG.read_text(errors="replace")
            dns_evidence = all(marker in content for marker in (
                "[PASS] dns_query_accepted",
                "[PASS] dns_arp_next_hop_resolved 10.0.2.3",
                "[PASS] dns_lookup",
                "[PASS] icmp_echo_reply",
            ))
            if completion_marker in content and (
                    not FAT32_WRITE_TEST or
                    "[PASS] icmp_echo_reply" in content) and (
                    not UDP_NETWORK_TEST or
                    "[PASS] icmp_echo_reply" in content) and (
                    not DNS_NETWORK_TEST or dns_evidence):
                if UDP_NETWORK_TEST:
                    if completion_seen_at is None:
                        completion_seen_at = time.monotonic()
                    elif grace_complete(completion_seen_at, time.monotonic(),
                                        deadline):
                        saw_completion = True
                        break
                elif not (PROCESS_SELF_TEST or PROCESS_FAULT_TEST or
                          WITHOUT_USER_PROGRAMS):
                    saw_completion = True
                    break
                elif completion_seen_at is None:
                    completion_seen_at = time.monotonic()
                elif grace_complete(completion_seen_at, time.monotonic(),
                                    deadline):
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
    if echo_socket is not None:
        echo_socket.close()
        echo_thread.join(timeout=1)
    if editor_qmp_directory is not None:
        editor_qmp_directory.cleanup()

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

if not saw_completion:
    print("qemu smoke test: FAIL")
    print("completion observation did not finish before deadline")
    if editor_failure is not None:
        print("editor test detail:", editor_failure)
    elif EDITOR_TEST:
        print("editor test RED: expected real editor markers or key flow were absent")
    if DNS_NETWORK_TEST:
        missing_dns = [marker for marker in (
            "[PASS] dns_query_accepted",
            "[PASS] dns_arp_next_hop_resolved 10.0.2.3",
            "[PASS] dns_lookup",
        ) if marker not in content]
        print("missing DNS markers:", ", ".join(missing_dns) or "(none)")
    print("--- debug log ---")
    print(content or "(empty)")
    sys.exit(1)

if "[PANIC]" in content:
    print("qemu smoke test: FAIL")
    print("kernel panic marker found")
    print("--- debug log ---")
    print(content)
    sys.exit(1)

if UDP_NETWORK_TEST:
    if (not echo_result["received"] or echo_result["replies_sent"] != 3 or
            echo_result["error"] is not None):
        print("qemu smoke test: FAIL")
        print("UDP echo responder did not receive exact request and send all replies:",
              echo_result["error"] or "no datagram received")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)
    if (content.count("[BOOT] low_kernel_entry") != 1 or
            "#DF" in content or "double fault" in content.lower() or
            "triple fault" in content.lower() or "reset" in content.lower()):
        print("qemu smoke test: FAIL")
        print("UDP test encountered a reset or fatal fault")
        print("--- debug log ---")
        print(content or "(empty)")
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

if (WITHOUT_NETWORK or PROCESS_SELF_TEST or PROCESS_FAULT_TEST or
        PROCESS_PREEMPTION_TEST):
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
elif FILE_MANAGER_TEST:
    required.extend([
        "[PASS] rtl8139_detected",
        "[PASS] rtl8139_initialized",
        "[PASS] ethernet_ready",
        "[PASS] arp_ready",
        "[PASS] ipv4_ready",
        "[PASS] icmp_ready",
    ])
elif EDITOR_TEST:
    pass
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

if UDP_NETWORK_TEST:
    required.extend(["[PASS] udp_ready", "[PASS] udp_tx",
                     "[PASS] udp_echo_validated", "[PASS] udp_rx"])

if DNS_NETWORK_TEST:
    required.extend(["[PASS] udp_ready", "[PASS] dns_query_accepted",
                     "[PASS] dns_arp_next_hop_resolved 10.0.2.3",
                     "[PASS] dns_lookup"])

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

if PROCESS_PREEMPTION_TEST:
    required.extend([
        "[pid 1] entered non-yielding loop",
        "[PASS] user quantum expired",
        "[PASS] preempted context captured",
        "[PASS] timer preemption returned to host",
        "[PASS] desktop remained online after preemption",
        "[PASS] non-yielding process was preempted",
        "[pid 2] scheduled by timer preemption",
        "[PASS] preemptive round robin",
    ])

if WITHOUT_USER_PROGRAMS:
    required.append("[WARN] user_processes_offline")

if FAT32_WRITE_TEST:
    required.extend([
        "[PASS] fat32_write_touch",
        "[PASS] fat32_write_readback",
        "[PASS] fat32_write_mkdir",
        "[PASS] fat32_write_copy_readback",
        "[PASS] fat32_write_move_readback",
        "[PASS] fat32_write_remove",
        "[PASS] fat32_write_boot_guard",
        "[PASS] fat32_write_complete",
    ])

if EDITOR_TEST:
    required.extend([
        "[PASS] editor opened",
        "[PASS] editor returned to shell",
        "[PASS] editor saved",
        "[PASS] shell accepted input",
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

if EDITOR_TEST:
    editor_order = [
        "[PASS] editor opened",
        "[PASS] editor returned to shell",
        "[PASS] editor opened",
        "[PASS] editor saved",
        "[PASS] editor returned to shell",
        "[PASS] shell accepted input",
    ]
    positions = []
    cursor = 0
    for marker in editor_order:
        position = content.find(marker, cursor)
        positions.append(position)
        if position >= 0:
            cursor = position + len(marker)
    expected_counts = {
        "[PASS] editor opened": 2,
        "[PASS] editor returned to shell": 2,
        "[PASS] editor saved": 1,
        "[PASS] shell accepted input": 1,
    }
    if (any(position < 0 for position in positions) or
            positions != sorted(positions) or
            any(content.count(marker) != count
                for marker, count in expected_counts.items())):
        print("qemu smoke test: FAIL")
        print("editor lifecycle markers were missing, repeated, or out of order")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)
    if editor_boot_digest is not None and image_digest(IMAGE) != editor_boot_digest:
        print("qemu smoke test: FAIL")
        print("editor test modified the Boot image")
        sys.exit(1)
    with tempfile.TemporaryDirectory() as temp_dir:
        saved = Path(temp_dir) / "SAVED.TXT"
        copied = subprocess.run(
            ["mcopy", "-i", str(STORAGE_IMAGE), "::SAVED.TXT", str(saved)],
            cwd=ROOT, capture_output=True, text=True, check=False)
        if copied.returncode != 0 or not saved.is_file() or \
                saved.read_bytes() != b"editor saved":
            print("qemu smoke test: FAIL")
            print("saved editor payload did not persist on the Test image")
            print(copied.stderr.strip())
            sys.exit(1)
    if subprocess.run(
            ["mdir", "-i", str(STORAGE_IMAGE), "::NOSAVE.TXT"],
            cwd=ROOT, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, check=False).returncode == 0:
        print("qemu smoke test: FAIL")
        print("clean quit created NOSAVE.TXT without a save")
        sys.exit(1)
    print("[PASS] editor data persisted on the same disposable Test image")
    print("[PASS] clean quit left NOSAVE.TXT absent")
    print("[PASS] Boot image remained unchanged")

if FILE_MANAGER_TEST:
    if file_manager_boot_digest is None or image_digest(IMAGE) != file_manager_boot_digest:
        print("qemu smoke test: FAIL")
        print("File Manager test modified the Boot image")
        sys.exit(1)
    if (file_manager_canonical_digest is not None and
            image_digest(CANONICAL_STORAGE_IMAGE) != file_manager_canonical_digest):
        print("qemu smoke test: FAIL")
        print("canonical Test image changed during File Manager test")
        sys.exit(1)
    fatal_markers = ("[PANIC]", "#DF", "double fault", "triple fault", "reset")
    if any(marker.lower() in content.lower() for marker in fatal_markers):
        print("qemu smoke test: FAIL")
        print("fatal kernel/QEMU diagnostic appeared during File Manager test")
        print(content)
        sys.exit(1)
    from file_manager_image_checks import verify as verify_file_manager_image
    image_errors = verify_file_manager_image(
        STORAGE_IMAGE, IMAGE, file_manager_boot_digest)
    if image_errors:
        print("qemu smoke test: FAIL")
        for error in image_errors:
            print(f"- {error}")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)
    print("[PASS] File Manager create/rename/delete state persisted on disposable Test image")
    print("[PASS] non-empty directory deletion was refused")
    print("[PASS] Boot image remained byte-for-byte unchanged")
    if file_manager_canonical_digest is not None:
        print("[PASS] canonical Test image remained unchanged")

if FAT32_WRITE_TEST:
    write_markers = required[-8:]
    positions = [content.find(marker) for marker in write_markers]
    if (positions != sorted(positions) or
            any(content.count(marker) != 1 for marker in write_markers) or
            content.count("[BOOT] low_kernel_entry") != 1 or
            "#DF" in content or "double fault" in content.lower() or
            "triple fault" in content.lower() or "reset" in content.lower()):
        print("qemu smoke test: FAIL")
        print("FAT32 write proof markers repeated, out of order, or fatal fault occurred")
        print(content or "(empty)")
        sys.exit(1)
    if (canonical_digest is not None and
            image_digest(CANONICAL_STORAGE_IMAGE) != canonical_digest):
        print("qemu smoke test: FAIL")
        print("canonical FAT32 source image changed during QEMU write test")
        sys.exit(1)
    with tempfile.TemporaryDirectory() as temp_dir:
        for name in ("MOVED.BIN", "COPY.BIN"):
            persisted = Path(temp_dir) / name
            copy = subprocess.run(
                ["mcopy", "-i", str(STORAGE_IMAGE),
                 f"::WRPROOF/{name}", str(persisted)],
                cwd=ROOT, capture_output=True, text=True, check=False)
            if (copy.returncode != 0 or not persisted.is_file() or
                    persisted.read_bytes() != FAT32_WRITE_BYTES):
                print("qemu smoke test: FAIL")
                print(f"persisted {name} bytes differ from QEMU write payload")
                print(copy.stderr.strip())
                sys.exit(1)
    for removed in ("::TOUCH.TXT", "::SCRATCH.TXT", "::EMPTY"):
        if subprocess.run(
                ["mdir", "-i", str(STORAGE_IMAGE), removed],
                cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                check=False).returncode == 0:
            print("qemu smoke test: FAIL")
            print(f"removed Test path still exists: {removed}")
            sys.exit(1)
    print("[PASS] FAT32 write persisted on the same disposable Test image")

if UDP_NETWORK_TEST:
    ordered_markers = [
        "[PASS] rtl8139_detected",
        "[PASS] rtl8139_initialized",
        "[PASS] ethernet_ready",
        "[PASS] arp_ready",
        "[PASS] ipv4_ready",
        "[PASS] icmp_ready",
        "[PASS] udp_ready",
        "[PASS] desktop_online",
        "[PASS] arp_gateway_resolved",
        "[PASS] udp_tx",
        "[PASS] udp_echo_validated",
        "[PASS] udp_rx",
    ]
    positions = [content.find(marker) for marker in ordered_markers]
    bad_order = positions != sorted(positions)
    bad_counts = [marker for marker in ordered_markers
                  if content.count(marker) != 1]
    if bad_order or bad_counts:
        print("qemu smoke test: FAIL")
        if positions[-1] < positions[-2]:
            print("udp_rx preceded callback payload validation")
        if content.count("[PASS] udp_echo_validated") != 1:
            print("udp_echo_validated was not one-shot:",
                  content.count("[PASS] udp_echo_validated"))
        if content.count("[PASS] udp_rx") != 1:
            print("udp_rx was not one-shot:", content.count("[PASS] udp_rx"))
        if not bad_order and not bad_counts:
            print("UDP completion markers were malformed")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)

if DNS_NETWORK_TEST:
    ordered_markers = [
        "[PASS] rtl8139_detected",
        "[PASS] arp_ready",
        "[PASS] udp_ready",
        "[PASS] dns_query_accepted",
        "[PASS] dns_arp_next_hop_resolved 10.0.2.3",
        "[PASS] dns_lookup",
    ]
    positions = [content.find(marker) for marker in ordered_markers]
    if positions != sorted(positions) or any(
            content.count(marker) != 1 for marker in ordered_markers):
        print("qemu smoke test: FAIL")
        print("DNS path markers were missing, repeated, or out of order")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)
    if (content.count("[BOOT] low_kernel_entry") != 1 or
            "#DF" in content or "double fault" in content.lower() or
            "triple fault" in content.lower() or "reset" in content.lower()):
        print("qemu smoke test: FAIL")
        print("DNS test encountered a reset or fatal fault")
        print("--- debug log ---")
        print(content or "(empty)")
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

if PROCESS_PREEMPTION_TEST:
    # These follow the actual state-transition boundaries:
    # host survival is known before redispatch, and selection occurs
    # before the selected userspace process executes its first write.
    preemption_markers = [
        "[pid 1] entered non-yielding loop",
        "[PASS] user quantum expired",
        "[PASS] preempted context captured",
        "[PASS] timer preemption returned to host",
        "[PASS] desktop remained online after preemption",
        "[PASS] non-yielding process was preempted",
        "[pid 2] scheduled by timer preemption",
        "[PASS] preemptive round robin",
    ]

    positions = [content.find(marker) for marker in preemption_markers]

    if (any(position < 0 for position in positions) or
            positions != sorted(positions)):
        print("qemu smoke test: FAIL")
        print("preemption markers did not appear in causal order")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)

    if any(content.count(marker) != 1 for marker in preemption_markers):
        print("qemu smoke test: FAIL")
        print("a one-shot preemption marker had an unexpected count")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)

    if ("#DF" in content or
            "double fault" in content.lower() or
            "triple fault" in content.lower() or
            "reset" in content.lower()):
        print("qemu smoke test: FAIL")
        print("preemption test encountered a reset or fatal fault")
        print("--- debug log ---")
        print(content or "(empty)")
        sys.exit(1)

print("[PASS] Linux95 booted in QEMU")
print("[PASS] x86_64 kernel entered kernel_main")
print("[PASS] kernel initialization reached graphical desktop")
if (WITHOUT_NETWORK or PROCESS_SELF_TEST or PROCESS_FAULT_TEST or
        PROCESS_PREEMPTION_TEST):
    print("[PASS] missing RTL8139 remained non-fatal")
elif WITHOUT_USER_PROGRAMS:
    print("[PASS] RTL8139 network stack initialized")
else:
    print("[PASS] RTL8139 network stack initialized")
    print("[PASS] ARP gateway resolved at 10.0.2.2")
    print("[PASS] ICMP echo reply received from 10.0.2.2")
    if UDP_NETWORK_TEST:
        print("[PASS] UDP echo received through RTL8139")
    if DNS_NETWORK_TEST:
        print("[PASS] DNS A lookup completed through RTL8139")
print("QEMU smoke test: PASS")
