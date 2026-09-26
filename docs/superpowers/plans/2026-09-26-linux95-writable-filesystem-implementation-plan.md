# Linux95 Writable FAT32 Filesystem v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` to implement this plan task-by-task. Each task follows strict RED -> GREEN, ends with a focused commit, and receives a fresh review before the next task.

**Goal:** Add safe basic FAT32 mutations on the Test/slave disk through reusable filesystem/VFS APIs and terminal commands, while preserving Boot/master read-only behavior and existing Linux95 functionality.

**Architecture:** Retain validated mounted FAT32 geometry in the existing FAT32 layer and add a focused `fat32_write.cpp/.hpp` mutation backend. Filesystem and VFS expose operation-oriented wrappers; a small testable terminal command module parses and dispatches commands. File creation/replacement and copy use bounded-buffer, unpublished-chain construction followed by directory publication.

**Tech Stack:** Freestanding C++17; existing ATA/storage, FAT32, filesystem/VFS, shell, and QEMU fixture infrastructure; host C++ tests with a bounded fake FAT32 disk and sector failure injection; Python QEMU harness and `mtools` fixture inspection.

**Spec:** `docs/superpowers/specs/2026-09-26-linux95-writable-filesystem-design.md` (binding authority)

## Global Constraints

- All mutation targets `storage::DiskId::Test`; no public filesystem/VFS mutation API accepts a disk selector.
- The storage boundary continues to deny `storage::write_sector(storage::DiskId::Boot, ...)`.
- Writable v1 requires FAT mirroring enabled; every BPB-reported FAT copy is updated. Read-only traversal honors the BPB-selected active FAT when mirroring is disabled.
- Mutations use volume-rooted paths with optional leading `/`, no current directory, no `.`/`..` user components, and maximum 127 path bytes plus NUL.
- Names are ASCII 8.3 only, compared case-insensitively and stored uppercase; no LFN or Unicode/case-folding extension.
- `write` replaces arbitrary bytes; empty files and multi-cluster growth/shrink are supported; file sizes above `UINT32_MAX` return `Unsupported` before allocation.
- `touch` on an existing regular file is a no-op success; `mkdir` requires an existing parent; `rm` only removes files/empty directories; `cp` is files-only and never overwrites.
- `mv` moves files within the Test volume and renames directories only within the same parent; cross-parent directory moves are `Unsupported`.
- All FAT/cluster traversals are bounded by validated volume geometry and use checked arithmetic.
- New-chain data/FAT links precede directory publication; an old chain is freed only after successful publication. The filesystem is not journaled and promises no full crash/power-loss atomicity.
- Preserve existing FAT32 reads, VFS reads, desktop/input, RTL8139/Ethernet/ARP/IPv4/ICMP/UDP/DNS, Ring 3, preemptive scheduling, and no-network fallback.
- Every task adds/runs focused failing tests before production implementation, proves RED for the missing behavior, implements the smallest change, and proves focused GREEN before advancing.
- Preserve existing Makefile test/source-check ordering; do not add prerequisites that skip or reorder existing checks.
- Do not touch `release/` or unrelated files; do not push, merge, publish, or remove branches/worktrees.

## Review Focus

- A BPB disables mirroring or selects an invalid active FAT: reads must follow the selected FAT; writable mutation must reject unsupported no-mirroring geometry, and invalid geometry must fail mount (Task 1 tests).
- A secondary FAT-copy write fails after an earlier copy succeeded: return `IoError`, attempt bounded repair, and do not publish dependent metadata (Task 1 tests).
- Allocation reuses a cluster with garbage bytes for directory growth: zero every sector before linking and before a slot can become visible (Task 2 tests).
- Invalid 8.3 punctuation, case variants, path separators, `.`/`..`, or over-capacity paths: reject before writes or resolve case-insensitively as specified (Task 2 tests).
- Disk full, overflow, or injected I/O during a multi-cluster replacement/copy: preserve old contents or keep destination absent and avoid narrowing/allocating after rejected size arithmetic (Tasks 3 and 5 tests).
- A move targets a directory from another parent or a path that already exists: directory cross-parent moves return `Unsupported`; collisions return `AlreadyExists` without metadata changes (Task 6 tests).
- A write is attempted against Boot or through an API that bypasses shell parsing: storage rejects Boot writes and source checks constrain mutation calls to the Test-bound FAT32 writer (Tasks 7 and 9 tests).

