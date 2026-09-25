# Linux95 Ring 3 Processes and Cooperative Scheduler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add real x86_64 Ring 3 ELF64 user processes, per-process address spaces, cooperative round-robin scheduling, dual `int 0x80` and `syscall/sysretq` system-call entry, and user-fault isolation without regressing the existing Linux95 desktop, FAT32, mouse, or RTL8139 networking.

**Architecture:** Linux95 keeps its graphical desktop/event loop as the Ring 0 scheduler host. Each user process owns a separate CR3 root, private user mappings, a user stack, and a supervisor-only kernel stack; kernel mappings are shared into every process page table with the U/S bit clear. User programs are static non-PIE ELF64 files loaded from FAT32, enter Ring 3 through `iretq`, call one shared dispatcher through either `int 0x80` or `syscall`, and cooperatively return to the kernel host on `yield`, `exit`, or a fatal user exception.

**Tech Stack:** freestanding C++17, NASM x86_64 assembly, legacy BIOS/QEMU `pc`, existing Linux95 paging/physical-memory/FAT32/VFS/IDT/PIT/desktop/network stack, Python QEMU smoke harness, host-side C++ tests.

**Spec:** `docs/superpowers/specs/2026-09-25-linux95-process-scheduler-user-mode-design.md`

## Global Constraints

- Keep the graphical desktop, PS/2 mouse/keyboard, FAT32/VFS, PCI/RTL8139, `ip`, and `ping` behavior working.
- Cooperative scheduling comes first; PIT-driven preemption is a separate follow-up milestone.
- Fixed process table size: **16** user-process slots.
- PID 0 is reserved for the kernel host; user PIDs start at 1.
- Each user process gets its own top-level page table, private user pages, user stack, and supervisor-only kernel stack.
- Kernel mappings copied/shared into a user address space must remain supervisor-only (`U/S = 0`).
- User mappings use 4 KiB pages and permissions derived from ELF segment flags.
- Support both DPL3 `int 0x80` and x86_64 `syscall/sysretq` from day one.
- Both syscall mechanisms feed one common dispatcher.
- Syscall ABI: `RAX=number`, `RDI,RSI,RDX,R10,R8,R9=args`, `RAX=result`.
- Initial syscall numbers are fixed: `0=write`, `1=yield`, `2=exit`.
- Initial userspace files are `/USER/INIT.ELF` and `/USER/WORKER.ELF`.
- ELF support is limited to little-endian x86_64 `ET_EXEC` images with `PT_LOAD`; reject dynamic/PIE/interpreter/relocation requirements.
- A fatal CPL3 exception terminates the offending process; a CPL0 exception keeps the existing panic behavior.
- No dynamic linker, shared libraries, fork, threads, signals, demand paging, copy-on-write, swapping, writable FAT32, user sockets, SMP, or preemption in this plan.
- Do not touch `release/`.
- No push, PR, or remote publication unless the user explicitly asks.
- Use TDD: establish RED before each production change and rerun the focused test before the full suite.
- Every task ends in a separate local commit after fresh verification.

## Review Focus

1. **Non-canonical or kernel-space SYSRET targets** — `sysretq` must never execute with an invalid user RIP/RSP; tests in Task 8 pin canonical-user-address validation and fallback-to-process-termination behavior.
2. **User buffers crossing page boundaries** — `write` must validate every covered page and reject overflow/kernel-only mappings; tests in Task 7 cover a buffer spanning two pages with the second page invalid.
3. **Malicious ELF arithmetic and overlapping segments** — the loader must reject integer overflow, file-range overflow, `p_filesz > p_memsz`, and overlapping user segments; tests in Task 4 cover all four.
4. **Accidentally user-accessible kernel mappings** — cloning the kernel side of a process page table must preserve `U/S = 0`; Task 3 adds a query test/source check that fails if a copied kernel mapping becomes user-accessible.
5. **CPL3 fault misclassified as a kernel panic** — a user page fault/GP/UD kills only that process while CPL0 faults still panic; Task 12 adds a dedicated QEMU negative test and source checks.

---

## File Structure

The plan introduces these focused units:

```text
kernel/
├── arch/x86_64/
│   ├── segments.hpp/.cpp        # kernel/user GDT selectors and descriptor construction
│   ├── segments.asm             # lgdt, CS reload, ltr helpers
│   ├── tss.hpp/.cpp             # 64-bit TSS storage and RSP0 updates
│   └── msr.hpp                  # rdmsr/wrmsr helpers for SYSCALL
├── memory/
│   └── user_space.hpp/.cpp      # per-process CR3 roots, user mappings, range validation/copy
├── process/
│   ├── process.hpp/.cpp         # fixed process table, PID/state/resource ownership
│   ├── scheduler.hpp/.cpp       # cooperative round-robin host-facing scheduler
│   ├── context.hpp              # normalized saved user CPU state
│   └── context.asm              # Ring3 entry/resume and return-to-host assembly
├── syscall/
│   ├── syscall.hpp/.cpp         # shared ABI and dispatcher
│   ├── int80_entry.asm          # DPL3 interrupt syscall entry
│   └── syscall_entry.asm        # SYSCALL/SYSRET entry
└── user/
    └── elf.hpp/.cpp             # ELF64 validation and process image loading

user/
├── include/linux95_syscall.hpp  # tiny freestanding syscall wrappers
├── crt/start.asm                # userspace _start
├── init/main.cpp                # PID1 test executable
├── worker/main.cpp              # PID2 test executable
└── user.ld                      # non-PIE fixed-address user linker script

tests/
├── host/segments_test.cpp
├── host/process_test.cpp
├── host/scheduler_test.cpp
├── host/user_space_test.cpp
├── host/elf_test.cpp
├── host/syscall_test.cpp
├── qemu_smoke.py
├── source_checks.py
└── prepare_fat32_image.py
```

Existing files expected to change:

```text
Makefile
kernel/kernel.cpp
kernel/arch/interrupts.cpp
kernel/arch/interrupts.hpp
kernel/memory/memory.hpp
kernel/memory/memory.cpp
kernel/memory/paging.cpp
kernel/memory/paging.hpp
kernel/gui/desktop.cpp
tests/qemu_smoke.py
tests/source_checks.py
tests/prepare_fat32_image.py
README.md
```

At execution time, create an isolated worktree from the current local `v1.0-dev` using the `superpowers:using-git-worktrees` workflow before Task 1. Do not develop this milestone directly in the main checkout.

---

### Task 1: Install a Kernel-Owned GDT, Ring 3 Selectors, and 64-bit TSS

**Files:**
- Create: `kernel/arch/x86_64/segments.hpp`
- Create: `kernel/arch/x86_64/segments.cpp`
- Create: `kernel/arch/x86_64/segments.asm`
- Create: `kernel/arch/x86_64/tss.hpp`
- Create: `kernel/arch/x86_64/tss.cpp`
- Create: `tests/host/segments_test.cpp`
- Modify: `Makefile`
- Modify: `kernel/kernel.cpp`
- Modify: `tests/source_checks.py`

**Interfaces:**
- Produces:
  - `constexpr uint16_t arch::x86_64::kKernelCodeSelector = 0x08`
  - `constexpr uint16_t arch::x86_64::kKernelDataSelector = 0x10`
  - `constexpr uint16_t arch::x86_64::kUserDataSelector = 0x1B`
  - `constexpr uint16_t arch::x86_64::kUserCodeSelector = 0x23`
  - `void arch::x86_64::initialize_segments()`
  - `void arch::x86_64::set_tss_rsp0(uint64_t rsp0)`
  - `uint64_t arch::x86_64::tss_rsp0()`

