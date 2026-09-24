# Linux95 FAT32 Read-Only Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add safe read-only FAT32 support to Linux95 Kernel on the QEMU IDE slave disk, including directory listing, file reads, shell commands, host tests, QEMU self-tests, and CI coverage.

**Architecture:** Keep `storage::read_sector()` as the block layer. Add a FAT32 backend for BPB validation, FAT-chain traversal, directory parsing, path lookup, and file reads, then expose it through a small generic read-only filesystem facade used by the shell. The QEMU slave disk becomes a deterministic 64 MiB FAT32 superfloppy.

**Tech Stack:** Freestanding C++17, x86_64, NASM, GNU binutils, Python 3, QEMU, dosfstools, mtools, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-24-linux95-fat32-readonly-design.md`

## Global Constraints

- FAT32 starts at LBA 0 on `storage::DiskId::Test`; no MBR/GPT.
- Sector size is 512 bytes.
- Filesystem layer is read-only; `kernel/filesystem/` must never call `storage::write_sector()`.
- DOS 8.3 only; LFN entries are ignored.
- Lookup is ASCII case-insensitive.
- Cluster walking is bounded by validated cluster count.
- No STL containers, exceptions, RTTI, hosted runtime, AHCI, NVMe, USB, writable FAT, full VFS, permissions, or userspace file descriptors.
- Existing memory and ATA/storage tests stay mandatory.
- QEMU fixture is 64 MiB FAT32, one 512-byte sector per cluster.

## Review Focus

1. Malformed BPB arithmetic must fail without overflow/out-of-range reads.
2. Cyclic/free/bad/reserved FAT entries must return `Corrupt` without hanging.
3. A file size longer than its chain must return `Corrupt`, not read unrelated clusters.
4. Invalid 8.3/path boundaries must be rejected while mixed-case valid paths work.
5. ATA self-test sector 64 must be restored before FAT32 mount; marker order proves it.

---

### Task 1: Pure FAT32 helpers and host tests

**Files:**
- Create: `kernel/filesystem/fat32_helpers.hpp`
- Create: `tests/host/fat32_helpers_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces:
```cpp
uint16_t le16(const uint8_t*);
uint32_t le32(const uint8_t*);
bool is_power_of_two(uint32_t);
uint32_t fat28(uint32_t);
bool is_eoc(uint32_t);
bool is_bad_cluster(uint32_t);
bool is_free_cluster(uint32_t);
bool is_reserved_cluster(uint32_t);
bool checked_add_u32(uint32_t, uint32_t, uint32_t&);
bool checked_mul_u32(uint32_t, uint32_t, uint32_t&);
bool cluster_to_lba(uint32_t first_data, uint32_t spc, uint32_t cluster, uint32_t&);
char ascii_upper(char);
bool ascii_iequals(const char*, const char*);
bool format_short_name(const uint8_t raw[11], char out[13]);
bool valid_path_component(const char*, size_t);
```

- [ ] **Step 1: Write failing tests**

Create `tests/host/fat32_helpers_test.cpp`:
```cpp
#include "filesystem/fat32_helpers.hpp"
#include <assert.h>
#include <string.h>
using namespace linux95::filesystem::fat32::helpers;

int main() {
    const uint8_t le[] = {0x34,0x12,0x78,0x56};
    assert(le16(le) == 0x1234u);
    assert(le32(le) == 0x56781234u);
    assert(is_power_of_two(1) && is_power_of_two(8));
    assert(!is_power_of_two(0) && !is_power_of_two(3));
    assert(fat28(0xF1234567u) == 0x01234567u);
    assert(is_eoc(0x0FFFFFF8u));
    assert(is_bad_cluster(0x0FFFFFF7u));
    assert(is_free_cluster(0));

    uint32_t out = 0;
    assert(checked_add_u32(1,2,out) && out == 3);
    assert(!checked_add_u32(0xFFFFFFFFu,1,out));
    assert(checked_mul_u32(4,8,out) && out == 32);
    assert(!checked_mul_u32(0xFFFFFFFFu,2,out));
    assert(cluster_to_lba(100,4,3,out) && out == 104);
    assert(!cluster_to_lba(100,1,1,out));

    uint8_t raw[11] = {'R','E','A','D','M','E',' ',' ','T','X','T'};
    char name[13] = {};
    assert(format_short_name(raw, name));
    assert(strcmp(name, "README.TXT") == 0);
    assert(ascii_iequals("readme.txt", "README.TXT"));
    assert(valid_path_component("README.TXT", 10));
    assert(!valid_path_component("ABCDEFGHI.TXT", 13));
    return 0;
}
```

