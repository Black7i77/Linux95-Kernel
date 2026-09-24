# Linux95 Read-Only VFS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a full read-only VFS above the existing FAT32 facade with 64 global file-descriptor slots, 32 separate directory handles, `open/read/close/stat/fstat`, `opendir/readdir/closedir`, shell migration, and boot/host verification.

**Architecture:** Keep FAT32 as the only backend and extend the existing filesystem facade with two generic read-only primitives: path metadata lookup and indexed directory-entry lookup. Build `linux95::filesystem::vfs` above that facade; VFS owns descriptor/handle state and offsets, while FAT32 continues to own path traversal and data reads. No write/create/delete API is introduced.

**Tech Stack:** Freestanding C++17, NASM/GNU binutils build, existing FAT32/ATA PIO backend, host C++ tests with `assert`, Python source/QEMU smoke tests, GNU Make.

**Spec:** `docs/superpowers/specs/2026-09-24-linux95-vfs-readonly-design.md`

## Global Constraints

- VFS is read-only.
- File-descriptor table contains exactly 64 slots total.
- File descriptors `0`, `1`, and `2` are reserved for future stdin/stdout/stderr.
- Regular file descriptors are allocated from `3` through `63`, so at most 61 regular files can be open at once.
- Directory handles use a separate table containing exactly 32 slots, numbered `0` through `31`.
- Accept both `/README.TXT` and `README.TXT` as FAT32-rooted paths.
- `"/"` is a valid directory path; an empty path is invalid.
- `.` and `..` are not supported or exposed.
- FAT32 remains DOS 8.3 only; no LFN support is added.
- VFS path storage is fixed at 128 bytes including the terminator: maximum accepted path length is 127 bytes.
- Paths longer than 127 bytes return `Status::Unsupported`; no dynamic path allocation is introduced.
- No VFS or filesystem-facade code may call `storage::write_sector`.
- No public `write`, `create`, `mkdir`, `unlink`, `rename`, or `truncate` filesystem/VFS API is added.
- Existing FAT32, storage, memory, and QEMU tests must continue to pass.

## Review Focus

- **Descriptor-capacity boundary:** 64 total FD slots means only 61 regular opens; the 62nd simultaneous regular-file open must fail with `TooManyOpenFiles`, and closing fd 3 must make fd 3 reusable.
- **Path-buffer boundary:** null, empty, and 128-byte-or-longer paths must fail without writing past the 128-byte path arrays; exactly 127 bytes plus terminator is the maximum representable path.
- **Read-offset correctness on backend failure:** a failed backend read must not advance the VFS descriptor offset; a successful read advances it by exactly `bytes_read`, including zero at EOF.
- **Directory cursor correctness on error/EOF:** backend failure must not advance `next_index`; EOF sets `end=true`, leaves the index stable, and repeated EOF calls stay successful.
- **Reset semantics:** `vfs::initialize()` must clear all open file descriptors and directory handles so the next regular file gets fd 3 and the next directory gets handle 0.

---

## File Structure

### New files

- `kernel/filesystem/vfs.hpp` — public read-only VFS API, constants, `FileStat`, and `DirectoryEntry`.
- `kernel/filesystem/vfs.cpp` — fixed descriptor tables, path-copy guard, file and directory operations.
- `kernel/filesystem/vfs_self_test.hpp` — boot self-test declaration.
- `kernel/filesystem/vfs_self_test.cpp` — QEMU-visible VFS functional self-test and debug markers.
- `tests/host/vfs_test.cpp` — host-only VFS tests against a fake filesystem facade.

### Modified files

- `kernel/filesystem/filesystem.hpp` — extend `Status`; add generic read-only backend metadata and indexed-directory APIs.
- `kernel/filesystem/filesystem.cpp` — forward new generic APIs to FAT32.
- `kernel/filesystem/fat32.hpp` — declare FAT32 metadata and indexed-directory primitives.
- `kernel/filesystem/fat32.cpp` — add shared path resolution, metadata lookup, and indexed directory lookup.
- `tests/host/fat32_mount_test.cpp` — pin metadata/root/indexed-directory behavior in the real FAT32 backend.
- `kernel/kernel.cpp` — initialize VFS and run VFS self-test after the FAT32 self-test.
- `kernel/terminal/shell.cpp` — migrate `ls` and `cat` to VFS handles/descriptors.
- `tests/filesystem_source_checks.py` — enforce shell/VFS layering and no writable VFS API.
- `tests/qemu_smoke.py` — require VFS boot markers.
- `Makefile` — build/link VFS objects and host VFS test with correct header dependencies.
- `README.md` — document VFS milestone and added debug checkpoints.