- [ ] **Step 1: Write the descriptor RED test**

Create `tests/host/segments_test.cpp`:

```cpp
#include <cassert>
#include <cstdint>
#include "arch/x86_64/segments.hpp"

int main() {
    using namespace linux95::arch::x86_64;

    static_assert(kKernelCodeSelector == 0x08);
    static_assert(kKernelDataSelector == 0x10);
    static_assert(kUserDataSelector == 0x1B);
    static_assert(kUserCodeSelector == 0x23);

    const uint64_t user_code = make_code_data_descriptor(
        0,
        0xFFFFF,
        DescriptorPrivilege::Ring3,
        SegmentKind::Code
    );

    // Present bit.
    assert((user_code & (1ULL << 47)) != 0);
    // DPL == 3.
    assert(((user_code >> 45) & 0x3ULL) == 3);
    // Executable code bit.
    assert((user_code & (1ULL << 43)) != 0);

    TssDescriptor tss = make_tss_descriptor(
        0x0000000012345000ULL,
        103
    );
    assert((tss.low & (1ULL << 47)) != 0);
    assert(((tss.low >> 40) & 0xFULL) == 0x9);
    assert(tss.high == 0);

    return 0;
}
```

Add a focused Makefile target `build/host-segments-test` and run it from `make test`.

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
make build/host-segments-test
```

Expected: compilation fails because `arch/x86_64/segments.hpp` and the descriptor helpers do not exist.

- [ ] **Step 3: Implement descriptor construction**

Define in `segments.hpp`:

```cpp
namespace linux95::arch::x86_64 {

constexpr uint16_t kKernelCodeSelector = 0x08;
constexpr uint16_t kKernelDataSelector = 0x10;
constexpr uint16_t kUserDataSelector = 0x1B;
constexpr uint16_t kUserCodeSelector = 0x23;
constexpr uint16_t kTssSelector = 0x28;

enum class DescriptorPrivilege : uint8_t {
    Ring0 = 0,
    Ring3 = 3,
};

enum class SegmentKind : uint8_t {
    Code,
    Data,
};

struct TssDescriptor {
    uint64_t low;
    uint64_t high;
};

constexpr uint64_t make_code_data_descriptor(
    uint32_t base,
    uint32_t limit,
    DescriptorPrivilege privilege,
    SegmentKind kind
);

constexpr TssDescriptor make_tss_descriptor(
    uint64_t base,
    uint32_t limit
);

void initialize_segments();

}
```

Build a six-entry GDT in `segments.cpp`:

```text
0: null
1: Ring0 code
2: Ring0 data
3: Ring3 data
4: Ring3 code
5-6: 64-bit TSS descriptor
```

Keep the existing Ring0 selector values `0x08` and `0x10` so current interrupt code does not silently change selector assumptions.

Implement `segments.asm` helpers for `lgdt`, reloading Ring0 data selectors, reloading `CS` with a far return/jump, and `ltr`.

- [ ] **Step 4: Implement the TSS**

In `tss.hpp`:

```cpp
namespace linux95::arch::x86_64 {

struct [[gnu::packed]] TaskStateSegment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
};

void initialize_tss();
void set_tss_rsp0(uint64_t rsp0);
uint64_t tss_rsp0();

}
```

Initialize `io_map_base = sizeof(TaskStateSegment)` so userspace cannot implicitly use an I/O bitmap.

- [ ] **Step 5: Wire initialization after early memory/interrupt prerequisites but before any Ring 3 work**

Add:

```cpp
arch::x86_64::initialize_segments();
arch::x86_64::initialize_tss();
```

to the kernel initialization path before the process subsystem is started.

Add source checks that require:
- Ring3 selectors with RPL 3,
- one `ltr` helper,
- one `set_tss_rsp0` implementation.

- [ ] **Step 6: Verify GREEN and regression**

Run:

```bash
make build/host-segments-test
./build/host-segments-test
make test
make test-qemu
nm -u build/kernel.elf
git diff --check
```

Expected:
- host descriptor test exits 0,
- current desktop/network/no-network tests still pass,
- `nm -u` prints nothing,
- diff check prints nothing.

- [ ] **Step 7: Commit**

```bash
git add Makefile kernel/arch/x86_64/segments.hpp kernel/arch/x86_64/segments.cpp \
  kernel/arch/x86_64/segments.asm kernel/arch/x86_64/tss.hpp \
  kernel/arch/x86_64/tss.cpp kernel/kernel.cpp tests/host/segments_test.cpp \
  tests/source_checks.py
git commit -m "Add Ring 3 segment and TSS foundation"
```

---

### Task 2: Add the Fixed Process Table and Pure Round-Robin Policy

**Files:**
- Create: `kernel/process/context.hpp`
- Create: `kernel/process/process.hpp`
- Create: `kernel/process/process.cpp`
- Create: `kernel/process/scheduler.hpp`
- Create: `kernel/process/scheduler.cpp`
- Create: `tests/host/process_test.cpp`
- Create: `tests/host/scheduler_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces:
  - `enum class process::State { Unused, Created, Ready, Running, Blocked, Exited }`
  - `enum class process::ReturnKind { Iret, Sysret }`
  - `struct process::UserContext`
  - `struct process::Process`
  - `void process::initialize()`
  - `Process* process::allocate()`
  - `Process* process::find(uint32_t pid)`
  - `void process::release(Process&)`
  - `size_t process::capacity()` returning 16
  - `int scheduler::choose_next(const Process* table, size_t count, int previous_slot)`

- [ ] **Step 1: Write process-table RED tests**

Create `tests/host/process_test.cpp`:

```cpp
#include <cassert>
#include "process/process.hpp"

int main() {
    using namespace linux95::process;

    initialize();
    assert(capacity() == 16);

    Process* first = allocate();
    Process* second = allocate();

    assert(first != nullptr);
    assert(second != nullptr);
    assert(first->pid == 1);
    assert(second->pid == 2);
    assert(first->state == State::Created);
    assert(second->state == State::Created);

    for (int i = 0; i < 14; ++i) {
        assert(allocate() != nullptr);
    }
    assert(allocate() == nullptr);

    const uint32_t old_pid = first->pid;
    release(*first);
    Process* replacement = allocate();
    assert(replacement != nullptr);
    assert(replacement->pid > old_pid);

    return 0;
}
```

- [ ] **Step 2: Write scheduler RED tests**

Create `tests/host/scheduler_test.cpp`:

```cpp
#include <cassert>
#include "process/scheduler.hpp"

int main() {
    using namespace linux95::process;

    Process table[4]{};

    table[0].state = State::Ready;
    table[1].state = State::Blocked;
    table[2].state = State::Ready;
    table[3].state = State::Exited;

    assert(linux95::scheduler::choose_next(table, 4, -1) == 0);
    assert(linux95::scheduler::choose_next(table, 4, 0) == 2);
    assert(linux95::scheduler::choose_next(table, 4, 2) == 0);

    table[0].state = State::Blocked;
    table[2].state = State::Exited;
    assert(linux95::scheduler::choose_next(table, 4, 2) == -1);

    return 0;
}
```

- [ ] **Step 3: Verify RED**

Run:

```bash
make build/host-process-test build/host-scheduler-test
```

