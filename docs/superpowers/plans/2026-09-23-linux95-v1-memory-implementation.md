# Linux95 Kernel v1.0 Memory Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Linux95 Kernel from its proven v0.2 low-memory execution model to a tested higher-half execution model with HHDM, an E820-backed 4 KiB physical-page allocator, a 4 KiB virtual-memory manager, in-kernel memory self-tests, and automated QEMU verification.

**Architecture:** Keep the existing BIOS Stage 1/Stage 2 contract and raw kernel load at physical `0x00100000`. The kernel first runs through the current low identity mapping, builds new page tables, retains the low mapping, adds a higher-half alias and HHDM, reloads `CR3`, then re-enters kernel code through the higher-half alias. Kernel code is built position-independently enough for the low and high aliases to share the same physical image; build-time relocation checks reject unsafe absolute text/data relocations.

**Tech Stack:** NASM, freestanding C++17, GNU `g++`, GNU `ld`/`objcopy`/`readelf`/`nm`, x86_64 4-level paging, BIOS E820, QEMU, Python 3 host tests.

**Spec:** `docs/superpowers/specs/2026-09-23-linux95-v1-memory-design.md`

## Global Constraints

- Work only on branch `v1.0-dev`; do not modify stable `main` or tag `v0.2.0`.
- Physical kernel load address remains `0x00100000`.
- Higher-half region base remains `0xFFFFFFFF80000000`.
- The physical kernel alias at 1 MiB is `0xFFFFFFFF80100000`.
- HHDM base remains `0xFFFF800000000000`.
- Bootstrap mapping uses 2 MiB huge pages.
- Normal VM mappings use 4 KiB pages.
- Stage 1 and Stage 2 boot flow remain compatible with the current raw image layout.
- Low identity mappings remain present until higher-half execution, HHDM, allocator, VM manager, and self-tests are proven.
- QEMU debug port `0xE9` remains the automated boot trace.
- No ATA, filesystem, networking, USB, audio, SMP, user mode, processes, or GUI in this milestone.
- The bootstrap HHDM mapper supports physical RAM up to 64 GiB. If E820 reports an address beyond 64 GiB, initialization must emit `[PANIC] bootstrap_hhdm_limit` and halt rather than silently managing unreachable RAM.

## Review Focus

1. **E820 regions crossing page boundaries:** usable memory must be rounded inward so partially reserved 4 KiB pages are never returned by the allocator; Task 6 includes a host test.
2. **Physical RAM above the 64 GiB bootstrap-HHDM limit:** the kernel must fail with an explicit panic checkpoint instead of truncating silently; Task 4 includes a QEMU/source test.
3. **Position-dependent relocations introduced by future C++ changes:** the build must reject unsafe absolute kernel relocations before boot; Task 1 adds a relocation audit.
4. **Double free or freeing a reserved page:** the page allocator must refuse the operation and preserve accounting; Task 6 includes host tests.
5. **Mapping over an existing 4 KiB mapping:** `map_page()` must return false and leave the old mapping untouched; Task 7 includes an in-kernel self-test.

---

## File Structure

### New files

- `kernel/memory/address.hpp` — address constants and alignment/HHDM helpers.
- `kernel/memory/page_bitmap.hpp` — hardware-independent bitmap allocator primitive.
- `kernel/memory/physical.hpp` — physical allocator public API.
- `kernel/memory/physical.cpp` — E820 discovery, reservation policy, page allocation.
- `kernel/memory/paging.hpp` — page flags, page-table entry helpers, VM API.
- `kernel/memory/paging.cpp` — 4 KiB page mapping, translation, unmapping.
- `kernel/memory/virtual.hpp` — higher-half/HHDM transition API.
- `kernel/memory/virtual.cpp` — bootstrap table construction and VM coordination.
- `kernel/memory/self_test.hpp` — memory self-test public API.
- `kernel/memory/self_test.cpp` — allocator/HHDM/mapping runtime tests.
- `kernel/arch/x86_64/control_regs.hpp` — CR3/TLB helpers.
- `kernel/arch/x86_64/paging_bootstrap.asm` — CR3 reload and high-alias re-entry trampoline.
- `tests/host/page_bitmap_test.cpp` — hosted allocator unit tests.
- `tests/memory_source_checks.py` — structural/constants/linker-source checks.
- `tests/relocation_checks.py` — linked ELF relocation/model audit.

### Modified files

- `kernel/entry.asm` — preserve boot handoff and support high-alias re-entry.
- `kernel/kernel.cpp` — split low bootstrap from normal kernel initialization and emit checkpoints.
- `kernel/memory/memory.cpp` — coordinate E820 statistics with the new allocator.
- `kernel/memory/memory.hpp` — expose validated map/max-physical-memory helpers.
- `kernel/terminal/shell.cpp` — make `mem` report new allocator/VM statistics.
- `kernel/panic/panic.cpp` — retain debug-port panic reporting.
- `linker.ld` — expose kernel physical boundaries and reserve bootstrap paging storage.
- `Makefile` — compile new objects, hosted tests, relocation audit, and stronger QEMU test.
- `tests/qemu_smoke.py` — require all v1.0 memory checkpoints.
- `tests/source_checks.py` — version/source expectations updated for v1.0 milestone.
- `README.md` — document v1.0-dev memory milestone only after verification.

---

### Task 1: Lock the v0.2 Baseline and Add Failing v1.0 Build Checks

**Files:**
- Modify: `Makefile`
- Create: `tests/memory_source_checks.py`
- Create: `tests/relocation_checks.py`
- Modify: `tests/qemu_smoke.py`

**Interfaces:**
- Consumes: existing `build/kernel.elf`, `build/linux95-kernel.img`, QEMU debug log.
- Produces: `make test-memory-source`, `make test-relocations`, and v1.0 checkpoint requirements.

