# Linux95 Kernel v1.0 Memory Foundation Design

Date: 2026-09-23
Branch: `v1.0-dev`
Base: Linux95 Kernel v0.2 Interactive (`v0.2.0`)

## 1. Goal

Build the first Linux95 Kernel v1.0 foundation on top of the known-good v0.2 kernel without changing the stable `main` branch or `v0.2.0` tag.

This milestone adds:

- a higher-half kernel transition,
- a higher-half direct physical-memory map (HHDM),
- a 4 KiB physical-page allocator,
- a 4 KiB virtual-memory manager,
- stronger heap/memory management,
- memory self-tests,
- automated QEMU boot checkpoints.

This milestone does **not** add ATA, filesystems, networking, USB, audio, SMP, user mode, processes, or a GUI.

## 2. Existing Boot Contract

The existing custom BIOS boot path remains the starting point:

1. Stage 1 loads Stage 2.
2. Stage 2 enables A20, obtains the E820 map, checks long-mode support, loads the raw kernel at physical `0x00100000`, enters long mode, and jumps to the low kernel entry.
3. The kernel receives `BootInfo` in `RDI`.
4. The current v0.2 low identity mapping stays available during the transition.

The v1.0 memory work begins **inside the kernel**, not inside Stage 2.

## 3. Address Layout

### Physical kernel load

`0x0000000000100000` (1 MiB)

### Higher-half kernel region base

`0xFFFFFFFF80000000`

The initial bootstrap mapping maps physical address `0x00000000` to this higher-half base with 2 MiB pages.

Therefore the kernel physically loaded at `0x00100000` appears at:

`0xFFFFFFFF80100000`

This becomes the actual higher-half kernel execution address.

### Higher-half direct map

`HHDM_BASE = 0xFFFF800000000000`

For physical address `P`:

`HHDM(P) = HHDM_BASE + P`

Example:

- physical `0x00100000`
- HHDM virtual `0xFFFF800000100000`

### Page sizes

- Bootstrap transition: 2 MiB huge pages.
- Normal VM manager: 4 KiB pages.

## 4. Transition Sequence

The kernel starts at the existing low virtual/physical address and performs the following sequence:

1. Disable interrupts.
2. Validate `BootInfo`.
3. Initialize the debug console.
4. Emit `[BOOT] low_kernel_entry`.
5. Build new bootstrap page tables.
6. Keep the existing low identity mapping.
7. Add a higher-half mapping for the kernel region.
8. Add the HHDM mapping for the physical memory range needed by the kernel.
9. Emit `[PASS] bootstrap_tables_created`.
10. Load the new PML4 into `CR3`.
11. Emit `[PASS] cr3_reloaded`.
12. Jump to the higher-half kernel entry at `0xFFFFFFFF80100000`.
13. Emit `[PASS] higher_half_entry`.
14. Confirm HHDM access.
15. Emit `[PASS] hhdm_online`.
16. Initialize the physical-page allocator.
17. Emit `[PASS] physical_allocator_online`.
18. Initialize the 4 KiB virtual-memory manager.
19. Emit `[PASS] virtual_memory_online`.
20. Run memory self-tests.
21. Emit `[PASS] memory_self_test`.
22. Re-enable interrupts.
23. Start the existing interactive shell.
24. Emit `[PASS] shell_ready`.

Low mappings are not removed until the higher-half mapping, HHDM, allocator, VM manager, and self-tests have passed.

## 5. Components

### `kernel/memory/address.hpp`

Defines:

- `kPageSize = 4096`
- `kHugePageSize = 2 * 1024 * 1024`
- `kKernelPhysicalBase = 0x00100000`
- `kKernelRegionBase = 0xFFFFFFFF80000000`
- `kKernelVirtualBase = 0xFFFFFFFF80100000`
- `kHhdmBase = 0xFFFF800000000000`

Provides checked helpers for:

- physical to HHDM virtual conversion,
- HHDM virtual to physical conversion,
- 4 KiB alignment.

### `kernel/arch/x86_64/control_regs.hpp`

Provides minimal x86_64 helpers for:

- reading/writing `CR3`,
- invalidating one TLB entry with `invlpg`,
- interrupt enable/disable where needed.

### `kernel/arch/x86_64/paging_bootstrap.asm`

Provides the architecture transition helper that reloads `CR3` and transfers execution to the higher-half continuation.

It does not own policy or allocate page tables.

### `kernel/memory/physical.hpp/.cpp`

Implements a bitmap physical-page allocator using 4 KiB pages.

Responsibilities:

- consume the BIOS E820 memory map,
- identify usable physical memory,
- reserve all non-usable areas,
- reserve kernel image memory,
- reserve bootloader/BootInfo/E820/page-table memory,
- reserve VGA and critical low-memory ranges,
- allocate one page,
- allocate contiguous pages when explicitly needed,
- free pages that were allocated by the allocator,
- report total/free/used page counts.

The allocator must never return reserved pages.