---

### Task 1: Add backend metadata and indexed-directory primitives

**Files:**
- Modify: `kernel/filesystem/filesystem.hpp`
- Modify: `kernel/filesystem/filesystem.cpp`
- Modify: `kernel/filesystem/fat32.hpp`
- Modify: `kernel/filesystem/fat32.cpp`
- Modify: `tests/host/fat32_mount_test.cpp`

**Interfaces:**
- Consumes: existing `filesystem::Status`, `filesystem::Entry`, FAT32 list/read APIs.
- Produces:
  - `Status filesystem::stat_path(const char* path, Entry& entry)`
  - `Status filesystem::read_directory_entry(const char* path, uint32_t index, Entry& entry, bool& end)`
  - matching FAT32 functions.

- [ ] **Step 1: Write the failing backend tests**

Add assertions to `tests/host/fat32_mount_test.cpp` after successful initialization:

```cpp
filesystem::Entry metadata = {};

assert(filesystem::stat_path(
    "/README.TXT",
    metadata) == filesystem::Status::Ok);
assert(!metadata.is_directory);
assert(metadata.size == 34u);

assert(filesystem::stat_path(
    "DOCS",
    metadata) == filesystem::Status::Ok);
assert(metadata.is_directory);

assert(filesystem::stat_path(
    "/",
    metadata) == filesystem::Status::Ok);
assert(metadata.is_directory);
assert(metadata.size == 0u);

assert(filesystem::stat_path(
    "/MISSING.TXT",
    metadata) == filesystem::Status::NotFound);

filesystem::Entry indexed = {};
bool end = true;

assert(filesystem::read_directory_entry(
    "/",
    0,
    indexed,
    end) == filesystem::Status::Ok);
assert(!end);
assert(indexed.name[0] != '\0');

uint32_t root_entries = 0;
for (;;) {
    assert(filesystem::read_directory_entry(
        "/",
        root_entries,
        indexed,
        end) == filesystem::Status::Ok);

    if (end) {
        break;
    }

    ++root_entries;
    assert(root_entries < 16u);
}

assert(root_entries == 3u);

assert(filesystem::read_directory_entry(
    "/README.TXT",
    0,
    indexed,
    end) == filesystem::Status::NotDirectory);
```

- [ ] **Step 2: Run the test and verify RED**

```bash
rm -f build/host-fat32-mount-test
make build/host-fat32-mount-test
```

Expected: compile failure because the two new facade functions do not exist.

- [ ] **Step 3: Extend `Status` and declare the new facade APIs**

In `kernel/filesystem/filesystem.hpp`, append these values after existing `Unsupported`:

```cpp
InvalidDescriptor,
InvalidHandle,
TooManyOpenFiles,
TooManyOpenDirectories,
```

Add:

```cpp
Status stat_path(
    const char* path,
    Entry& entry);

Status read_directory_entry(
    const char* path,
    uint32_t index,
    Entry& entry,
    bool& end);
```

- [ ] **Step 4: Add matching declarations in `fat32.hpp` and forwarding in `filesystem.cpp`**

```cpp
Status stat_path(
    const char* path,
    Entry& entry);

Status read_directory_entry(
    const char* path,
    uint32_t index,
    Entry& entry,
    bool& end);
```

Facade forwarding:

```cpp
Status stat_path(
    const char* path,
    Entry& entry)
{
    return fat32::stat_path(path, entry);
}

Status read_directory_entry(
    const char* path,
    uint32_t index,
    Entry& entry,
    bool& end)
{
    return fat32::read_directory_entry(
        path,
        index,
        entry,
        end);
}
```

- [ ] **Step 5: Add shared FAT32 path-resolution helpers**

Create internal `resolve_entry(...)` and `resolve_directory_cluster(...)` helpers by extracting the existing component-walk logic from `read_file()` and `list_directory()`. Preserve case-insensitive DOS 8.3 lookup, `NotDirectory`, `Corrupt`, and `Unsupported` behavior. `resolve_directory_cluster("/", ...)` must return `root_cluster`.

Use this exact root behavior in the public metadata function:

```cpp
if (path[0] == '/' &&
    path[1] == '\0') {
    out.name[0] = '/';
    out.name[1] = '\0';

    for (uint32_t i = 2; i < 13; ++i) {
        out.name[i] = '\0';
    }

    out.is_directory = true;
    out.size = 0;
    return Status::Ok;
}
```