---

## File Map

Expected new files:

- `kernel/filesystem/fat32_write.hpp/.cpp` — private FAT allocation, chain, directory, and file mutation implementation.
- `kernel/terminal/filesystem_commands.hpp/.cpp` — small host-testable parser/dispatcher for the six writable commands; no generic shell framework.
- `kernel/filesystem/fat32_write_self_test.hpp/.cpp` — QEMU-only writable Test-volume acceptance routine.
- `tests/host/fat32_write_test.cpp` — fake-disk FAT32 mutation integration tests, extended task-by-task.
- `tests/host/filesystem_write_test.cpp` — filesystem/VFS facade forwarding/status tests using backend test doubles.
- `tests/host/filesystem_command_test.cpp` — command parser/dispatch tests using VFS test doubles.

Expected modifications:

- `kernel/filesystem/fat32_helpers.hpp` — checked BPB/short-name/path helpers as needed.
- `kernel/filesystem/fat32.hpp/.cpp` — retained mounted geometry and active-FAT-aware reads.
- `kernel/filesystem/filesystem.hpp/.cpp` — stable statuses and filesystem mutation facade.
- `kernel/filesystem/vfs.hpp/.cpp` — reusable mutation wrappers, preserving existing read handles/APIs.
- `kernel/terminal/shell.cpp` — help and dispatch to the command module.
- `kernel/storage/disk.cpp/.hpp` — no production change expected; existing `disk_write_allowed` and `write_sector` Boot guard remain authoritative. Modify only if a test demonstrates an actual boundary defect.
- `kernel/kernel.cpp`, `Makefile` — dedicated QEMU test build/rules and test invocation, isolated from normal startup.
- `tests/filesystem_source_checks.py`, `tests/prepare_fat32_image.py`, `tests/qemu_smoke.py` — enforce the write boundary and exercise disposable Test-disk mutations.

### Interfaces established by this plan

Extend existing `linux95::filesystem::Status` without changing the meaning of existing values. Add `AlreadyExists`, `InvalidName`, `NoSpace`, `DirectoryNotEmpty`, and `ReadOnly`; preserve `NotMounted`, `IoError`, `InvalidFilesystem`, `NotFound`, `NotDirectory`, `IsDirectory`, `Corrupt`, `Unsupported`, `InvalidDescriptor`, and `InvalidHandle`. Use `Unsupported` for a mounted no-mirroring BPB on mutation, a too-large file, and unsupported directory-move topology. Missing intermediate/final components use `NotFound`; a non-directory parent uses `NotDirectory`.

`fat32.hpp` exposes a read-only geometry snapshot for internal sharing. It includes FAT begin/count/sectors-per-FAT, mirroring flag and active-FAT index, first data LBA, sectors per cluster, data-cluster count, and root cluster. `mount()` validates and retains it; reader and writer derive FAT locations from this same snapshot. Geometry must not become public filesystem/VFS mutation detail.

Private mutation functions in `fat32_write.hpp` use the existing filesystem status type and `storage::DiskId::Test` internally:

```cpp
Status touch(const char* path);
Status write_file(const char* path, const uint8_t* data, size_t size);
Status mkdir(const char* path);
Status remove(const char* path);
Status copy_file(const char* source, const char* destination);
Status move(const char* source, const char* destination);
```

The public filesystem and `filesystem::vfs` layers expose the same six operation semantics and argument forms, forwarding to the FAT32 mutation module without exposing clusters, FAT values, LBAs, or directory offsets. VFS APIs do not use file descriptors for these path operations and do not alter existing descriptor/directory-handle behavior.

