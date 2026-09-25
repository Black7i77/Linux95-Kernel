# Linux95 Process, Scheduler, Ring 3, and User-Mode Design

**Date:** 2026-09-25
**Status:** Design for review
**Target branch:** `v1.0-dev`
**Milestone:** Linux95 user processes, cooperative scheduling, Ring 3, dual syscall entry, and ELF64 loading

## 1. Purpose

Linux95 already has a working graphical desktop, PS/2 keyboard and mouse input, paging and memory management, heap support, ATA storage, FAT32/VFS reading, PCI discovery, RTL8139 Ethernet, ARP, IPv4, ICMP, and terminal networking commands.

This milestone adds the next operating-system boundary: **separate user processes that execute in x86_64 Ring 3 and communicate with the kernel through system calls**.

The first version deliberately uses **cooperative scheduling**. Once the process model, address-space switching, fault isolation, and system-call paths are stable, the same scheduler model will be extended to PIT-driven preemption.

The existing desktop, storage, mouse, and networking behavior must remain functional throughout the work.

## 2. Success Criteria

The milestone is complete when Linux95 can:

1. Boot normally into the graphical desktop.
2. Discover two ELF64 programs on the FAT32 test disk:
   - `/USER/INIT.ELF`
   - `/USER/WORKER.ELF`
3. Create one process per program with a distinct PID.
4. Give each process:
   - its own page-table root,
   - private user code/data mappings,
   - a private user stack,
   - a private supervisor-only kernel stack,
   - a saved CPU context.
5. Enter each program in x86_64 Ring 3.
6. Execute system calls through both:
   - `int 0x80`
   - `syscall`
7. Support the initial syscall set:
   - `write`
   - `yield`
   - `exit`
8. Switch cooperatively between the two user processes.
9. Terminate a faulty user process without panicking the kernel.
10. Preserve desktop, FAT32, mouse, RTL8139, `ip`, and `ping` behavior.
11. Pass host tests, source/architecture checks, QEMU smoke tests, unresolved-symbol checks, and manual acceptance tests.

A representative successful trace is:

```text
[pid 1] hello through int 0x80
[pid 2] hello through syscall
[pid 1] resumed
[pid 2] resumed
[pid 2] exit
[pid 1] exit
```

## 3. Scope

### In scope

- Fixed-size process table.
- PIDs and process lifecycle states.
- Cooperative round-robin scheduling.
- Saved x86_64 user CPU context.
- Separate page-table root for each user process.
- Shared supervisor-only kernel mappings in each process address space.
- Per-process user stack.
- Per-process kernel stack.
- x86_64 TSS setup and `RSP0` updates.
- Ring 3 entry.
- DPL 3 `int 0x80` syscall gate.
- x86_64 `syscall/sysretq` entry path.
- One shared syscall dispatcher.
- `write`, `yield`, and `exit`.
- Minimal static ELF64 loader using the existing FAT32/VFS layer.
- User-mode exception isolation.
- Two small test user programs.
- QEMU automated validation.
- Transition-ready scheduler design for later PIT preemption.

### Explicitly out of scope for this milestone

- PIT-driven preemptive scheduling.
- Multicore/SMP scheduling.
- Fork/clone.
- Parent/child process trees.
- Signals.
- Threads.
- Dynamic linking.
- Shared libraries.
- PIE executables.
- ELF relocations.
- Demand paging.
- Copy-on-write.
- Swapping.
- Writable FAT32.
- User login/accounts.
- File descriptors beyond the minimal `write` interface.
- User-mode networking APIs or sockets.
- User-mode GUI applications.
- USB/audio expansion.
- Full POSIX compatibility.

These items can build on this milestone later without changing its core process boundary.

## 4. Process Model

Linux95 will use a fixed process table with **16 slots**.

Each slot contains enough state to describe one user process without requiring dynamic allocation in the scheduler hot path.

Conceptually:

