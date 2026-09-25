# Linux95 Preemptive Scheduling Design

**Date:** 2026-09-25  
**Target branch:** `v1.0-dev`  
**Repository:** `Black7i77/Linux95-Kernel`  
**Status:** Design approved in conversation; written-spec review required before implementation planning.

## 1. Purpose

Linux95 already has Ring 3 processes, per-process address spaces, cooperative round-robin scheduling, `int 0x80` and `syscall/sysretq` syscall paths, process exit/reaping, user-fault isolation, and a graphical kernel host loop that services networking, input, terminal work, and redraws between user-process slices.

The next milestone adds **timer-driven preemption for Ring 3 user processes only**. A CPU-bound user process that never calls `yield` must no longer be able to monopolize the machine. The kernel itself remains non-preemptible.

Success means a non-yielding Ring 3 process can be interrupted after a bounded quantum, its full user context can be saved safely, control returns through the existing kernel host scheduler, another Ready process can run, and the graphical/network host remains responsive.

## 2. Goals

1. Preempt only Ring 3 execution using the existing PIT IRQ0 timer.
2. Keep the kernel non-preemptible.
3. Use a **50 ms user quantum**.
4. Preserve the existing strict round-robin scheduler.
5. Route every expired quantum back through the existing kernel host scheduler before redispatch.
6. Resume timer-preempted processes with `IRETQ`.
7. Preserve existing cooperative `yield`, `exit`, fault isolation, fast syscall, desktop, and networking behavior.
8. Add a dedicated QEMU workload that proves preemption without relying on cooperative `yield`.

## 3. Non-goals

This milestone does **not** add:

- kernel preemption;
- scheduler priorities;
- SMP scheduling;
- APIC timer support;
- blocking/sleep queues;
- dynamic or adaptive quantum lengths;
- real-time scheduling;
- threads, `fork`, or `clone`;
- new process states beyond those already required by the current scheduler;
- direct Ring 3-to-Ring 3 switching inside the timer interrupt.

## 4. Existing architecture to preserve

The current design already provides the pieces this milestone should reuse:

- PIT timing at 100 Hz;
- IRQ0 delivered as vector 32;
- a complete CPL3 interrupt frame containing general registers plus RIP/CS/RFLAGS/RSP/SS;
- a fixed process table and strict round-robin `choose_next()` policy;
- per-process CR3, kernel stack, TSS.RSP0 setup, and user context;
- `process_save_host()` / `process_restore_host()` for host continuation;
- `process_resume_user()` with IRET and SYSRET resume paths;
- a centralized `scheduler::return_to_host()` path that restores the host CR3 and host context;
- a desktop host loop that polls networking and then invokes `scheduler::run_once()`.

The preemption milestone extends these mechanisms rather than introducing a second scheduler or second context-switch architecture.

## 5. Chosen architecture

The selected architecture is:

```text
IRQ0 from Ring 3
    -> account PIT tick
    -> consume one user-quantum tick
    -> if quantum remains: EOI -> IRETQ to same process
    -> if quantum expires:
         EOI
         save complete Ring 3 context
         mark process Ready
         HostReason::Preempt
         restore kernel host CR3/context
         return to desktop host loop
         host services run
         next scheduler dispatch uses round-robin
         selected process receives fresh 50 ms quantum
```

The timer interrupt **never switches directly from one user process to another**. The host scheduler remains the scheduling hub.

## 6. Timer and quantum semantics

### 6.1 PIT frequency

The PIT remains configured at **100 Hz**. One PIT tick is therefore approximately **10 ms**.

### 6.2 Quantum length

A user dispatch receives a fresh quantum of:

```text
5 CPL3 timer ticks x 10 ms = 50 ms
```

The scheduler owns a single private quantum counter, conceptually:

```cpp
g_user_quantum_ticks_remaining
```

The raw counter should remain private to the scheduler implementation. Interrupt code should interact through a narrow scheduler API rather than directly modifying the global.

### 6.3 What consumes the quantum

Only PIT interrupts that interrupted **CPL3** consume a user-quantum tick.

A PIT interrupt from **CPL0** still advances system PIT time and sends the PIC EOI, but it does not consume the user quantum and does not trigger scheduling.

Therefore the 50 ms quantum measures approximately 50 ms of actual Ring 3 execution, not wall-clock time spent inside kernel syscalls or other kernel work.