Expected: compile fails because the process/scheduler APIs do not exist.

- [ ] **Step 4: Implement minimal process model**

`UserContext` must hold at least:

```cpp
struct UserContext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip;
    uint64_t rsp;
    uint64_t rflags;
    uint16_t cs;
    uint16_t ss;
    ReturnKind return_kind;
};
```

`Process` must include:

```cpp
struct Process {
    uint32_t pid;
    State state;
    uint64_t page_table_physical;
    uint64_t user_entry;
    uint64_t user_stack_top;
    uint64_t kernel_stack_base;
    uint64_t kernel_stack_top;
    UserContext context;
    int64_t exit_code;
    uint32_t fault_vector;
};
```

Back the table with:

```cpp
static Process g_processes[16]{};
static uint32_t g_next_pid = 1;
```

No heap allocation is required for the table itself.

- [ ] **Step 5: Implement pure round-robin selection**

`choose_next` must:
- begin scanning at `(previous_slot + 1) % count`,
- select only `State::Ready`,
- scan at most `count` entries,
- return `-1` if none are ready.

- [ ] **Step 6: Verify GREEN and full tests**

```bash
make build/host-process-test build/host-scheduler-test
./build/host-process-test
./build/host-scheduler-test
make test
git diff --check
```

- [ ] **Step 7: Commit**

```bash
git add Makefile kernel/process/context.hpp kernel/process/process.hpp \
  kernel/process/process.cpp kernel/process/scheduler.hpp \
  kernel/process/scheduler.cpp tests/host/process_test.cpp \
  tests/host/scheduler_test.cpp
git commit -m "Add process table and cooperative scheduler policy"
```

---

### Task 3: Build Per-Process User Address Spaces and User-Range Validation

**Files:**
- Create: `kernel/memory/user_space.hpp`
- Create: `kernel/memory/user_space.cpp`
- Create: `tests/host/user_space_test.cpp`
- Modify: `kernel/memory/paging.hpp`
- Modify: `kernel/memory/paging.cpp`
- Modify: `kernel/memory/memory.hpp`
- Modify: `kernel/memory/memory.cpp`
- Modify: `Makefile`
- Modify: `tests/source_checks.py`

**Interfaces:**
- Produces:
  - `struct memory::UserAddressSpace { uint64_t root_physical; }`
  - `enum class memory::UserAccess { Read, Write, Execute }`
  - `struct memory::PageInfo { bool present; bool user; bool writable; bool executable; uint64_t physical; }`
  - `bool memory::create_user_address_space(UserAddressSpace&)`
  - `void memory::destroy_user_address_space(UserAddressSpace&)`
  - `bool memory::map_user_page(UserAddressSpace&, uint64_t virt, uint64_t phys, bool writable, bool executable)`
  - `bool memory::query_page(uint64_t root_physical, uint64_t virt, PageInfo&)`
  - `bool memory::validate_user_range(uint64_t root_physical, uint64_t address, size_t length, UserAccess access)`
  - `bool memory::copy_from_user(uint64_t root_physical, void* dst, uint64_t src_user, size_t length)`

Use these fixed initial user virtual constants:

```cpp
constexpr uint64_t kUserImageBase  = 0x0000400000000000ULL;
constexpr uint64_t kUserImageLimit = 0x0000400100000000ULL;
constexpr uint64_t kUserStackTop   = 0x00007FFFFFF00000ULL;
constexpr size_t   kUserStackPages = 8;
```

The page immediately below the lowest mapped stack page stays unmapped as a guard.

- [ ] **Step 1: Write RED tests for range arithmetic**

Create `tests/host/user_space_test.cpp` around pure helpers exposed by `user_space.hpp`:

```cpp
#include <cassert>
#include <cstdint>
#include "memory/user_space.hpp"

int main() {
    using namespace linux95::memory;

    assert(is_canonical_user_address(kUserImageBase));
    assert(is_canonical_user_address(kUserStackTop - 8));
    assert(!is_canonical_user_address(0xFFFF800000000000ULL));

    assert(user_range_arithmetic_valid(kUserImageBase, 16));
    assert(!user_range_arithmetic_valid(UINT64_MAX - 3, 8));

    const UserPageSpan one = user_page_span(0x4000, 1);
    assert(one.first_page == 0x4000);
    assert(one.last_page == 0x4000);

    const UserPageSpan cross = user_page_span(0x4FFF, 2);
    assert(cross.first_page == 0x4000);
    assert(cross.last_page == 0x5000);

    return 0;
}
```

- [ ] **Step 2: Verify RED**

```bash
make build/host-user-space-test
```

Expected: missing header/functions.

- [ ] **Step 3: Implement pure canonical/range/page-span helpers**

Implement:
- lower canonical user range only: addresses `< 0x0000800000000000ULL`,
- overflow-safe `[address, address+length)` validation,
- zero-length ranges as valid only when the starting pointer itself is canonical,
- 4 KiB page rounding.

- [ ] **Step 4: Extend paging with page-query support**

Add a read-only page-table walk:

```cpp
bool query_page(
    uint64_t root_physical,
    uint64_t virtual_address,
    PageInfo& out
);
```

The walk must combine permissions across all page-table levels:
- if any level is not present: `present=false`,
- user access is allowed only if every traversed level has U/S set,
- writable only if every required level permits writes,
- executable is false if NX applies at any level when NXE is enabled.

Do not change existing kernel mappings while adding the query path.

- [ ] **Step 5: Implement process page-table roots**

`create_user_address_space` must:
1. allocate and zero a new PML4 page using the existing physical-page allocator,
2. copy/share existing kernel PML4 entries needed by Linux95,
3. never set the U/S bit on those copied kernel entries,
4. leave the PML4 slots that cover `kUserImageBase` and `kUserStackTop` available for private user mappings.

If the current kernel mapping occupies either chosen user PML4 slot, fail the new source/boot check rather than silently aliasing it; adjust the linker/user constants once in this task before proceeding.

- [ ] **Step 6: Implement user mappings and range validation**

`map_user_page` allocates intermediate page tables as needed with U/S set only along the user mapping branch.

`validate_user_range` walks every covered 4 KiB page. For `Write`, each page must be present, user-accessible, and writable. For `Execute`, each must be present, user-accessible, and executable.

This directly covers the review-focus case where a buffer begins in a valid page but crosses into an invalid second page.

Add a testable `validate_user_page_sequence` pure helper if needed by the host build, with a sequence such as:

```cpp
PageInfo pages[2] = {
    {true, true, true, false, 0x1000},
    {true, false, true, false, 0x2000},
};
assert(!validate_page_sequence(pages, 2, UserAccess::Read));
```

- [ ] **Step 7: Implement `copy_from_user`**

Copy at most one page fragment at a time:
1. validate the current user page,
2. translate it to physical/kernel-accessible memory using existing Linux95 physical-memory mapping conventions,
3. copy only up to the page boundary,
4. repeat.

Do not dereference a raw user virtual pointer while running on the kernel host CR3.

- [ ] **Step 8: Add source checks for isolation**

Require:
- user mappings set U/S,
- copied kernel PML4 entries do not gain U/S,
- kernel stacks are never passed to `map_user_page`,
- `validate_user_range` iterates every covered page.

- [ ] **Step 9: Verify**

```bash
make build/host-user-space-test
./build/host-user-space-test
make test
make test-qemu
nm -u build/kernel.elf
git diff --check
```

- [ ] **Step 10: Commit**

