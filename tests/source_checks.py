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
require("kernel/arch/keyboard.cpp", "kBufferSize = 128")
require("kernel/terminal/shell.cpp", '"help"')
require("kernel/terminal/shell.cpp", '"reboot"')
require("kernel/terminal/shell.cpp", "Linux95 Kernel v1.0 Storage Foundation")
require("kernel/kernel.cpp", "[PASS] higher_half_entry")
require("kernel/kernel.cpp", "[PASS] memory_self_test")
require("linker.ld", ". = 0x100000;")

require_rtl8139_hardware_isolation()
require_network_coordinator_contract()

if "Linux95 Kernel v0.1" in (ROOT / "kernel/kernel.cpp").read_text():
    raise SystemExit("FAIL: stale v0.1 kernel banner found")

print("source checks: PASS")
