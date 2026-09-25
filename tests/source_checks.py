#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def require(path, needle):
    text = (ROOT / path).read_text()
    if needle not in text:
        raise SystemExit(f"FAIL: {needle!r} missing from {path}")

def require_rtl8139_hardware_isolation():
    rtl_source_path = ROOT / "kernel/drivers/rtl8139.cpp"
    rtl_header_path = ROOT / "kernel/drivers/rtl8139.hpp"

    if not rtl_source_path.is_file():
        raise SystemExit("FAIL: missing file: kernel/drivers/rtl8139.cpp")
    if not rtl_header_path.is_file():
        raise SystemExit("FAIL: missing file: kernel/drivers/rtl8139.hpp")

    for path in (ROOT / "kernel").rglob("*"):
        if path.suffix not in {".cpp", ".hpp"}:
            continue
        text = path.read_text()
        if path != ROOT / "kernel/pci/pci.cpp":
            for port in ("0xCF8", "0xCFC"):
                if port in text:
                    relative = path.relative_to(ROOT)
                    raise SystemExit(
                        f"FAIL: PCI config port {port} outside pci.cpp: {relative}")

    rtl_source = rtl_source_path.read_text()
    register_offsets = (
        "kIdr0 = 0x00",
        "kTxStatus0 = 0x10",
        "kTxAddress0 = 0x20",
        "kRxBufferStart = 0x30",
        "kCommand = 0x37",
        "kCurrentAddress = 0x38",
        "kInterruptMask = 0x3C",
        "kReceiveConfig = 0x44",
        "kConfig1 = 0x52",
    )
    for declaration in register_offsets:
        if declaration not in rtl_source:
            raise SystemExit(
                f"FAIL: RTL8139 register declaration missing: {declaration}")

    for path in (ROOT / "kernel/drivers").glob("rtl8139*.hpp"):
        text = path.read_text().lower()
        if "irq" in text or "interrupt" in text:
            relative = path.relative_to(ROOT)
            raise SystemExit(f"FAIL: RTL8139 IRQ interface introduced: {relative}")

    lower_source = rtl_source.lower()
    if "irq_handler" in lower_source or "interrupt_handler" in lower_source:
        raise SystemExit("FAIL: RTL8139 IRQ handler introduced")

    required_dma_contract = (
        "alignas(4096)\nuint8_t g_rx_buffer[8192 + 16 + 1500]",
        "alignas(4)\nuint8_t g_tx_buffers[4][1536]",
        "memory::kernel_virtual_to_physical",
    )
    for contract in required_dma_contract:
        if contract not in rtl_source:
            raise SystemExit(f"FAIL: RTL8139 DMA contract missing: {contract}")

    for forbidden in ("kKernelRegionBase", "0xFFFFFFFF80000000"):
        if forbidden in rtl_source:
            raise SystemExit(
                f"FAIL: RTL8139 driver duplicates DMA translation: {forbidden}")

def require_network_coordinator_contract():
    network_path = ROOT / "kernel/net/network.cpp"
    if not network_path.is_file():
        raise SystemExit("FAIL: missing file: kernel/net/network.cpp")

    network_source = network_path.read_text()
    required = (
        "Ipv4Address{{10, 0, 2, 15}}",
        "Ipv4Address{{255, 255, 255, 0}}",
        "Ipv4Address{{10, 0, 2, 2}}",
        "kMaxFramesPerPoll = 8",
    )
    for contract in required:
        if contract not in network_source:
            raise SystemExit(
                f"FAIL: network coordinator contract missing: {contract}")

    compact_source = "".join(network_source.split())
    if "while(rtl8139::poll_receive(" in compact_source:
        raise SystemExit("FAIL: unbounded RTL8139 receive loop introduced")

    desktop_source = (ROOT / "kernel/gui/desktop.cpp").read_text()
    if "network::poll();" not in desktop_source:
        raise SystemExit("FAIL: network::poll() missing from graphical runtime loop")