```cpp
enum class ProcessState {
    Unused,
    Created,
    Ready,
    Running,
    Blocked,
    Exited,
};

enum class UserReturnKind {
    Iret,
    Sysret,
};

struct UserContext {
    // General-purpose registers required for restart.
    // Saved user RIP, RSP, RFLAGS, CS, and SS.
    // Return mechanism information.
};

struct Process {
    uint32_t pid;
    ProcessState state;

    uint64_t page_table_physical;
    uint64_t user_entry;
    uint64_t user_stack_top;

    uint64_t kernel_stack_base;
    uint64_t kernel_stack_top;

    UserContext context;

    int64_t exit_code;
};
```

The exact internal field arrangement can follow existing Linux95 coding conventions, but the observable model is fixed by this design.

### PID rules

- PID 0 is reserved for the kernel/scheduler host.
- User PIDs begin at 1.
- PIDs are monotonically allocated from a 32-bit counter.
- A process-table slot may be reused after the prior process reaches `Exited` and has been reaped.
- Reusing a slot does not reuse the old PID immediately.

### Process states

```text
Unused
  ↓
Created
  ↓
Ready
  ↓
Running
  ├── yield      → Ready
  ├── blocking   → Blocked
  ├── user fault → Exited
  └── exit       → Exited
```

`Blocked` exists in the model from day one, even though the first syscall set does not require a general blocking API. This avoids changing the public process-state model when timers, files, or sockets later need blocking waits.

## 5. Cooperative Scheduler

The first scheduler is cooperative and round-robin.

The kernel/desktop loop acts as the scheduler host. It remains Ring 0 code and is not one of the 16 user process slots.

The high-level flow is:

```text
kernel desktop/event loop
        ↓
scheduler selects next Ready process
        ↓
switch CR3 if required
        ↓
update TSS.RSP0
        ↓
restore user context
        ↓
enter/resume Ring 3
        ↓
user process calls yield/exit or faults
        ↓
save process context
        ↓
return to scheduler host
        ↓
desktop/network/storage polling continues
        ↓
scheduler selects next Ready process
```

This structure deliberately prevents a cooperative user program from bypassing the desktop/event-loop host during a normal `yield`. It keeps the graphical desktop and polling network stack responsive while process support is being proven.

### Selection policy

- Scan process slots round-robin starting after the most recently selected slot.
- Choose the next `Ready` process.
- If no user process is `Ready`, remain in the kernel host loop.
- `Exited` processes are never selected.
- `Blocked` processes are skipped until another subsystem marks them `Ready`.

### Cooperative limitation

A Ring 3 process that never issues a syscall and never faults can monopolize the CPU in this phase.

That is an accepted temporary limitation.

The later preemptive phase will make PIT timer ticks trigger scheduler entry without changing the process table, saved context model, or userspace ABI.

## 6. Address-Space Model

Every user process receives its own top-level x86_64 page table.

Each process address space contains two classes of mappings.

### Kernel mappings

Existing Linux95 kernel mappings are copied or shared into every process page-table hierarchy as appropriate.

They remain:

- present where required,
- supervisor-only (`U/S = 0`),
- writable only where the existing kernel requires it,
- executable only where required by the existing kernel mapping design.

Ring 3 must never be able to read or write a page merely because the kernel also maps it.

### User mappings

Each process receives private user mappings for:

- ELF loadable segments,
- user stack.

These pages have `U/S = 1`.

Writable and executable permissions are derived from the ELF segment flags instead of mapping every user page RWX.

### Page size

The initial user process implementation uses normal **4 KiB pages**.

Huge pages, demand paging, copy-on-write, and shared user pages are out of scope.

### User stack

Each process receives a private user stack.

Requirements:

- user-accessible,
- read/write,
- non-executable where the current paging implementation can express that safely,
- page-aligned,
- mapped below a guard page when practical with the current page-table API.

The initial stack contains no POSIX-style `argc/argv/envp` contract. The first two test programs require no command-line arguments.

### Kernel stack

Each process receives a private kernel stack that is:

- supervisor-only,
- never user-accessible,
- used for privilege transitions and syscall/exception handling for that process.

Before entering or resuming a user process, Linux95 sets `TSS.RSP0` to that process's kernel stack top.

## 7. TSS and Privilege Transition

Linux95 will install the x86_64 Task State Segment needed for Ring 3 to Ring 0 interrupt transitions.

The TSS is not used for hardware task switching.