- [ ] **Step 2: Add Makefile target and confirm RED**
```make
$(BUILD)/host-fat32-helpers-test: tests/host/fat32_helpers_test.cpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-filesystem: $(BUILD)/host-fat32-helpers-test
>$(BUILD)/host-fat32-helpers-test
```
Run `make test-host-filesystem`; expected compile failure because the header does not exist.

- [ ] **Step 3: Implement minimal helpers**
Use checked arithmetic like:
```cpp
inline bool checked_add_u32(uint32_t a, uint32_t b, uint32_t& out) {
    if (b > 0xFFFFFFFFu - a) return false;
    out = a + b;
    return true;
}
inline bool checked_mul_u32(uint32_t a, uint32_t b, uint32_t& out) {
    if (a != 0 && b > 0xFFFFFFFFu / a) return false;
    out = a * b;
    return true;
}
```
`valid_path_component()` accepts base length 1-8 and optional extension length 1-3; reject slash, repeated dot, empty base, spaces, control bytes, and `.`/`..`.

- [ ] **Step 4: Run GREEN**
Run `make test-host-filesystem`; expected PASS.

- [ ] **Step 5: Commit**
```bash
git add kernel/filesystem/fat32_helpers.hpp tests/host/fat32_helpers_test.cpp Makefile
git commit -m "Add FAT32 helper tests"
```

---

### Task 2: Deterministic FAT32 test disk

**Files:**
- Create: `tests/prepare_fat32_image.py`
- Modify: `Makefile`
- Modify: `.github/workflows/linux95-ci.yml`

**Interfaces:** Produces `build/linux95-storage-test.img`, exactly 64 MiB, FAT32, label `LINUX95`, one sector/cluster, with `README.TXT`, `CHAIN.TXT`, and `DOCS/KERNEL.TXT`.

- [ ] **Step 1: Make image target fail first**
Change `prepare-storage-test-image` to call:
```make
>$(PYTHON) tests/prepare_fat32_image.py $(STORAGE_TEST_IMAGE)
```
Run `make prepare-storage-test-image`; expected FAIL because script is missing.

- [ ] **Step 2: Implement image creator**
Create:
```python
#!/usr/bin/env python3
from pathlib import Path
import subprocess, sys, tempfile

image = Path(sys.argv[1]).resolve()
image.parent.mkdir(parents=True, exist_ok=True)
image.unlink(missing_ok=True)
with image.open("wb") as f:
    f.truncate(64 * 1024 * 1024)

subprocess.run(["mkfs.fat","-F","32","-s","1","-n","LINUX95",str(image)], check=True)

with tempfile.TemporaryDirectory() as td:
    p = Path(td)
    (p/"README.TXT").write_bytes(b"Linux95 FAT32 filesystem online.\r\n")
    (p/"CHAIN.TXT").write_bytes(bytes(ord("A") + (i % 26) for i in range(1536)))
    (p/"KERNEL.TXT").write_bytes(b"Linux95 reads FAT32 subdirectories.\r\n")
    subprocess.run(["mmd","-i",str(image),"::DOCS"], check=True)
    subprocess.run(["mcopy","-o","-i",str(image),str(p/"README.TXT"),"::README.TXT"], check=True)
    subprocess.run(["mcopy","-o","-i",str(image),str(p/"CHAIN.TXT"),"::CHAIN.TXT"], check=True)
    subprocess.run(["mcopy","-o","-i",str(image),str(p/"KERNEL.TXT"),"::DOCS/KERNEL.TXT"], check=True)
```

