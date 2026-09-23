# Linux95 Kernel v1.0 Storage Foundation Design

Date: 2026-09-23  
Branch: `v1.0-storage-dev`  
Base: `v1.0-memory-foundation`

## Goal

Add the first persistent-storage subsystem to Linux95 Kernel while preserving the known-good higher-half memory foundation.

This milestone adds:

- Primary IDE controller ATA PIO polling.
- LBA28 addressing.
- ATA IDENTIFY.
- 512-byte sector reads.
- 512-byte sector writes.
- Cache flush.
- A hard safety rule that forbids writes to the primary master boot disk.
- A disposable QEMU primary-slave test disk used for automated write/read verification.
- A `diskinfo` shell command.
- Storage-specific automated QEMU checkpoints and failure reporting.

This milestone does not add FAT, partitions, AHCI, DMA, IRQ-driven ATA, NVMe, USB storage, or real-hardware write support.

## Safety Model

The QEMU storage layout is:

```text
Primary IDE controller

Master:
  Linux95 boot image
  Kernel access: READ ONLY

Slave:
  Disposable QEMU test disk
  Kernel access: READ + WRITE
```

The kernel API itself enforces the write restriction. A caller cannot write to the master by passing the wrong flag.

`write_sector(Drive::Master, ...)` must fail without issuing an ATA write command.

The automated storage self-test writes only to the slave test disk.

## Hardware Scope

Controller:

- Primary legacy IDE controller only.
- Command block base: `0x1F0`.
- Control block base: `0x3F6`.

Registers:

- `0x1F0` data
- `0x1F1` error/features
- `0x1F2` sector count
- `0x1F3` LBA low
- `0x1F4` LBA mid
- `0x1F5` LBA high
- `0x1F6` drive/head
- `0x1F7` command/status
- `0x3F6` alternate status/device control

Addressing:

- LBA28 only.
- Maximum addressable sector: `0x0FFFFFFF`.
- Sector size: 512 bytes.
- One-sector operations for this milestone.

## Driver API

Files:

```text
kernel/storage/ata.hpp
kernel/storage/ata.cpp
kernel/storage/disk.hpp
kernel/storage/disk.cpp
kernel/storage/storage_self_test.hpp
kernel/storage/storage_self_test.cpp
```

### ATA layer

```cpp
namespace linux95::storage::ata {

enum class Drive : uint8_t {
    Master = 0,
    Slave = 1,
};

struct DeviceInfo {
    bool present;
    bool ata_device;
    uint32_t lba28_sector_count;
    char model[41];
};

bool identify(Drive drive, DeviceInfo& info);
bool read_sector(Drive drive, uint32_t lba, uint8_t* buffer);
bool write_sector(Drive drive, uint32_t lba, const uint8_t* buffer);
bool flush_cache(Drive drive);

}
```

Rules:

- Null buffers are rejected.
- LBAs above `0x0FFFFFFF` are rejected.
- `write_sector(Drive::Master, ...)` is always rejected.
- `write_sector()` on the slave waits for completion and then calls `flush_cache()`.
- All polling loops have finite timeouts.
- ERR and DF status bits cause failure.
- DRQ must be set before transferring sector words.

## Polling and Timeout Behavior

The driver never spins forever.

Every wait loop uses a fixed iteration budget.

The driver distinguishes:

- device not present,
- timeout waiting for BSY to clear,
- ERR,
- DF,
- DRQ not asserted,
- invalid LBA,
- forbidden master write.

The low-level ATA API returns `false` on failure. The storage self-test emits a more specific debug marker before returning failure.

## IDENTIFY

`identify()`:

1. Selects the requested drive.
2. Clears sector count and LBA registers.
3. Sends command `0xEC`.
4. Treats status `0x00` as no device.
5. Waits for BSY to clear with timeout.
6. Rejects non-ATA signatures when LBA mid/high are non-zero.
7. Waits for DRQ or error.
8. Reads 256 16-bit words from the data port.
9. Extracts:
   - LBA28 sector count from words 60-61.
   - model string from words 27-46, swapping ATA byte order.
10. Null-terminates and trims trailing spaces from the model.

## Sector Read

`read_sector()`:

1. Validate buffer and LBA.
2. Select master/slave using `0xE0 | drive_bit | high_lba_nibble`.
3. Program sector count `1`.
4. Program LBA low/mid/high.
5. Send READ SECTORS command `0x20`.
6. Wait for BSY clear and DRQ set.
7. Read 256 words from `0x1F0` into the 512-byte buffer.
8. Perform the required 400 ns delay before returning.

