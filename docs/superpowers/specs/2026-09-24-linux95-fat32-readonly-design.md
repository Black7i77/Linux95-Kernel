# Linux95 FAT32 Read-Only Foundation

Date: 2026-09-24
Branch: v1.0-fat32-dev
Base: v1.0-dev

## Goal

Add the first filesystem subsystem to Linux95 Kernel.

Linux95 will mount a FAT32 filesystem from the QEMU IDE slave disk and
provide safe read-only access to files and directories.

## Scope

This milestone includes:

- FAT32 superfloppy at LBA 0
- 512-byte sectors
- FAT32 BPB validation
- cluster-to-LBA translation
- FAT chain traversal
- root directories
- subdirectories
- DOS 8.3 filenames
- case-insensitive lookup
- file reading
- shell commands: fsinfo, ls, cat
- host tests
- QEMU filesystem tests

This milestone does NOT include:

- FAT writes
- file creation or deletion
- long filenames
- MBR or GPT
- full VFS
- permissions
- userspace file descriptors

## Disk Layout

Primary IDE master:
- linux95-kernel.img
- boot disk
- read-only kernel policy

Primary IDE slave:
- linux95-storage-test.img
- 64 MiB FAT32 superfloppy
- existing ATA self-test may write/restore its test sector
- filesystem access is read-only

## Architecture

ATA PIO
  -> storage::read_sector()
  -> FAT32 backend
  -> read-only filesystem facade
  -> shell

The shell must not depend directly on ATA registers or raw FAT details.


## Filesystem Interface

The shell will use a small read-only filesystem interface instead of
calling FAT32 directly.

Expected public operations:

- initialize filesystem
- query volume information
- list a directory
- read a file
- return structured status codes

Possible statuses include:

- Ok
- NotMounted
- IoError
- InvalidFilesystem
- NotFound
- NotDirectory
- IsDirectory
- Corrupt
- Unsupported

There will be no filesystem write API.

## FAT32 Volume State

The FAT32 backend stores validated geometry including:

- disk identifier
- bytes per sector
- sectors per cluster
- reserved sector count
- FAT count
- sectors per FAT
- total sectors
- FAT starting LBA
- first data LBA
- root cluster
- cluster count
- mounted state

The FAT32 volume mounts only:

storage::DiskId::Test

The Linux95 boot disk is not used as the FAT32 volume.

## Boot Sector Validation

Mounting begins by reading sector 0 of the slave disk.

The FAT32 BPB must pass these checks:

- boot signature is 0x55AA
- bytes per sector equals 512
- sectors per cluster is non-zero
- sectors per cluster is a power of two
- reserved sector count is non-zero
- FAT count is non-zero
- FAT32 sectors-per-FAT is non-zero
- total sector count is non-zero
- root cluster is at least 2
- filesystem geometry arithmetic does not overflow
- FAT metadata remains inside the disk
- data region remains inside the disk
- calculated LBAs remain inside the ATA device size

Invalid metadata must fail the mount cleanly.

Partially validated filesystem state must never become active.

## FAT Entry Handling

FAT32 entries are 32-bit little-endian values.

Only the lower 28 bits are significant:

value = value & 0x0FFFFFFF

Important values:

- 0x00000000 means free cluster
- normal allocated clusters begin at 2
- 0x0FFFFFF7 means bad cluster
- 0x0FFFFFF8 through 0x0FFFFFFF mean end of chain

Free, reserved, bad, or out-of-range clusters found in an active file
chain are treated as filesystem corruption.

## Cluster to LBA Translation

For cluster N:

LBA = first_data_lba + (N - 2) * sectors_per_cluster

Every calculation must be bounds checked.

A cluster must:

- be at least cluster 2
- be inside the valid cluster range
- map completely inside the FAT32 volume
- map completely inside the ATA disk

## Cluster Chain Safety

Linux95 must never hang on a damaged FAT.

Every cluster traversal will have a maximum step count based on the
validated cluster count.

This protects against:

