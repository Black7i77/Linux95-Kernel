# Linux95 Kernel

Linux95 Kernel is an experimental x86_64 freestanding C++ kernel with a custom legacy BIOS bootloader.

## v1.0 VFS Foundation (development milestone)

This branch builds on the Linux95 storage and FAT32 foundation and adds the first read-only virtual filesystem (VFS) while preserving the higher-half memory architecture.

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
- read-only VFS layered above the filesystem/FAT32 backend
- regular-file descriptors `3..63`; fd `0`, `1`, and `2` remain reserved
- exactly 61 simultaneous regular-file descriptors
- separate 32-slot directory-handle table
- VFS `open` / `read` / `close` / `stat` / `fstat`
- VFS `opendir` / `readdir` / `closedir`
- lowest-free descriptor and directory-handle reuse
- per-descriptor file offsets with successful-read-only advancement
- stable directory cursor on backend error and EOF
- maximum VFS path length of 127 characters plus terminator
- shell `ls` and `cat` file access routed exclusively through VFS
- VFS boot-time self-tests
- FAT/filesystem/VFS path is intentionally read-only in this milestone
- IDT and exception handling
- legacy PIC + 100 Hz PIT
- interrupt-driven PS/2 keyboard
- VBE 1280x720x32 graphical desktop with VGA text-shell fallback
- QEMU debug-port checkpoints and automated smoke testing
- polling RTL8139 Ethernet networking under QEMU
- static IPv4, ARP, and ICMP Echo support

The Linux95 boot image is attached as the primary IDE master and is treated as read-only by the kernel. A disposable QEMU test image is attached as the primary IDE slave and is the only writable disk in this milestone.


## Graphics desktop

Linux95 v1.0 boots into a graphical desktop when the BIOS/VBE framebuffer handoff is available.

Current desktop features include:

- 1280x720x32 VBE framebuffer
- freestanding software renderer
- PS/2 mouse support
- top panel and Applications menu
- Terminal and System Info applications
- focus, drag, resize, minimize, close, and restore
- uptime/status display
- VGA text-shell fallback when framebuffer graphics are unavailable
- QEMU graphical smoke testing with standard VGA

The graphical boot path reports `[PASS] framebuffer_mapped`, `[PASS] renderer_online`, and `[PASS] desktop_online` through the debug port.

Linux95 remains a legacy BIOS/VBE project for this milestone.

## Experimental userspace

Linux95 experimentally supports static ELF64 x86_64 processes in Ring 3.
Each process has a separate user address space and participates in bounded,
cooperative round-robin scheduling from the desktop host. The initial syscall
set provides `write`, `yield`, and `exit` through both `int 0x80` and the
x86_64 `syscall`/`sysretq` path. Fatal user exceptions are isolated to the
offending process.

Initial user-process startup is non-fatal. If the initial user programs are
missing or safely rejected, Linux95 reports userspace offline and continues
to the desktop with storage, input, and otherwise-healthy networking
available.

This is not a general-purpose process environment. It has no preemption,
`fork`/`clone`, threads, dynamic linking, shared libraries, or full POSIX
compatibility.

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
- `ip`
- `ping <IPv4 address>`
- `reboot`

`mem` reports E820 information, physical page counts, page size, HHDM base, CR3, and heap usage.

`diskinfo` reports the primary master boot disk and primary slave test disk, including ATA model, LBA28 sector count, presence, and write policy.

`fsinfo` reports the mounted FAT32 geometry and read-only mode.

`ls [path]` opens directories through the read-only VFS and lists DOS 8.3 entries one at a time with `readdir`. `ls` with no path lists `/`.

`cat <path>` opens and reads files through VFS file descriptors in bounded chunks. Examples include `cat README.TXT` and `cat DOCS/KERNEL.TXT`.

## Networking

This milestone supports QEMU's emulated RTL8139 adapter using a polling-only driver. Both `make run` and `make run-debug` configure QEMU with:

```text
-netdev user,id=net0
-device rtl8139,netdev=net0
```

The interface uses a fixed configuration:

- IPv4 address: `10.0.2.15`
- netmask: `255.255.255.0`
- gateway: `10.0.2.2`

Use `ip` to display interface state, the RTL8139 MAC address, and the static IPv4 configuration. Use `ping <IPv4 address>` to start an asynchronous ICMP Echo request; for example, `ping 10.0.2.2` reaches QEMU's user-network gateway without blocking the desktop loop.

The QEMU smoke build enables `LINUX95_QEMU_NETWORK_SELF_TEST` in a separate test-only kernel image. Normal builds never ping automatically. The automated test also boots a second mode without RTL8139 and verifies that the desktop still starts offline.

The process-focused QEMU tests also cover two-process cooperative execution,
user-fault isolation, and desktop startup with `/USER/INIT.ELF` and
`/USER/WORKER.ELF` deliberately absent from the FAT fixture.

Networking is intentionally limited to RTL8139/QEMU, Ethernet II, ARP, static IPv4, and ICMP Echo. DHCP, DNS, TCP, IPv6, Wi-Fi, and general socket APIs are not implemented yet.

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

`make test-qemu` recreates `build/linux95-storage-test.img` as a deterministic 64 MiB FAT32 superfloppy, proves a real RTL8139 ARP/ICMP exchange, and then proves desktop boot without a network device.

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
[PASS] vfs_initialize
[PASS] vfs_file_open
[PASS] vfs_file_read
[PASS] vfs_stat
[PASS] vfs_directory_open
[PASS] vfs_readdir
[PASS] vfs_self_test
[PASS] shell_ready
```

## Scope

This is still a small standalone experimental kernel. The VFS and FAT32 layers are intentionally read-only, use DOS 8.3 names, and do not provide a current-working-directory or `chdir` model. The VFS exposes no write/create/delete API. It does **not** include writable FAT operations, long file names, partition parsing, AHCI, IRQ-driven ATA, NVMe, USB networking, DHCP, DNS, TCP, Wi-Fi, audio, SMP, preemptive process scheduling, threads, dynamic linking, shared libraries, or full POSIX compatibility.

It is not Linux ABI compatible and is separate from the Debian-based Linux95 distribution.

## License

Linux95 Kernel is released under the MIT License. See [`LICENSE`](LICENSE).