- [ ] **Step 1: Verify the branch and clean baseline before editing**

Run:

```bash
git branch --show-current
git status --short
make clean
make
make test
make test-qemu
```

Expected before v1.0 edits:

```text
v1.0-dev
source checks: PASS
image checks: PASS
QEMU smoke test: PASS
```

If the branch is not `v1.0-dev` or the tree is dirty, stop and fix that before continuing.

- [ ] **Step 2: Write a failing memory source check**

Create `tests/memory_source_checks.py`:

```python
#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

required_files = [
    "kernel/memory/address.hpp",
    "kernel/memory/page_bitmap.hpp",
    "kernel/memory/physical.hpp",
    "kernel/memory/physical.cpp",
    "kernel/memory/paging.hpp",
    "kernel/memory/paging.cpp",
    "kernel/memory/virtual.hpp",
    "kernel/memory/virtual.cpp",
    "kernel/memory/self_test.hpp",
    "kernel/memory/self_test.cpp",
    "kernel/arch/x86_64/control_regs.hpp",
    "kernel/arch/x86_64/paging_bootstrap.asm",
]

required_literals = {
    "kernel/memory/address.hpp": [
        "0x00100000",
        "0xFFFFFFFF80000000",
        "0xFFFFFFFF80100000",
        "0xFFFF800000000000",
        "4096",
    ],
    "kernel/kernel.cpp": [
        "[BOOT] low_kernel_entry",
        "[PASS] higher_half_entry",
        "[PASS] physical_allocator_online",
        "[PASS] virtual_memory_online",
        "[PASS] memory_self_test",
    ],
}

errors = []

for relative in required_files:
    if not (ROOT / relative).is_file():
        errors.append(f"missing file: {relative}")

for relative, literals in required_literals.items():
    path = ROOT / relative
    if not path.is_file():
        errors.append(f"missing file: {relative}")
        continue
    text = path.read_text(errors="replace")
    for literal in literals:
        if literal not in text:
            errors.append(f"{relative}: missing {literal}")

if errors:
    print("memory source checks: FAIL")
    for error in errors:
        print(error)
    sys.exit(1)

print("memory source checks: PASS")
```

- [ ] **Step 3: Run the new source check and verify RED**

Run:

```bash
python3 tests/memory_source_checks.py
```

Expected: `memory source checks: FAIL` because the new v1.0 files do not exist yet.

- [ ] **Step 4: Write the relocation audit**

Create `tests/relocation_checks.py`:

```python
#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ELF = ROOT / "build/kernel.elf"

if not ELF.is_file():
    print("relocation checks: FAIL - build/kernel.elf missing")
    sys.exit(1)

result = subprocess.run(
    ["readelf", "-rW", str(ELF)],
    check=True,
    capture_output=True,
    text=True,
)

forbidden = (
    "R_X86_64_32 ",
    "R_X86_64_32S",
    "R_X86_64_64 ",
)

bad = [line for line in result.stdout.splitlines()
       if any(kind in line for kind in forbidden)]

if bad:
    print("relocation checks: FAIL")
    print("position-dependent relocations remain:")
    for line in bad:
        print(line)
    sys.exit(1)

print("relocation checks: PASS")
```

This audit is intentionally strict for linked runtime sections. If a legitimate linker-only symbol later produces one of these relocations, change the code to use an address-relative form instead of weakening the test.

- [ ] **Step 5: Update Makefile test targets but do not make them green yet**

Add:

```make
HOST_CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -O2 -Ikernel

test-memory-source:
>$(PYTHON) tests/memory_source_checks.py

test-relocations: all
>$(PYTHON) tests/relocation_checks.py
```

Add both targets to `.PHONY`, and make `test` call `test-memory-source` after the existing v0.2 source/image checks.

Do not add the final v1.0 QEMU markers to the normal green path yet; they belong in Task 9 after the runtime implementation exists.

- [ ] **Step 6: Commit the red-test harness**

```bash
git add Makefile tests/memory_source_checks.py tests/relocation_checks.py
git commit -m "test: define v1 memory foundation checks"
```

---

### Task 2: Add Address Constants and x86_64 Control-Register Helpers

**Files:**
- Create: `kernel/memory/address.hpp`
- Create: `kernel/arch/x86_64/control_regs.hpp`
- Test: `tests/memory_source_checks.py`

**Interfaces:**
- Produces:
  - `linux95::memory::kPageSize`
  - `linux95::memory::kHugePageSize`
  - `linux95::memory::kKernelPhysicalBase`
  - `linux95::memory::kKernelRegionBase`
  - `linux95::memory::kKernelVirtualBase`
  - `linux95::memory::kHhdmBase`
  - `align_down()`, `align_up()`, `physical_to_hhdm()`, `hhdm_to_physical()`
  - `linux95::arch::x86_64::read_cr3()`
  - `write_cr3(uint64_t)`
  - `invlpg(uint64_t)`

- [ ] **Step 1: Extend the failing source test for helper names**

Add these literals to `required_literals["kernel/memory/address.hpp"]`:

```python
"align_down",
"align_up",
"physical_to_hhdm",
"hhdm_to_physical",
```

Run:

```bash
python3 tests/memory_source_checks.py
```

Expected: FAIL.

- [ ] **Step 2: Implement `address.hpp`**

Create:

```cpp
#pragma once

#include <stdint.h>

namespace linux95::memory {

constexpr uint64_t kPageSize = 4096ULL;
constexpr uint64_t kHugePageSize = 2ULL * 1024ULL * 1024ULL;

constexpr uint64_t kKernelPhysicalBase = 0x00100000ULL;
constexpr uint64_t kKernelRegionBase = 0xFFFFFFFF80000000ULL;
constexpr uint64_t kKernelVirtualBase = 0xFFFFFFFF80100000ULL;
constexpr uint64_t kHhdmBase = 0xFFFF800000000000ULL;
constexpr uint64_t kBootstrapHhdmLimit = 64ULL * 1024ULL * 1024ULL * 1024ULL;

constexpr uint64_t align_down(uint64_t value, uint64_t alignment)
{
    return value & ~(alignment - 1ULL);
}

constexpr uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1ULL) & ~(alignment - 1ULL);
}

constexpr uint64_t physical_to_hhdm(uint64_t physical)
{
    return kHhdmBase + physical;
}

constexpr uint64_t hhdm_to_physical(uint64_t virtual_address)
{
    return virtual_address - kHhdmBase;
}

constexpr bool is_page_aligned(uint64_t value)
{
    return (value & (kPageSize - 1ULL)) == 0;
}

} // namespace linux95::memory
```

- [ ] **Step 3: Implement `control_regs.hpp`**

Create:

```cpp
#pragma once

#include <stdint.h>

namespace linux95::arch::x86_64 {

inline uint64_t read_cr3()
{
    uint64_t value;
    asm volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

inline void write_cr3(uint64_t value)
{
    asm volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

inline void invlpg(uint64_t virtual_address)
{
    asm volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

} // namespace linux95::arch::x86_64
```

- [ ] **Step 4: Run the focused source test**

```bash
python3 tests/memory_source_checks.py
```

Expected: still FAIL because later files are intentionally missing, but the errors for `address.hpp` and `control_regs.hpp` must disappear.

- [ ] **Step 5: Commit**

```bash
git add kernel/memory/address.hpp kernel/arch/x86_64/control_regs.hpp tests/memory_source_checks.py
git commit -m "feat: add v1 memory address primitives"
```

---

### Task 3: Make the Kernel Image Safe to Execute Through a High Alias

**Files:**
- Modify: `Makefile`
- Modify: `linker.ld`
- Modify: `kernel/entry.asm`
- Create: `kernel/arch/x86_64/paging_bootstrap.asm`
- Test: `tests/relocation_checks.py`
- Test: `tests/memory_source_checks.py`

**Interfaces:**
- Produces linker symbols:
  - `__kernel_phys_start`
  - `__kernel_phys_end`
  - `__bootstrap_pt_pool_start`
  - `__bootstrap_pt_pool_end`
- Produces assembly entry points:
  - `_start`
  - `linux95_reload_cr3_and_reenter(uint64_t new_cr3, uint64_t high_entry, uint64_t boot_info)`

- [ ] **Step 1: Add a linker-symbol source test**

Extend `tests/memory_source_checks.py` with:

```python
linker = (ROOT / "linker.ld").read_text(errors="replace")
for symbol in (
    "__kernel_phys_start",
    "__kernel_phys_end",
    "__bootstrap_pt_pool_start",
    "__bootstrap_pt_pool_end",
):
    if symbol not in linker:
        errors.append(f"linker.ld: missing {symbol}")
```

Run:

```bash
python3 tests/memory_source_checks.py
```

Expected: FAIL for the new symbols.

- [ ] **Step 2: Reserve a NOLOAD bootstrap page-table pool in `linker.ld`**

Keep the link origin at `0x100000`. Add explicit symbols around the kernel image and reserve 80 page-aligned 4 KiB bootstrap pages:

```ld
ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    __kernel_phys_start = .;

    .text : ALIGN(4096)
    {
        *(.text*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata*)
    }

    .data : ALIGN(4096)
    {
        *(.data*)
    }

    .bss : ALIGN(4096)
    {
        *(COMMON)
        *(.bss*)
    }

    .bootstrap_pt_pool (NOLOAD) : ALIGN(4096)
    {
        __bootstrap_pt_pool_start = .;
        . += 80 * 4096;
        __bootstrap_pt_pool_end = .;
    }

    __kernel_phys_end = .;
}
```

The pool is zeroed explicitly before use; do not assume BIOS/QEMU RAM starts at zero.

- [ ] **Step 3: Switch kernel C++ to PIE-style code generation**

Replace `-fno-pie -fno-pic` in `CXXFLAGS` with:

```make
-fpie
```

Keep `-mno-red-zone -mno-mmx -mno-sse -mno-sse2`.

Do not add libc or a dynamic runtime.

- [ ] **Step 4: Add the re-entry assembly helper**

Create `kernel/arch/x86_64/paging_bootstrap.asm`:

```asm
[BITS 64]

global linux95_reload_cr3_and_reenter

section .text

; rdi = new CR3 physical address
; rsi = higher-half entry virtual address
; rdx = BootInfo pointer to preserve in RDI after the jump
linux95_reload_cr3_and_reenter:
    cli

    mov cr3, rdi

    mov rax, rsi
    mov rdi, rdx
    jmp rax
```

The higher-half entry itself stays a C ABI function implemented in Task 5.

- [ ] **Step 5: Add the new assembly object to Makefile**

Add:

```make
$(BUILD)/paging_bootstrap.o: kernel/arch/x86_64/paging_bootstrap.asm | $(BUILD)
>$(NASM) -f elf64 $< -o $@
```

Add `build/paging_bootstrap.o` to `KERNEL_OBJS`.

- [ ] **Step 6: Build and run relocation audit**

Run:

```bash
make clean
make
python3 tests/relocation_checks.py
```

Expected: PASS. If it fails, inspect the exact listed relocation and rewrite that source expression to be RIP-relative/PIE-safe. Do not weaken the forbidden relocation list.

- [ ] **Step 7: Commit**