FAT short-name helpers use bounded input/output: validate a component and encode one 11-byte uppercase short name (8-byte space-padded base plus 3-byte space-padded extension). A full API path is at most 127 bytes plus NUL. The command module provides `bool execute_filesystem_command(terminal::Output&, char*)`, returning true only for the six recognized commands.

The fake FAT32 storage in `fat32_write_test.cpp` supplies Test-disk `info`, `read_sector`, and `write_sector` doubles, an in-memory bounded volume image, and deterministic selected-LBA/write-ordinal failure injection. The facade and command tests may use narrow test doubles at their interface boundaries; integration tests must exercise the real FAT writer and real storage calls. No production shortcut may bypass `storage::write_sector`.

---

### Task 1: Retained FAT32 Geometry, Read FAT Selection, and Chain Primitives

**Files:**
- Modify: `kernel/filesystem/fat32_helpers.hpp`
- Modify: `kernel/filesystem/fat32.hpp/.cpp`
- Modify: `kernel/filesystem/filesystem.hpp` (new statuses consumed by writer)
- Create: `kernel/filesystem/fat32_write.hpp/.cpp`
- Create: `tests/host/fat32_write_test.cpp`
- Modify: `Makefile`, `tests/filesystem_source_checks.py`

**Interfaces:**
- Consumes: `storage::DiskId::Test`, current BPB parser, storage read/write boundary.
- Produces: retained geometry snapshot; active-FAT-aware read chain traversal; private `read_fat_entry`, `write_fat_entry`, `allocate_chain`, `free_chain`, and checked `file_cluster_count(uint64_t, uint32_t, uint32_t&)` helpers; the shared status values above.

- [ ] **Step 1: Write failing geometry and primitive tests.** Add fake BPBs and assert mirrored-mode reads use FAT #0 and require mirrored copies, disabled mirroring with nonzero active index reads that selected FAT, ordinary `read_file` and nested directory traversal follow that FAT, and an out-of-range selected index fails mount. Assert mutation on a mounted no-mirroring volume returns `Unsupported` without writes. Assert supported mirrored mutations update every FAT copy, 28-bit writes preserve reserved high bits, allocation links and terminates chains, freeing is bounded, and malformed/out-of-range/cyclic FAT chains return `Corrupt`. A secondary-copy failure returns `IoError`, attempts bounded rollback, and yields no successful allocation.
- [ ] **Step 2: Establish the RED seam.** Add `build/host-fat32-write-test` and append it to `test-host-filesystem` after existing filesystem tests, without changing existing `test` prerequisite/source-check ordering. Run `make build/host-fat32-write-test && ./build/host-fat32-write-test`; record failure due to missing geometry/mutation API or behavior (not a malformed fixture).
- [ ] **Step 3: Implement retained geometry and active-FAT reads.** Parse FAT32 extended flags and retain validated geometry on successful Test mount. Reject invalid active index when mirroring is disabled. Make all existing FAT-chain reads select FAT #0 when mirroring is enabled and the BPB-selected active FAT otherwise. Keep all geometry arithmetic checked.
- [ ] **Step 4: Implement FAT entry and chain mutation.** Implement bounded entry access, free-cluster scan, allocation, link/EOC and free logic. Support mutations only for a mounted mirrored Test volume; update all `fat_count` FAT copies for each entry. If a copy fails, restore confirmed writes to the old value, bounded by FAT count; return `IoError` and prevent dependent publication. Do not make FSInfo a correctness dependency; the current code does not use FSInfo.
- [ ] **Step 5: Run focused GREEN.** Run `make build/host-fat32-write-test && ./build/host-fat32-write-test`, `make test-host-filesystem`, and `python3 tests/filesystem_source_checks.py`. Require active-FAT lookup/read tests, all-copy mutation, partial-copy failure, and bounded-chain cases GREEN.
- [ ] **Step 6: Run regression and commit.** Run `make test`; commit only Task 1 files with message `Add writable FAT32 geometry and chain primitives`.

### Task 2: 8.3 Path Resolution and Safe Directory-Entry Primitives