- [ ] **Step 3: Verify fixture**
Run:
```bash
make prepare-storage-test-image
stat -c%s build/linux95-storage-test.img
mdir -i build/linux95-storage-test.img ::
mdir -i build/linux95-storage-test.img ::DOCS
mtype -i build/linux95-storage-test.img ::README.TXT
```
Expected size `67108864` and all three fixtures visible.

- [ ] **Step 4: Update CI dependencies**
Add `v1.0-fat32-dev` to push branches and packages:
```yaml
            dosfstools \
            mtools \
```

- [ ] **Step 5: Commit**
```bash
git add tests/prepare_fat32_image.py Makefile .github/workflows/linux95-ci.yml
git commit -m "Create deterministic FAT32 test image"
```

---

### Task 3: FAT32 mount and validated BPB

**Files:**
- Create: `kernel/filesystem/fat32.hpp`
- Create: `kernel/filesystem/fat32.cpp`
- Create: `kernel/filesystem/filesystem.hpp`
- Create: `kernel/filesystem/filesystem.cpp`
- Modify: `kernel/filesystem/fat32_helpers.hpp`
- Modify: `tests/host/fat32_helpers_test.cpp`
- Modify: `Makefile`

**Interfaces:**
```cpp
namespace linux95::filesystem {
enum class Status : uint8_t {
    Ok, NotMounted, IoError, InvalidFilesystem,
    NotFound, NotDirectory, IsDirectory, Corrupt, Unsupported
};
struct Entry { char name[13]; bool is_directory; uint32_t size; };
struct VolumeInfo {
    bool mounted;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t root_cluster;
};
using EntryVisitor = bool (*)(const Entry&, void*);
bool initialize();
Status list_directory(const char*, EntryVisitor, void*);
Status read_file(const char*, uint32_t, uint8_t*, size_t, size_t&, uint32_t&);
const VolumeInfo& volume_info();
}
```

- [ ] **Step 1: Add RED BPB tests**
Add:
```cpp
struct BpbGeometry {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t fat_begin_lba;
    uint32_t first_data_lba;
    uint32_t cluster_count;
};
bool parse_bpb(const uint8_t sector[512], uint32_t disk_sectors, BpbGeometry&);
```
Test valid BPB plus failures for bad `55AA`, bytes/sector !=512, sectors/cluster 0 or non-power-of-two, FAT count 0, root cluster <2, FS version !=0, metadata beyond disk, and arithmetic overflow.

Run `make test-host-filesystem`; expected FAIL.

- [ ] **Step 2: Implement `parse_bpb()`**
Decode offsets 11,13,14,16,32,36,42,44,510/511 explicitly. Use checked math for every derived value. Require `cluster_count >= 65525`.

Run host tests; expected PASS.

- [ ] **Step 3: Implement mount/facade skeleton**
`fat32::mount(storage::DiskId::Test)`:
1. clear old state,
2. verify ATA test disk present,
3. read LBA 0,
4. parse BPB against `storage::info(...).lba28_sector_count`,
5. copy only validated geometry,
6. set mounted.

At this task, `list_directory()`/`read_file()` may return `Unsupported`, but all public signatures compile.

- [ ] **Step 4: Build**
Add `fat32.o` and `filesystem.o` to `KERNEL_OBJS`.
Run:
```bash
make clean
make
make test-host-filesystem
```
Expected PASS.

- [ ] **Step 5: Commit**
```bash
git add kernel/filesystem tests/host/fat32_helpers_test.cpp Makefile
git commit -m "Mount validated FAT32 volumes"
```

---

### Task 4: Directories, paths, and safe file reads

**Files:**
- Modify: `kernel/filesystem/fat32_helpers.hpp`
- Modify: `kernel/filesystem/fat32.cpp`
- Modify: `kernel/filesystem/filesystem.cpp`
- Modify: `tests/host/fat32_helpers_test.cpp`

**Interfaces:** Internal `read_fat_entry()`, `find_entry()`, `resolve_path()`, plus complete `list_directory()` and `read_file()`.

