# Linux95 Preemptive Scheduling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add PIT-driven, 50 ms, Ring-3-only preemptive round-robin scheduling to Linux95 while keeping the kernel non-preemptible and preserving the existing host scheduler, syscall paths, desktop, and networking.

**Architecture:** IRQ0 remains the only timer source and runs at 100 Hz. Only timer interrupts that arrived from CPL3 consume the current five-tick user quantum; on expiry the kernel saves the full user interrupt frame, marks the process Ready, sends EOI, and returns through the existing host scheduler using `HostReason::Preempt`. Redispatch remains strict round-robin, each dispatch gets a fresh five-tick quantum, and timer-preempted contexts always resume with `IRETQ`.

**Tech Stack:** x86_64 freestanding C++17, NASM, legacy 8259 PIC, PIT IRQ0, BIOS/QEMU `pc` machine, FAT32 test fixtures, Python QEMU smoke harness, GNU Make, host-side C++ assertion tests.

**Spec:** `docs/superpowers/specs/2026-09-25-linux95-preemptive-scheduling-design.md`

## Global Constraints

- PIT stays at **100 Hz**; one user quantum is **5 CPL3 timer ticks = approximately 50 ms**.
- Only timer IRQs that interrupted **CPL3** consume user quantum.
- The kernel remains **non-preemptible**; CPL0 timer IRQs tick/EOI/return without scheduling.
- Every expired user quantum returns through the **kernel host scheduler** before any user process is redispatched.
- Every dispatch receives a **fresh five-tick quantum**; unused quantum is never preserved.
- Timer-preempted contexts resume through **`IRETQ` only**.
- Scheduling policy remains **strict round-robin**; no priorities are added.
- Add a distinct **`HostReason::Preempt`**; do not report timer preemption as `Yield`.
- Use a dedicated **`process::handle_user_preempt()`** path; do not put process-state/context policy directly into IRQ routing.
- Send PIC EOI **before** the non-returning transfer from IRQ0 to the host scheduler.
- Do not add an unconditional `swapgs` to the timer/common interrupt path.
- Do not allocate, free, reap, poll networking, or run GUI work in IRQ0.
- Preserve existing `int 0x80`, `syscall/sysretq`, cooperative yield, exit/reaping, fault isolation, networking, and desktop behavior.
- Preserve unrelated untracked `release/` content and the unrelated RTL8139 implementation-plan file; do not stage, delete, or modify them.
- Follow RED -> GREEN TDD and make small commits after each independently passing task.

## File Structure

**Modify:**
- `kernel/process/scheduler.hpp` — public scheduler quantum API and `HostReason::Preempt`.
- `kernel/process/scheduler.cpp` — private five-tick counter, fresh-quantum dispatch reset, preemption host-return diagnostics, and shared current-process validation used by fault/preempt paths.
- `kernel/process/process.hpp` — interrupt-frame capture API and `handle_user_preempt()` declaration.
- `kernel/process/process.cpp` — pure interrupt-frame-to-`UserContext` copier that can be host-tested.
- `kernel/arch/interrupts.cpp` — IRQ0 CPL3 detection, scheduler quantum accounting, EOI ordering, and dispatch to `handle_user_preempt()` on expiry.
- `tests/host/scheduler_test.cpp` — quantum accounting, reset, no-CPL0-consumption, round-robin, and distinct host-reason tests.
- `tests/host/process_test.cpp` — exact register/context-copy test for timer-preempted state.
- `tests/prepare_fat32_image.py` — dedicated preemption fixture mode that maps preemption ELFs to `/USER/INIT.ELF` and `/USER/WORKER.ELF`.
- `tests/qemu_smoke.py` — `--process-preemption-test` mode and required ordering/count checks.
- `Makefile` — preemption user binaries, test image, dependencies, and QEMU target.

**Create:**
- `user/preempt_hog/main.cpp` — PID1 marker followed by an infinite non-yielding CPU loop.
- `user/preempt_worker/main.cpp` — PID2 marker followed by an infinite non-yielding CPU loop.
- `tests/preemption_source_checks.py` — narrow static invariants: no timer-path `swapgs`, EOI before non-returning preempt handoff, IRET path still present, SYSRET path still present.

**Do not modify unless a failing test proves it is required:**
- `kernel/arch/interrupts.asm`
- `kernel/process/context.asm`
- `kernel/syscall/syscall_entry.asm`
- `kernel/syscall/syscall.cpp`
- normal `user/init/main.cpp` and `user/worker/main.cpp`