It provides the Ring 0 stack pointer used when a privilege-changing interrupt or exception enters the kernel.

Requirements:

- valid 64-bit TSS descriptor in the GDT,
- `ltr` executed during initialization,
- `RSP0` points to the current process's kernel stack,
- scheduler updates `RSP0` before returning to each user process.

The first transition into a new Ring 3 process uses a controlled `iretq` frame containing the user code selector, user stack selector, entry RIP, RFLAGS, and user RSP.

## 8. System-Call ABI

Linux95 supports two system-call entry mechanisms from day one:

1. `int 0x80`
2. x86_64 `syscall`

Both entry paths normalize their input into one internal syscall frame and invoke the same C++ syscall dispatcher.

### Register ABI

```text
RAX = syscall number
RDI = argument 1
RSI = argument 2
RDX = argument 3
R10 = argument 4
R8  = argument 5
R9  = argument 6
```

Return value:

```text
RAX = result
```

Negative return values are reserved for errors in the initial ABI.

### Syscall numbers

```text
0 = write
1 = yield
2 = exit
```

These numbers are fixed once user programs are built against them.

## 9. `int 0x80` Entry

The IDT receives an interrupt gate for vector `0x80` with DPL 3 so Ring 3 code may invoke it.

On entry from Ring 3:

1. CPU performs the privilege transition.
2. CPU uses the current TSS `RSP0`.
3. Assembly saves required registers.
4. Entry code constructs/normalizes the common syscall frame.
5. Common dispatcher executes the syscall.
6. If the same process continues, registers are restored.
7. Return to user mode uses `iretq`.

The interrupt path must never trust user-controlled segment, pointer, or length values without validation.

## 10. `syscall/sysretq` Entry

Linux95 enables long-mode fast syscalls using the required MSRs:

- `IA32_EFER.SCE`
- `IA32_STAR`
- `IA32_LSTAR`
- `IA32_FMASK`

Unlike a privilege-changing interrupt, x86_64 `syscall` does **not** automatically switch to the TSS kernel stack.

Linux95 therefore establishes a minimal single-core kernel CPU-local syscall state.

The entry path must:

1. preserve the user RSP before using any kernel stack,
2. switch to the current process's supervisor-only kernel stack,
3. save the user return RIP and RFLAGS supplied by the architecture,
4. save the register state needed by the common syscall frame,
5. invoke the same C++ dispatcher used by `int 0x80`.

A minimal GS-based CPU-local record may be used so the assembly stub can safely obtain the active kernel stack before touching user memory. If this approach is used, the kernel initializes the required GS-base MSRs and uses `swapgs` symmetrically on syscall entry/exit.

### `sysretq` safety

Before returning with `sysretq`, Linux95 validates that the saved user RIP and RSP are canonical user-space addresses and that return flags are acceptable.

If those checks fail, the current process is terminated instead of executing an unsafe `sysretq`.

The `syscall` path uses `sysretq` for a valid direct return to the same process.

Scheduler-driven resumption of an arbitrary saved process may use its saved return kind so that a process entered through `int 0x80` resumes through `iretq` and a process safely resumable through the fast path may use `sysretq`.

## 11. Common Syscall Dispatcher

Both syscall entry stubs call one dispatcher.

Conceptually:

```cpp
int64_t dispatch(Process& process, SyscallFrame& frame);
```

The dispatcher:

- validates the syscall number,
- validates all user pointers and lengths before kernel dereference,
- invokes the corresponding implementation,
- stores the result in `RAX`,
- requests scheduler action when required.

Unknown syscall numbers return an error and do not panic the kernel.

## 12. Initial Syscalls

### `write`

Purpose: allow the first user programs to emit text through a controlled kernel path.

Proposed ABI:

```text
RAX = 0
RDI = fd
RSI = user buffer
RDX = byte length
```

Initial behavior:

- only stdout/stderr-style descriptors required by the test programs are accepted,
- maximum bytes per call: 4096,
- kernel validates the complete user buffer range page-by-page,
- buffer must refer only to user-accessible mapped pages,
- output is forwarded through a small kernel output sink and mirrored to the QEMU-visible debug path used by automated tests.

