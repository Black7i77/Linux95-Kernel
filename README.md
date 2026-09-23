# Linux95 Kernel

Linux95 Kernel is an experimental x86_64 freestanding C++ kernel with a custom legacy BIOS bootloader.

## v0.2 Interactive

The v0.2 milestone adds:

- x86_64 IDT and CPU exception handling
- legacy PIC interrupt controller
- 100 Hz PIT timer
- interrupt-driven PS/2 keyboard input
- BIOS E820 memory statistics
- simple aligned kernel heap
- VGA text console with scrolling and backspace
- built-in `linux95>` shell

Commands:

- `help`
- `clear`
- `version`
- `mem`
- `uptime`
- `reboot`

## Build

Requirements:

- NASM
- GNU g++
- GNU ld / binutils
- GNU make
- Python 3
- QEMU x86_64

Build and test:

```bash
make clean
make
make test
```

Run:

```bash
make run
```

The kernel is standalone. It is not Linux ABI compatible and is not a replacement for the Linux kernel used by the Debian-based Linux95 distribution.