## Review Focus

1. **Quantum underflow / duplicate expiry:** once the fifth CPL3 tick expires a quantum, additional calls before redispatch must not produce repeated expiry events. Task 1 pins this with a sixth-tick assertion.
2. **CPL0 timer delivery:** kernel timer IRQs must not consume a user quantum. Task 1 pins this by injecting kernel ticks between user ticks and proving expiry still occurs on the fifth user tick.
3. **Corrupt or untracked current-process state:** preemption must fail safely instead of returning to an untrusted process. Task 3 reuses the existing strict process-table/state validation and panics on invariant failure.
4. **EOI lost on non-returning preemption:** IRQ0 must be acknowledged before host transfer. Task 4 adds a static source check that pins the call order.
5. **Resume-mode/context corruption:** every GPR plus RIP/RSP/RFLAGS/CS/SS must survive capture and timer-preempted contexts must force `ReturnKind::Iret`. Task 2 tests every field; Task 7 checks the built object still contains IRETQ and the fast-syscall SYSRET path.

---

### Task 1: Add five-tick scheduler quantum accounting

**Files:**
- Modify: `kernel/process/scheduler.hpp`
- Modify: `kernel/process/scheduler.cpp`
- Modify: `tests/host/scheduler_test.cpp`

**Interfaces:**
- Produces: `constexpr uint32_t kUserQuantumTicks = 5;`
- Produces: `void reset_user_quantum();`
- Produces: `bool on_timer_tick(bool interrupted_user);` — returns `true` exactly once when a live quantum transitions from 1 tick to 0; kernel ticks and calls made while already expired return `false`.
- Consumes: existing `choose_next(...)` and process states.

- [ ] **Step 1: Write the failing host tests for quantum behavior**

Add `<cstdint>` to the test if needed, then append these assertions before `return 0;` in `tests/host/scheduler_test.cpp`:

```cpp
    using linux95::scheduler::on_timer_tick;
    using linux95::scheduler::reset_user_quantum;

    reset_user_quantum();
    for (uint32_t tick = 0; tick < 4; ++tick) {
        assert(!on_timer_tick(true));
    }
    assert(on_timer_tick(true));
    assert(!on_timer_tick(true));

    reset_user_quantum();
    assert(!on_timer_tick(true));
    assert(!on_timer_tick(false));
    assert(!on_timer_tick(false));
    assert(!on_timer_tick(true));
    assert(!on_timer_tick(true));
    assert(!on_timer_tick(true));
    assert(on_timer_tick(true));

    reset_user_quantum();
    for (uint32_t tick = 0; tick < 4; ++tick) {
        assert(!on_timer_tick(true));
    }
    reset_user_quantum();
    for (uint32_t tick = 0; tick < 4; ++tick) {
        assert(!on_timer_tick(true));
    }
    assert(on_timer_tick(true));
```

- [ ] **Step 2: Run the scheduler host test and verify RED**

Run:

```bash
make test-host-scheduler
```

Expected: compile failure because `reset_user_quantum()` and `on_timer_tick(bool)` do not exist yet.

- [ ] **Step 3: Add the minimal scheduler quantum API and implementation**

In `kernel/process/scheduler.hpp`, add `<cstdint>` and:

```cpp
constexpr uint32_t kUserQuantumTicks = 5;

void reset_user_quantum();
bool on_timer_tick(bool interrupted_user);
```

In the private namespace in `kernel/process/scheduler.cpp`, add:

```cpp
uint32_t g_user_quantum_ticks_remaining = 0;
```

Then add these namespace-level functions:

```cpp
void reset_user_quantum()
{
    g_user_quantum_ticks_remaining = kUserQuantumTicks;
}

bool on_timer_tick(bool interrupted_user)
{
    if (!interrupted_user || g_user_quantum_ticks_remaining == 0) {
        return false;
    }

    --g_user_quantum_ticks_remaining;
    return g_user_quantum_ticks_remaining == 0;
}
```

Do **not** reset the counter from syscall-return code.

- [ ] **Step 4: Run the scheduler host test and verify GREEN**

Run:

```bash
make test-host-scheduler
```

Expected: PASS.

- [ ] **Step 5: Commit the quantum-accounting slice**

```bash
git add kernel/process/scheduler.hpp kernel/process/scheduler.cpp tests/host/scheduler_test.cpp
git commit -m "Add user scheduling quantum accounting"
```

---

### Task 2: Centralize interrupt-frame capture into `UserContext`