**Files:**
- Modify: `kernel/filesystem/fat32_helpers.hpp`, `kernel/filesystem/fat32_write.hpp/.cpp`
- Extend: `tests/host/fat32_write_test.cpp`
- Modify: `Makefile` only if the focused target needs new source dependencies.

**Interfaces:**
- Consumes: Task 1 mounted geometry, FAT read/write and chain operations.
- Produces: bounded root-based path resolution, 11-byte short-name encoding, directory slot read/create/update/delete, and directory-chain growth helpers.

- [ ] **Step 1: Write failing name/path tests.** Verify valid uppercase/lowercase/mixed ASCII 8.3 names encode uppercase and pad base/extension to 11 bytes; case-insensitive create-then-lookup works. Reject empty base/extension, multiple dots, overlong names, spaces/control/non-ASCII, FAT-forbidden `" * + , / : ; < = > ? [ \\ ] |`, unsupported punctuation, and user `.`/`..`. Test optional leading slash resolving at volume root, nested paths, missing parent, file-as-parent, empty/repeated/trailing separators, path over 127 bytes, and zero sector writes for invalid paths.
- [ ] **Step 2: Prove RED.** Run `make build/host-fat32-write-test && ./build/host-fat32-write-test`; confirm missing encoding/path/directory primitives cause the expected failures.
- [ ] **Step 3: Implement bounded path resolver and name encoder.** Parse paths into fixed buffers bounded by `vfs::kPathCapacity`; no cwd, disk selector, LFN, or dynamic allocation. Return `NotFound` for missing components and `NotDirectory` for non-directory parents. Encode only ASCII letters/digits and the approved FAT punctuation set, with case-insensitive matching and uppercase padded on-disk encoding.
- [ ] **Step 4: Implement directory entry operations and growth.** Preserve unowned short-entry fields during updates; find slots across the complete chain. When no slot exists, allocate one cluster, clear every sector, establish EOC, link it to the old tail, and only then use its first slot. On pre-link failure reclaim best-effort; if later entry publication fails, make bounded best-effort unlink/free only where write outcomes are known.
- [ ] **Step 5: Test stale-cluster and failure behavior.** Seed a reusable cluster with garbage that looks like live directory entries; force growth and prove the cluster is zeroed before link and no stale name becomes visible. Inject clear, EOC, link, entry-publication, and rollback failures and assert no slot is used early and cleanup stays bounded. Exhaust free clusters while forcing directory growth and assert `NoSpace`, no published entry, and no corrupted existing directory chain.
- [ ] **Step 6: Run focused GREEN and commit.** Run `make build/host-fat32-write-test && ./build/host-fat32-write-test`, `python3 tests/filesystem_source_checks.py`, and `make test-host-filesystem`; commit Task 2 changes with message `Add FAT32 short-name and directory mutation helpers`.

### Task 3: Touch and Copy-on-Write Arbitrary-Byte File Replacement

**Files:**
- Modify: `kernel/filesystem/fat32_write.hpp/.cpp`
- Extend: `tests/host/fat32_write_test.cpp`

**Interfaces:**
- Consumes: Task 1 chain primitives and Task 2 paths/entry publication.
- Produces: `touch(path)` and `write_file(path, data, size)` with the specified status behavior.

- [ ] **Step 1: Write failing file tests.** Cover new empty file, existing regular-file `touch` preserving all metadata/data, `touch` on directory => `IsDirectory`, arbitrary bytes including NUL, empty replacement, multi-cluster readback, growth, shrink, and exact boundary sizes just below/at/above a cluster. Pass `size > UINT32_MAX` with an unreadable sentinel pointer and assert `Unsupported`, zero writes/allocations, and no narrowing.
- [ ] **Step 2: Prove RED.** Run `make build/host-fat32-write-test && ./build/host-fat32-write-test`; observe the absent touch/write behavior.
- [ ] **Step 3: Implement checked-size touch/write.** Check null-data/zero-length semantics and reject sizes above the FAT32 32-bit size limit before allocation. Calculate cluster count with overflow-safe arithmetic. Stream caller bytes through a fixed sector/cluster buffer into an unpublished new chain; complete data and mirrored FAT metadata before publishing the new directory entry/size. For zero length, publish cluster 0 and size 0. Free the old chain only after publication.
- [ ] **Step 4: Inject replacement failures.** Test disk full, FAT-copy/chain update failure, data-sector failure, directory-sector publication failure, cleanup failure, and old-chain free failure. Before publication the old file remains authoritative; after publication cleanup failure returns `IoError` without reverting or freeing the live new chain.
- [ ] **Step 5: Run focused GREEN and commit.** Run `make build/host-fat32-write-test && ./build/host-fat32-write-test` and `make test-host-filesystem`; commit Task 3 changes with message `Add FAT32 touch and copy-on-write write`.