```bash
git add Makefile linker.ld kernel/entry.asm kernel/arch/x86_64/paging_bootstrap.asm tests/memory_source_checks.py
git commit -m "feat: prepare kernel for higher-half alias execution"
```

---

### Task 4: Build Bootstrap Page Tables and HHDM

**Files:**
- Create: `kernel/memory/virtual.hpp`
- Create: `kernel/memory/virtual.cpp`
- Modify: `kernel/memory/memory.hpp`
- Modify: `kernel/memory/memory.cpp`
- Modify: `kernel/kernel.cpp`
- Test: `tests/memory_source_checks.py`

**Interfaces:**
- Consumes: BootInfo E820 map, bootstrap pool linker symbols.
- Produces:
  - `uint64_t memory::maximum_physical_address(const BootInfo&)`
  - `bool virtual_memory::prepare_bootstrap(const BootInfo&)`
  - `uint64_t virtual_memory::bootstrap_cr3()`
  - `uint64_t virtual_memory::higher_half_alias(uint64_t low_address)`

- [ ] **Step 1: Add failing tests for bootstrap checkpoint strings**

Extend `required_literals["kernel/kernel.cpp"]` with:

```python
"[PASS] bootstrap_tables_created",
"[PASS] cr3_reloaded",
"[PASS] hhdm_online",
"[PANIC] bootstrap_hhdm_limit",
```

Run:

```bash
python3 tests/memory_source_checks.py
```

Expected: FAIL.

- [ ] **Step 2: Add maximum-physical-address calculation**

In `memory.hpp` expose:

```cpp
uint64_t maximum_physical_address(const linux95::BootInfo& boot_info);
```

In `memory.cpp`, iterate all E820 entries using overflow-safe end calculation:

```cpp
const uint64_t end =
    entry.length > UINT64_MAX - entry.base
        ? UINT64_MAX
        : entry.base + entry.length;
```

Track the largest end address.

- [ ] **Step 3: Define bootstrap paging API**

Create `virtual.hpp`:

```cpp
#pragma once

#include <stdint.h>

#include "boot_info.hpp"

namespace linux95::memory::virtual_memory {

bool prepare_bootstrap(const linux95::BootInfo& boot_info);
uint64_t bootstrap_cr3();
uint64_t higher_half_alias(uint64_t low_address);
bool hhdm_contains(uint64_t physical_address);

} // namespace linux95::memory::virtual_memory
```

- [ ] **Step 4: Implement an 80-page bootstrap table pool**

In `virtual.cpp` declare:

```cpp
extern "C" uint8_t __bootstrap_pt_pool_start[];
extern "C" uint8_t __bootstrap_pt_pool_end[];
```

Treat the low linker addresses as physical/identity-mapped pointers during bootstrap.

Before allocation, zero every byte in the pool explicitly.

Use a local page allocator:

```cpp
static uint64_t g_pool_next = 0;

static uint64_t allocate_bootstrap_table()
{
    const uint64_t start =
        reinterpret_cast<uint64_t>(__bootstrap_pt_pool_start);
    const uint64_t end =
        reinterpret_cast<uint64_t>(__bootstrap_pt_pool_end);
    const uint64_t address = start + g_pool_next;

    if (address + kPageSize > end) {
        return 0;
    }

    g_pool_next += kPageSize;
    return address;
}
```

Every returned table must be 4 KiB aligned and already zero.

- [ ] **Step 5: Map the three bootstrap regions with 2 MiB pages**

Build a new PML4 that contains:

1. Identity mapping for physical `0..2 MiB`.
2. Higher-half region mapping:
   - virtual `0xFFFFFFFF80000000..+2 MiB`
   - physical `0..2 MiB`.
3. HHDM mapping from `kHhdmBase` to physical `0..align_up(max_phys, 2 MiB)`.

Use 2 MiB PDE entries with flags:

```cpp
constexpr uint64_t kPresent = 1ULL << 0;
constexpr uint64_t kWritable = 1ULL << 1;
constexpr uint64_t kHuge = 1ULL << 7;
```

Reject `max_phys > kBootstrapHhdmLimit` and emit:

```cpp
debug::write("[PANIC] bootstrap_hhdm_limit\n");
```

then return `false`.

With an 80-page pool, the plan supports one PML4, required PDPTs, and enough PDs to map the full 64 GiB limit with 2 MiB pages.

- [ ] **Step 6: Compile only**

Run:

```bash
make clean
make
```

Expected: build succeeds. Do not perform the high jump yet.

- [ ] **Step 7: Commit**

```bash
git add kernel/memory/virtual.hpp kernel/memory/virtual.cpp \
        kernel/memory/memory.hpp kernel/memory/memory.cpp \
        kernel/kernel.cpp tests/memory_source_checks.py Makefile
git commit -m "feat: build bootstrap higher-half page tables"
```

---

### Task 5: Re-enter the Kernel Through the Higher-Half Alias

**Files:**
- Modify: `kernel/kernel.cpp`
- Modify: `kernel/terminal/vga.cpp`
- Modify: `tests/qemu_smoke.py`
- Test: `tests/qemu_smoke.py`

**Interfaces:**
- Consumes:
  - `virtual_memory::prepare_bootstrap()`
  - `virtual_memory::bootstrap_cr3()`
  - `linux95_reload_cr3_and_reenter()`
- Produces:
  - `extern "C" [[noreturn]] void linux95_higher_half_entry(const BootInfo*)`

- [ ] **Step 1: Make QEMU require only the transition checkpoint for this task**

Temporarily set `required` in `tests/qemu_smoke.py` to:

```python
required = [
    "[BOOT] low_kernel_entry",
    "[PASS] bootstrap_tables_created",
    "[PASS] cr3_reloaded",
    "[PASS] higher_half_entry",
]
```