**Files:**
- Modify: `kernel/process/process.hpp`
- Modify: `kernel/process/process.cpp`
- Modify: `tests/host/process_test.cpp`
- Modify: `Makefile` only if the new `arch/interrupts.hpp` dependency is not already discovered by the explicit rule.

**Interfaces:**
- Produces: `void capture_interrupt_context(Process& process, const interrupts::InterruptFrame& frame);`
- Consumes: `interrupts::InterruptFrame`, `Process::context`, `ReturnKind::Iret`.
- Later tasks use this same function from both user-fault handling and timer-preemption handling.

- [ ] **Step 1: Write a host test that fills every interrupt-frame field with a unique value**

Add `#include "arch/interrupts.hpp"` to `tests/host/process_test.cpp`, then add before `return 0;`:

```cpp
    linux95::interrupts::InterruptFrame frame{};
    frame.r15 = 0x15;
    frame.r14 = 0x14;
    frame.r13 = 0x13;
    frame.r12 = 0x12;
    frame.r11 = 0x11;
    frame.r10 = 0x10;
    frame.r9 = 0x09;
    frame.r8 = 0x08;
    frame.rbp = 0x70;
    frame.rdi = 0x71;
    frame.rsi = 0x72;
    frame.rdx = 0x73;
    frame.rcx = 0x74;
    frame.rbx = 0x75;
    frame.rax = 0x76;
    frame.rip = 0x0000400000001234ULL;
    frame.rsp = 0x00007FFFFFEFF000ULL;
    frame.rflags = 0x202;
    frame.cs = 0x23;
    frame.ss = 0x1B;

    Process captured{};
    capture_interrupt_context(captured, frame);

    assert(captured.context.r15 == frame.r15);
    assert(captured.context.r14 == frame.r14);
    assert(captured.context.r13 == frame.r13);
    assert(captured.context.r12 == frame.r12);
    assert(captured.context.r11 == frame.r11);
    assert(captured.context.r10 == frame.r10);
    assert(captured.context.r9 == frame.r9);
    assert(captured.context.r8 == frame.r8);
    assert(captured.context.rbp == frame.rbp);
    assert(captured.context.rdi == frame.rdi);
    assert(captured.context.rsi == frame.rsi);
    assert(captured.context.rdx == frame.rdx);
    assert(captured.context.rcx == frame.rcx);
    assert(captured.context.rbx == frame.rbx);
    assert(captured.context.rax == frame.rax);
    assert(captured.context.rip == frame.rip);
    assert(captured.context.rsp == frame.rsp);
    assert(captured.context.rflags == frame.rflags);
    assert(captured.context.cs == frame.cs);
    assert(captured.context.ss == frame.ss);
    assert(captured.context.return_kind == ReturnKind::Iret);
```

- [ ] **Step 2: Run the process host test and verify RED**

```bash
make test-host-process
```

Expected: compile failure because `capture_interrupt_context` is undefined.

- [ ] **Step 3: Implement the exact copier once**

In `kernel/process/process.hpp` add:

```cpp
void capture_interrupt_context(
    Process& process,
    const interrupts::InterruptFrame& frame);
```

In `kernel/process/process.cpp`, include `arch/interrupts.hpp` and implement:

```cpp
void capture_interrupt_context(
    Process& process,
    const interrupts::InterruptFrame& frame)
{
    process.context.r15 = frame.r15;
    process.context.r14 = frame.r14;
    process.context.r13 = frame.r13;
    process.context.r12 = frame.r12;
    process.context.r11 = frame.r11;
    process.context.r10 = frame.r10;
    process.context.r9 = frame.r9;
    process.context.r8 = frame.r8;
    process.context.rbp = frame.rbp;
    process.context.rdi = frame.rdi;
    process.context.rsi = frame.rsi;
    process.context.rdx = frame.rdx;
    process.context.rcx = frame.rcx;
    process.context.rbx = frame.rbx;
    process.context.rax = frame.rax;
    process.context.rip = frame.rip;
    process.context.rsp = frame.rsp;
    process.context.rflags = frame.rflags;
    process.context.cs = static_cast<uint16_t>(frame.cs);
    process.context.ss = static_cast<uint16_t>(frame.ss);
    process.context.return_kind = ReturnKind::Iret;
}
```

Replace the duplicated register-copy block inside the existing user-fault path with this helper, leaving fault metadata and exit behavior unchanged.

- [ ] **Step 4: Run process and fault-sensitive host tests**

```bash
make test-host-process test-host-scheduler test-host-syscall
```