### Task 4: Directory Creation and Non-Recursive Removal

**Files:**
- Modify: `kernel/filesystem/fat32_write.hpp/.cpp`
- Extend: `tests/host/fat32_write_test.cpp`

**Interfaces:**
- Consumes: Task 1 allocation/free and Task 2 paths/entry/directory-growth helpers.
- Produces: `mkdir(path)` and `remove(path)`.

- [ ] **Step 1: Write failing directory tests.** Cover mkdir with valid existing parent, absent parent, existing target, invalid name, full-parent growth, and correct `.`/`..` including root-parent convention. Cover rm files, empty directories, non-empty directories, root, and dot-entry rejection.
- [ ] **Step 2: Prove RED.** Run `make build/host-fat32-write-test && ./build/host-fat32-write-test`; observe the missing operation failures.
- [ ] **Step 3: Implement mkdir.** Allocate one directory cluster, clear all sectors, initialize `.` and `..`, then publish the parent entry last. Clean unpublished storage on preparation failure where possible; do not expose a partially initialized directory.
- [ ] **Step 4: Implement remove.** Determine directory emptiness while ignoring only `.` and `..`; reject non-empty with `DirectoryNotEmpty`, root/dot with the specified invalid-path status. Delete the parent entry before freeing the chain; never free if deletion failed.
- [ ] **Step 5: Inject failures and prove GREEN.** Inject allocation, sector clear, dot-entry write, parent publication, deletion, and mirrored FAT/free failures. Run `make build/host-fat32-write-test && ./build/host-fat32-write-test`; verify no success is reported after required-write failure and old/visible data is preserved where operation ordering permits.
- [ ] **Step 6: Run regressions and commit.** Run `make test-host-filesystem && make test`; commit Task 4 changes with message `Add FAT32 mkdir and empty-directory removal`.

### Task 5: Streamed File Copy Without Overwrite

**Files:**
- Modify: `kernel/filesystem/fat32_write.hpp/.cpp`
- Extend: `tests/host/fat32_write_test.cpp`

**Interfaces:**
- Consumes: Task 1 chain helpers, Task 2 path/slot helpers, Task 3 bounded sector transfer approach.
- Produces: `copy_file(source, destination)`.

- [ ] **Step 1: Write failing copy tests.** Cover empty and multi-cluster source files, exact byte contents/size, nested destination parent, source unchanged, directory source => `IsDirectory`, and existing destination => `AlreadyExists` with no writes. Include source sizes below/at/above cluster boundaries and malformed/internal size/cluster arithmetic rejection before allocation.
- [ ] **Step 2: Prove RED.** Run `make build/host-fat32-write-test && ./build/host-fat32-write-test`; observe absent copy behavior.
- [ ] **Step 3: Implement bounded copy.** Validate paths/source/destination and checked size/cluster count before allocating. Allocate an unpublished chain and stream source sectors through fixed buffers; do not buffer a complete file. Complete data and all FAT copies before destination entry publication.
- [ ] **Step 4: Test partial-copy cleanup.** Inject source read failure, disk full, primary/secondary FAT write failure, destination data write failure, directory growth/publication failure, and cleanup failure. Pre-publication failure leaves destination absent and source unchanged; reclaim unpublished clusters best-effort without freeing any uncertain published chain.
- [ ] **Step 5: Run focused GREEN and commit.** Run `make build/host-fat32-write-test && ./build/host-fat32-write-test` and `make test-host-filesystem`; commit Task 5 changes with message `Add streamed FAT32 file copy`.