require("kernel/boot_info.hpp", "static_assert(sizeof(FramebufferInfo) == 28")
require("kernel/boot_info.hpp", "static_assert(sizeof(BootInfo) == 45")
require("kernel/boot_info.hpp", "static_assert(sizeof(E820Entry) == 24")
require("kernel/entry.asm", "extern _bss_start")
require("kernel/entry.asm", "rep stosb")
require("kernel/arch/interrupts.asm", "iretq")
require("kernel/arch/interrupts.cpp", "set_gate")
require("kernel/arch/pic.cpp", "0x20")
require("kernel/arch/pit.cpp", "1193182")
require("kernel/arch/x86_64/segments.hpp", "kUserDataSelector = 0x1B")
require("kernel/arch/x86_64/segments.hpp", "kUserCodeSelector = 0x23")
require("kernel/arch/x86_64/segments.asm", "ltr ax")
require("kernel/arch/x86_64/tss.cpp", "void set_tss_rsp0(uint64_t rsp0)")
require("kernel/arch/interrupts.cpp", "set_gate(0x80, int80_entry, 3)")
require("kernel/syscall/int80_entry.asm", "iretq")
require("kernel/process/context.asm", "process_resume_user")
require("kernel/process/context.asm", "process_restore_host")
require("kernel/process/context.asm", "iretq")
require("kernel/process/scheduler.cpp", "bool run_once()")
require("kernel/process/scheduler.cpp", "write_cr3(selected.page_table_physical)")
require("kernel/process/scheduler.cpp", "set_tss_rsp0(selected.kernel_stack_top)")
require("kernel/process/scheduler.cpp", "set_cpu_process")
require("kernel/process/scheduler.cpp", "process_restore_host")
require("kernel/gui/desktop.cpp", "scheduler::run_once()")
require("kernel/syscall/syscall.cpp", "capture_context")
require("kernel/syscall/syscall.cpp", "frame->cs & 3U")
require("kernel/syscall/syscall.cpp", "kMaxWriteLength = 4096")
require("kernel/syscall/syscall.cpp", "memory::validate_user_range")
require("kernel/syscall/syscall.cpp", "memory::copy_from_user")
require("kernel/syscall/syscall.cpp", "initialize_fast_path")
require("kernel/syscall/syscall.cpp", "valid_sysret_target")
require("kernel/syscall/syscall.cpp", "kIa32Efer")
require("kernel/syscall/syscall.cpp", "kIa32Star")
require("kernel/syscall/syscall.cpp", "kIa32Lstar")
require("kernel/syscall/syscall.cpp", "kIa32Fmask")
require("kernel/syscall/syscall.cpp", "frame->rflags")
require("kernel/arch/x86_64/msr.hpp", "rdmsr")
require("kernel/arch/x86_64/msr.hpp", "wrmsr")
syscall_entry_source = (ROOT / "kernel/syscall/syscall_entry.asm").read_text()
for required in ("swapgs", "sysretq", "mov [gs:16], rsp",
                 "mov rsp, [gs:8]", "call syscall_bridge"):
    if required not in syscall_entry_source:
        raise SystemExit(f"FAIL: syscall entry contract missing: {required}")
entry_swapgs = syscall_entry_source.find("swapgs")
entry_save_rsp = syscall_entry_source.find("mov [gs:16], rsp")
entry_load_stack = syscall_entry_source.find("mov rsp, [gs:8]")
entry_call = syscall_entry_source.find("call syscall_bridge")
if not (0 <= entry_swapgs < entry_save_rsp < entry_load_stack < entry_call):
    raise SystemExit("FAIL: syscall stack switch does not precede dispatcher call")
context_source = (ROOT / "kernel/process/context.asm").read_text()
if "process_resume_user" not in context_source or "iretq" not in context_source:
    raise SystemExit("FAIL: Ring 3 IRET resume path missing")
if "reinterpret_cast<const char*>(frame.rsi)" in (
        ROOT / "kernel/syscall/syscall.cpp").read_text():
    raise SystemExit("FAIL: raw userspace pointer dereference in syscall write")
require("kernel/arch/keyboard.cpp", "kBufferSize = 128")
require("kernel/terminal/shell.cpp", '"help"')
require("kernel/terminal/shell.cpp", '"reboot"')
require("kernel/terminal/shell.cpp", "Linux95 Kernel v1.0 Storage Foundation")
require("kernel/kernel.cpp", "[PASS] higher_half_entry")
require("kernel/kernel.cpp", "[PASS] memory_self_test")
require("linker.ld", ". = 0x100000;")
require("kernel/memory/user_space.cpp", "entry = physical | kPresentBit | kWriteBit | kUserBit;")
require("kernel/memory/user_space.cpp", "destination[i] = source[i] & ~kUserBit;")
require("kernel/memory/user_space.cpp", "validate_user_range")
require("kernel/memory/user_space.cpp", "for (uint64_t page = span.first_page;")

require_rtl8139_hardware_isolation()
require_network_coordinator_contract()

if "Linux95 Kernel v0.1" in (ROOT / "kernel/kernel.cpp").read_text():
    raise SystemExit("FAIL: stale v0.1 kernel banner found")

print("source checks: PASS")
require(
    "kernel/arch/interrupts.cpp",
    "entry.selector = arch::x86_64::kKernelCodeSelector;",
)