- self-referencing clusters
- multi-cluster loops
- invalid FAT entries
- corrupted images

If the traversal limit is exceeded, the operation returns Corrupt.

## Directory Support

The first FAT32 version supports standard 32-byte short directory
entries.

Supported:

- normal files
- directories
- root directory
- nested directories
- DOS 8.3 names

Ignored safely:

- deleted entries
- FAT long-filename entries
- volume labels
- unused directory entries

Examples:

README  TXT becomes README.TXT
KERNEL  TXT becomes KERNEL.TXT
DOCS        becomes DOCS

Filename lookup will be ASCII case-insensitive.


## Path Handling

Supported examples:

/
README.TXT
/README.TXT
DOCS
DOCS/KERNEL.TXT
/DOCS/KERNEL.TXT

Paths use slash-separated DOS 8.3 components.

Malformed paths and empty intermediate components are rejected.

Dot and dot-dot traversal are not exposed in this milestone.

## File Reading

The FAT32 reader supports:

- zero-byte files
- single-cluster files
- multi-cluster files
- reads beginning at a non-zero offset
- reads crossing sector boundaries
- reads crossing cluster boundaries
- reads ending at EOF

The caller supplies the destination buffer.

The filesystem reports:

- bytes actually read
- complete file size
- structured error status

A read beginning at or beyond EOF returns zero bytes.

The FAT32 implementation never writes filesystem sectors.

## Shell Commands

Three filesystem commands are added.

### fsinfo

Displays FAT32 volume information.

Example:

FAT32 volume:
  Disk: test/slave
  Mounted: yes
  Bytes/sector: 512
  Sectors/cluster: 1
  FATs: 2
  Root cluster: 2
  Mode: read-only

### ls

With no argument:

ls

lists the root directory.

Examples:

ls
ls DOCS

Possible output:

README.TXT
CHAIN.TXT
DOCS/

### cat

Reads a text file.

Examples:

cat README.TXT
cat DOCS/KERNEL.TXT

cat uses a bounded buffer and streams the file instead of loading the
entire file into memory.

## Shell Parser

The current fixed-command shell will gain a minimal parser supporting:

command

and:

command argument

Examples:

ls
ls DOCS
cat README.TXT

This milestone does not add:

- quoting
- pipes
- redirection
- wildcards
- variables
- scripting

## FAT32 Test Image

The disposable QEMU slave disk becomes:

build/linux95-storage-test.img

Size:

64 MiB

Filesystem:

FAT32 superfloppy starting at LBA 0

Host tools:

mkfs.fat
mtools

The image contains deterministic files:

README.TXT
CHAIN.TXT
DOCS/KERNEL.TXT

README.TXT contains a known short message.

CHAIN.TXT is deliberately larger than one FAT32 cluster so Linux95 must
follow a FAT chain to read the complete file.

DOCS/KERNEL.TXT verifies subdirectory traversal.

## Build Dependencies

Normal Linux95 kernel compilation does not depend on FAT formatting
tools.

The QEMU filesystem test path requires:

- dosfstools
- mtools

GitHub Actions will install those packages together with the existing
Linux95 build and QEMU dependencies.

## Boot Integration

The boot sequence becomes:

memory initialization
higher-half transition
physical allocator
virtual-memory validation
memory self-test
ATA/storage self-test
heap initialization
FAT32 mount
filesystem self-test
interrupt initialization
PIC initialization
PIT initialization
keyboard initialization
shell

The ATA storage self-test must completely restore its temporary sector
before the FAT32 filesystem is mounted.

## Filesystem Self-Test

The kernel QEMU self-test verifies:

1. FAT32 volume mounts successfully.
2. BPB geometry is valid.
3. Root directory can be enumerated.
4. README.TXT can be found.
5. README.TXT contents are correct.
6. DOCS is recognized as a directory.
7. DOCS/KERNEL.TXT can be found and read.
8. CHAIN.TXT requires multiple clusters.
9. The FAT cluster chain ends correctly.
10. No FAT32 write operation exists.