- [ ] **Step 6: Implement indexed directory lookup**

Add an internal scan that counts only decoded `DirectoryEntryKind::Normal` entries. On `DirectoryEntryKind::End` or FAT-chain EOC before the requested logical index, return `Status::Ok` with `end=true`. Deleted/LFN/volume-label/dot entries must not consume the logical index. Propagate I/O and corruption errors unchanged.

Public wrapper:

```cpp
Status read_directory_entry(
    const char* path,
    uint32_t index,
    Entry& entry,
    bool& end)
{
    end = false;

    uint32_t directory_cluster = 0;
    const Status status =
        resolve_directory_cluster(
            path,
            directory_cluster);

    if (status != Status::Ok) {
        return status;
    }

    return read_directory_entry_at(
        directory_cluster,
        index,
        entry,
        end);
}
```

- [ ] **Step 7: Refactor existing list/read functions to use the shared resolver without changing behavior**

`list_directory()` should call `resolve_directory_cluster()` then existing `emit_directory()`. `read_file()` should call `resolve_entry()` for the final entry and preserve all existing cluster-chain/EOF validation.

- [ ] **Step 8: Run backend tests GREEN**

```bash
make build/host-fat32-mount-test
./build/host-fat32-mount-test
make test-host-filesystem
```

Expected: exit 0.

- [ ] **Step 9: Commit**

```bash
git add \
  kernel/filesystem/filesystem.hpp \
  kernel/filesystem/filesystem.cpp \
  kernel/filesystem/fat32.hpp \
  kernel/filesystem/fat32.cpp \
  tests/host/fat32_mount_test.cpp

git commit -m "Add VFS backend lookup primitives"
```

---

### Task 2: Implement regular-file descriptors and metadata

**Files:**
- Create: `kernel/filesystem/vfs.hpp`
- Create: `kernel/filesystem/vfs.cpp`
- Create: `tests/host/vfs_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `filesystem::stat_path(...)`, `filesystem::read_file(...)`.
- Produces: `initialize/open/read/close/stat/fstat` under `linux95::filesystem::vfs`.

- [ ] **Step 1: Create the public VFS header**

```cpp
#pragma once

#include "filesystem/filesystem.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::filesystem::vfs {

constexpr size_t kFileDescriptorCount = 64;
constexpr int kFirstFileDescriptor = 3;
constexpr size_t kDirectoryHandleCount = 32;
constexpr size_t kPathCapacity = 128;

struct FileStat {
    bool is_directory;
    uint32_t size;
};

struct DirectoryEntry {
    char name[13];
    bool is_directory;
    uint32_t size;
};

void initialize();
int open(const char* path, Status& status);
Status read(int fd, uint8_t* buffer, size_t size, size_t& bytes_read);
Status close(int fd);
Status stat(const char* path, FileStat& info);
Status fstat(int fd, FileStat& info);
int opendir(const char* path, Status& status);
Status readdir(int dir_handle, DirectoryEntry& entry, bool& end);
Status closedir(int dir_handle);

} // namespace linux95::filesystem::vfs
```

- [ ] **Step 2: Write RED host tests using a fake filesystem facade**

`tests/host/vfs_test.cpp` must define fake `filesystem::stat_path`, `filesystem::read_file`, and `filesystem::read_directory_entry` functions so `vfs.cpp` can be tested without ATA/FAT32. The fake backend recognizes `/README.TXT` and `README.TXT` as a 6-byte file containing `HELLO\n`, `/DOCS` and `DOCS` as directories, `/` as a directory, and unknown paths as `NotFound`.

Test these exact behaviors:
- first open returns fd 3,
- `fstat(3)` reports regular file size 6,
- two reads return `HE` then `LLO\n`,
- third read returns `Ok` with zero bytes,
- close then second close yields `InvalidDescriptor`,
- `open("/DOCS")` fails with `IsDirectory`,
- fd 0/1/2 are invalid for read/fstat/close,
- 61 opens consume fd 3..63; the 62nd fails `TooManyOpenFiles`,
- closing fd 3 makes the next open return 3,
- `initialize()` invalidates existing descriptors,
- 128-character path is rejected with `Unsupported`,
- injected backend `IoError` does not advance the file offset.

- [ ] **Step 3: Run RED**

```bash
g++ -std=c++17 -Wall -Wextra -Werror -O2 -Ikernel \
  tests/host/vfs_test.cpp \
  kernel/filesystem/vfs.cpp \
  -o /tmp/linux95-vfs-test