Because phase 1 is single-core and cooperative, the user process cannot concurrently remap or mutate its address space while the kernel validates and copies the same buffer.

### `yield`

Purpose: voluntarily return control to the cooperative scheduler.

Proposed ABI:

```text
RAX = 1
```

Behavior:

- save the current user context,
- mark the process `Ready`,
- return to the kernel scheduler host,
- allow desktop/network polling,
- round-robin to the next ready process on the next scheduling point.

### `exit`

Purpose: terminate the current process.

Proposed ABI:

```text
RAX = 2
RDI = exit code
```

Behavior:

- record the exit code,
- mark process `Exited`,
- never return to that user context,
- return control to the scheduler host.

## 13. User Pointer Validation

Every syscall that receives a userspace pointer must validate it before kernel dereference.

Validation checks:

- range arithmetic does not overflow,
- each covered page is present,
- each covered page is marked user-accessible,
- required read/write permission is present for the operation,
- range does not enter a kernel-only mapping.

A bad userspace pointer produces a syscall error or process termination as appropriate; it does not produce a kernel panic.

This helper becomes a foundation for future file, socket, and GUI syscalls.

## 14. ELF64 Loader

Linux95 will load user programs from the existing FAT32/VFS layer.

Initial files:

```text
/USER/INIT.ELF
/USER/WORKER.ELF
```

Both names are compatible with the existing DOS 8.3-oriented FAT32 design.

### Supported ELF subset

The loader accepts only:

- ELF64,
- little-endian,
- x86_64 (`EM_X86_64`),
- static executable (`ET_EXEC`),
- valid program-header table,
- `PT_LOAD` segments.

The loader rejects:

- ELF32,
- wrong architecture,
- malformed/truncated headers,
- dynamic executables,
- interpreter requests,
- PIE,
- relocations,
- overlapping invalid segments,
- segments entering kernel-reserved virtual ranges,
- arithmetic overflow,
- file ranges beyond the ELF file.

### Segment loading

For each `PT_LOAD` segment:

1. validate file and virtual ranges,
2. allocate enough physical 4 KiB pages,
3. map them into the new process address space,
4. copy `p_filesz` bytes from the ELF image,
5. zero the remaining `p_memsz - p_filesz` region for BSS,
6. apply user page permissions derived from ELF flags.

The entry point must lie in a mapped executable user segment.

### Executable layout

The first user programs are linked as non-PIE static ELF64 executables at a fixed user virtual range chosen not to overlap Linux95 kernel-reserved mappings.

The exact base address is centralized in the userspace linker script rather than duplicated through kernel code.

## 15. User Program Build Pipeline

The repository will gain a tiny freestanding userspace build path.

The programs use:

- no libc,
- no C++ runtime,
- no dynamic linker,
- a small userspace syscall wrapper layer,
- a dedicated user linker script.

The disk-image preparation step installs:

```text
/USER/INIT.ELF
/USER/WORKER.ELF
```

into the deterministic FAT32 test image before QEMU starts.

The kernel does not embed these programs in its binary.

This proves that Linux95 is loading separate executables from its filesystem rather than merely jumping to a blob linked into Ring 0.

## 16. First Test Programs

### `INIT.ELF`

Responsibilities:

1. print a line using `int 0x80`,
2. yield,
3. print a resumed line using `syscall`,
4. yield again,
5. exit with code 0.

### `WORKER.ELF`

Responsibilities:

1. print a line using `syscall`,
2. yield,
3. print a resumed line using `int 0x80`,
4. exit with code 0.

Together they prove:

- two distinct PIDs,
- two ELF loads,
- two address spaces,
- both syscall mechanisms,
- cooperative round-robin behavior,
- process resumption,
- clean exit.

A separate negative test program or test mode deliberately performs an illegal Ring 3 memory access to prove user-fault isolation.

## 17. User Fault Isolation

Exceptions originating from CPL 3 are treated differently from kernel faults.

Examples include:

- page fault,
- general protection fault,
- invalid opcode.

For a user-originated fatal exception:

1. capture enough diagnostic information for QEMU logs,
2. mark the current process `Exited` with a fault reason,
3. discard that process's future execution,
4. return to the scheduler host,
5. continue running Linux95.