Run:

```bash
make test-qemu
```

Expected: FAIL because the new markers are not all emitted yet.

- [ ] **Step 2: Split low bootstrap and high continuation in `kernel.cpp`**

Use this shape:

```cpp
extern "C" void linux95_reload_cr3_and_reenter(
    uint64_t new_cr3,
    uint64_t high_entry,
    uint64_t boot_info);

extern "C" [[noreturn]]
void linux95_higher_half_entry(const linux95::BootInfo* boot_info);

extern "C" void kernel_main(const linux95::BootInfo* boot_info)
{
    io::disable_interrupts();
    debug::write("[BOOT] low_kernel_entry\n");

    if (boot_info == nullptr ||
        boot_info->magic != linux95::kBootInfoMagic) {
        panic::halt("invalid BootInfo");
    }

    if (!memory::virtual_memory::prepare_bootstrap(*boot_info)) {
        panic::halt("bootstrap paging failed");
    }

    debug::write("[PASS] bootstrap_tables_created\n");

    const uint64_t low_entry =
        reinterpret_cast<uint64_t>(&linux95_higher_half_entry);
    const uint64_t high_entry =
        memory::virtual_memory::higher_half_alias(low_entry);

    debug::write("[PASS] cr3_reloaded\n");

    linux95_reload_cr3_and_reenter(
        memory::virtual_memory::bootstrap_cr3(),
        high_entry,
        reinterpret_cast<uint64_t>(boot_info));

    __builtin_unreachable();
}
```

The `cr3_reloaded` marker is written immediately before the assembly helper because no code can safely log between the `mov cr3` and the high jump.

- [ ] **Step 3: Implement high continuation**

Start the high continuation with:

```cpp
extern "C" [[noreturn]]
void linux95_higher_half_entry(const linux95::BootInfo* boot_info)
{
    debug::write("[PASS] higher_half_entry\n");

    const uint64_t physical_test = 0xB8000ULL;
    volatile uint16_t* hhdm_vga =
        reinterpret_cast<volatile uint16_t*>(
            memory::physical_to_hhdm(physical_test));

    const uint16_t original = hhdm_vga[0];
    hhdm_vga[0] = original;

    debug::write("[PASS] hhdm_online\n");

    // Remaining normal kernel initialization is added in Tasks 6-9.
    for (;;) {
        asm volatile("hlt");
    }
}
```

This first HHDM test reads/writes the same VGA cell value, avoiding a visible change while proving the alias resolves.

- [ ] **Step 4: Run QEMU transition test**

```bash
make clean
make
make test-qemu
```

Expected:

```text
[PASS] Linux95 booted in QEMU
...
[PASS] higher_half_entry
```

and no `[PANIC]`.

- [ ] **Step 5: Commit**

```bash
git add kernel/kernel.cpp tests/qemu_smoke.py
git commit -m "feat: execute kernel through higher-half alias"
```

---

### Task 6: Implement and Host-Test the 4 KiB Physical Page Allocator

**Files:**
- Create: `kernel/memory/page_bitmap.hpp`
- Create: `kernel/memory/physical.hpp`
- Create: `kernel/memory/physical.cpp`
- Create: `tests/host/page_bitmap_test.cpp`
- Modify: `Makefile`
- Modify: `kernel/kernel.cpp`

**Interfaces:**
- Produces:
  - `PageBitmap::initialize(uint8_t*, uint64_t)`
  - `PageBitmap::mark_used(uint64_t)`
  - `PageBitmap::mark_free(uint64_t)`
  - `PageBitmap::allocate()`
  - `physical::initialize(const BootInfo&)`
  - `physical::allocate_page()`
  - `physical::free_page(uint64_t)`
  - `physical::reserve_range(uint64_t, uint64_t)`
  - `physical::total_pages()`
  - `physical::free_pages()`
  - `physical::used_pages()`

- [ ] **Step 1: Write the hosted allocator test first**

Create `tests/host/page_bitmap_test.cpp`:

```cpp
#include <assert.h>
#include <stdint.h>

#include "memory/page_bitmap.hpp"

int main()
{
    uint8_t storage[2] = {};
    linux95::memory::PageBitmap bitmap;

    bitmap.initialize(storage, 16);

    assert(bitmap.capacity() == 16);
    assert(bitmap.free_count() == 0);

    for (uint64_t i = 0; i < 16; ++i) {
        assert(bitmap.mark_free(i));
    }

    assert(bitmap.free_count() == 16);

    const uint64_t a = bitmap.allocate();
    const uint64_t b = bitmap.allocate();

    assert(a != linux95::memory::PageBitmap::kInvalid);
    assert(b != linux95::memory::PageBitmap::kInvalid);
    assert(a != b);
    assert(bitmap.free_count() == 14);

    assert(bitmap.mark_used(a) == false);
    assert(bitmap.mark_free(a));
    assert(bitmap.mark_free(a) == false);
    assert(bitmap.free_count() == 15);

    bitmap.mark_used(15);
    assert(bitmap.free_count() == 14);

    return 0;
}
```

This pins double-free and already-used behavior.

- [ ] **Step 2: Add host-test Makefile target and verify RED**

Add:

```make
$(BUILD)/host-page-bitmap-test: tests/host/page_bitmap_test.cpp kernel/memory/page_bitmap.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-memory: $(BUILD)/host-page-bitmap-test
>$(BUILD)/host-page-bitmap-test
```

Run:

```bash
make test-host-memory
```

Expected: FAIL because `page_bitmap.hpp` does not exist.

- [ ] **Step 3: Implement `PageBitmap`**

Implement a fixed-storage class that:

- starts with all pages used,
- uses one bit per page,
- refuses indexes `>= page_count`,
- returns `kInvalid = UINT64_MAX` when full,
- changes free count only when state actually changes,
- uses first-fit allocation.

No dynamic allocation is allowed.

- [ ] **Step 4: Run hosted bitmap test GREEN**

```bash
make test-host-memory
```

Expected: exit 0.

- [ ] **Step 5: Add an E820 inward-rounding host case**

Extend the host test with helper assertions around page-range math:

```cpp
using linux95::memory::align_down;
using linux95::memory::align_up;

assert(align_up(0x1003, 4096) == 0x2000);
assert(align_down(0x2FFF, 4096) == 0x2000);
```

For an E820 usable interval `[base, base + length)`, physical allocator code must free only:

```cpp
const uint64_t first = align_up(base, kPageSize);
const uint64_t last = align_down(end, kPageSize);
```

and only if `last > first`.

- [ ] **Step 6: Implement `physical.cpp` E820 policy**

Initialization sequence:

1. Determine maximum physical address.
2. Place the bitmap at the first page-aligned usable E820 range large enough for the bitmap and below the 64 GiB HHDM limit.
3. Through HHDM, initialize every bitmap bit to used.
4. For each E820 type-1 region, inward-round and mark pages free.
5. Re-reserve:
   - page 0 through 1 MiB,
   - BootInfo page,
   - E820 map pages,
   - kernel `[__kernel_phys_start, __kernel_phys_end)`,
   - bootstrap page-table pool,
   - VGA `0xB8000..0xC0000`,
   - bitmap storage itself.
6. Never free or allocate an address `>= kBootstrapHhdmLimit`.

`allocate_page()` returns a physical byte address (`page_index * 4096`), not a page index.

- [ ] **Step 7: Emit allocator checkpoint in high continuation**

After `physical::initialize(*boot_info)` succeeds:

```cpp
debug::write("[PASS] physical_allocator_online\n");
```

On failure:

```cpp
debug::write("[PANIC] physical_allocator_init\n");
panic::halt("physical allocator init failed");
```

- [ ] **Step 8: Run host + build tests**

```bash
make clean
make
make test-host-memory
python3 tests/memory_source_checks.py
```

Expected: hosted memory test PASS; source test may still fail only for later paging/self-test files.

- [ ] **Step 9: Commit**

```bash
git add kernel/memory/page_bitmap.hpp kernel/memory/physical.hpp \
        kernel/memory/physical.cpp tests/host/page_bitmap_test.cpp \
        Makefile kernel/kernel.cpp
git commit -m "feat: add e820 physical page allocator"
```

---

### Task 7: Implement the 4 KiB Virtual Memory Manager

**Files:**
- Create: `kernel/memory/paging.hpp`
- Create: `kernel/memory/paging.cpp`
- Modify: `kernel/kernel.cpp`

**Interfaces:**
- Consumes: `physical::allocate_page()`, HHDM conversion, active `CR3`.
- Produces:
  - `enum class PageFlags : uint64_t`
  - `bool paging::map_page(uint64_t va, uint64_t pa, uint64_t flags)`
  - `bool paging::unmap_page(uint64_t va)`
  - `uint64_t paging::translate(uint64_t va)`
  - `bool paging::is_mapped(uint64_t va)`

- [ ] **Step 1: Add failing source requirements**

Add these literals to the paging source check:

```python
"map_page",
"unmap_page",
"translate",
"is_mapped",
```

Run:

```bash
python3 tests/memory_source_checks.py
```

Expected: FAIL.

- [ ] **Step 2: Define page-table index helpers**

In `paging.hpp` define:

```cpp
constexpr uint16_t pml4_index(uint64_t va) { return (va >> 39) & 0x1FF; }
constexpr uint16_t pdpt_index(uint64_t va) { return (va >> 30) & 0x1FF; }
constexpr uint16_t pd_index(uint64_t va)   { return (va >> 21) & 0x1FF; }
constexpr uint16_t pt_index(uint64_t va)   { return (va >> 12) & 0x1FF; }
```

Define flags:

```cpp
constexpr uint64_t kPagePresent = 1ULL << 0;
constexpr uint64_t kPageWritable = 1ULL << 1;
constexpr uint64_t kPageHuge = 1ULL << 7;
constexpr uint64_t kPageGlobal = 1ULL << 8;
constexpr uint64_t kPageNoExecute = 1ULL << 63;
constexpr uint64_t kAddressMask = 0x000FFFFFFFFFF000ULL;
```

- [ ] **Step 3: Implement table walking**

Rules:

- Read active PML4 physical address from `CR3 & kAddressMask`.
- Access every table page through `physical_to_hhdm()`.
- When an intermediate entry is absent, allocate one physical page, zero all 512 entries, and install it present+writable.
- If an intermediate entry has `kPageHuge`, return failure because Task 7's 4 KiB manager must not silently split bootstrap huge mappings.

- [ ] **Step 4: Implement `map_page()`**

Reject if VA or PA is not 4 KiB aligned.

Walk/create PML4 → PDPT → PD → PT.

If the PT entry is already present, return `false` without changing it.

Otherwise install:

```cpp
(physical & kAddressMask) | requested_flags | kPagePresent
```

and invalidate that VA with `invlpg`.

- [ ] **Step 5: Implement `translate()`**

Return `UINT64_MAX` when not mapped.

Support both:

- 2 MiB huge PDEs already created by bootstrap,
- 4 KiB PTE mappings.

For a huge PDE:

```cpp
physical_base = pde & 0x000FFFFFFFE00000ULL;
offset = va & 0x1FFFFFULL;
```

For a 4 KiB PTE:

```cpp
physical_base = pte & kAddressMask;
offset = va & 0xFFFULL;
```