```

Expected: fail because `vfs.cpp` is not implemented yet.

- [ ] **Step 4: Implement fixed tables and safe path copy**

In `vfs.cpp`, define:

```cpp
struct FileDescriptor {
    bool in_use;
    char path[kPathCapacity];
    uint32_t offset;
    uint32_t size;
};

struct DirectoryHandle {
    bool in_use;
    char path[kPathCapacity];
    uint32_t next_index;
};

FileDescriptor file_descriptors[kFileDescriptorCount] = {};
DirectoryHandle directory_handles[kDirectoryHandleCount] = {};
```

`copy_path()` must reject null, empty, and any path that cannot fit including the terminator. `initialize()` zeroes both tables.

- [ ] **Step 5: Implement file API**

`open()` validates/copies the path, calls `stat()`, rejects directories, and allocates the lowest free fd from 3..63. `read()` calls the backend at the stored offset and advances only on `Status::Ok`; if the backend-reported size changes, return `Corrupt`. `close()` clears the slot. `fstat()` returns the stored regular-file size. `stat()` maps `filesystem::Entry` to `FileStat`.

Use this exact successful offset update guard:

```cpp
if (got >
    static_cast<size_t>(
        UINT32_MAX - slot.offset)) {
    return Status::Corrupt;
}

slot.offset +=
    static_cast<uint32_t>(got);
bytes_read = got;
```

- [ ] **Step 6: Add Makefile host target**

```make
$(BUILD)/host-vfs-test: \
	tests/host/vfs_test.cpp \
	kernel/filesystem/vfs.cpp \
	kernel/filesystem/vfs.hpp \
	kernel/filesystem/filesystem.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/vfs_test.cpp kernel/filesystem/vfs.cpp -o $@
```

Add it to `test-host-filesystem` prerequisites and execution lines.

- [ ] **Step 7: Run GREEN**

```bash
make build/host-vfs-test
./build/host-vfs-test
make test-host-filesystem
```

- [ ] **Step 8: Commit**

```bash
git add kernel/filesystem/vfs.hpp kernel/filesystem/vfs.cpp tests/host/vfs_test.cpp Makefile
git commit -m "Add read-only VFS file descriptors"
```

---

### Task 3: Add separate directory handles and `readdir`

**Files:**
- Modify: `kernel/filesystem/vfs.cpp`
- Modify: `tests/host/vfs_test.cpp`

**Interfaces:**
- Consumes: `filesystem::stat_path(...)`, `filesystem::read_directory_entry(...)`.
- Produces: `opendir/readdir/closedir`.

- [ ] **Step 1: Add RED directory tests**

Fake root directory:

```text
index 0 -> README.TXT, regular, size 6
index 1 -> DOCS, directory, size 0
index 2 -> end=true
```

Test:
- first directory handle is 0,
- successive `readdir` calls return README.TXT then DOCS then EOF,
- repeated EOF stays `Ok` with `end=true`,
- closed handle returns `InvalidHandle`,
- file path passed to `opendir` fails `NotDirectory`,
- 32 opens allocate 0..31; 33rd fails `TooManyOpenDirectories`,
- closing 0 makes 0 reusable,
- `initialize()` invalidates old handles,
- injected backend error does not advance `next_index`.

- [ ] **Step 2: Run RED**

```bash
make build/host-vfs-test
./build/host-vfs-test
```

Expected: assertion failure until directory operations exist.

- [ ] **Step 3: Implement handle validation and `opendir`**

`opendir()` uses the same fixed path copy and `stat()`; regular files return `NotDirectory`; allocate lowest free handle 0..31 with `next_index=0`.

- [ ] **Step 4: Implement `readdir` with stable error/EOF cursor**

```cpp
Status readdir(
    int dir_handle,
    DirectoryEntry& entry,
    bool& end)
{
    end = false;

    if (!valid_dir_handle(dir_handle)) {
        return Status::InvalidHandle;
    }

    DirectoryHandle& slot =
        directory_handles[dir_handle];

    Entry backend_entry = {};
    bool backend_end = false;

    const Status status =
        filesystem::read_directory_entry(
            slot.path,
            slot.next_index,
            backend_entry,
            backend_end);

    if (status != Status::Ok) {
        return status;
    }

    if (backend_end) {
        end = true;
        return Status::Ok;
    }

    for (size_t i = 0; i < 13; ++i) {
        entry.name[i] = backend_entry.name[i];
    }

    entry.is_directory = backend_entry.is_directory;
    entry.size = backend_entry.size;
    ++slot.next_index;
    return Status::Ok;
}
```

`closedir()` clears the selected slot.

- [ ] **Step 5: Run GREEN and commit**

```bash
make build/host-vfs-test
./build/host-vfs-test
make test-host-filesystem

