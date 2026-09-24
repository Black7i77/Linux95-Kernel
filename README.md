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
- deterministic 64 MiB FAT32 primary-slave test disk
- automated write / read / compare / restore ATA storage self-test
- read-only FAT32 filesystem mounted from the primary IDE slave
- validated FAT32 BPB and 512-byte sectors
- DOS 8.3 case-insensitive file lookup
- root and subdirectory traversal
- bounded multi-cluster FAT-chain reads
- FAT32 boot-time filesystem self-tests
- FAT/filesystem path is intentionally read-only in this milestone
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
- `fsinfo`
- `ls [path]`
- `cat <path>`
- `uptime`
- `reboot`

`mem` reports E820 information, physical page counts, page size, HHDM base, CR3, and heap usage.

`diskinfo` reports the primary master boot disk and primary slave test disk, including ATA model, LBA28 sector count, presence, and write policy.

`fsinfo` reports the mounted FAT32 geometry and read-only mode.

`ls [path]` lists DOS 8.3 entries in the FAT32 root or a subdirectory. `ls` with no path lists `/`.

`cat <path>` reads a file through the read-only filesystem layer in bounded chunks. Examples include `cat README.TXT` and `cat DOCS/KERNEL.TXT`.

## Build and verify

Requirements:

- NASM
- GNU g++
- GNU ld / binutils
- GNU make
- Python 3
- QEMU x86_64
- dosfstools (`mkfs.fat`)
- mtools (`mmd`, `mcopy`, `mdir`, `mtype`)

Run the full development verification flow:

```bash
make clean
make
make test
make test-qemu
```

`make test-qemu` recreates `build/linux95-storage-test.img` as a deterministic 64 MiB FAT32 superfloppy before booting QEMU.

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
[PASS] fat32_mount
[PASS] fat32_root_list
[PASS] fat32_file_lookup
[PASS] fat32_file_read
[PASS] fat32_subdirectory
[PASS] fat32_cluster_chain
[PASS] filesystem_self_test
[PASS] shell_ready
```

## Scope

This is still a small standalone experimental kernel. The FAT32 layer is intentionally read-only and limited to DOS 8.3 names. It does **not** include writable FAT operations, long file names, partition parsing, AHCI, DMA, IRQ-driven ATA, NVMe, USB storage, networking, audio, SMP, user mode, processes, or a graphical desktop.

It is not Linux ABI compatible and is separate from the Debian-based Linux95 distribution.