A fault originating from CPL 0 continues to use the existing kernel panic path.

This boundary is mandatory: malformed or buggy userspace must not be able to crash the entire OS simply by causing a normal CPU exception.

## 18. Context Switching

The scheduler saves and restores the architectural state required for a process to resume exactly where it stopped.

The saved context includes the required general-purpose registers plus user control state:

- RIP,
- RSP,
- RFLAGS,
- user CS,
- user SS,
- syscall return-kind metadata.

Before resuming a process:

1. set it `Running`,
2. load its page-table root into CR3 if different,
3. update TSS `RSP0`,
4. update CPU-local syscall kernel-stack information,
5. restore its saved context,
6. return to Ring 3 through the correct return mechanism.

When a process yields/exits/faults:

1. save/finish its context as appropriate,
2. update its state,
3. switch back to the kernel host address space if required,
4. continue the desktop/event-loop host.

## 19. Interaction With Existing Linux95 Subsystems

### Desktop and mouse

The graphical desktop remains kernel-resident during this milestone.

User scheduling must not remove or bypass the existing desktop event loop.

The previous cursor save-under and top-panel mouse fixes must remain intact.

### Networking

RTL8139 remains kernel-resident and polling-based.

`network::poll()` continues to run from the existing kernel/desktop flow.

User processes do not receive raw NIC or network-stack access in this milestone.

The existing `ip` and `ping` terminal commands remain operational.

### Storage and filesystem

ATA, FAT32, and VFS remain kernel-resident.

The ELF loader consumes the current read-only VFS API.

Writable FAT32 is explicitly deferred.

### Memory manager

The new user-address-space code extends the current paging/memory subsystem rather than replacing it.

Existing kernel mappings and DMA assumptions for RTL8139 must remain valid.

## 20. Failure Handling

The process subsystem is not allowed to make the existing desktop boot dependent on userspace success during early development.

Recommended behavior:

- process subsystem initialization failure: log an error and leave user mode disabled while preserving the desktop where safe,
- missing `/USER/INIT.ELF`: log a clear failure and continue kernel operation,
- malformed ELF: reject that program without kernel panic,
- no free process slot: return failure,
- user mapping allocation failure: tear down partially created process resources,
- bad syscall number: return error,
- bad user pointer: return error or terminate offending process,
- Ring 3 fatal exception: terminate offending process,
- Ring 0 exception: existing panic behavior.

Automated tests must distinguish an intentionally rejected userspace program from a kernel crash.

## 21. Testing Strategy

Development follows TDD: establish a failing test or source check before implementing each behavior.

### Host tests

Host tests should cover pure logic wherever hardware is not required:

- process slot allocation,
- PID assignment,
- state transitions,
- round-robin selection,
- ELF header validation,
- program-header range validation,
- BSS size calculations,
- ELF permission translation,
- syscall-number dispatch,
- user-range overflow checks,
- user virtual-range validation.

### Source/architecture checks

Checks should verify critical invariants that are difficult to exercise as host C++:

- Ring 3 selectors exist,
- TSS setup exists,
- `int 0x80` gate is DPL 3,
- `syscall` MSRs are configured,
- syscall assembly entry symbols exist,
- process kernel stacks are not user-mapped,
- user pages set the U/S bit,
- kernel pages remain supervisor-only,
- no test-only automatic user process behavior leaks into the normal kernel where inappropriate.

### QEMU positive smoke test

Automated QEMU markers should prove:

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

Exact wording may match existing test-harness conventions.

### QEMU fault-isolation test

A dedicated test image or compile-time test mode runs one process that intentionally performs an illegal user access.

Expected result:

```text
[PASS] user fault captured
[PASS] faulty process terminated
[PASS] kernel survived user fault
[PASS] desktop remained online
```

The negative test must not be active in a normal Linux95 build.

### Regression verification

Every final milestone verification includes:

```bash
make clean
make all
make test
make test-qemu
nm -u build/kernel.elf
git diff --check
```

The existing network/no-network QEMU coverage must continue to pass.

## 22. Manual Acceptance

After automated verification, boot the normal merged image with:

```bash
make run
```

Verify:

1. Linux95 graphical desktop appears.
2. Mouse reaches and interacts with the top panel.
3. Window redraw behavior remains stable.
4. Terminal remains usable.
5. `ip` reports RTL8139 configuration.
6. `ping 10.0.2.2` receives a reply.
7. User-process test output appears as designed.
8. A normal user-process exit does not freeze the desktop.
9. No kernel panic occurs during normal process scheduling.

## 23. Phase 2: Preemptive Scheduling

Preemption is intentionally a follow-up milestone after this design is proven.

The later phase will reuse:

- the same process table,
- the same states,
- the same saved user context,
- the same page-table model,
- the same syscall ABI,
- the same TSS/kernel-stack model.

The PIT interrupt will gain a scheduling policy that may interrupt a running user process, save its context, mark it ready, and select another runnable process.

Additional requirements for that phase will include:

- scheduler reentrancy rules,
- interrupt-safe critical sections,
- preemption disable counters or equivalent,
- time slices,
- correct handling of kernel execution during timer interrupts.

None of that complexity is introduced into the cooperative milestone.

## 24. Security and Isolation Invariants

The following are non-negotiable acceptance rules:

1. Ring 3 cannot write kernel pages.
2. Ring 3 cannot access another process's private pages.
3. Ring 3 cannot access a process kernel stack.
4. User pointers are validated before kernel dereference.
5. ELF ranges are validated before mapping/copying.
6. A Ring 3 fatal exception kills the process rather than panicking the kernel.
7. An invalid syscall number does not panic the kernel.
8. `sysretq` is used only with validated canonical user return state.
9. Existing RTL8139 DMA mappings remain valid after CR3 switching.
10. Test-only fault/autostart hooks do not silently alter normal release behavior.

## 25. Proposed Source Organization

The implementation should stay modular.

A likely layout is:

```text
kernel/
├── process/
│   ├── process.hpp
│   ├── process.cpp
│   ├── scheduler.hpp
│   ├── scheduler.cpp
│   ├── context.hpp
│   └── context.asm
│
├── syscall/
│   ├── syscall.hpp
│   ├── syscall.cpp
│   ├── syscall_entry.asm
│   └── int80_entry.asm
│
├── user/
│   ├── elf.hpp
│   └── elf.cpp
│
└── arch/
    └── x86_64/
        ├── tss.hpp
        └── tss.cpp

user/
├── include/
│   └── linux95_syscall.hpp
├── crt/
│   └── start.asm
├── init/
│   └── main.cpp
├── worker/
│   └── main.cpp
└── user.ld
```

The exact split may be adjusted to fit existing Linux95 conventions, but process management, scheduling, ELF parsing, and syscall handling must remain separate responsibilities rather than becoming one large file.

## 26. Implementation Order

The implementation plan should decompose the milestone approximately in this dependency order:

1. Ring 3 GDT/TSS foundation.
2. Process data model and fixed process table.
3. User page-table creation and protected mappings.
4. Pure ELF64 parsing/validation.
5. ELF segment mapping from VFS.
6. First Ring 3 entry with a minimal embedded or test-only transition probe if necessary.
7. `int 0x80` entry plus common dispatcher.
8. `syscall/sysretq` entry plus CPU-local stack switch.
9. `write`.
10. Cooperative `yield`.
11. `exit`.
12. Two external ELF user programs in FAT32 fixture.
13. Round-robin cooperative scheduling.
14. User-fault isolation.
15. Full regression and manual acceptance.
16. Separate follow-up design/plan for PIT preemption.

A temporary transition probe used during implementation must not replace the final requirement that normal user programs are loaded as ELF64 files from FAT32.

## 27. Completion Boundary

This milestone is considered complete only when the normal Linux95 branch contains the process/user-mode implementation and the merged result passes the full regression suite.

It is **not** considered complete merely because:

- Ring 3 can be entered once,
- one embedded code blob runs,
- only `int 0x80` works,
- only `syscall` works,
- only one process runs,
- QEMU debug output appears while the desktop is broken,
- automated tests pass but the normal image cannot still use mouse/networking.

The result must be a stable extension of the existing Linux95 kernel, not a replacement demo.