### `kernel/memory/paging.hpp/.cpp`

Implements x86_64 4-level paging structures and mapping operations.

API includes:

- `map_page(virtual, physical, flags)`
- `unmap_page(virtual)`
- `translate(virtual)`
- `is_mapped(virtual)`

Page tables themselves come from the physical allocator after it is online.

### `kernel/memory/virtual.hpp/.cpp`

Owns the higher-level virtual-memory policy:

- bootstrap higher-half mapping creation,
- HHDM setup,
- kernel-region mapping,
- page-table activation,
- transition bookkeeping,
- safe removal of obsolete bootstrap mappings after verification.

### `kernel/memory/memory.cpp`

Coordinates initialization order and self-tests.

## 6. Physical Allocator Rules

The physical allocator uses one bitmap bit per 4 KiB physical page.

Bit meaning:

- `1` = reserved/used
- `0` = free

Initialization starts with every page marked reserved. Then E820 type-1 usable ranges are cleared to free, followed by re-reserving all kernel-critical ranges.

At minimum, these must be reserved:

- physical `0x00000000` through the end of critical low-memory bootstrap state,
- Stage 1 and Stage 2 memory,
- BootInfo,
- E820 map,
- bootstrap page tables,
- VGA memory around `0x000B8000`,
- kernel physical image from `__kernel_phys_start` through `__kernel_phys_end`,
- allocator bitmap storage itself.

Allocator metadata must live in explicitly reserved physical memory.

## 7. Virtual Memory Rules

The VM manager uses 4 KiB pages and x86_64 4-level translation:

- PML4
- PDPT
- PD
- PT

Supported flags for this milestone:

- present,
- writable,
- executable/non-executable where supported,
- global where appropriate.

User/supervisor separation is not required yet because v1.0 has no user mode in this milestone.

All page-table memory must be zeroed before use.

`map_page()` must reject invalid alignment and refuse to silently overwrite an existing mapping unless explicitly requested by a future replace API.

`unmap_page()` invalidates the TLB entry.

## 8. Linker Contract

The linker script gains explicit symbols for the physical kernel range and higher-half virtual addresses.

The build must preserve the physical image layout expected by the current Stage 2 raw-sector loader.

The kernel binary remains loadable at physical `0x00100000`.

The final link must expose enough symbols to:

- reserve the kernel physical range,
- identify the higher-half continuation address,
- calculate physical addresses of statically allocated bootstrap paging objects where required.

The exact linker implementation must be validated by `readelf`/`nm` tests before QEMU boot tests run.

## 9. Debugging and Failure Reporting

The existing QEMU debug port at `0xE9` is the primary automated boot trace.

Required checkpoints:

```text
[BOOT] low_kernel_entry
[PASS] bootstrap_tables_created
[PASS] cr3_reloaded
[PASS] higher_half_entry
[PASS] hhdm_online
[PASS] physical_allocator_online
[PASS] virtual_memory_online
[PASS] memory_self_test
[PASS] shell_ready
```

Any fatal memory failure emits a `[PANIC]` marker before halting.

The QEMU smoke test reports the last checkpoint seen when boot does not reach the shell.

## 10. Memory Self-Tests

The automated in-kernel memory test must, at minimum:

1. allocate several physical pages,
2. verify returned pages are distinct and 4 KiB aligned,
3. write/read a pattern through their HHDM addresses,
4. map one allocated page at a dedicated test virtual address,
5. write/read through that virtual mapping,
6. verify `translate()` returns the expected physical address,
7. unmap the test page,
8. verify the mapping is gone,
9. free all temporary physical pages,
10. verify allocator accounting returns to the pre-test count.

The test must avoid kernel code/data, VGA, boot structures, and page-table memory.

## 11. Automated Tests

`make test` continues to run source and image checks and gains structural validation for the new memory subsystem.

`make test-qemu` boots the kernel headlessly and requires every checkpoint listed above.

A failed checkpoint is a failed test.

The existing visible `make run` remains available for final manual keyboard/display testing.

## 12. Safety and Rollback

All work happens on `v1.0-dev`.

Stable recovery points remain:

- branch `main`
- tag `v0.2.0`

No v1.0 memory work is merged into `main` until:

- the project builds from clean state,
- host-side tests pass,
- QEMU smoke tests pass,
- the higher-half shell is manually booted once,
- Git working tree is clean after the release checkpoint.

## 13. Completion Criteria

This milestone is complete only when a clean build produces a kernel that:

- starts through the existing BIOS bootloader,
- enters the low kernel bootstrap,
- activates new page tables,
- executes kernel code at `0xFFFFFFFF80100000`,
- provides a working HHDM at `0xFFFF800000000000`,
- initializes the E820-backed 4 KiB bitmap physical allocator,
- initializes the 4 KiB virtual-memory manager,
- passes the in-kernel memory self-test,
- reaches the existing interactive shell,
- passes `make test`,
- passes `make test-qemu`.

Disk/filesystem work begins only after this milestone is green.