- [ ] **Step 6: Implement `unmap_page()`**

Only unmap 4 KiB PTE mappings.

Return `false` for absent mappings or a huge-page mapping.

Clear the PTE and call `invlpg(va)`.

Do not free page-table pages yet; reclaiming empty tables is outside this milestone.

- [ ] **Step 7: Compile and commit**

```bash
make clean
make
git add kernel/memory/paging.hpp kernel/memory/paging.cpp \
        tests/memory_source_checks.py Makefile
git commit -m "feat: add 4k virtual memory manager"
```

---

### Task 8: Add In-Kernel Memory Self-Tests

**Files:**
- Create: `kernel/memory/self_test.hpp`
- Create: `kernel/memory/self_test.cpp`
- Modify: `kernel/kernel.cpp`

**Interfaces:**
- Consumes: physical allocator, HHDM, 4 KiB VM manager.
- Produces: `bool self_test::run()`.

- [ ] **Step 1: Add failing source check for the self-test API**

Require:

```python
"self_test::run",
```

from `kernel/kernel.cpp`, then run:

```bash
python3 tests/memory_source_checks.py
```

Expected: FAIL.

- [ ] **Step 2: Define self-test API**

Create `self_test.hpp`:

```cpp
#pragma once

namespace linux95::memory::self_test {

bool run();

} // namespace linux95::memory::self_test
```

- [ ] **Step 3: Implement allocator/HHDM test**

In `self_test.cpp`:

1. Record `free_pages()` before test.
2. Allocate three physical pages.
3. Verify none is `UINT64_MAX`.
4. Verify each is 4 KiB aligned.
5. Verify all three are distinct.
6. Convert each through `physical_to_hhdm()`.
7. Write distinct 64-bit patterns into the first 8 bytes.
8. Read the patterns back and compare.

Use patterns:

```cpp
0x1122334455667788ULL
0x8877665544332211ULL
0xA5A55A5AF0F00F0FULL
```

- [ ] **Step 4: Implement map/translate/unmap test**

Use dedicated test VA:

```cpp
constexpr uint64_t kTestVa = 0xFFFF900000100000ULL;
```

Requirements:

```cpp
assert-like check: !paging::is_mapped(kTestVa)
map_page(kTestVa, page_a, kPageWritable) == true
map_page(kTestVa, page_b, kPageWritable) == false
paging::translate(kTestVa) == page_a
write/read through reinterpret_cast<volatile uint64_t*>(kTestVa)
unmap_page(kTestVa) == true
paging::is_mapped(kTestVa) == false
```

The second `map_page` is the required "do not overwrite existing mapping" test.

- [ ] **Step 5: Free temporary pages and verify accounting**

After unmapping:

```cpp
physical::free_page(page_a);
physical::free_page(page_b);
physical::free_page(page_c);
```

Require:

```cpp
physical::free_pages() == free_before
```

Any failed check returns `false`.

- [ ] **Step 6: Wire self-test checkpoint**

In the high continuation:

```cpp
if (!memory::self_test::run()) {
    debug::write("[PANIC] memory_self_test\n");
    panic::halt("memory self-test failed");
}

debug::write("[PASS] memory_self_test\n");
```

- [ ] **Step 7: Build**

```bash
make clean
make
```

Expected: success.

- [ ] **Step 8: Commit**

```bash
git add kernel/memory/self_test.hpp kernel/memory/self_test.cpp \
        kernel/kernel.cpp tests/memory_source_checks.py Makefile
git commit -m "test: add in-kernel memory self tests"
```

---

### Task 9: Restore Full Kernel Initialization and Strengthen QEMU Automation

**Files:**
- Modify: `kernel/kernel.cpp`
- Modify: `tests/qemu_smoke.py`
- Modify: `kernel/terminal/shell.cpp`
- Modify: `kernel/terminal/shell.hpp`

**Interfaces:**
- Restores existing v0.2 IDT/PIC/PIT/keyboard/shell startup after memory self-tests.
- Produces final v1.0 QEMU checkpoint contract.

- [ ] **Step 1: Make QEMU demand the complete final checkpoint list**

Set:

```python
required = [
    "[BOOT] low_kernel_entry",
    "[PASS] bootstrap_tables_created",
    "[PASS] cr3_reloaded",
    "[PASS] higher_half_entry",
    "[PASS] hhdm_online",
    "[PASS] physical_allocator_online",
    "[PASS] virtual_memory_online",
    "[PASS] memory_self_test",
    "[PASS] shell_ready",
]
```

Enhance failure output to print the last matching checkpoint:

```python
seen = [marker for marker in required if marker in content]
if missing:
    print("last checkpoint:", seen[-1] if seen else "(none)")
```

Run:

```bash
make test-qemu
```

Expected: FAIL until normal kernel initialization is restored.

- [ ] **Step 2: Restore v0.2 device initialization after memory self-tests**

In higher-half continuation, after allocator/VM/self-test:

```cpp
interrupts::initialize();
pic::initialize();
pit::initialize(100);
keyboard::initialize();

io::enable_interrupts();
```

Then restore the existing banner and shell startup.

Immediately before `shell::run()`:

```cpp
debug::write("[PASS] shell_ready\n");
```

- [ ] **Step 3: Update the visible version banner**

Change visible kernel banner to:

```text
Linux95 Kernel v1.0 Memory Foundation
```

Do not claim full v1.0 OS completion; this is only the v1.0 memory milestone.

- [ ] **Step 4: Expand `mem` command**

The `mem` command must display:

- E820 total/usable bytes already available from v0.2 memory stats,
- physical allocator total pages,
- used pages,
- free pages,
- page size (`4096`),
- HHDM base,
- current CR3 physical address.