git add kernel/filesystem/vfs.cpp tests/host/vfs_test.cpp
git commit -m "Add VFS directory handles"
```

---

### Task 4: Add boot-time VFS self-test and QEMU markers

**Files:**
- Create: `kernel/filesystem/vfs_self_test.hpp`
- Create: `kernel/filesystem/vfs_self_test.cpp`
- Modify: `kernel/kernel.cpp`
- Modify: `tests/qemu_smoke.py`
- Modify: `Makefile`

**Interfaces:**
- Consumes all VFS APIs.
- Produces seven required VFS debug markers.

- [ ] **Step 1: Make QEMU test RED**

Add these markers after `[PASS] filesystem_self_test` and before `[PASS] shell_ready` in `tests/qemu_smoke.py`:

```python
    "[PASS] vfs_initialize",
    "[PASS] vfs_file_open",
    "[PASS] vfs_file_read",
    "[PASS] vfs_stat",
    "[PASS] vfs_directory_open",
    "[PASS] vfs_readdir",
    "[PASS] vfs_self_test",
```

Run `make test-qemu`; expect missing-marker failure.

- [ ] **Step 2: Add `vfs_self_test.hpp`**

```cpp
#pragma once

namespace linux95::filesystem::vfs::self_test {

bool run();

} // namespace linux95::filesystem::vfs::self_test
```

- [ ] **Step 3: Implement boot self-test**

The self-test must open `/README.TXT` as fd 3, verify the 34-byte fixture content, verify `stat("/DOCS")` and `fstat(fd)`, close the file, open `/`, iterate until EOF and confirm README.TXT/CHAIN.TXT/DOCS, close the directory, and emit the markers at each successful stage. Any failure returns false; no descriptor/handle may remain open on success.

- [ ] **Step 4: Integrate boot initialization**

Add `vfs.hpp` and `vfs_self_test.hpp` includes to `kernel/kernel.cpp`. After existing filesystem self-test succeeds:

```cpp
filesystem::vfs::initialize();
debug::write("[PASS] vfs_initialize\n");

if (!filesystem::vfs::self_test::run()) {
    debug::write("[PANIC] vfs_self_test\n");
    panic::halt("VFS self-test failed");
}
```

- [ ] **Step 5: Add Makefile kernel objects and dependencies**

Add `vfs.o` and `vfs_self_test.o` to `KERNEL_OBJS`, compile each with the freestanding C++ flags, and make `kernel.o` depend on the two VFS headers.

- [ ] **Step 6: Run GREEN and commit**

```bash
make clean
make
make test
make test-qemu
grep -E '\[(BOOT|PASS|FAIL|PANIC)\]' build/qemu-debug.log

git add kernel/filesystem/vfs_self_test.hpp kernel/filesystem/vfs_self_test.cpp kernel/kernel.cpp tests/qemu_smoke.py Makefile
git commit -m "Add VFS boot self-test"
```

Expected: all existing and VFS markers, ending with `[PASS] shell_ready`.

---

### Task 5: Migrate `ls` and `cat` to VFS and enforce layering

**Files:**
- Modify: `kernel/terminal/shell.cpp`
- Modify: `tests/filesystem_source_checks.py`
- Modify: `Makefile`

**Interfaces:**
- Consumes VFS file/directory APIs.
- Produces shell behavior with no direct `filesystem::read_file` or `filesystem::list_directory` calls.

- [ ] **Step 1: Make the source check RED**

Read `vfs.hpp` in `tests/filesystem_source_checks.py` and require these strings in shell source:

```python
required_vfs_shell_calls = (
    "filesystem::vfs::open",
    "filesystem::vfs::read",
    "filesystem::vfs::close",
    "filesystem::vfs::opendir",
    "filesystem::vfs::readdir",
    "filesystem::vfs::closedir",
)
```

Reject `filesystem::read_file` and `filesystem::list_directory` in shell source. Apply the existing writable-API regex to both `filesystem.hpp` and `vfs.hpp`.

Run:

```bash
python3 tests/filesystem_source_checks.py
```

Expected: FAIL until shell migration.

- [ ] **Step 2: Migrate `ls`**

Include `filesystem/vfs.hpp`. Remove the old callback-only `print_entry`. `print_ls` must `opendir`, loop `readdir`, print each name plus `/` for directories, close on normal completion, and close before returning from any post-open error. Keep existing `ls:` error wording.

- [ ] **Step 3: Migrate `cat`**

`print_cat` must `open` once, repeatedly call `vfs::read` into the existing 128-byte buffer, print bytes, stop only on zero-byte successful read, and `close` on all post-open exits. Remove shell-owned file offset/size tracking. Keep existing `cat:` error wording and usage string.

- [ ] **Step 4: Fix shell Makefile dependencies**

```make
$(BUILD)/shell.o: \
	kernel/terminal/shell.cpp \
	kernel/terminal/shell.hpp \
	kernel/filesystem/filesystem.hpp \
	kernel/filesystem/vfs.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@