```bash
git add Makefile kernel/memory/user_space.hpp kernel/memory/user_space.cpp \
  kernel/memory/paging.hpp kernel/memory/paging.cpp kernel/memory/memory.hpp \
  kernel/memory/memory.cpp tests/host/user_space_test.cpp \
  tests/source_checks.py
git commit -m "Add isolated user address spaces"
```

---

### Task 4: Add Strict ELF64 Parsing and Segment Validation

**Files:**
- Create: `kernel/user/elf.hpp`
- Create: `kernel/user/elf.cpp`
- Create: `tests/host/elf_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces:
  - `enum class user::ElfStatus`
  - `struct user::ElfImageInfo`
  - `struct user::LoadSegment`
  - `ElfStatus user::inspect_elf64(const uint8_t* data, size_t size, ElfImageInfo&)`
  - `bool user::segment_permissions(uint32_t elf_flags, bool& writable, bool& executable)`

- [ ] **Step 1: Write malformed-input RED tests**

Create an in-memory minimal ELF fixture in `tests/host/elf_test.cpp` and cover:

```cpp
assert(inspect_elf64(valid.data(), valid.size(), info) == ElfStatus::Ok);
assert(info.entry == kUserImageBase);

auto bad_magic = valid;
bad_magic[0] = 0;
assert(inspect_elf64(bad_magic.data(), bad_magic.size(), info) == ElfStatus::BadMagic);

auto truncated = valid;
truncated.resize(sizeof(Elf64Header) - 1);
assert(inspect_elf64(truncated.data(), truncated.size(), info) == ElfStatus::Truncated);

auto filesz_gt_memsz = valid;
set_first_phdr_sizes(filesz_gt_memsz, 0x2000, 0x1000);
assert(inspect_elf64(filesz_gt_memsz.data(), filesz_gt_memsz.size(), info)
       == ElfStatus::InvalidSegment);

auto file_overflow = valid;
set_first_phdr_file_range(file_overflow, UINT64_MAX - 8, 32);
assert(inspect_elf64(file_overflow.data(), file_overflow.size(), info)
       == ElfStatus::RangeOverflow);

auto overlapping = two_segment_fixture_with_overlap();
assert(inspect_elf64(overlapping.data(), overlapping.size(), info)
       == ElfStatus::OverlappingSegments);
```

Also reject:
- wrong class,
- wrong endian,
- wrong machine,
- `ET_DYN`,
- `PT_INTERP`,
- entry outside executable `PT_LOAD`,
- segment outside `[kUserImageBase, kUserImageLimit)`.

- [ ] **Step 2: Verify RED**

```bash
make build/host-elf-test
```

Expected: missing ELF API.

- [ ] **Step 3: Implement packed on-disk ELF structs and overflow-safe parser**

Keep all parser arithmetic overflow checked before addition/multiplication.

Required ELF constants:
- `ELFCLASS64 = 2`
- `ELFDATA2LSB = 1`
- `ET_EXEC = 2`
- `EM_X86_64 = 62`
- `PT_LOAD = 1`
- `PT_INTERP = 3`
- `PF_X = 1`
- `PF_W = 2`
- `PF_R = 4`

Reject a program-header table whose `e_phoff + e_phnum * e_phentsize` exceeds the file.

Reject overlapping virtual `PT_LOAD` ranges after page expansion.

- [ ] **Step 4: Implement permission translation**

Rules:
- user code pages: readable/executable, not writable when `PF_W` is clear,
- user data pages: readable/writable, non-executable when `PF_X` is clear,
- never deliberately create RWX when the ELF does not request both W and X.

- [ ] **Step 5: Verify GREEN**

```bash
make build/host-elf-test
./build/host-elf-test
make test
git diff --check
```

- [ ] **Step 6: Commit**

```bash
git add Makefile kernel/user/elf.hpp kernel/user/elf.cpp tests/host/elf_test.cpp
git commit -m "Add strict ELF64 user image validation"
```

---

### Task 5: Build Freestanding ELF64 User Programs and Install Them in the FAT32 Fixture

**Files:**
- Create: `user/include/linux95_syscall.hpp`
- Create: `user/crt/start.asm`
- Create: `user/init/main.cpp`
- Create: `user/worker/main.cpp`
- Create: `user/user.ld`
- Modify: `Makefile`
- Modify: `tests/prepare_fat32_image.py`
- Modify: `tests/image_checks.py`

**Interfaces:**
- Produces:
  - `build/user/init.elf`
  - `build/user/worker.elf`
  - FAT32 files `/USER/INIT.ELF` and `/USER/WORKER.ELF`

- [ ] **Step 1: Add RED image checks**

Extend `tests/image_checks.py` so it fails until both user executables exist and validate as ELF64 x86_64 `ET_EXEC` images with entry points inside the configured user image window.

Also extend the FAT32 preparation check so the fixture contains a `USER` directory and both 8.3-compatible files.

Run:

```bash
python3 tests/image_checks.py
```

Expected: FAIL because user ELF files do not exist.

- [ ] **Step 2: Add the userspace linker script**

`user/user.ld`:

```ld
ENTRY(_start)

SECTIONS
{
    . = 0x0000400000000000;

    .text : ALIGN(4096) {
        *(.text.start)
        *(.text*)
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }
}
```

Link with `-nostdlib -static -no-pie` and ensure no interpreter segment is emitted.

- [ ] **Step 3: Add userspace `_start`**

`user/crt/start.asm` must define `_start`, call `user_main`, then invoke `exit` if `user_main` returns.

Do not use libc initialization.

- [ ] **Step 4: Define both syscall wrappers**

In `linux95_syscall.hpp` implement separate wrappers for both mechanisms.

Example signatures:

```cpp
namespace linux95::user {

long int80_call(long number, long a1 = 0, long a2 = 0, long a3 = 0);
long syscall_call(long number, long a1 = 0, long a2 = 0, long a3 = 0);

long write_int80(const char* data, unsigned long length);
long write_syscall(const char* data, unsigned long length);
[[noreturn]] void exit_int80(long code);
[[noreturn]] void exit_syscall(long code);
void yield_int80();
void yield_syscall();

}
```

Preserve the x86_64 syscall ABI requirement that arg4 uses `r10`, not `rcx`, even though the first three syscalls only need up to three args.

- [ ] **Step 5: Add initial programs**

`INIT.ELF`:

```cpp
extern "C" int user_main() {
    static constexpr char a[] = "[pid 1] hello through int 0x80\n";
    static constexpr char b[] = "[pid 1] resumed through syscall\n";

    linux95::user::write_int80(a, sizeof(a) - 1);
    linux95::user::yield_int80();
    linux95::user::write_syscall(b, sizeof(b) - 1);
    linux95::user::yield_syscall();
    return 0;
}
```

`WORKER.ELF`:

```cpp
extern "C" int user_main() {
    static constexpr char a[] = "[pid 2] hello through syscall\n";
    static constexpr char b[] = "[pid 2] resumed through int 0x80\n";

    linux95::user::write_syscall(a, sizeof(a) - 1);
    linux95::user::yield_syscall();
    linux95::user::write_int80(b, sizeof(b) - 1);
    return 0;
}
```

They are allowed to build before the kernel can execute them.

- [ ] **Step 6: Install into the deterministic FAT32 image**

Extend `tests/prepare_fat32_image.py` using the same FAT tooling already used by the fixture:
- create `/USER`,
- copy `build/user/init.elf` to `/USER/INIT.ELF`,
- copy `build/user/worker.elf` to `/USER/WORKER.ELF`.

Keep all existing FAT32 test files intact.

- [ ] **Step 7: Verify**

```bash
make build/user/init.elf build/user/worker.elf
python3 tests/image_checks.py
python3 tests/prepare_fat32_image.py build/linux95-storage-test.img
make test
git diff --check
```

- [ ] **Step 8: Commit**

```bash
git add Makefile user/include/linux95_syscall.hpp user/crt/start.asm \
  user/init/main.cpp user/worker/main.cpp user/user.ld \
  tests/prepare_fat32_image.py tests/image_checks.py