Expected: PASS.

- [ ] **Step 5: Commit shared context capture**

```bash
git add kernel/process/process.hpp kernel/process/process.cpp kernel/process/scheduler.cpp tests/host/process_test.cpp Makefile
git commit -m "Share user interrupt context capture"
```

---

### Task 3: Add `HostReason::Preempt` and the dedicated preemption handler

**Files:**
- Modify: `kernel/process/scheduler.hpp`
- Modify: `kernel/process/scheduler.cpp`
- Modify: `kernel/process/process.hpp`
- Modify: `tests/host/scheduler_test.cpp`

**Interfaces:**
- Produces: `HostReason::Preempt`.
- Produces: `[[noreturn]] void process::handle_user_preempt(const interrupts::InterruptFrame& frame);`
- Consumes: `capture_interrupt_context(...)`, existing CPU-local current-process state, existing `scheduler::return_to_host(...)`.

- [ ] **Step 1: Add the failing enum-distinction and round-robin assertions**

In `tests/host/scheduler_test.cpp`, add:

```cpp
    static_assert(
        static_cast<int>(linux95::scheduler::HostReason::Preempt) !=
        static_cast<int>(linux95::scheduler::HostReason::Yield));
    static_assert(
        static_cast<int>(linux95::scheduler::HostReason::Preempt) !=
        static_cast<int>(linux95::scheduler::HostReason::Exit));
    static_assert(
        static_cast<int>(linux95::scheduler::HostReason::Preempt) !=
        static_cast<int>(linux95::scheduler::HostReason::Fault));

    Process preempt_table[3]{};
    preempt_table[0].state = State::Ready;
    preempt_table[1].state = State::Ready;
    preempt_table[2].state = State::Blocked;
    assert(linux95::scheduler::choose_next(preempt_table, 3, 0) == 1);
    preempt_table[1].state = State::Blocked;
    assert(linux95::scheduler::choose_next(preempt_table, 3, 0) == 0);
```

- [ ] **Step 2: Run scheduler test and verify RED**

```bash
make test-host-scheduler
```

Expected: compile failure because `HostReason::Preempt` does not exist.

- [ ] **Step 3: Add the preemption reason and handler**

Extend the enum in `kernel/process/scheduler.hpp`:

```cpp
enum class HostReason {
    Yield,
    Exit,
    Fault,
    Preempt,
};
```

Declare in `kernel/process/process.hpp`:

```cpp
[[noreturn]] void handle_user_preempt(
    const interrupts::InterruptFrame& frame);
```

In `kernel/process/scheduler.cpp`, factor the existing current-process table/range/state checks used by `handle_user_fault()` into a private helper that returns a validated `Process*`. Preserve the existing higher-half alias normalization before comparing against `table()`.

Use that helper from a new `process::handle_user_preempt()` implementation:

```cpp
[[noreturn]] void handle_user_preempt(
    const interrupts::InterruptFrame& frame)
{
    if ((frame.cs & 0x3U) != 0x3U) {
        panic::halt("Kernel attempted to preempt non-user context");
    }

    Process* current = validated_current_process();
    if (current == nullptr) {
        panic::halt("Timer preemption without tracked running process");
    }

    capture_interrupt_context(*current, frame);
    current->state = State::Ready;

    scheduler::return_to_host(
        *current,
        current->context,
        scheduler::HostReason::Preempt);
}
```

Do not allocate, reap, or switch CR3 here; `return_to_host()` already owns host CR3/context restoration.

- [ ] **Step 4: Run host regression tests**

```bash
make test-host-process test-host-scheduler test-host-syscall
```

Expected: PASS.

- [ ] **Step 5: Commit the dedicated preemption path**

```bash
git add kernel/process/scheduler.hpp kernel/process/scheduler.cpp kernel/process/process.hpp tests/host/scheduler_test.cpp
git commit -m "Add dedicated user preemption host return"
```

---

### Task 4: Wire PIT IRQ0 expiry to the preemption handler and fresh dispatch quantum

**Files:**
- Modify: `kernel/arch/interrupts.cpp`
- Modify: `kernel/process/scheduler.cpp`
- Create: `tests/preemption_source_checks.py`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `scheduler::on_timer_tick(bool interrupted_user)`.
- Consumes: `process::handle_user_preempt(...)`.
- Produces: fresh quantum reset inside every successful `scheduler::run_once()` dispatch.
- Produces: IRQ0 EOI-before-preemption ordering.

- [ ] **Step 1: Add a failing static source test for IRQ and GS invariants**