### 6.4 Quantum reset policy

Every dispatch starts with a fresh five-tick quantum.

- Voluntary `yield` forfeits the unused remainder.
- Timer preemption forfeits no state; when the process is dispatched again it receives a fresh five ticks.
- A newly Ready process receives a fresh five ticks.
- An ordinary syscall that returns directly to the same process does **not** reset the quantum.
- No per-process leftover-quantum accounting is preserved.

### 6.5 Expiry with only one Ready process

An expired quantum always returns to the host scheduler, even when no other process is Ready. This guarantees the desktop, input, terminal, and network host code receives CPU time. The same process may then be selected again with a fresh 50 ms quantum.

## 7. Scheduler policy

The existing strict round-robin policy remains unchanged.

On timer preemption:

1. the currently Running process becomes `Ready`;
2. the scheduler returns to the host;
3. the next call to `run_once()` scans from the slot after the previously dispatched slot;
4. the next Ready process is selected;
5. if no other process is Ready, the original process may be selected again after the scan wraps;
6. the selected process gets a new five-tick quantum.

No priorities are introduced in this milestone.

## 8. Host return reasons

Extend the current host-return reason enum with a distinct value:

```cpp
HostReason::Preempt
```

It remains separate from:

```text
Yield
Exit
Fault
Preempt
```

This distinction is required for diagnostics, tests, and future scheduler policy. A timer preemption must never be reported as a voluntary `yield`.

## 9. IRQ0 control flow

Vector 32 handling becomes conceptually:

```text
pit::on_irq()

if interrupt did not originate from CPL3:
    pic::send_eoi(0)
    return

if scheduler says user quantum has not expired:
    pic::send_eoi(0)
    return

pic::send_eoi(0)
process::handle_user_preempt(*frame)   // non-returning on success
```

The **EOI is sent before the non-returning preemption transfer**. Once `handle_user_preempt()` transfers to the saved host context, control does not return to `interrupt_dispatch()`, so postponing the EOI would leave IRQ0 in service.

The IRQ path must not allocate memory, reap processes, poll networking, update GUI state, or run general scheduler policy.

## 10. `handle_user_preempt()`

Add a dedicated process-layer path, conceptually:

```cpp
[[noreturn]] void handle_user_preempt(const interrupts::InterruptFrame& frame);
```

Its responsibilities are:

1. verify the interrupt came from CPL3;
2. resolve the current process from existing kernel-visible CPU-local process state;
3. validate that the current-process pointer identifies a real slot in the process table;
4. validate that the process has a nonzero PID, is `Running`, owns a valid page-table physical address, and owns a valid kernel stack;
5. copy the complete interrupt register state into `Process::context`;
6. set `context.return_kind = ReturnKind::Iret`;
7. set the process state to `Ready`;
8. record one-time test diagnostics where appropriate;
9. call `scheduler::return_to_host(..., HostReason::Preempt)`.

The process-table pointer validation should be at least as strict as the existing user-fault path. A private helper may be shared by fault capture and preemption capture to avoid two independent copies of the same validation/context-copy logic.

If a quantum expires after a CPL3 interrupt but no valid tracked Running process can be established, that is a **kernel scheduler invariant failure**, not a recoverable user fault. The kernel should fail safely rather than IRET into an untrusted or untracked state.

## 11. Saved context and resume method

A CPL3 timer interrupt already supplies all state required to resume the interrupted instruction stream:

```text
R15 R14 R13 R12 R11 R10 R9 R8
RBP RDI RSI RDX RCX RBX RAX
RIP RSP RFLAGS CS SS
```

The saved `UserContext` must represent the exact interrupted user state, subject only to the existing user-RFLAGS sanitization required before redispatch.

Every timer-preempted context uses:

```text
ReturnKind::Iret
```

It does not preserve or reconstruct a prior SYSRET return mode. Timer preemption is an interrupt-return problem, and the existing IRET user-resume path is the canonical resume mechanism.

The interrupted IRQ frame may remain on that process's kernel stack after control jumps back to the host. It is not reused as the future resume frame; `process_resume_user()` constructs the new IRET frame from `UserContext` on redispatch.

## 12. CR3 and TSS rules

### 12.1 Entry

When a Ring 3 process is dispatched, the existing scheduler continues to:

1. save the host CR3;
2. switch to the process CR3;
3. set `TSS.RSP0` to that process's supervisor kernel-stack top;
4. set the current-process CPU-local state;
5. enter user mode.

### 12.2 Timer interrupt

A timer IRQ from CPL3 enters using the process kernel stack selected by `TSS.RSP0`. The process CR3 remains active while the interrupt handler captures the context. Shared supervisor kernel mappings make the interrupt and scheduler code available in that address space.

### 12.3 Return to host

On an expired quantum, the existing centralized host-return path is reused:

```text
process CR3
 -> scheduler::return_to_host()
 -> restore saved host CR3
 -> clear current process CPU-local state
 -> process_restore_host()
 -> saved host continuation resumes
```

There must not be a second ad-hoc CR3 restoration mechanism for timer preemption.

## 13. GS and syscall isolation

This milestone must preserve the fast syscall `swapgs` ownership established by the previous Ring 3 work.

The normal hardware interrupt stub does not execute `swapgs`, and the timer-preemption path does not require GS-relative per-CPU memory accesses. Therefore IRQ0 preemption must not add an unconditional `swapgs` merely because the interrupt originated from Ring 3.

Rules:

- the existing `syscall` entry/exit path continues to own its current `swapgs` pairing;
- IRQ0 code uses ordinary kernel/global references to obtain scheduler and current-process state;
- timer preemption must not alter the syscall path's GS state;
- a PIT interrupt that occurs while executing CPL0 kernel code never schedules, even if future kernel code enables interrupts inside a syscall.

Any later change that introduces GS-relative accesses in the common interrupt path must revisit this design explicitly rather than silently adding `swapgs` to IRQ0.

## 14. Interrupt-state rules

The timer interrupt runs through an interrupt gate, so maskable interrupts are disabled on entry.

During a preemption transfer:

- keep maskable interrupts disabled while saving context and transferring to the host;
- do not deliberately enable nested maskable interrupts inside the preemption path;
- send the PIC EOI before the non-returning jump back to the host;
- let the existing host-context restoration restore the host RFLAGS/interrupt state.

Kernel code remains non-preemptible throughout this milestone.

## 15. Host-loop behavior

The desktop host remains the service loop. After a preemption returns to the host, normal kernel work proceeds outside IRQ context.

The host loop continues to service, in its existing bounded loop:

- networking;
- scheduler dispatch;
- terminal polling;
- mouse events;
- keyboard events;
- uptime/status updates;
- dirty-region redraw;
- idle `hlt` when appropriate.

The design does not run these services from IRQ0. Preemption merely guarantees that a non-yielding user process cannot prevent the host from regaining control.

## 16. Dedicated preemption test workload

Normal `/USER/INIT.ELF` and `/USER/WORKER.ELF` behavior stays unchanged.

Add dedicated preemption-test user programs and a dedicated FAT32/QEMU fixture. The test image may place the test binaries at the existing startup paths so the kernel startup logic does not need a second process-loader architecture.

Recommended fixture:

### PID 1: non-yielding hog

- starts in Ring 3;
- enters an infinite CPU loop;
- makes no `yield` syscall;
- does not exit;
- cannot voluntarily return control to the host scheduler.

### PID 2: preemption worker

- can execute only after PID 1 is forcibly preempted;
- emits a marker such as:

```text
[pid 2] scheduled by timer preemption
```

- then remains CPU-bound without using `yield`, allowing repeated timer-driven alternation to be observed;
- the QEMU harness may stop the VM after required success markers are observed.

This is stronger than a long-but-finite loop: without working timer preemption, PID 1 loops forever and PID 2 can never execute.

## 17. Test diagnostics

Important diagnostics should be emitted **once**, not once per 10 ms PIT tick.

Expected milestone markers may include:

```text
[PASS] user quantum expired
[PASS] preempted context captured
[PASS] timer preemption returned to host
[pid 2] scheduled by timer preemption
[PASS] non-yielding process was preempted
[PASS] preemptive round robin
[PASS] desktop remained online after preemption
```

Test-only PID transition diagnostics may be added if needed to prove an ordering such as:

```text
PID1 preempted -> PID2 dispatched -> PID2 preempted -> PID1 dispatched
```

Normal boot should not spam per-quantum debug output.

## 18. Test strategy

Implementation follows RED -> GREEN TDD.