git commit -m "Add Linux95 ELF64 user programs"
```

---

### Task 6: Load ELF Segments From VFS Into a Process Address Space

**Files:**
- Modify: `kernel/user/elf.hpp`
- Modify: `kernel/user/elf.cpp`
- Modify: `kernel/process/process.hpp`
- Modify: `kernel/process/process.cpp`
- Create: `tests/host/elf_loader_plan_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes:
  - `memory::create_user_address_space`
  - `memory::map_user_page`
  - `user::inspect_elf64`
  - existing VFS read API
- Produces:
  - `user::ElfStatus user::load_process_image(const char* path, process::Process&)`
  - fully initialized `Process` with `page_table_physical`, `user_entry`, `user_stack_top`, kernel stack, and `State::Ready`

- [ ] **Step 1: Add RED tests for page-layout planning**

Before hardware-backed mapping, factor a pure loader plan:

```cpp
struct SegmentPagePlan {
    uint64_t first_page;
    uint64_t page_count;
    bool writable;
    bool executable;
    uint64_t file_offset;
    uint64_t file_bytes;
    uint64_t memory_bytes;
};

ElfStatus build_load_plan(
    const uint8_t* elf,
    size_t size,
    SegmentPagePlan* out,
    size_t out_capacity,
    size_t& out_count
);
```

Test:
- one unaligned segment spanning two pages,
- BSS where `p_memsz > p_filesz`,
- segment count exceeding output capacity,
- overlapping page-expanded segments.

- [ ] **Step 2: Verify RED**

```bash
make build/host-elf-loader-plan-test
```

- [ ] **Step 3: Implement load-plan generation and pass the host test**

Zero BSS bytes are represented explicitly by `memory_bytes > file_bytes`; never read BSS bytes from the file.

- [ ] **Step 4: Implement VFS-backed loading**

`load_process_image` must:
1. open/read the full ELF using existing read-only VFS interfaces,
2. validate it,
3. create a fresh user address space,
4. allocate one physical page for each planned user page,
5. zero every page before copying file bytes,
6. map with user permissions derived from ELF flags,
7. create an 8-page user stack below `kUserStackTop` with one unmapped guard page,
8. allocate a supervisor-only kernel stack,
9. set initial `UserContext`:
   - `rip = ELF entry`,
   - `rsp = kUserStackTop`,
   - `rflags = 0x202`,
   - `cs = kUserCodeSelector`,
   - `ss = kUserDataSelector`,
   - `return_kind = ReturnKind::Iret`,
10. set `State::Ready`.

On any failure, release every page allocated for that partially constructed process and return a non-`Ok` status.

- [ ] **Step 5: Add kernel-side initialization without entering Ring 3 yet**

At boot, load both files into process slots and emit debug markers only when each load succeeds:

```text
[PASS] process subsystem initialized
[PASS] pid1 ELF loaded
[PASS] pid2 ELF loaded
```

Do not enter user mode in this task.

- [ ] **Step 6: Verify**

```bash
make build/host-elf-loader-plan-test
./build/host-elf-loader-plan-test
make test
make test-qemu
nm -u build/kernel.elf
git diff --check
```

Existing QEMU tests must still pass even though the new user programs are only loaded, not executed.

- [ ] **Step 7: Commit**

```bash
git add Makefile kernel/user/elf.hpp kernel/user/elf.cpp \
  kernel/process/process.hpp kernel/process/process.cpp \
  tests/host/elf_loader_plan_test.cpp kernel/kernel.cpp
git commit -m "Load ELF64 images into user processes"
```

---

### Task 7: Add the Common Syscall Dispatcher, `int 0x80`, and Safe `write`

**Files:**
- Create: `kernel/syscall/syscall.hpp`
- Create: `kernel/syscall/syscall.cpp`
- Create: `kernel/syscall/int80_entry.asm`
- Create: `tests/host/syscall_test.cpp`
- Modify: `kernel/arch/interrupts.cpp`
- Modify: `kernel/arch/interrupts.hpp`
- Modify: `Makefile`
- Modify: `tests/source_checks.py`

**Interfaces:**
- Produces:
  - `struct syscall::Frame`
  - `enum class syscall::Action { ReturnToUser, YieldToHost, ExitToHost }`
  - `struct syscall::Result { int64_t value; Action action; }`
  - `Result syscall::dispatch(process::Process&, Frame&)`
  - interrupt symbol `int80_entry`
  - DPL3 IDT gate at vector `0x80`

- [ ] **Step 1: Write dispatcher RED tests**

`tests/host/syscall_test.cpp`:

```cpp
#include <cassert>
#include "syscall/syscall.hpp"

int main() {
    using namespace linux95;

    process::Process p{};
    p.pid = 1;
    p.state = process::State::Running;

    syscall::Frame unknown{};
    unknown.rax = 999;
    const auto unknown_result = syscall::dispatch_for_test(p, unknown);
    assert(unknown_result.value < 0);
    assert(unknown_result.action == syscall::Action::ReturnToUser);

    syscall::Frame yield{};
    yield.rax = 1;
    const auto yield_result = syscall::dispatch_for_test(p, yield);
    assert(yield_result.action == syscall::Action::YieldToHost);

    syscall::Frame exit{};
    exit.rax = 2;
    exit.rdi = 7;
    const auto exit_result = syscall::dispatch_for_test(p, exit);
    assert(exit_result.action == syscall::Action::ExitToHost);

    return 0;
}
```

`dispatch_for_test` exercises syscall-number/action logic without dereferencing real user memory.

- [ ] **Step 2: Verify RED**

```bash
make build/host-syscall-test
```

- [ ] **Step 3: Implement the dispatcher contract**

Define syscall numbers in one enum:

```cpp
enum class Number : uint64_t {
    Write = 0,
    Yield = 1,
    Exit = 2,
};
```

Unknown numbers return a stable negative error, for example `-38` (`ENOSYS`-style), without panicking.

- [ ] **Step 4: Install DPL3 `int 0x80`**

Extend the IDT gate builder so vector `0x80` is:
- present,
- 64-bit interrupt gate,
- DPL 3.

The assembly entry must save all registers required by `Frame`, then pass a pointer to a C++ bridge.

The bridge identifies the current process and calls the common dispatcher.

For an ordinary `write`, restore registers and return with `iretq`.

- [ ] **Step 5: Implement safe `write`**

Use ABI:

```text
RAX = 0
RDI = fd
RSI = user buffer
RDX = length
```

Rules:
- accept only fd 1 and 2,
- reject length > 4096,
- validate the entire user range before copying,
- copy into a fixed 4096-byte kernel buffer,
- emit through the existing debug output path,
- return number of bytes written.

Add the review-focus host test around a test validation provider:
- page 1 valid user readable,
- page 2 supervisor-only,
- buffer begins at offset 4095 with length 2,
- result must be rejected.

