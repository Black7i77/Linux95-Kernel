# Linux95 Kernel

Linux95 Kernel is an experimental x86_64 freestanding C++ kernel with a custom legacy BIOS bootloader.

## v1.0 Memory Foundation (development milestone)

This branch builds on the proven v0.2 interactive kernel and adds the first major v1.0 memory architecture:

- custom Stage 1 / Stage 2 legacy BIOS boot path
- x86_64 long mode
- low bootstrap entry at physical `0x00100000`
- higher-half kernel execution at `0xFFFFFFFF80100000`
- higher-half direct physical map (HHDM) at `0xFFFF800000000000`
- 2 MiB bootstrap mappings for the transition
- BIOS E820-backed 4 KiB physical page allocator
- separate used/reserved page bitmaps
- 4 KiB virtual-memory map / translate / unmap operations
- memory self-tests that exercise allocation, HHDM access, mappings, translation, unmapping, and accounting
- IDT and exception handling
- legacy PIC + 100 Hz PIT
- interrupt-driven PS/2 keyboard
- VGA text terminal and `linux95>` shell
- QEMU debug-port checkpoints and automated smoke testing

The low `0..2 MiB` identity mapping is intentionally retained in this milestone for the bootstrap stack and legacy interrupt/bootstrap structures. Removing that dependency is a later cleanup step.

## Shell commands

- `help`
- `clear`
- `version`
- `mem`
- `uptime`
- `reboot`

`mem` now reports E820 memory information, physical page counts, page size, HHDM base, CR3, and heap usage.

## Build and verify

Requirements:

- NASM
- GNU g++
- GNU ld / binutils
- GNU make
- Python 3
- QEMU x86_64

Run the full development verification flow:

```bash
make clean
make
make test
make test-qemu
```

For a visible QEMU window:

```bash
make run
```

The automated QEMU test requires these checkpoints in order:

```text
[BOOT] low_kernel_entry
[PASS] bootstrap_tables_created
[PASS] cr3_reloaded
[PASS] higher_half_entry
[PASS] hhdm_online
[PASS] physical_allocator_online
[PASS] virtual_memory_online
[PASS] memory_self_test
[PASS] shell_ready
```

## Scope

This is still a small standalone experimental kernel. This milestone does **not** yet include ATA storage, a filesystem, networking, USB, audio, SMP, user mode, processes, or a graphical desktop.

It is not Linux ABI compatible and is separate from the Debian-based Linux95 distribution.