- [ ] **Step 1: Add RED directory/path tests**
Test 32-byte entries for normal file, directory bit `0x10`, LFN `0x0F`, volume label `0x08`, deleted `0xE5`, end marker `0x00`, high+low cluster words, mixed-case lookup. Test invalid `DOCS//KERNEL.TXT`, `ABCDEFGHI.TXT`, `README.TXTT`, `.`, `..`.

Run `make test-host-filesystem`; expected FAIL.

- [ ] **Step 2: Implement pure directory decoder**
Decode byte offsets explicitly; do not cast untrusted disk bytes to packed structs.

- [ ] **Step 3: Implement FAT entry lookup**
For cluster `n`:
```text
offset = n * 4
sector = fat_begin_lba + offset / 512
inside = offset % 512
value = le32(sector + inside) & 0x0FFFFFFF
```
Return `Corrupt` for free/bad/reserved/out-of-range entries. EOC is `0x0FFFFFF8..0x0FFFFFFF`.

- [ ] **Step 4: Implement bounded directory/path traversal**
Process every 32-byte entry in every sector of a cluster; follow FAT after each cluster; cap transitions at `cluster_count`. Intermediate path components must be directories.

- [ ] **Step 5: Add RED file-read planning tests**
Cover offset==EOF, offset>EOF, 600-byte file with 512-byte clusters requiring 2 clusters, 1536-byte file requiring 3 clusters, and final partial-sector copy bounds.

- [ ] **Step 6: Implement `read_file()`**
Required behavior:
1. resolve path,
2. reject directory with `IsDirectory`,
3. expose file size,
4. return 0 bytes if offset>=EOF/capacity==0,
5. walk to cluster containing offset,
6. read only requested sectors/bytes,
7. never copy beyond file size or buffer,
8. if EOC arrives before file size is satisfied, return `Corrupt`,
9. stop exactly at file size,
10. cap cluster transitions at `cluster_count`.

- [ ] **Step 7: Run tests and commit**
```bash
make
make test-host-filesystem
make test
git add kernel/filesystem tests/host/fat32_helpers_test.cpp
git commit -m "Read FAT32 directories and files"
```

---

### Task 5: Boot-time FAT32 self-test and QEMU markers

**Files:**
- Create: `kernel/filesystem/filesystem_self_test.hpp`
- Create: `kernel/filesystem/filesystem_self_test.cpp`
- Modify: `kernel/kernel.cpp`
- Modify: `Makefile`
- Modify: `tests/qemu_smoke.py`

**Interfaces:** Produces:
```text
[PASS] fat32_mount
[PASS] fat32_root_list
[PASS] fat32_file_lookup
[PASS] fat32_file_read
[PASS] fat32_subdirectory
[PASS] fat32_cluster_chain
[PASS] filesystem_self_test
```

- [ ] **Step 1: Make QEMU test RED**
Add those seven markers to `required` in `tests/qemu_smoke.py` between storage and shell markers. Run `make test-qemu`; expected FAIL for missing FAT32 markers.

- [ ] **Step 2: Wire boot order**
After storage self-test and heap init:
```cpp
if (!filesystem::initialize()) {
    debug::write("[FAIL] fat32_mount\n");
    panic::halt("FAT32 mount failed");
}
debug::write("[PASS] fat32_mount\n");

if (!filesystem::self_test::run()) {
    debug::write("[PANIC] filesystem_self_test\n");
    panic::halt("Filesystem self-test failed");
}
```
Keep interrupts/PIC/PIT/keyboard after this block.

- [ ] **Step 3: Implement self-test**
Validate:
- root contains `README.TXT`, `CHAIN.TXT`, `DOCS`,
- README exact bytes `Linux95 FAT32 filesystem online.\r\n`,
- `DOCS/KERNEL.TXT` exact bytes,
- `CHAIN.TXT` is read in 128-byte chunks and every byte equals `'A' + (index % 26)`,
- emit each marker only after success.

- [ ] **Step 4: Build and verify order**
Run:
```bash
make clean
make
make test
make test-qemu
grep -nE 'ata_restore|storage_self_test|fat32_mount|filesystem_self_test|shell_ready' build/qemu-debug.log
```
Expected order: restore -> storage_self_test -> fat32_mount -> filesystem_self_test -> shell_ready.