Create `tests/preemption_source_checks.py`:

```python
#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
interrupts_cpp = (root / "kernel/arch/interrupts.cpp").read_text()
interrupts_asm = (root / "kernel/arch/interrupts.asm").read_text()
context_asm = (root / "kernel/process/context.asm").read_text()
syscall_asm = (root / "kernel/syscall/syscall_entry.asm").read_text()

assert "scheduler::on_timer_tick" in interrupts_cpp
assert "process::handle_user_preempt" in interrupts_cpp

irq32 = interrupts_cpp[interrupts_cpp.index("if (frame->vector == 32)"):]
irq32 = irq32[:irq32.index("if (frame->vector == 33)")]
assert irq32.index("pic::send_eoi(0)") < irq32.index("process::handle_user_preempt")

assert "swapgs" not in interrupts_asm
assert "iretq" in context_asm
assert "o64 sysret" in context_asm
assert syscall_asm.count("swapgs") == 2

print("preemption source checks: PASS")
```

Add a Make target:

```make
test-preemption-source:
>$(PYTHON) tests/preemption_source_checks.py
```

and include it in `.PHONY` plus the main `test` target.

- [ ] **Step 2: Run the source test and verify RED**

```bash
make test-preemption-source
```

Expected: assertion failure because IRQ0 does not yet call the scheduler/preemption APIs.

- [ ] **Step 3: Implement IRQ0 accounting and dispatch reset**

In `kernel/arch/interrupts.cpp`, include `process/scheduler.hpp` and change vector 32 handling to this shape:

```cpp
    if (frame->vector == 32) {
        pit::on_irq();

        const bool from_user =
            (frame->cs & 0x3U) == 0x3U;
        const bool expired =
            scheduler::on_timer_tick(from_user);

        pic::send_eoi(0);

        if (expired) {
            process::handle_user_preempt(*frame);
        }
        return;
    }
```

In `scheduler::run_once()`, reset the user quantum only after a process is selected/validated and immediately before transferring into its address space/user context:

```cpp
    selected.state = process::State::Running;
    reset_user_quantum();
```

Do not call `reset_user_quantum()` on ordinary syscall return.

- [ ] **Step 4: Run source and host tests**

```bash
make test-preemption-source test-host-scheduler test-host-process test-host-syscall
```

Expected: PASS.

- [ ] **Step 5: Commit IRQ wiring**

```bash
git add kernel/arch/interrupts.cpp kernel/process/scheduler.cpp tests/preemption_source_checks.py Makefile
git commit -m "Preempt Ring 3 processes on PIT quantum expiry"
```

---

### Task 5: Add one-time preemption diagnostics without IRQ spam

**Files:**
- Modify: `kernel/process/scheduler.cpp`

**Interfaces:**
- Consumes: `HostReason::Preempt`, selected PID, previous preempted PID.
- Produces one-time markers used by the dedicated QEMU test.

- [ ] **Step 1: Define the exact marker contract in code comments before implementation**

Immediately beside scheduler diagnostic state, add this comment and state variables:

```cpp
// Preemption diagnostics are one-shot milestone evidence, never per-tick logs.
bool g_quantum_expired_emitted = false;
bool g_context_captured_emitted = false;
bool g_preempt_host_return_emitted = false;
bool g_non_yielding_preempted_emitted = false;
bool g_preemptive_round_robin_emitted = false;
uint32_t g_last_preempted_pid = 0;
```

The exact required output contract is:

```text
[PASS] user quantum expired
[PASS] preempted context captured
[PASS] timer preemption returned to host
[PASS] non-yielding process was preempted
[PASS] preemptive round robin
[PASS] desktop remained online after preemption
```

- [ ] **Step 2: Run the future QEMU mode name now and verify RED because it does not exist**

```bash
python3 tests/qemu_smoke.py --process-preemption-test
```

Expected: usage error / exit 2. This establishes the integration test remains RED before fixture work.

- [ ] **Step 3: Add one-shot diagnostics at state-transition boundaries**

Use the markers only when the corresponding event is known to have occurred:

```cpp
// In on_timer_tick(), on the 1 -> 0 transition only:
if (g_user_quantum_ticks_remaining == 0) {
    if (!g_quantum_expired_emitted) {
        g_quantum_expired_emitted = true;
        debug::write("[PASS] user quantum expired\n");
    }
    return true;
}
```

After `capture_interrupt_context()` in `handle_user_preempt()`:

```cpp
if (!g_context_captured_emitted) {
    g_context_captured_emitted = true;
    debug::write("[PASS] preempted context captured\n");
}
g_last_preempted_pid = current->pid;
```

After `process_restore_host()` returns into `run_once()` and `g_last_host_reason == HostReason::Preempt`, emit the host-return marker once. When the next selected PID differs from `g_last_preempted_pid`, emit `non-yielding process was preempted` once. After a later preemption and wrap back to the original PID, emit `preemptive round robin` once. Keep the bookkeeping bounded to PIDs and booleans; do not allocate.

Emit:

```cpp
if (!g_preempt_host_return_emitted) {
    g_preempt_host_return_emitted = true;
    debug::write("[PASS] timer preemption returned to host\n");
    debug::write("[PASS] desktop remained online after preemption\n");
}
```

only after the host continuation has actually resumed.

- [ ] **Step 4: Re-run host tests and source checks**

```bash
make test-host-scheduler test-host-process test-preemption-source
```

Expected: PASS, with no host-test dependency explosion from unused scheduler sections.

- [ ] **Step 5: Commit diagnostics**

```bash
git add kernel/process/scheduler.cpp
git commit -m "Add preemption milestone diagnostics"
```

---

### Task 6: Add dedicated non-yielding Ring 3 test programs

**Files:**
- Create: `user/preempt_hog/main.cpp`
- Create: `user/preempt_worker/main.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces: `build/user/preempt_hog.elf`.
- Produces: `build/user/preempt_worker.elf`.
- Consumes: existing `linux95_syscall.hpp`, common `user/crt/start.asm`, and `user/user.ld`.

- [ ] **Step 1: Create the PID1 non-yielding hog source**

`user/preempt_hog/main.cpp`:

```cpp
#include "linux95_syscall.hpp"

extern "C" int user_main()
{
    static constexpr char started[] =
        "[pid 1] entered non-yielding loop\n";
    linux95::user::write_int80(started, sizeof(started) - 1);

    volatile uint64_t value = 0;
    for (;;) {
        value = value * 1664525ULL + 1013904223ULL;
        asm volatile("" : "+r"(value) : : "memory");
    }
}
```

It intentionally makes **no yield or exit syscall after the startup marker**.

- [ ] **Step 2: Create the PID2 non-yielding worker source**

`user/preempt_worker/main.cpp`:

```cpp
#include "linux95_syscall.hpp"

extern "C" int user_main()
{
    static constexpr char scheduled[] =
        "[pid 2] scheduled by timer preemption\n";
    linux95::user::write_syscall(scheduled, sizeof(scheduled) - 1);

    volatile uint64_t value = 1;
    for (;;) {
        value = value * 1103515245ULL + 12345ULL;
        asm volatile("" : "+r"(value) : : "memory");
    }
}
```

It also makes no cooperative yield.

- [ ] **Step 3: Add build rules for both ELFs**

Follow the existing init/worker rules exactly:

```make
$(BUILD)/user/preempt_hog.o: user/preempt_hog/main.cpp user/include/linux95_syscall.hpp | $(BUILD)/user
>$(CXX) $(USER_CXXFLAGS) -c $< -o $@

$(BUILD)/user/preempt_worker.o: user/preempt_worker/main.cpp user/include/linux95_syscall.hpp | $(BUILD)/user
>$(CXX) $(USER_CXXFLAGS) -c $< -o $@

$(BUILD)/user/preempt_hog.elf: $(BUILD)/user/start.o $(BUILD)/user/preempt_hog.o user/user.ld
>$(LD) -nostdlib -static -no-pie -z max-page-size=0x1000 -T user/user.ld -o $@ $(BUILD)/user/start.o $(BUILD)/user/preempt_hog.o

