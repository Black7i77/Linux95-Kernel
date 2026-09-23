# Linux95 Kernel v1.0 Storage Foundation Implementation Plan

**Branch:** v1.0-storage-dev

**Base:** v1.0-memory-foundation

## Goal

Add a safe ATA PIO storage subsystem to Linux95 Kernel.

The Linux95 boot image remains the primary IDE master and must never be
written by the kernel.

A disposable 16 MiB QEMU disk is attached as the primary IDE slave and is
used for read/write testing.

## Storage architecture

Primary IDE controller:

- Command base: 0x1F0
- Control base: 0x3F6
- ATA PIO polling
- LBA28
- 512-byte sectors
- finite polling timeouts

Drive layout:

- Master = Linux95 boot disk
- Slave = disposable QEMU storage-test disk

Write policy:

- Master writes are always rejected.
- Slave writes are permitted.
- No shell command exposes arbitrary raw writes.

## Files

Create:

kernel/storage/ata.hpp
kernel/storage/ata.cpp
kernel/storage/ata_helpers.hpp
kernel/storage/disk.hpp
kernel/storage/disk.cpp
kernel/storage/storage_self_test.hpp
kernel/storage/storage_self_test.cpp
tests/host/ata_helpers_test.cpp
tests/storage_source_checks.py

Modify:

kernel/kernel.cpp
kernel/terminal/shell.cpp
Makefile
tests/qemu_smoke.py
tests/source_checks.py
README.md

## Task 1 — Storage test harness

Write tests before the driver.

Create tests/storage_source_checks.py.

The test must require:

- ata.hpp
- ata.cpp
- ata_helpers.hpp
- disk.hpp
- disk.cpp
- storage_self_test.hpp
- storage_self_test.cpp
- diskinfo shell command
- storage debug checkpoints

Run:

python3 tests/storage_source_checks.py

Expected result before implementation:

storage source checks: FAIL

Commit:

git add Makefile tests/storage_source_checks.py
git commit -m "test: define storage foundation checks"

## Task 2 — ATA contracts and pure helpers

Create ata.hpp with:

namespace linux95::storage::ata

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

Create ata_helpers.hpp.

Required behavior:

- valid LBA range is 0x00000000 through 0x0FFFFFFF
- 0x10000000 is rejected
- master writes forbidden
- slave writes allowed
- ATA model string byte order decoded correctly
- trailing model spaces trimmed
- deterministic 512-byte test patterns

Create host test:

tests/host/ata_helpers_test.cpp

Run:

make test-host-storage

The test must fail before helpers exist and pass after implementation.

## Task 3 — ATA IDENTIFY

Implement the primary IDE registers:

0x1F0 data
0x1F1 error
0x1F2 sector count
0x1F3 LBA low
0x1F4 LBA mid
0x1F5 LBA high
0x1F6 drive/head
0x1F7 command/status
0x3F6 alternate status

Status bits:

ERR = 0x01
DRQ = 0x08
DF  = 0x20
BSY = 0x80

IDENTIFY command:

0xEC

identify() must:

1. select master/slave
2. clear sector/LBA registers
3. issue IDENTIFY
4. reject status 0x00 or 0xFF
5. wait for BSY to clear
6. reject ERR or DF
7. require DRQ
8. read 256 words
9. decode model
10. read LBA28 sector count from words 60-61

All waits must have finite timeout counters.

## Task 4 — Sector reads

READ SECTORS command:

0x20

read_sector() must:

- reject null buffer
- reject invalid LBA
- select requested drive
- program one-sector LBA28 request
- wait for BSY clear
- require DRQ
- read exactly 256 16-bit words
- fail on ERR, DF or timeout

Run:

make clean
make

## Task 5 — Protected writes

WRITE SECTORS command:

0x30

CACHE FLUSH command:

0xE7

write_sector() must first do:

if master:
    return false

No ATA WRITE command may be issued to master.

Slave write procedure:

1. validate buffer/LBA
2. select slave
3. send one-sector LBA28 request
4. send 0x30
5. wait for DRQ
6. write 256 words
7. wait for completion
8. issue 0xE7
9. wait for completion

## Task 6 — Disk abstraction

Create DiskId:

enum class DiskId : uint8_t {
    Boot = 0,
    Test = 1,
};

Mapping:

Boot -> ATA Master
Test -> ATA Slave

Public API:

bool initialize();
bool read_sector(DiskId disk, uint32_t lba, uint8_t* buffer);
bool write_sector(DiskId disk, uint32_t lba, const uint8_t* buffer);
const ata::DeviceInfo& info(DiskId disk);

The disk layer must independently reject Boot writes.

## Task 7 — Disposable QEMU slave disk

Create:

build/linux95-storage-test.img

Size:

16 MiB

Make command:

dd if=/dev/zero \
   of=build/linux95-storage-test.img \
   bs=1M count=16 status=none

QEMU mapping must be explicit:

Linux95 boot image:
primary IDE master / index 0

Storage test image:
primary IDE slave / index 1

Before each automated QEMU storage test:

rm -f build/linux95-storage-test.img

Then recreate it.

The image remains under build/ and is never committed.

## Task 8 — Storage self-test

Test sector:

LBA 64 on the slave only.

Sequence:

1. IDENTIFY master
2. emit [PASS] ata_master_identify
3. IDENTIFY slave
4. emit [PASS] ata_slave_identify
5. read and save original slave sector 64
6. emit [PASS] ata_read
7. generate pattern A
8. write pattern A
9. read pattern A back
10. compare all 512 bytes
11. generate pattern B
12. write pattern B
13. read pattern B back
14. compare all 512 bytes
15. restore original sector 64
16. read restored sector
17. verify original bytes restored
18. emit [PASS] ata_restore
19. attempt Boot-disk write
20. require the API to reject it
21. emit [PASS] master_write_guard
22. emit [PASS] storage_self_test

Required storage checkpoints:

[PASS] ata_master_identify
[PASS] ata_slave_identify
[PASS] ata_read
[PASS] ata_write
[PASS] ata_restore
[PASS] master_write_guard
[PASS] storage_self_test

If a destructive write succeeds but a later test fails, restoration must
still be attempted before returning failure.

## Task 9 — Kernel initialization

Run the storage self-test after the existing memory self-test.

Order:

memory system
memory self-test
ATA/disk initialization
storage self-test
interrupts/devices
shell

Fatal failure must emit:

[PANIC] storage_self_test

before halting.

## Task 10 — diskinfo shell command

Add:

diskinfo

Output must include:

Boot disk:
Present
Interface: ATA PIO
Mode: LBA28
Model
Sector count
Writable: no

Test disk:
Present
Interface: ATA PIO
Mode: LBA28
Model
Sector count
Writable: yes

Do not add raw write commands to the shell.

## Task 11 — Automated QEMU verification

make test-qemu must require all existing memory checkpoints plus:

[PASS] ata_master_identify
[PASS] ata_slave_identify
[PASS] ata_read
[PASS] ata_write
[PASS] ata_restore
[PASS] master_write_guard
[PASS] storage_self_test
[PASS] shell_ready

Any [PANIC] marker fails the test.

The failure report must print the last checkpoint reached.

## Task 12 — Final verification

Run:

make clean
make
make test
make test-qemu

Required host results:

source checks: PASS
image checks: PASS
memory source checks: PASS
storage source checks: PASS
relocation checks: PASS
QEMU smoke test: PASS

Then manually run:

make run

Shell checks:

version
mem
diskinfo
uptime
help

Completion requirements:

- master detected
- slave detected
- slave reads work
- slave writes work
- cache flush works
- two pattern comparisons succeed
- original slave sector restored
- master writes rejected
- diskinfo works
- memory tests remain green
- QEMU tests remain green

FAT/filesystem work starts only after this storage foundation is verified.

Do not merge to main or create a storage release tag until the working
result has been reviewed.