### Task 6: File Moves and Same-Parent Directory Renames

**Files:**
- Modify: `kernel/filesystem/fat32_write.hpp/.cpp`
- Extend: `tests/host/fat32_write_test.cpp`

**Interfaces:**
- Consumes: Task 2 path/entry helpers and Task 4 directory type/parent logic.
- Produces: `move(source, destination)`.

- [ ] **Step 1: Write failing move tests.** Cover file rename in place, file move across parents, unchanged source file data chain, same-parent non-empty directory rename, destination collision, missing paths/parents, root/dot rejection, directory cross-parent move => `Unsupported`, and invalid/self move rejection. Assert identical-path file and directory operations return `AlreadyExists`. Verify rejected operations perform no metadata writes.
- [ ] **Step 2: Prove RED.** Run `make build/host-fat32-write-test && ./build/host-fat32-write-test`; observe missing move behavior.
- [ ] **Step 3: Implement safe metadata moves.** For same-parent rename update only the existing entry name, preserving its metadata. For files across parents, publish the destination entry before deleting the source; preserve the chain and never copy/free file data. Refuse directory moves whose resolved parents differ; directories may only be renamed within their existing parent. Reject root and dot entries before mutation.
- [ ] **Step 4: Test write failures and rollback.** Inject destination-entry, source-delete, and rollback failures. Make bounded best-effort restoration only when write outcomes are known; return `IoError` on failed repair, never free moved-object data, and do not report success until required metadata writes complete.
- [ ] **Step 5: Run focused GREEN and commit.** Run `make build/host-fat32-write-test && ./build/host-fat32-write-test` and `make test-host-filesystem`; commit Task 6 changes with message `Add FAT32 file moves and directory renames`.

### Task 7: Filesystem Facade, Writable VFS Wrappers, and Disk Boundary

**Files:**
- Modify: `kernel/filesystem/filesystem.hpp/.cpp`, `kernel/filesystem/vfs.hpp/.cpp`
- Modify: `tests/filesystem_source_checks.py`, `Makefile`
- Create: `tests/host/filesystem_write_test.cpp`

**Interfaces:**
- Consumes: Tasks 1-6 internal operations and existing filesystem status conventions.
- Produces: public `filesystem::{touch,write_file,mkdir,remove,copy_file,move}` and matching `filesystem::vfs` path-operation wrappers; existing reads remain unchanged.

- [ ] **Step 1: Write failing facade tests.** Use backend test doubles to assert exact path/data/length forwarding and status propagation for each operation. Confirm no disk selector is accepted. Test `NotMounted` when the Test FAT32 volume is unmounted and distinguish it from `ReadOnly`. Verify existing VFS descriptors, directory handles, and read APIs still work.
- [ ] **Step 2: Prove RED.** Run `make build/host-filesystem-write-test && ./build/host-filesystem-write-test`; expect missing declarations/forwarders.
- [ ] **Step 3: Implement thin wrappers.** Add filesystem methods delegating to `fat32_write`; add VFS wrappers delegating to filesystem methods. Keep FAT structures private and do not route path mutations through descriptor handles.
- [ ] **Step 4: Enforce/prove Test-only writes.** Writer uses only `DiskId::Test`; preserve `storage::write_sector` Boot denial. Test Boot-sector write denial directly and unchanged Boot bytes. Update source checks to permit raw sector mutation only in `fat32_write.cpp` (plus an explicitly test-only denial probe), and forbid it from read/backend/facade/VFS/shell production files. Replace the baseline source check that rejects writable filesystem declarations with positive checks that the operation APIs exist only at the filesystem/VFS boundary, public headers expose no FAT internals, and terminal dispatch calls VFS rather than FAT/storage directly.
- [ ] **Step 5: Run focused GREEN and regression.** Run `make build/host-filesystem-write-test && ./build/host-filesystem-write-test`, `python3 tests/filesystem_source_checks.py`, `make test-host-filesystem`, and `make test`. Register the facade host test in the existing filesystem host suite without reordering source-check execution.
- [ ] **Step 6: Commit.** Commit Task 7 files with message `Expose writable FAT32 filesystem and VFS APIs`.

