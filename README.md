# Linux95 Kernel

Linux95 Kernel is an experimental x86_64 freestanding C++ kernel with a custom legacy BIOS bootloader.

## v1.0 Storage Foundation (development milestone)

This branch builds on the proven `v1.0-memory-foundation` checkpoint and adds the first Linux95 storage subsystem while preserving the higher-half memory architecture.

Verified architecture in this milestone:

- custom Stage 1 / Stage 2 legacy BIOS boot path
- x86_64 long mode
- low bootstrap entry at physical `0x00100000`
- higher-half kernel execution at `0xFFFFFFFF80100000`
- higher-half direct physical map (HHDM) at `0xFFFF800000000000`
- BIOS E820-backed 4 KiB physical page allocator
- 4 KiB virtual-memory map / translate / unmap operations
- memory self-tests
- primary IDE ATA PIO polling driver
- LBA28 addressing
- ATA IDENTIFY
- 512-byte sector reads
- slave-only 512-byte sector writes
- ATA cache flush
- kernel-enforced primary-master write guard
- disposable 16 MiB QEMU primary-slave storage test disk
- automated write / read / compare / restore storage self-test
- IDT and exception handling
- legacy PIC + 100 Hz PIT
- interrupt-driven PS/2 keyboard
- VGA text terminal and `linux95>` shell
- QEMU debug-port checkpoints and automated smoke testing

The Linux95 boot image is attached as the primary IDE master and is treated as read-only by the kernel. A disposable QEMU test image is attached as the primary IDE slave and is the only writable disk in this milestone.

## Shell commands

- `help`
- `clear`
- `version`
- `mem`
- `diskinfo`
- `uptime`
- `reboot`

`mem` reports E820 information, physical page counts, page size, HHDM base, CR3, and heap usage.

`diskinfo` reports the primary master boot disk and primary slave test disk, including ATA model, LBA28 sector count, presence, and write policy.

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

`make test-qemu` recreates `build/linux95-storage-test.img` as a fresh 16 MiB raw disk before booting QEMU.

For a visible QEMU window:

```bash
make run
```

The automated QEMU test requires the memory checkpoints plus these storage checkpoints:

```text
[PASS] ata_master_identify
[PASS] ata_slave_identify
[PASS] ata_read
[PASS] ata_write
[PASS] ata_restore
[PASS] master_write_guard
[PASS] storage_self_test
[PASS] shell_ready
```

## Scope

This is still a small standalone experimental kernel. This milestone does **not** yet include FAT/filesystem support, partition parsing, AHCI, DMA, IRQ-driven ATA, NVMe, USB storage, networking, audio, SMP, user mode, processes, or a graphical desktop.

FAT/filesystem support is the next storage milestone after the ATA foundation is verified.

It is not Linux ABI compatible and is separate from the Debian-based Linux95 distribution.

## License

Linux95 Kernel is released under the MIT License. See [`LICENSE`](LICENSE).