The production path must call `memory::validate_user_range`/`copy_from_user`, not raw-dereference `RSI`.

- [ ] **Step 6: Add source checks**

Require:
- vector `0x80`,
- DPL3 gate,
- `iretq` in `int80_entry.asm`,
- no direct `reinterpret_cast<const char*>(frame.rsi)` write path,
- max 4096-byte copy bound.

- [ ] **Step 7: Verify**

```bash
make build/host-syscall-test
./build/host-syscall-test
make test
make test-qemu
nm -u build/kernel.elf
git diff --check
```

- [ ] **Step 8: Commit**

```bash
git add Makefile kernel/syscall/syscall.hpp kernel/syscall/syscall.cpp \
  kernel/syscall/int80_entry.asm kernel/arch/interrupts.cpp \
  kernel/arch/interrupts.hpp tests/host/syscall_test.cpp \
  tests/source_checks.py
git commit -m "Add int80 syscall entry and safe write"
```

---

### Task 8: Add `SYSCALL/SYSRETQ` With a Safe Kernel-Stack Switch

**Files:**
- Create: `kernel/arch/x86_64/msr.hpp`
- Create: `kernel/syscall/syscall_entry.asm`
- Modify: `kernel/syscall/syscall.hpp`
- Modify: `kernel/syscall/syscall.cpp`
- Modify: `kernel/process/process.hpp`
- Modify: `kernel/arch/x86_64/tss.cpp`
- Modify: `tests/host/syscall_test.cpp`
- Modify: `tests/source_checks.py`
- Modify: `Makefile`

**Interfaces:**
- Produces:
  - `void syscall::initialize_fast_path()`
  - `bool syscall::valid_sysret_target(uint64_t rip, uint64_t rsp)`
  - `syscall_entry` symbol used as IA32_LSTAR
  - single-core CPU-local current-process/kernel-stack record

- [ ] **Step 1: Add SYSRET safety RED tests**

Append to `tests/host/syscall_test.cpp`:

```cpp
assert(valid_sysret_target(
    0x0000400000001000ULL,
    0x00007FFFFFEFF000ULL
));

assert(!valid_sysret_target(
    0xFFFF800000001000ULL,
    0x00007FFFFFEFF000ULL
));

assert(!valid_sysret_target(
    0x0000400000001000ULL,
    0xFFFF800000001000ULL
));

assert(!valid_sysret_target(
    0x0000800000000000ULL,
    0x00007FFFFFEFF000ULL
));
```

- [ ] **Step 2: Verify RED**

```bash
make build/host-syscall-test
```

- [ ] **Step 3: Implement MSR helpers and fast-path setup**

`msr.hpp` provides inline `rdmsr`/`wrmsr`.

Program:
- `IA32_EFER` SCE bit,
- `IA32_STAR`,
- `IA32_LSTAR = &syscall_entry`,
- `IA32_FMASK` to clear at least IF, DF, and TF on entry.

Build STAR from the actual selectors and add a host-test helper for its encoding.

- [ ] **Step 4: Implement CPU-local syscall stack state**

Because `syscall` does not use TSS.RSP0 automatically, keep a single-core structure containing:
- current process pointer,
- current process kernel stack top,
- temporary saved user RSP.

Initialize the GS base MSRs to this record and use `swapgs` on entry and exit.

The assembly order must not push onto the user stack after entering Ring 0:
1. `swapgs`,
2. save user RSP into CPU-local storage,
3. load kernel RSP from CPU-local storage,
4. push/save RCX (user RIP), R11 (user RFLAGS), user RSP, and GPRs,
5. call the shared C++ dispatcher.

- [ ] **Step 5: Implement safe direct `sysretq`**

For `Action::ReturnToUser`:
- restore result in RAX,
- validate canonical lower-half RIP/RSP,
- sanitize return RFLAGS,
- restore RCX/R11/RSP as required by SYSRET,
- `swapgs`,
- execute `sysretq`.

If validation fails, do **not** execute SYSRET. Mark the current process exited/faulted and return to the scheduler host path introduced in Task 9. Until Task 9 exists, the bridge may route this invalid-target case to a controlled kernel debug halt used only by the focused QEMU self-test; Task 9 must replace that temporary path before final acceptance.

- [ ] **Step 6: Add source checks**

Require:
- writes to EFER/STAR/LSTAR/FMASK,
- `swapgs`,
- kernel-stack switch before any normal call instruction,
- `sysretq`,
- canonical target validation.

- [ ] **Step 7: Verify**

```bash
make build/host-syscall-test
./build/host-syscall-test
make test
make test-qemu
nm -u build/kernel.elf
git diff --check
```

- [ ] **Step 8: Commit**

```bash
git add Makefile kernel/arch/x86_64/msr.hpp kernel/syscall/syscall_entry.asm \
  kernel/syscall/syscall.hpp kernel/syscall/syscall.cpp \
  kernel/process/process.hpp kernel/arch/x86_64/tss.cpp \
  tests/host/syscall_test.cpp tests/source_checks.py
git commit -m "Add syscall sysret entry path"
```

---

### Task 9: Add Ring 3 Entry/Resume and Cooperative Return to the Kernel Host

**Files:**
- Create: `kernel/process/context.asm`
- Modify: `kernel/process/context.hpp`
- Modify: `kernel/process/scheduler.hpp`
- Modify: `kernel/process/scheduler.cpp`
- Modify: `kernel/syscall/syscall.cpp`
- Modify: `kernel/syscall/int80_entry.asm`
- Modify: `kernel/syscall/syscall_entry.asm`
- Modify: `kernel/gui/desktop.cpp`
- Modify: `Makefile`
- Modify: `tests/qemu_smoke.py`
- Modify: `tests/source_checks.py`

**Interfaces:**
- Produces:
  - `bool scheduler::run_once()`
  - `[[noreturn]] scheduler::return_to_host(process::Process&, process::UserContext&, HostReason)`
  - assembly `process_resume_user`
  - assembly `process_restore_host`
  - `enum class scheduler::HostReason { Yield, Exit, Fault }`

- [ ] **Step 1: Add QEMU RED expectation for first Ring 3 entry**

Add a process-enabled QEMU test mode that expects:

```text
[PASS] entered ring3
[PASS] int80 syscall path
```

Do not change the normal test expectation yet.

Run the focused mode:

```bash
python3 tests/qemu_smoke.py --process-self-test
```

Expected: FAIL missing both markers.

- [ ] **Step 2: Implement host context save/restore**

`run_once()`:
1. choose next `Ready`,
2. mark it `Running`,
3. remember its slot as the round-robin cursor,
4. switch to the process CR3,
5. set TSS.RSP0 and CPU-local fast-syscall kernel stack,
6. save the kernel host callee-saved registers/RSP/resume RIP,
7. jump to `process_resume_user`.

`process_resume_user` restores the `UserContext` and enters user mode:
- `iretq` for `ReturnKind::Iret`,
- `sysretq` only when `ReturnKind::Sysret` and targets pass Task 8 validation.

- [ ] **Step 3: Make `yield` return to host**

When syscall number 1 is dispatched:
- capture the normalized user context from the current entry frame,
- set `state = Ready`,
- set the appropriate return kind,
- switch CR3 back to the kernel host root,
- restore the saved host context,
- return from `scheduler::run_once()`.

The dispatcher action must be `YieldToHost`; it must not busy-wait.

- [ ] **Step 4: Poll existing kernel services between user slices**