- [ ] **Step 5: Commit**
```bash
git add kernel/filesystem/filesystem_self_test.* kernel/kernel.cpp Makefile tests/qemu_smoke.py
git commit -m "Add FAT32 QEMU self-test"
```

---

### Task 6: Shell commands, source invariants, docs, and final verification

**Files:**
- Modify: `kernel/terminal/shell.cpp`
- Create: `tests/filesystem_source_checks.py`
- Modify: `Makefile`
- Modify: `.github/workflows/linux95-ci.yml`
- Modify: `README.md`

**Interfaces:** Adds `fsinfo`, `ls [path]`, `cat <path>`.

- [ ] **Step 1: Add minimal command parser**
Split the existing 64-byte command buffer at first space:
```cpp
struct ParsedCommand { char* name; char* argument; };
```
Skip repeated spaces before argument. `cat` without argument prints usage.

- [ ] **Step 2: Implement commands**
`fsinfo` prints mounted state, test/slave, 512 bytes/sector, sectors/cluster, FAT count, sectors/FAT, total sectors, root cluster, read-only mode.

`ls` uses:
```cpp
bool print_entry(const filesystem::Entry& entry, void*) {
    vga::write(entry.name);
    if (entry.is_directory) vga::put_char('/');
    vga::put_char('\n');
    return true;
}
```

`cat` streams 128-byte chunks:
```cpp
uint8_t buffer[128];
uint32_t offset = 0;
for (;;) {
    size_t got = 0;
    uint32_t size = 0;
    auto s = filesystem::read_file(path, offset, buffer, sizeof(buffer), got, size);
    if (s != filesystem::Status::Ok) break;
    for (size_t i = 0; i < got; ++i) vga::put_char(static_cast<char>(buffer[i]));
    offset += static_cast<uint32_t>(got);
    if (got == 0 || offset >= size) break;
}
```

- [ ] **Step 3: Add source checks**
Create `tests/filesystem_source_checks.py` that fails unless:
- `fat32.cpp` contains `storage::read_sector`,
- no file under `kernel/filesystem/` contains `storage::write_sector`,
- FAT mask `0x0FFFFFFF` exists,
- LFN `0x0F` handling exists,
- cluster traversal references `cluster_count`,
- shell contains `fsinfo`, `ls`, `cat`,
- public facade has no create/delete/rename/truncate/mkdir/write API.

Wire it into `make test`.

- [ ] **Step 4: Update README and CI**
README documents FAT32 read-only foundation, 8.3 support, subdirectories, multi-cluster reads, commands, and non-goals. CI keeps `contents: read`, installs `dosfstools`/`mtools`, and runs on `v1.0-fat32-dev`.

- [ ] **Step 5: Manual shell check**
Run `make run`, then:
```text
fsinfo
ls
ls DOCS
cat README.TXT
cat DOCS/KERNEL.TXT
```
Expected valid FAT32 info, fixture listings, and exact text output.

- [ ] **Step 6: Full verification**
```bash
make clean
make
make test
make test-qemu
grep -E '\[(BOOT|PASS|FAIL|PANIC)\]' build/qemu-debug.log
grep -R "storage::write_sector" kernel/filesystem && exit 1 || true
git diff --check
git status --short
```
Expected: all test suites PASS, all FAT32 markers present, no FAIL/PANIC, no filesystem write call, no whitespace errors.

- [ ] **Step 7: Commit**
```bash
git add kernel/terminal/shell.cpp tests/filesystem_source_checks.py         Makefile .github/workflows/linux95-ci.yml README.md
git commit -m "Complete Linux95 FAT32 read-only foundation"
git push
```

- [ ] **Step 8: Reconcile license before PR**
PR #2 (`license-mit`) was still open when this plan was written. Merge it into `v1.0-dev`, then update `v1.0-fat32-dev` from the new base and rerun the entire Step 6 verification before opening the FAT32 PR. Do not duplicate the license manually.

- [ ] **Step 9: Final review gate**
Review the full branch specifically for BPB overflow, disk bounds, FAT loop termination, 8.3 parsing, shell buffer safety, and accidental writes. Resolve findings and rerun Step 6 before merge.