## Sector Write

`write_sector()`:

1. Reject master immediately.
2. Validate buffer and LBA.
3. Select slave.
4. Program one-sector LBA28 request.
5. Send WRITE SECTORS command `0x30`.
6. Wait for BSY clear and DRQ set.
7. Write 256 words to `0x1F0`.
8. Wait for completion.
9. Flush cache with command `0xE7`.
10. Return failure on timeout, ERR, or DF.

## Disk Abstraction

`disk.hpp/.cpp` wraps the ATA driver so the shell and later filesystem code do not depend directly on ATA register details.

```cpp
namespace linux95::storage {

enum class DiskId : uint8_t {
    Boot = 0,
    Test = 1,
};

bool initialize();
bool read_sector(DiskId disk, uint32_t lba, uint8_t* buffer);
bool write_sector(DiskId disk, uint32_t lba, const uint8_t* buffer);
const ata::DeviceInfo& info(DiskId disk);

}
```

Mapping:

- `DiskId::Boot` -> ATA master.
- `DiskId::Test` -> ATA slave.

The disk wrapper repeats the policy that boot-disk writes are forbidden.

## Automated QEMU Test Disk

The Makefile creates a fresh disposable raw disk for `make test-qemu`.

Target image:

```text
build/linux95-storage-test.img
```

Size:

- 16 MiB.
- Raw format.
- Recreated from zeros before each storage QEMU test.

QEMU attaches:

```text
linux95-kernel.img       -> primary master
linux95-storage-test.img -> primary slave
```

The test image is a build artifact and remains ignored by Git.

## Storage Self-Test

The self-test runs only when the slave test disk is present.

Test sequence:

1. IDENTIFY master.
2. IDENTIFY slave.
3. Confirm master exists.
4. Confirm slave exists.
5. Confirm the slave reports at least 128 sectors.
6. Read slave sector 64 and save its original 512 bytes in RAM.
7. Generate deterministic test pattern A.
8. Write pattern A to slave sector 64.
9. Read sector 64 back.
10. Compare every byte.
11. Generate pattern B.
12. Write pattern B to slave sector 64.
13. Read and compare pattern B.
14. Restore the original sector 64 contents.
15. Read sector 64 one final time and verify restoration.
16. Attempt a master write through the public API and require that it is rejected before any hardware write occurs.

Patterns are deterministic and include the byte index so stuck/repeated data is detected.

Any failure emits `[PANIC] storage_self_test_<reason>` and stops the storage milestone from reporting success.

## Debug Checkpoints

The existing memory-foundation checkpoints remain required.

New storage checkpoints:

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

`make test-qemu` fails if any required marker is missing or any `[PANIC]` marker appears.

## Shell Command

Add:

```text
diskinfo
```

Example output:

```text
Boot disk:
  Present: yes
  Interface: ATA PIO
  Mode: LBA28
  Model: QEMU HARDDISK
  Sectors: ...

Test disk:
  Present: yes
  Writable: yes
  Model: QEMU HARDDISK
  Sectors: ...
```

The shell does not expose arbitrary sector writes in this milestone.

## Host-Side Tests

Hardware-independent tests cover:

- LBA28 maximum boundary.
- Master-write policy.
- ATA model-string byte swapping/trimming.
- deterministic storage-test pattern generation.
- null buffer validation helpers where factored into pure code.

Hardware register behavior is verified by QEMU.

## Makefile Workflow

The normal workflow remains:

```bash
make clean
make
make test
make test-qemu
make run
```

`make test` runs host/source/image/relocation tests.

`make test-qemu`:

1. Builds Linux95.
2. Recreates the 16 MiB disposable test disk.
3. Boots QEMU headlessly with master + slave.
4. Captures debug port `0xE9`.
5. Requires all memory and storage checkpoints.
6. Terminates QEMU after `[PASS] shell_ready`.

`make run` also attaches a disposable slave disk, but the visible boot remains interactive.

## Completion Criteria

The storage-foundation milestone is complete only when:

- the memory-foundation tests still pass,
- the kernel identifies the primary master,
- the kernel identifies the primary slave,
- one-sector slave reads work,
- one-sector slave writes work,
- cache flush succeeds,
- write/read comparison succeeds for two patterns,
- the original test sector is restored,
- boot-disk writes are rejected,
- `diskinfo` works,
- `make test` passes,
- `make test-qemu` passes,
- visible QEMU still reaches the interactive shell,
- Git working tree is clean after the milestone commit.

FAT/filesystem work begins only after this storage milestone is green.
