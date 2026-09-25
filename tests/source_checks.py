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
require("kernel/process/scheduler.cpp", "asm volatile(\"cli\"")
require("kernel/process/scheduler.cpp", "valid_sysret_context")
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
require("kernel/syscall/syscall.cpp", "sanitize_user_rflags")
require("kernel/syscall/syscall.cpp", "sanitize_user_rflags(frame->rflags)")
require("kernel/syscall/syscall.cpp", "valid_sysret_context")
require("kernel/syscall/syscall.cpp", "dispatch_control")
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
if "1 << 8 | 1 << 10 | 3 << 12" not in context_source or "or r11, 0x202" not in context_source:
    raise SystemExit("FAIL: SYSRET resume does not preserve safe user IF")
interrupts_source = (ROOT / "kernel/arch/interrupts.cpp").read_text()
if not all(token in interrupts_source for token in (
        "set_gate(2, isr_stub_table[2], 0, 1)",
        "set_gate(8, isr_stub_table[8], 0, 2)",
        "set_gate(18, isr_stub_table[18], 0, 3)")):
    raise SystemExit("FAIL: non-maskable/fatal entries lack dedicated IST stacks")
resume_call = (ROOT / "kernel/process/scheduler.cpp").read_text()
if not (resume_call.find('asm volatile("cli"') <
        resume_call.find("process::process_resume_user")):
    raise SystemExit("FAIL: interrupts are not disabled before Ring 3 transition")
syscall_source = (ROOT / "kernel/syscall/syscall.cpp").read_text()
bridge_source = syscall_source[syscall_source.index("extern \"C\" uint64_t syscall_bridge"):]
yield_branch = bridge_source.find("result.action == linux95::syscall::Action::YieldToHost")
yield_validation = bridge_source.find("valid_sysret_context(process.context)")
host_return = bridge_source.find("scheduler::return_to_host(", yield_validation)
if not (0 <= yield_branch < yield_validation < host_return):
    raise SystemExit("FAIL: yielded SYSRET context is not validated before host return")
user_wrapper_source = (ROOT / "user/include/linux95_syscall.hpp").read_text()
if "write_syscall" in user_wrapper_source and not all(value in user_wrapper_source for value in (
        "syscall_call(0, 1, reinterpret_cast<long>(data)",
        "int80_call(0, 1, reinterpret_cast<long>(data)")):
    raise SystemExit("FAIL: write wrappers do not pass fd, buffer, length")
require("kernel/user/elf.cpp", "PT_DYNAMIC")
require("kernel/user/elf.cpp", "ElfStatus::Unsupported")
require("kernel/user/elf.cpp", "heap::rewind")
require("kernel/arch/x86_64/control_regs.hpp", "disable_user_fp_state")
require("kernel/syscall/syscall.cpp", "disable_user_fp_state")
require("kernel/arch/x86_64/tss.cpp", "g_tss.ist1")
require("kernel/arch/x86_64/tss.cpp", "g_tss.ist2")
require("kernel/arch/x86_64/tss.cpp", "g_tss.ist3")
makefile_source = (ROOT / "Makefile").read_text()
if "$(STORAGE_TEST_IMAGE): tests/prepare_fat32_image.py $(BUILD)/user/init.elf $(BUILD)/user/worker.elf" not in makefile_source:
    raise SystemExit("FAIL: storage fixture lacks user ELF build prerequisites")
if "prepare-storage-test-image: $(STORAGE_TEST_IMAGE)" not in makefile_source:
    raise SystemExit("FAIL: storage fixture target is not dependency-ordered")
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

interrupt_source = (ROOT / "kernel/arch/interrupts.cpp").read_text()
if "const bool from_user = (frame->cs & 0x3U) == 0x3U;" not in interrupt_source:
    raise SystemExit("FAIL: exception origin is not classified from saved CS")
require("kernel/arch/interrupts.cpp", "process::handle_user_fault(")
require("kernel/arch/interrupts.cpp", "panic::exception(frame->vector, frame->error_code);")
require("kernel/arch/interrupts.asm", "ISR_ERR   13")
require("kernel/arch/interrupts.asm", "ISR_ERR   14")
require("kernel/arch/interrupts.asm", "ISR_NOERR 6")
require("kernel/process/process.hpp", "bool handle_user_fault(uint8_t vector,")
require("kernel/process/scheduler.cpp", "asm volatile(\"mov %%cr2, %0\"")
require("kernel/process/scheduler.cpp", "scheduler::HostReason::Fault")
require("kernel/process/scheduler.cpp", "[PASS] kernel survived user fault")
require("kernel/process/scheduler.cpp", "[PASS] faulty process terminated")
require("kernel/arch/interrupts.hpp", "uint64_t rsp;")
require("kernel/arch/interrupts.hpp", "uint64_t ss;")
require("user/fault/main.cpp", "0x0000500000000000ULL")
require("tests/prepare_fat32_image.py", "--process-fault")
require("tests/prepare_fat32_image.py", "::USER/FAULT.ELF")
require("tests/qemu_smoke.py", "--process-fault-test")
require("tests/qemu_smoke.py", "[PASS] user fault captured")
require("tests/qemu_smoke.py", "[PASS] kernel survived user fault")

makefile_text = (ROOT / "Makefile").read_text()
normal_build_line = next(
    line for line in makefile_text.splitlines() if line.startswith("all:")
)
if "fault" in normal_build_line:
    raise SystemExit("FAIL: FAULT.ELF leaked into the normal build")
kernel_startup = (ROOT / "kernel/kernel.cpp").read_text()
if "FAULT.ELF" in kernel_startup:
    raise SystemExit("FAIL: FAULT.ELF leaked into normal process autostart")

require_rtl8139_hardware_isolation()
require_network_coordinator_contract()

if "Linux95 Kernel v0.1" in (ROOT / "kernel/kernel.cpp").read_text():
    raise SystemExit("FAIL: stale v0.1 kernel banner found")

print("source checks: PASS")
require(
    "kernel/arch/interrupts.cpp",
    "entry.selector = arch::x86_64::kKernelCodeSelector;",
)