### Task 8: Terminal Writable Commands

**Files:**
- Create: `kernel/terminal/filesystem_commands.hpp/.cpp`
- Modify: `kernel/terminal/shell.cpp`, `Makefile`, `tests/filesystem_source_checks.py`
- Create: `tests/host/filesystem_command_test.cpp`

**Interfaces:**
- Consumes: Task 7 VFS methods and `terminal::Output`.
- Produces: `bool terminal::execute_filesystem_command(Output&, char*)`; true only for `touch`, `mkdir`, `write`, `rm`, `cp`, `mv`.

- [ ] **Step 1: Write failing command tests.** Test command recognition, exact usage/arity, all six commands, VFS fake calls, statuses mapped to clear messages, and unrelated command passthrough. Verify `write PATH DATA` parses PATH as one token and passes the remaining payload verbatim, including internal spaces/punctuation; no new quote/escape grammar. No remainder after PATH means a zero-byte write. Verify fixed command-capacity rejection remains safe.
- [ ] **Step 2: Prove RED.** Run `make build/host-filesystem-command-test && ./build/host-filesystem-command-test`; expect the command API/behavior failures.
- [ ] **Step 3: Implement the small dispatcher.** Parse only required path tokens; preserve the `write` remainder as byte data. Call only writable VFS APIs. Keep disk/FAT handling out of the shell. Add command help and operation-specific success/error text; extend source checks to require this VFS routing and forbid storage-sector or FAT-internal calls from shell/command production code.
- [ ] **Step 4: Run focused GREEN.** Run `make build/host-filesystem-command-test && ./build/host-filesystem-command-test`, `make build/host-shell-session-test && ./build/host-shell-session-test`, `python3 tests/filesystem_source_checks.py`, and `python3 tests/source_checks.py`. Register the command host test in the existing filesystem host suite while preserving test/source-check ordering.
- [ ] **Step 5: Run full host tests and commit.** Run `make test`; commit Task 8 files with message `Add writable FAT32 terminal commands`.

### Task 9: Disposable QEMU Writable-Filesystem Proof

**Files:**
- Create: `kernel/filesystem/fat32_write_self_test.hpp/.cpp`
- Modify: `kernel/kernel.cpp`, `Makefile`, `tests/prepare_fat32_image.py`, `tests/qemu_smoke.py`, and source checks as needed.

**Interfaces:**
- Consumes: Task 7 public VFS/filesystem operations and Task 8 commands (commands are not required for kernel self-test calls).
- Produces: isolated writable-FAT32 QEMU mode using a disposable Test/slave image and test-only self-test routine.

- [ ] **Step 1: Add failing QEMU expectations first.** Add a `--fat32-write-test` harness mode requiring real create, write/readback, mkdir, copy, move, remove, and Boot write-denial proof markers. Assert that it uses a disposable Test image and that no normal fixture is reused or mutated. Run `python3 tests/qemu_smoke.py --fat32-write-test`; RED is expected until test build and writer exist.
- [ ] **Step 2: Add isolated build/image rules.** Create a dedicated kernel test define/object/image and FAT fixture target; boot the ordinary master kernel image plus the fixture as ATA slave/Test. Keep the write test out of normal boot and preserve all existing fault, preemption, UDP/DNS, process, and no-network fixture conventions.
- [ ] **Step 3: Implement test-only real operations.** Invoke public filesystem/VFS APIs to touch an 8.3 path, write bytes, read/stat them back, create a directory, copy a file, move/rename a file, and remove selected files/empty directories. Emit markers only after the actual operation and readback succeed. Compare Boot-sector bytes before/after a direct guarded `write_sector(Boot, ...)` rejection.
- [ ] **Step 4: Prove persistence and focused GREEN.** Run `python3 tests/qemu_smoke.py --fat32-write-test` and require all operations/readback plus no panic. After QEMU exits, independently inspect the same unreset Test image with `mtools` or an equivalent fixture verifier to confirm a selected written/moved/copied file and exact bytes persisted. Do not recreate/format the image between QEMU and inspection. Then rerun from a separately fresh fixture for repeatability if useful.
- [ ] **Step 5: Run full QEMU regressions and commit.** Run `make test-qemu` and the focused writable mode. Confirm normal RTL8139/Ethernet/ARP/IPv4/ICMP/UDP/DNS, Ring 3, preemptive scheduling, desktop, and no-network fallback still pass; commit Task 9 files with message `Prove writable FAT32 operations in QEMU`.