### 18.1 Host/unit tests

Extend scheduler/process host tests to cover at least:

- a fresh dispatch resets the quantum to five ticks;
- CPL3 ticks decrement the quantum;
- the fifth CPL3 tick reports expiry exactly once;
- resetting after dispatch restores five ticks;
- kernel/CPL0 timer handling does not consume a user quantum;
- `HostReason::Preempt` remains distinct from Yield/Exit/Fault;
- strict round-robin still selects the next Ready slot after a preempted process is marked Ready;
- single-Ready-process wraparound selects the same process only after host return;
- interrupt-frame-to-user-context copying preserves all defined registers and forces `ReturnKind::Iret`.

### 18.2 QEMU preemption test

Add a dedicated test mode, for example `--process-preemption-test`, integrated with the existing QEMU smoke-test style.

The QEMU test must fail if PID 2 never runs. Passing requires evidence that:

1. PID 1 entered a non-yielding loop;
2. its quantum expired;
3. context was captured;
4. the host scheduler regained control;
5. PID 2 ran despite PID 1 never yielding;
6. repeated preemption/redispatch preserves user execution;
7. there is no kernel panic, double fault, triple fault, or unexpected reset.

### 18.3 Existing regressions

Before completion, rerun the existing suite and preserve:

- normal build;
- host tests;
- normal QEMU boot;
- RTL8139/network QEMU path;
- no-network QEMU fallback;
- cooperative Ring 3 process test;
- user-fault isolation test;
- missing-user-programs nonfatal boot;
- both `int 0x80` and fast `syscall` paths;
- process exit and reaping;
- graphical desktop, keyboard, mouse, terminal, redraw, and uptime behavior.

### 18.4 Static verification

Completion also requires:

```text
nm -u build/kernel.elf
```

to remain empty, and:

```text
git diff --check
```

to report clean output.

Object/disassembly inspection should confirm that timer-preempted contexts resume through IRETQ and that the existing SYSRETQ path remains intact for fast-syscall returns.

## 19. Manual acceptance test

Run Linux95 graphically with the dedicated non-yielding workload active and verify:

1. the desktop remains visible and responsive;
2. the mouse continues to move and interact with the top panel;
3. terminal/input processing remains alive;
4. uptime continues updating;
5. network polling continues to make progress where networking is available;
6. the non-yielding Ring 3 workloads continue receiving CPU time in round-robin slices;
7. no panic or reset occurs during repeated preemption.

The key user-visible result is that a CPU-bound Ring 3 program can no longer freeze Linux95's host services simply by refusing to call `yield`.

## 20. Safety invariants

The implementation must preserve all of the following:

```text
- Never preempt CPL0 kernel execution.
- Never switch directly Ring3 -> Ring3 inside IRQ0.
- Never resume a timer-preempted context with SYSRETQ.
- Never leave IRQ0 without sending EOI on the preemption path.
- Never allocate/free/reap processes inside IRQ0.
- Never run GUI/network/general host services inside IRQ0.
- Never trust an unchecked current-process pointer.
- Never add an unpaired or unconditional swapgs to timer IRQ handling.
- Never enable nested maskable interrupts during context capture.
- Never preserve unused quantum across dispatches.
```

## 21. Completion criteria

The milestone is complete only when all of the following are true:

- a dedicated non-yielding Ring 3 workload proves timer preemption is necessary and working;
- the 100 Hz PIT produces a five-tick/50 ms user quantum;
- only CPL3 ticks consume the quantum;
- every expired quantum returns through the host scheduler;
- timer-preempted contexts resume through IRETQ;
- strict round-robin remains the scheduling policy;
- `HostReason::Preempt` is distinct and testable;
- normal cooperative scheduling still works;
- fault isolation still works;
- SYSRET/INT80 syscall behavior still works;
- network and no-network tests pass;
- the graphical desktop remains responsive under a non-yielding user workload;
- all required host/QEMU/static checks pass with no unresolved critical or important review findings.

## 22. Deferred follow-up work

After this milestone is stable, later designs may consider:

- sleep and blocking process states;
- wait queues;
- scheduler priorities;
- dynamic quantum selection;
- kernel preemption;
- SMP and per-CPU scheduler state;
- APIC timers;
- process CPU accounting and user-visible scheduler statistics.

These are intentionally excluded from the present implementation plan.