In the desktop/kernel event loop, call `scheduler::run_once()` at a bounded point after normal input/network polling.

Do not create an infinite scheduler loop that prevents:
- `network::poll()`,
- mouse processing,
- keyboard processing,
- dirty redraw processing.

One `run_once()` call runs at most one cooperative user slice until that process yields/exits/faults.

- [ ] **Step 5: Emit the first Ring 3 marker only after the transition really occurred**

The `int 0x80` bridge may emit:

```text
[PASS] entered ring3
[PASS] int80 syscall path
```

only after confirming the saved incoming CS has CPL3 (`CS & 3 == 3`).

- [ ] **Step 6: Verify focused QEMU GREEN**

```bash
python3 tests/qemu_smoke.py --process-self-test
make test
make test-qemu
nm -u build/kernel.elf
git diff --check
```

The process self-test should reach Ring 3 and return to the desktop host after `yield`.

- [ ] **Step 7: Commit**

```bash
git add Makefile kernel/process/context.asm kernel/process/context.hpp \
  kernel/process/scheduler.hpp kernel/process/scheduler.cpp \
  kernel/syscall/syscall.cpp kernel/syscall/int80_entry.asm \
  kernel/syscall/syscall_entry.asm kernel/gui/desktop.cpp \
  tests/qemu_smoke.py tests/source_checks.py
git commit -m "Enter Ring 3 and cooperatively yield"
```

---

### Task 10: Complete `exit` and Process Resource Cleanup

**Files:**
- Modify: `kernel/syscall/syscall.cpp`
- Modify: `kernel/process/process.hpp`
- Modify: `kernel/process/process.cpp`
- Modify: `kernel/process/scheduler.cpp`
- Modify: `tests/host/process_test.cpp`
- Modify: `tests/qemu_smoke.py`

**Interfaces:**
- Produces:
  - `void process::mark_exited(Process&, int64_t code)`
  - `void process::reap_exited()`
  - syscall `exit` never returns to the exiting user context

- [ ] **Step 1: Add RED lifecycle tests**

Append to `process_test.cpp`:

```cpp
Process p{};
p.pid = 42;
p.state = State::Running;

mark_exited(p, 7);
assert(p.state == State::Exited);
assert(p.exit_code == 7);

reap_one_for_test(p);
assert(p.state == State::Unused);
assert(p.pid == 0);
```

Resource cleanup test doubles must verify that user pages/page tables and kernel-stack pages are released exactly once.

- [ ] **Step 2: Verify RED**

```bash
make build/host-process-test
```

- [ ] **Step 3: Implement `exit`**

For syscall 2:
- record `RDI` as exit code,
- mark process `Exited`,
- never place it back into Ready,
- return to scheduler host,
- emit one process-exit debug marker in QEMU test mode.

- [ ] **Step 4: Implement reaping**

Reaping occurs from Ring 0 host context, never while still using the process's kernel stack.

Release:
- user data/code physical pages,
- private intermediate page-table pages,
- user stack pages,
- process kernel-stack pages.

Do not free shared kernel page-table structures/mappings.

- [ ] **Step 5: Verify**

```bash
make build/host-process-test
./build/host-process-test
python3 tests/qemu_smoke.py --process-self-test
make test
make test-qemu
git diff --check
```

- [ ] **Step 6: Commit**

```bash
git add kernel/syscall/syscall.cpp kernel/process/process.hpp \
  kernel/process/process.cpp kernel/process/scheduler.cpp \
  tests/host/process_test.cpp tests/qemu_smoke.py
git commit -m "Add user process exit and reaping"
```

---

### Task 11: Prove Two-Process Round-Robin Scheduling Through Both Syscall Paths

**Files:**
- Modify: `kernel/syscall/syscall.cpp`
- Modify: `kernel/process/scheduler.cpp`
- Modify: `tests/qemu_smoke.py`
- Modify: `user/init/main.cpp`
- Modify: `user/worker/main.cpp`

**Interfaces:**
- Consumes all prior process/syscall/user-program interfaces.
- Produces the final positive ordering guarantee for the cooperative milestone.

- [ ] **Step 1: Tighten QEMU RED expectations**

The process self-test must require these markers in this order:

```text
[PASS] process subsystem initialized
[PASS] pid1 ELF loaded
[PASS] pid2 ELF loaded
[PASS] entered ring3
[PASS] int80 syscall path
[PASS] syscall path
[PASS] cooperative process switch
[PASS] pid2 exited
[PASS] pid1 exited
[PASS] desktop remained online
```

Also capture user output and assert that all four messages appear:

```text
[pid 1] hello through int 0x80
[pid 2] hello through syscall
[pid 1] resumed through syscall
[pid 2] resumed through int 0x80
```

Run:

```bash
python3 tests/qemu_smoke.py --process-self-test
```

Expected: RED until both programs fully alternate and exit.

- [ ] **Step 2: Emit mechanism markers from actual entry paths**

- `int80 syscall path` marker is emitted only by `int80_entry` bridge from CPL3.
- `syscall path` marker is emitted only by the fast-syscall bridge after a successful CPL3-origin syscall.
- `cooperative process switch` is emitted only after scheduler observes one PID yield and a different PID become Running.

Do not emit markers merely because initialization functions were called.

- [ ] **Step 3: Make exit ordering deterministic**

Keep user programs as specified:
- PID1 yields twice,
- PID2 yields once,
- PID2 exits first,
- PID1 exits after being scheduled again.

The scheduler remains round-robin; do not special-case PIDs in scheduler code.

- [ ] **Step 4: Verify process QEMU test and all existing modes**

```bash
python3 tests/qemu_smoke.py --process-self-test
python3 tests/qemu_smoke.py
python3 tests/qemu_smoke.py --without-network
make test
make test-qemu
nm -u build/kernel.elf
git diff --check
```

- [ ] **Step 5: Commit**

```bash
git add kernel/syscall/syscall.cpp kernel/process/scheduler.cpp \
  tests/qemu_smoke.py user/init/main.cpp user/worker/main.cpp
git commit -m "Run cooperative Ring 3 processes"
```

---

### Task 12: Isolate Fatal User Exceptions From Kernel Panics

**Files:**
- Modify: `kernel/arch/interrupts.cpp`
- Modify: `kernel/arch/interrupts.hpp`
- Modify: `kernel/process/scheduler.hpp`
- Modify: `kernel/process/scheduler.cpp`
- Modify: `kernel/process/process.hpp`
- Create: `user/fault/main.cpp`
- Modify: `Makefile`
- Modify: `tests/prepare_fat32_image.py`
- Modify: `tests/qemu_smoke.py`
- Modify: `tests/source_checks.py`

**Interfaces:**
- Produces:
  - `bool process::handle_user_fault(uint8_t vector, uint64_t error_code, const InterruptFrame&)`
  - test-only `/USER/FAULT.ELF`
  - QEMU mode `--process-fault-test`

- [ ] **Step 1: Add the negative QEMU RED test**

Require:

```text
[PASS] user fault captured
[PASS] faulty process terminated
[PASS] kernel survived user fault
[PASS] desktop remained online
```

Run:

```bash
python3 tests/qemu_smoke.py --process-fault-test
```

Expected: FAIL because no fault program/isolation path exists.

- [ ] **Step 2: Add a test-only fault ELF**

`user/fault/main.cpp` deliberately reads from an unmapped canonical user address:

```cpp
extern "C" int user_main() {
    volatile const uint64_t* bad =
        reinterpret_cast<volatile const uint64_t*>(0x0000500000000000ULL);
    volatile uint64_t value = *bad;
    (void)value;
    return 1;
}
```

Build/install it only for the process-fault test image or test-mode FAT fixture. Do not launch it in a normal build.

- [ ] **Step 3: Classify exception origin using saved CS**

In the common exception path:

```cpp
const bool from_user = (frame.cs & 0x3U) == 0x3U;
```

For CPL3 fatal exceptions such as #PF, #GP, and #UD:
- store the vector/fault reason on the current process,
- mark it Exited,
- return to scheduler host,
- do not call kernel panic.

For CPL0:
- preserve the existing panic path unchanged.

- [ ] **Step 4: Ensure page-fault CR2/error diagnostics remain available**

Record CR2 for user page faults before returning to host so QEMU logs can show which user address faulted.

- [ ] **Step 5: Add source checks**

Require:
- CPL check based on saved CS,
- user-fault path references scheduler/process termination,
- Ring0 path still references existing panic behavior,
- no normal-build autostart of `FAULT.ELF`.

- [ ] **Step 6: Verify GREEN plus positive regression**

```bash
python3 tests/qemu_smoke.py --process-fault-test
python3 tests/qemu_smoke.py --process-self-test
make test
make test-qemu
nm -u build/kernel.elf
git diff --check
```

- [ ] **Step 7: Commit**

```bash
git add Makefile kernel/arch/interrupts.cpp kernel/arch/interrupts.hpp \
  kernel/process/scheduler.hpp kernel/process/scheduler.cpp \
  kernel/process/process.hpp user/fault/main.cpp \
  tests/prepare_fat32_image.py tests/qemu_smoke.py tests/source_checks.py
git commit -m "Isolate fatal user process faults"
```

---

### Task 13: Make Userspace Startup Optional and Non-Fatal

**Files:**
- Modify: `kernel/kernel.cpp`
- Modify: `kernel/process/process.cpp`
- Modify: `kernel/user/elf.cpp`
- Modify: `tests/qemu_smoke.py`
- Modify: `README.md`

**Interfaces:**
- Produces:
  - normal boot continues to desktop if user-process initialization or ELF loading fails safely,
  - explicit debug status for process subsystem enabled/disabled.

- [ ] **Step 1: Add a no-user-files QEMU RED mode**

Add a QEMU harness mode that boots with a FAT fixture omitting `/USER/INIT.ELF` and `/USER/WORKER.ELF`.

Require:

```text
[WARN] user_processes_offline
[PASS] desktop_online
```

Run:

```bash
python3 tests/qemu_smoke.py --without-user-programs
```

Expected: FAIL until boot degrades gracefully.

- [ ] **Step 2: Make boot failure non-fatal**

If process initialization, user address-space setup, or ELF loading fails:
- emit a clear warning,
- do not enter Ring 3,
- keep desktop/input/networking online where the kernel itself is healthy.

Do not hide malformed ELF errors; include a status code in debug logging.

- [ ] **Step 3: Document the boundary**

Update README:
- Linux95 now supports experimental Ring 3 static ELF64 user programs,
- cooperative scheduling,
- `int 0x80` and `syscall`,
- no preemption, fork, threads, dynamic linking, or POSIX compatibility yet.

- [ ] **Step 4: Verify**

```bash
python3 tests/qemu_smoke.py --without-user-programs
python3 tests/qemu_smoke.py --process-self-test
python3 tests/qemu_smoke.py --process-fault-test
make test
make test-qemu
git diff --check
```

- [ ] **Step 5: Commit**

```bash
git add kernel/kernel.cpp kernel/process/process.cpp kernel/user/elf.cpp \
  tests/qemu_smoke.py README.md
git commit -m "Keep user process startup non-fatal"
```

---

### Task 14: Final Integrated Verification and Manual Acceptance

**Files:**
- Modify only if a verification bug is discovered; otherwise no production changes.
- Update: `README.md` only if actual verified behavior differs from its current wording.

**Interfaces:**
- No new API. This task proves the completed milestone.

- [ ] **Step 1: Start from a clean build**

```bash
make clean
make all
```

Expected: successful normal kernel and user-ELF builds.

- [ ] **Step 2: Run full host/source/image tests**

```bash
make test
```

Expected all current checks PASS, including:
- process,
- scheduler,
- user-space range,
- ELF,
- syscall,
- existing memory/storage/filesystem/network checks.

- [ ] **Step 3: Run every QEMU mode**

```bash
make test-qemu
python3 tests/qemu_smoke.py --process-self-test
python3 tests/qemu_smoke.py --process-fault-test
python3 tests/qemu_smoke.py --without-network
python3 tests/qemu_smoke.py --without-user-programs
```

Expected positive process markers:

```text
[PASS] process subsystem initialized
[PASS] pid1 ELF loaded
[PASS] pid2 ELF loaded
[PASS] entered ring3
[PASS] int80 syscall path
[PASS] syscall path
[PASS] cooperative process switch
[PASS] pid2 exited
[PASS] pid1 exited
[PASS] desktop remained online
```

Expected fault markers:

```text
[PASS] user fault captured
[PASS] faulty process terminated
[PASS] kernel survived user fault
[PASS] desktop remained online
```

Existing real networking test must still include successful RTL8139 initialization, ARP gateway resolution, and ICMP echo reply.

- [ ] **Step 4: Run binary hygiene checks**

```bash
echo "=== UNRESOLVED SYMBOLS ==="
nm -u build/kernel.elf

echo
echo "=== DIFF CHECK ==="
git diff --check
```

Expected: no output from either check.

- [ ] **Step 5: Inspect branch status and history**

```bash
git status --short --branch
git log --graph --decorate --oneline -25
```

Expected:
- implementation branch clean except deliberately preserved unrelated untracked items if the worktree inherited any,
- one separate commit per completed task,
- no changes under `release/`.

- [ ] **Step 6: Manual normal-image acceptance**

Run:

```bash
make run
```

Verify in Linux95:
1. graphical desktop appears,
2. mouse reaches the top panel,
3. window redraw does not regress,
4. Terminal accepts commands,
5. `ip` reports RTL8139 configuration,
6. `ping 10.0.2.2` receives an ICMP reply,
7. the normal user-process test output appears,
8. user processes exit without freezing the desktop,
9. no kernel panic occurs.

- [ ] **Step 7: Record the milestone verification commit only if documentation/test metadata changed**

If Task 14 changes nothing, do not create an empty commit.

If README or test metadata needed a truthful final update:

```bash
git add README.md tests/
git diff --cached --check
git commit -m "Document Ring 3 process milestone"
```

- [ ] **Step 8: Stop before integration**

Do not merge into `v1.0-dev`, remove the worktree, push, or publish automatically.

Use the `superpowers:finishing-a-development-branch` workflow and let the user choose the integration action after all verification is green.

---

## Post-Milestone Follow-Up: Preemptive Scheduling

Preemption is deliberately not implemented by this plan.

After this branch is accepted and integrated, write a separate design/plan for PIT-driven preemption that reuses:
- `Process`,
- `UserContext`,
- per-process CR3,
- per-process kernel stacks,
- TSS.RSP0,
- the current syscall ABI,
- fault isolation.

That follow-up must add interrupt-safe scheduler rules, preemption disabling/critical sections, and time slices rather than modifying this cooperative milestone ad hoc.