$(BUILD)/user/preempt_worker.elf: $(BUILD)/user/start.o $(BUILD)/user/preempt_worker.o user/user.ld
>$(LD) -nostdlib -static -no-pie -z max-page-size=0x1000 -T user/user.ld -o $@ $(BUILD)/user/start.o $(BUILD)/user/preempt_worker.o
```

- [ ] **Step 4: Build and inspect the new user programs**

```bash
make build/user/preempt_hog.elf build/user/preempt_worker.elf
readelf -h build/user/preempt_hog.elf
readelf -l build/user/preempt_hog.elf
readelf -h build/user/preempt_worker.elf
readelf -l build/user/preempt_worker.elf
```

Expected: both are ELF64 x86-64 `EXEC`, use the fixed user linker address, and contain no `PT_INTERP`.

- [ ] **Step 5: Commit the dedicated workloads**

```bash
git add user/preempt_hog/main.cpp user/preempt_worker/main.cpp Makefile
git commit -m "Add non-yielding preemption test programs"
```

---

### Task 7: Build the dedicated FAT32 preemption fixture and QEMU proof

**Files:**
- Modify: `tests/prepare_fat32_image.py`
- Modify: `tests/qemu_smoke.py`
- Modify: `Makefile`

**Interfaces:**
- Produces: `build/linux95-preemption-test.img`.
- Produces: `python3 tests/qemu_smoke.py --process-preemption-test`.
- Consumes: `build/user/preempt_hog.elf`, `build/user/preempt_worker.elf`.

- [ ] **Step 1: Extend the FAT32 fixture builder with a mutually exclusive preemption mode**

At argument parsing, support:

```python
process_fault = "--process-fault" in sys.argv[2:]
process_preemption = "--process-preemption" in sys.argv[2:]

if process_fault and process_preemption:
    print("process fixture modes are mutually exclusive")
    return 2
```

Define:

```python
user_preempt_hog = Path("build/user/preempt_hog.elf").resolve()
user_preempt_worker = Path("build/user/preempt_worker.elf").resolve()
```

For preemption mode, map:

```python
init_image = user_preempt_hog
worker_image = user_preempt_worker
```

and copy them to the existing startup paths:

```text
::USER/INIT.ELF
::USER/WORKER.ELF
```

Keep normal and fault fixture behavior unchanged.

- [ ] **Step 2: Add Makefile image wiring**

Add:

```make
PREEMPTION_TEST_IMAGE := $(BUILD)/linux95-preemption-test.img

$(PREEMPTION_TEST_IMAGE): tests/prepare_fat32_image.py $(BUILD)/user/preempt_hog.elf $(BUILD)/user/preempt_worker.elf | $(BUILD)
>$(PYTHON) tests/prepare_fat32_image.py $@ --process-preemption
```

- [ ] **Step 3: Add `--process-preemption-test` to the QEMU harness**

In `tests/qemu_smoke.py` add:

```python
PROCESS_PREEMPTION_TEST = "--process-preemption-test" in sys.argv[1:]
```

Include it in the mutually-exclusive mode count and usage string. For this mode:

```python
IMAGE = ROOT / "build" / "linux95-kernel.img"
STORAGE_IMAGE = ROOT / "build" / "linux95-preemption-test.img"
```

Before QEMU launch, build:

```python
if PROCESS_PREEMPTION_TEST:
    subprocess.run(
        ["make", "all", "build/linux95-preemption-test.img"],
        cwd=ROOT,
        check=True,
    )
```

Treat this mode like other no-NIC process tests when constructing QEMU networking arguments.

Use completion marker:

```python
"[PASS] preemptive round robin"
```

Required preemption markers:

```python
required.extend([
    "[pid 1] entered non-yielding loop",
    "[PASS] user quantum expired",
    "[PASS] preempted context captured",
    "[PASS] timer preemption returned to host",
    "[pid 2] scheduled by timer preemption",
    "[PASS] non-yielding process was preempted",
    "[PASS] preemptive round robin",
    "[PASS] desktop remained online after preemption",
])
```

Also require the marker order to match that sequence and reject reset/fault evidence:

```python
if ("#DF" in content or
        "double fault" in content.lower() or
        "triple fault" in content.lower() or
        "reset" in content.lower()):
    print("qemu smoke test: FAIL")
    print("preemption test encountered a reset or fatal fault")
    sys.exit(1)