Required debug markers:

[PASS] fat32_mount
[PASS] fat32_root_list
[PASS] fat32_file_lookup
[PASS] fat32_file_read
[PASS] fat32_subdirectory
[PASS] fat32_cluster_chain
[PASS] filesystem_self_test
[PASS] shell_ready

Filesystem failures emit a specific FAIL marker before startup fails.

## Host Tests

Hardware-independent host tests cover:

- little-endian FAT32 field decoding
- power-of-two sectors-per-cluster validation
- FAT entry 28-bit masking
- end-of-chain detection
- free cluster rejection
- bad cluster rejection
- reserved cluster rejection
- cluster-to-LBA arithmetic
- arithmetic overflow protection
- DOS 8.3 filename formatting
- ASCII case-insensitive comparison
- directory attribute parsing
- invalid cluster handling

Pure parser helpers remain separate from ATA hardware access.

## Source Checks

Automated source checks verify:

- FAT32 module exists
- filesystem facade exists
- FAT32 uses storage::read_sector()
- FAT32 never calls storage::write_sector()
- long filename entries are ignored safely
- FAT values are masked to 28 bits
- cluster traversal is bounded
- shell provides fsinfo
- shell provides ls
- shell provides cat

## QEMU Test Flow

make test-qemu will:

1. Build Linux95.
2. Create a fresh 64 MiB FAT32 test image.
3. Populate deterministic fixture files.
4. Boot linux95-kernel.img as IDE master.
5. Attach the FAT32 disk as IDE slave.
6. Capture debug port 0xE9.
7. Require all existing memory test markers.
8. Require all existing ATA/storage markers.
9. Require all FAT32 markers.
10. Fail if any PANIC marker appears.
11. Succeed only when shell_ready is reached.

Existing memory and storage tests remain mandatory.

## Read-Only Guarantee

The filesystem API exposes no operation for:

- writing files
- creating files
- deleting files
- renaming files
- truncating files
- creating directories
- deleting directories

The FAT32 implementation must never call:

storage::write_sector()

The existing ATA storage self-test remains the only component allowed
to temporarily write to the slave disk.

It restores its original data before FAT32 is mounted.

## Error Handling

Expected filesystem errors use status values instead of kernel panics.

Examples include:

- NotMounted
- IoError
- InvalidFilesystem
- NotFound
- NotDirectory
- IsDirectory
- Corrupt
- Unsupported

A bad user path must not panic Linux95.

A corrupt FAT must not cause:

- infinite loops
- integer overflow
- out-of-range LBAs
- reads outside the disk

Boot-time filesystem self-test failure may stop startup because the QEMU
fixture is expected to be valid.

## Non-Goals

This milestone does not implement:

- FAT32 writes
- long filenames
- Unicode filenames
- timestamps
- MBR
- GPT
- full VFS mount trees
- ext2 or ext4
- custom Linux95 filesystem
- multiple mounted volumes
- permissions
- ownership
- process file descriptors
- filesystem syscalls
- AHCI
- NVMe
- USB storage

## Completion Criteria

The FAT32 foundation is complete only when:

- Linux95 builds with warnings treated as errors
- existing memory tests pass
- existing ATA tests pass
- existing storage tests pass
- valid FAT32 volume mounts
- invalid BPB data is rejected
- cluster calculations are bounds checked
- corrupt cluster chains cannot hang Linux95
- root directory listing works
- subdirectory listing works
- 8.3 lookup is case-insensitive
- single-cluster file reads work
- multi-cluster file reads work
- fsinfo works
- ls works
- cat works
- FAT32 code performs no writes
- make test passes
- make test-qemu passes
- GitHub Actions passes
- interactive QEMU reaches the Linux95 shell

## Later Milestones

Possible future filesystem work:

1. FAT32 long filename support
2. MBR and GPT parsing
3. Writable FAT32
4. Full Linux95 VFS
5. Additional filesystems
6. Userspace filesystem syscalls