Use existing terminal integer/hex output helpers; add a `write_hex64()` helper only if the terminal does not already have one.

- [ ] **Step 5: Run the full QEMU test**

```bash
make clean
make
make test-qemu
```

Expected all required checkpoints and no `[PANIC]`.

- [ ] **Step 6: Commit**

```bash
git add kernel/kernel.cpp kernel/terminal/shell.cpp kernel/terminal/shell.hpp \
        tests/qemu_smoke.py
git commit -m "feat: complete v1 memory initialization path"
```

---

### Task 10: Integrate All Host, Image, Relocation, and QEMU Tests

**Files:**
- Modify: `Makefile`
- Modify: `tests/source_checks.py`
- Modify: `tests/image_checks.py`
- Modify: `tests/memory_source_checks.py`

**Interfaces:**
- Produces the final developer workflow:
  - `make`
  - `make test`
  - `make test-qemu`

- [ ] **Step 1: Make `make test` run every non-QEMU check**

Final target:

```make
test: all test-host-memory
>$(PYTHON) tests/source_checks.py
>$(PYTHON) tests/image_checks.py
>$(PYTHON) tests/memory_source_checks.py
>$(PYTHON) tests/relocation_checks.py
```

- [ ] **Step 2: Update image checks for kernel size and raw layout**

Keep assertions for:

- stage1 exactly 512 bytes,
- stage2 exactly 16 sectors,
- kernel starts at LBA 17,
- raw kernel <= current `KERNEL_SECTORS * 512`,
- image size matches `IMAGE_SECTORS * 512`.

If the new kernel binary exceeds the current 128-sector kernel budget, do **not** truncate it. Increase `KERNEL_SECTORS` and `IMAGE_SECTORS` together, update Stage 2's `KERNEL_SECTORS`, and add an image test that the Makefile and Stage 2 constants match.

- [ ] **Step 3: Update source checks to v1.0 milestone text**

Require:

```text
Linux95 Kernel v1.0 Memory Foundation
```

and reject accidental reintroduction of a `v0.1` banner.

- [ ] **Step 4: Run complete non-QEMU tests**

```bash
make clean
make
make test
```

Expected every host/source/image/relocation test PASS.

- [ ] **Step 5: Run QEMU**

```bash
make test-qemu
```

Expected final checkpoint PASS.

- [ ] **Step 6: Commit**

```bash
git add Makefile tests/source_checks.py tests/image_checks.py \
        tests/memory_source_checks.py tests/relocation_checks.py
git commit -m "test: integrate v1 memory verification pipeline"
```

---

### Task 11: Manual Visible Boot and Shell Verification

**Files:**
- No production source changes unless a test exposes a defect.

**Interfaces:**
- Validates keyboard/VGA behavior that the headless debug test does not visually exercise.

- [ ] **Step 1: Launch visible QEMU**

```bash
make run
```

Expected banner:

```text
Linux95 Kernel v1.0 Memory Foundation
Status: ONLINE
linux95>
```

- [ ] **Step 2: Run shell commands manually**

Enter:

```text
version
mem
uptime
help
clear
```

Expected:

- `version` reports v1.0 Memory Foundation.
- `mem` reports non-zero managed pages and `free < total`.
- `uptime` increases.
- keyboard input remains responsive.
- `clear` restores a usable prompt.

- [ ] **Step 3: Test unknown command and backspace**

Enter:

```text
banana
```

Expected unknown-command handling without panic.

Type a command with mistakes and Backspace; expected line editing remains functional.

- [ ] **Step 4: Do not commit if no source changed**

If the manual test exposes a bug, stop and use the systematic-debugging workflow before modifying code.

---

### Task 12: Documentation, Final Verification, and Milestone Checkpoint

**Files:**
- Modify: `README.md`
- No other files unless verification exposes a defect.

**Interfaces:**
- Produces a documented, reproducible `v1.0-dev` memory-foundation checkpoint.

- [ ] **Step 1: Update README with factual implemented scope**

Document only features verified in this milestone:

```text
- custom BIOS bootloader
- x86_64 long mode
- higher-half kernel alias
- HHDM
- E820-backed 4 KiB physical page allocator
- 4 KiB virtual-memory mappings
- IDT/PIC/PIT/PS2 keyboard
- VGA shell
- automated host/image/relocation/QEMU tests
```

Explicitly state that ATA/filesystem/network/GUI are not yet part of the kernel.

- [ ] **Step 2: Run the complete verification from a clean tree**

```bash
make clean
make
make test
make test-qemu
```

Required result:

```text
source checks: PASS
image checks: PASS
memory source checks: PASS
relocation checks: PASS
QEMU smoke test: PASS
```

The hosted bitmap test exits 0.

- [ ] **Step 3: Inspect the actual higher-half transition evidence**

Run:

```bash
cat build/qemu-debug.log
```

Require all nine markers in order and no `[PANIC]`.

- [ ] **Step 4: Inspect ELF and symbols**

Run:

```bash
readelf -h build/kernel.elf
nm -n build/kernel.elf | grep -E '__kernel_phys_(start|end)|__bootstrap_pt_pool_(start|end)'
```

Require:

- ELF entry remains the low bootstrap entry expected by Stage 2.
- kernel physical start is `0x100000`.
- bootstrap pool is page-aligned and lies inside the reserved kernel physical range.

- [ ] **Step 5: Commit documentation**

```bash
git add README.md
git commit -m "docs: document v1 memory foundation"
```

- [ ] **Step 6: Check final Git state**

```bash
git status
git log --oneline --decorate -12
```

Expected: branch `v1.0-dev`, clean working tree.

Do not merge to `main` and do not create a v1.0 release tag until the user explicitly asks after reviewing the working result.