```

- [ ] **Step 4: Run the dedicated proof and verify GREEN**

```bash
make build/linux95-preemption-test.img
python3 tests/qemu_smoke.py --process-preemption-test
```

Expected: PID1 enters its infinite loop, PIT preemption returns to host, PID2 runs despite PID1 never yielding, and a later timer-driven switch proves round-robin continuation.

Then add the preemption run to `test-qemu`:

```make
>$(PYTHON) tests/qemu_smoke.py --process-preemption-test
```

- [ ] **Step 5: Commit the QEMU proof**

```bash
git add tests/prepare_fat32_image.py tests/qemu_smoke.py Makefile
git commit -m "Prove timer preemption with non-yielding processes"
```

---

### Task 8: Full regression, binary inspection, and manual desktop acceptance

**Files:**
- Modify only if verification exposes a real defect.
- No feature expansion in this task.

**Interfaces:**
- Verifies all interfaces produced by Tasks 1-7.

- [ ] **Step 1: Clean-build and run the complete host/static suite**

```bash
make clean
make all
make test
```

Expected: PASS.

- [ ] **Step 2: Run normal, network, no-network, cooperative, fault, missing-userspace, and preemption QEMU checks**

```bash
make test-qemu
python3 tests/qemu_smoke.py --process-self-test
python3 tests/qemu_smoke.py --process-fault-test
python3 tests/qemu_smoke.py --without-user-programs
python3 tests/qemu_smoke.py --process-preemption-test
```

Expected: every mode PASS; no panic, double fault, triple fault, or reset.

- [ ] **Step 3: Perform static binary verification**

```bash
nm -u build/kernel.elf
objdump -d build/kernel.elf | grep -n -E 'iretq|sysretq|sysret'
git diff --check
```

Expected:
- `nm -u build/kernel.elf` prints nothing.
- disassembly shows the user IRET path and the existing SYSRET fast path.
- `git diff --check` prints nothing.

Also inspect the interrupt and scheduler objects if needed:

```bash
objdump -dr build/interrupts_asm.o
objdump -dr build/context.o
objdump -dr build/syscall_entry.o
```

Confirm no new `swapgs` was introduced into the common interrupt stub.

- [ ] **Step 4: Run the graphical manual acceptance test**

Boot Linux95 graphically with the preemption FAT32 image attached:

```bash
qemu-system-x86_64 \
  -machine pc \
  -m 128M \
  -boot c \
  -vga std \
  -drive if=ide,index=0,media=disk,format=raw,file=build/linux95-kernel.img \
  -drive if=ide,index=1,media=disk,format=raw,file=build/linux95-preemption-test.img \
  -netdev user,id=net0 \
  -device rtl8139,netdev=net0 \
  -no-reboot \
  -no-shutdown
```

Manually verify all seven acceptance items from the spec:

```text
1. Desktop remains visible/responsive.
2. Mouse moves and top-panel interaction works.
3. Terminal/input processing remains alive.
4. Uptime continues updating.
5. Network polling continues to make progress when the NIC is present.
6. Non-yielding Ring 3 processes continue receiving round-robin CPU slices.
7. No panic/reset occurs during repeated preemption.
```

- [ ] **Step 5: Run final branch review and commit only fixes found by verification**

If verification required no code changes, do not create an empty commit. If fixes were needed, rerun every affected test before committing:

```bash
git status --short
git diff --check
```

Do **not** stage unrelated untracked `release/` files or the unrelated RTL8139 plan.

---

## Final Completion Gate

Do not declare the milestone complete until all of these are evidenced in the final verification log:

```text
[ ] 100 Hz PIT unchanged
[ ] fresh 5-tick / ~50 ms quantum every dispatch
[ ] CPL0 timer ticks do not consume user quantum
[ ] fifth CPL3 tick preempts exactly once
[ ] HostReason::Preempt is distinct
[ ] complete user register context captured
[ ] timer-preempted context uses ReturnKind::Iret
[ ] EOI occurs before non-returning host transfer
[ ] strict round-robin preserved
[ ] single Ready process still returns through host at expiry
[ ] non-yielding PID1 cannot starve PID2
[ ] host desktop regains control between quanta
[ ] cooperative yield still passes
[ ] int 0x80 and syscall/sysretq still pass
[ ] exit/reaping still passes
[ ] user-fault isolation still passes
[ ] missing userspace still boots nonfatally
[ ] RTL8139 network and no-network paths pass
[ ] desktop/mouse/keyboard/terminal/uptime remain responsive
[ ] nm -u build/kernel.elf is empty
[ ] git diff --check is clean
[ ] no panic, double fault, triple fault, or reset
```

## Execution Notes

Start execution from the merged `v1.0-dev` state containing PR #3 / merge commit `4f059a318bb4a9d5848e419f3673c0016c3731ac` or a later clean descendant that has not changed these scheduler interfaces. Create an isolated worktree/feature branch at execution time using the superpowers worktree workflow; do not implement directly on the main `v1.0-dev` checkout.

Before production code, place the approved design spec at:

```text
docs/superpowers/specs/2026-09-25-linux95-preemptive-scheduling-design.md
```

and this implementation plan at:

```text
docs/superpowers/plans/2026-09-25-linux95-preemptive-scheduling-implementation-plan.md
```

Commit those documentation files on the feature branch before starting Task 1. Do not push, merge, delete branches/worktrees, or publish anything without explicit user approval.