```

- [ ] **Step 5: Run GREEN and commit**

```bash
python3 tests/filesystem_source_checks.py
make test
make test-qemu

git add kernel/terminal/shell.cpp tests/filesystem_source_checks.py Makefile
git commit -m "Route shell file access through VFS"
```

---

### Task 6: Document the VFS milestone and perform final verification

**Files:**
- Modify: `README.md`

**Interfaces:** No new code interface.

- [ ] **Step 1: Update README**

Add verified-architecture bullets for the read-only VFS, 64-slot FD table with 0/1/2 reserved, 32 directory handles, both API groups, shell migration, and VFS boot self-tests. State that `cat` now reads through the VFS descriptor layer. Add all seven VFS QEMU markers after `[PASS] filesystem_self_test`. Keep writable FAT32, processes, syscalls, and ELF execution explicitly out of scope.

- [ ] **Step 2: Run a fresh full verification**

```bash
make clean
make
make test
make test-qemu
```

- [ ] **Step 3: Verify markers and read-only boundary**

```bash
grep -E '\[(BOOT|PASS|FAIL|PANIC)\]' build/qemu-debug.log

grep -R "storage::write_sector" kernel/filesystem && exit 1 || true

grep -R -E '\b(write|create|mkdir|unlink|rename|truncate)\s*\(' \
  kernel/filesystem/vfs.hpp && exit 1 || true

git diff --check
git status --short
```

Expected: VFS markers present, no write matches, no writable public VFS functions, no whitespace errors.

- [ ] **Step 4: Commit docs**

```bash
git add README.md
git commit -m "Document Linux95 VFS foundation"
```

- [ ] **Step 5: Final branch verification after commit**

```bash
make test
make test-qemu
git diff --check
git status --short
git log --oneline -8
```

Expected: all tests pass and working tree is clean.

- [ ] **Step 6: Whole-branch review before integration**

Review the feature branch against `v1.0-dev` for read-only boundary violations, fd 0/1/2 allocation, capacity off-by-one errors, cursor advancement on error/EOF, stale handle reuse, path-buffer overrun, shell VFS bypass, missing Makefile header dependencies, and FAT32 traversal regressions. Fix every accepted finding with a failing regression test before integration.

---

## Expected Commit Sequence

```text
Add VFS backend lookup primitives
Add read-only VFS file descriptors
Add VFS directory handles
Add VFS boot self-test
Route shell file access through VFS
Document Linux95 VFS foundation
```

The existing `Design Linux95 read-only VFS` commit stays before these implementation commits.

## Final Acceptance Criteria

- `open()` allocates regular files from fd 3 upward.
- fd 0/1/2 are never allocated as regular files.
- exactly 61 regular files can be simultaneously open in the 64-slot table.
- closed descriptors are reusable, lowest free first.
- successful reads advance per-fd offset exactly by `bytes_read`; failed reads do not advance.
- EOF is successful with zero bytes.
- `stat` and `fstat` return correct type/size.
- directory handles are separate and exactly 32 can be open.
- `readdir` returns one logical DOS 8.3 entry per call and never exposes dot entries.
- `readdir` does not advance on backend error or EOF.
- both leading-slash and root-relative paths work.
- shell `ls`/`cat` access files only through VFS.
- no VFS write/create/delete API exists.
- no filesystem code calls `storage::write_sector`.
- all pre-existing memory/storage/FAT32 tests pass.
- QEMU emits every required VFS marker and reaches `[PASS] shell_ready`.
- README reflects the shipped read-only VFS.
- final working tree is clean.