### Task 10: Full Regression, Hygiene, and Whole-Branch Review

**Files:**
- No planned production changes; modify only files with a reproduced verification defect.

**Interfaces:**
- Consumes: all completed writable filesystem behavior from Tasks 1-9.
- Produces: verified milestone branch with no unresolved Critical/Important review findings.

- [ ] **Step 1: Clean build.** Run `make clean && make all`; confirm kernel and userspace ELFs rebuild.
- [ ] **Step 2: Full host/source/image suite.** Run `make test`; require all existing and new host, source, image, relocation, and shell checks pass.
- [ ] **Step 3: Full QEMU matrix.** Run `make test-qemu`, `python3 tests/qemu_smoke.py --fat32-write-test`, `python3 tests/qemu_smoke.py --process-self-test`, and `python3 tests/qemu_smoke.py --process-fault-test`; verify writable Test disk plus normal networking, UDP/DNS, Ring 3, user-fault isolation, preemption, and no-network modes.
- [ ] **Step 4: Binary and branch hygiene.** Run `nm -u build/kernel.elf` (no unresolved output), `git diff --check`, and `git status --short --branch`. Check the milestone range contains no `release/` paths.
- [ ] **Step 5: Review the full milestone diff against the approved spec.** Focus on Boot/Test isolation, FAT-copy consistency, bounded traversals and arithmetic, publication/free ordering, stale-directory prevention, VFS/shell boundaries, move restrictions, and regressions. Any Critical/Important finding requires a test-first fix and rerun of the full clean-build/host/QEMU matrix; document deferred minor findings.
- [ ] **Step 6: Finish without an empty commit.** If review finds no defect, make no verification-only commit. Report final evidence and stop; do not merge or push.

## Task-to-Spec Coverage

- Existing read path, retained BPB geometry, selected active FAT reads, mirrored writable FAT updates and failure rollback: Task 1.
- 8.3 rules, paths, directory entries/growth and corruption bounds: Tasks 1-2.
- `touch`, arbitrary-byte write, empty/multi-cluster files, growth/shrink, COW and failure preservation: Task 3.
- `mkdir`, `.`/`..`, `rm`, empty-directory check and non-recursive/root rejection: Task 4.
- File-only no-overwrite `cp` with bounded stream and unpublished destination: Task 5.
- File moves, same-parent directory rename, cross-parent-directory rejection and metadata rollback: Task 6.
- Reusable filesystem/VFS APIs and Test-only write boundary: Task 7.
- Terminal command parsing, payload preservation, help and clear errors: Task 8.
- Real disposable Test-disk QEMU operations, persistence inspection and Boot guard proof: Task 9.
- Full regression and whole-branch review: Task 10.
- FSInfo: current baseline has no FSInfo parsing/use; no allocation correctness dependency or FSInfo update is introduced.

## Focused Commit Sequence

1. `Add writable FAT32 geometry and chain primitives`
2. `Add FAT32 short-name and directory mutation helpers`
3. `Add FAT32 touch and copy-on-write write`
4. `Add FAT32 mkdir and empty-directory removal`
5. `Add streamed FAT32 file copy`
6. `Add FAT32 file moves and directory renames`
7. `Expose writable FAT32 filesystem and VFS APIs`
8. `Add writable FAT32 terminal commands`
9. `Prove writable FAT32 operations in QEMU`
10. No empty final-verification commit; use only a focused defect-fix commit if needed.
