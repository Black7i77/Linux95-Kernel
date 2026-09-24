from pathlib import Path

plan = r'''# Linux95 Graphics Desktop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot Linux95 into a 1280x720x32 VBE software-rendered desktop with PS/2 mouse input, click-to-focus windows, an interactive graphical terminal using the existing shell commands, a System Info app, and VGA text fallback when graphics is unavailable.

**Architecture:** Stage 2 discovers and sets an exact VBE 1280x720x32 linear-framebuffer mode and passes a firmware-neutral framebuffer description through `BootInfo`. The higher-half kernel maps that MMIO framebuffer into a dedicated virtual range, renders directly with dirty-region repainting, drives a PS/2 mouse through IRQ12, and runs a desktop event loop that owns the top panel, window manager, Terminal, and System Info. Existing shell logic is refactored behind an output/session interface so the same commands run in both VGA fallback and the GUI terminal.

**Tech Stack:** x86_64, legacy BIOS, VBE, NASM, freestanding C++17, PS/2 controller, 8259 PIC, PIT, QEMU `-vga std`, host-side C++17 tests, Python source/image/QEMU smoke checks, GNU Make.

**Spec:** `docs/superpowers/specs/2026-09-25-linux95-graphics-desktop-design.md`

## Global Constraints

- Initial graphics target is QEMU using legacy BIOS VBE.
- Required graphical mode is exactly 1280x720 at 32 bits per pixel.
- Require a direct-colour VBE mode with linear framebuffer support.
- Do not hard-code a VBE mode number; enumerate the mode list.
- Graphics rendering is CPU-only and writes directly to the visible framebuffer.
- Do not add a full-screen back buffer in this milestone.
- Use dirty-region redraws rather than unconditional full-screen repainting on mouse movement or typing.
- The top panel is at the top of the screen.
- Window focus is click-to-focus.
- Windows support z-order, dragging, resizing, minimize, restore, and close.
- The top panel contains Applications, Terminal and System Info quick-launch controls, task buttons, and uptime/status.
- Terminal and System Info are kernel-resident built-in apps for this milestone.
- The graphical Terminal must execute the existing real shell commands; do not duplicate command implementations.
- Keep VGA text mode as the fallback when VBE cannot be established before graphics-mode activation.
- Once VBE mode is active and Linux95 is in long mode, do not attempt BIOS calls to restore VGA.
- PS/2 mouse v1 uses three-byte packets and supports X/Y movement plus left, right, and middle buttons.
- Scroll-wheel, USB input, GPU acceleration, compositing, animations, transparency, user mode, processes, GUI file manager, networking, audio, UEFI, and GOP are outside this milestone.
- Mouse initialization failure alone must not prevent the graphical desktop from booting.
- Existing `make test` and `make test-qemu` regressions must remain green.
- Do not push, merge, or publish without explicit user approval.

## File Structure

### Boot and ABI
- Modify `boot/stage2.asm` — VBE discovery, exact mode selection, VBE mode set, framebuffer fields in BootInfo, expanded kernel loading.
- Modify `kernel/boot_info.hpp` — packed `FramebufferInfo` ABI appended to `BootInfo`.
- Modify `Makefile` — larger kernel image budget, new kernel objects, host test targets.
- Modify `tests/image_checks.py` — continue verifying Makefile/stage2 kernel geometry after the image budget grows.
- Create `tests/graphics_source_checks.py` — source-level checks for VBE path, BootInfo ABI markers, IRQ12 wiring, and no BIOS use from the 64-bit graphics code.

### Graphics
- Create `kernel/graphics/framebuffer_helpers.hpp` — pure validation, byte-size, mask-overlap, and mapping-layout helpers.
- Create `kernel/graphics/framebuffer.hpp`
- Create `kernel/graphics/framebuffer.cpp` — physical framebuffer mapping and live framebuffer view.
- Create `kernel/graphics/renderer.hpp`
- Create `kernel/graphics/renderer.cpp` — pixel packing, clipping, rectangles, lines, bitmap text.
- Create `kernel/graphics/font8x8.hpp` — fixed printable-ASCII bitmap font data.
- Create `tests/host/framebuffer_helpers_test.cpp`
- Create `tests/host/renderer_test.cpp`

### PS/2 input
- Create `kernel/arch/ps2.hpp`
- Create `kernel/arch/ps2.cpp` — bounded controller waits and mouse-device command routing.
- Create `kernel/arch/mouse_helpers.hpp` — pure three-byte packet decoder.
- Create `kernel/arch/mouse.hpp`
- Create `kernel/arch/mouse.cpp` — initialization, IRQ12 byte intake, mouse-event ring.
- Modify `kernel/arch/interrupts.cpp` — dispatch vector 44 to mouse IRQ.
- Modify `kernel/kernel.cpp` — initialize mouse and unmask IRQ12 only on success.
- Create `tests/host/mouse_helpers_test.cpp`

### Terminal and shell
- Create `kernel/terminal/output.hpp` — callback-based terminal output abstraction and integer/hex helpers.
- Create `kernel/terminal/vga_output.hpp`
- Create `kernel/terminal/vga_output.cpp` — adapter from `terminal::Output` to existing VGA functions.
- Create `kernel/terminal/shell_session.hpp`
- Create `kernel/terminal/shell_session.cpp` — reusable line editing, prompt, and command-submit session.
- Modify `kernel/terminal/shell.hpp`
- Modify `kernel/terminal/shell.cpp` — commands write through `terminal::Output`; expose reusable executor plus VGA fallback runner.
- Create `tests/host/shell_session_test.cpp`

### GUI and apps
- Create `kernel/gui/geometry.hpp` — `Point`, `Rect`, intersection, containment, clamp.
- Create `kernel/gui/window_manager.hpp`
- Create `kernel/gui/window_manager.cpp` — fixed-capacity window state, focus, z-order, drag, resize, minimize, restore, close.
- Create `tests/host/window_manager_test.cpp`
- Create `kernel/gui/terminal_model.hpp`
- Create `kernel/gui/terminal_model.cpp` — fixed-capacity terminal text/history model and graphical output sink.
- Create `tests/host/terminal_model_test.cpp`
- Create `kernel/gui/app.hpp` — built-in app callback interface.
- Create `kernel/gui/terminal_app.hpp`
- Create `kernel/gui/terminal_app.cpp`
- Create `kernel/gui/system_info_app.hpp`
- Create `kernel/gui/system_info_app.cpp`
- Create `kernel/gui/desktop.hpp`
- Create `kernel/gui/desktop.cpp` — top panel, app launching, event loop, dirty rectangles, cursor, app dispatch.
- Modify `kernel/kernel.cpp` — choose desktop vs VGA fallback.
- Modify `tests/qemu_smoke.py` — require graphics/desktop checkpoints with QEMU standard VGA.
- Modify `README.md` — document the Graphics Desktop Foundation milestone.

## Review Focus

1. **Malformed framebuffer arithmetic:** a huge pitch or address near `UINT64_MAX` must be rejected before addition/multiplication or page rounding can wrap. Task 1 adds explicit overflow tests.
2. **Exact VBE mode missing or mode-set failure:** Stage 2 must leave `framebuffer.available == 0` and continue to the VGA shell. Task 2 adds source invariants; Task 13 exercises the successful QEMU path and preserves the fallback branch.
3. **Corrupt/desynchronized PS/2 packets:** bytes without first-byte bit 3, X/Y overflow packets, and incomplete packets must be discarded/resynchronized without moving the pointer. Task 5 tests these cases.
4. **Window movement/resizing outside the desktop work area:** dragging/resizing must keep the full window inside the 1280x692 work area below the 28-pixel panel and honor minimum dimensions. Task 8 tests every boundary.
5. **Long terminal output:** fixed terminal history must scroll deterministically, preserve the newest lines, and `clear` must reset cursor/history state without out-of-bounds writes. Task 9 tests history wrap and clear.

---

### Task 1: Framebuffer ABI and pure validation helpers

**Files:**
- Modify: `kernel/boot_info.hpp`
- Create: `kernel/graphics/framebuffer_helpers.hpp`
- Create: `tests/host/framebuffer_helpers_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: existing packed `linux95::BootInfo`.
- Produces:
  - `linux95::FramebufferInfo`
  - `linux95::graphics::ValidationStatus validate_framebuffer(const FramebufferInfo&, uint64_t& byte_size)`
  - `linux95::graphics::MappingLayout mapping_layout(const FramebufferInfo&)`

- [ ] **Step 1: Write the failing host test**

Create `tests/host/framebuffer_helpers_test.cpp` with tests that assert:

```cpp
#include "boot_info.hpp"
#include "graphics/framebuffer_helpers.hpp"

#include <assert.h>
#include <stdint.h>

using linux95::FramebufferInfo;
using linux95::graphics::MappingLayout;
using linux95::graphics::ValidationStatus;
using linux95::graphics::mapping_layout;
using linux95::graphics::validate_framebuffer;

static FramebufferInfo valid_info()
{
    return FramebufferInfo{
        0xFD000123ULL,
        1280,
        720,
        1280 * 4,
        32,
        8, 16,
        8, 8,
        8, 0,
        1,
    };
}

int main()
{
    uint64_t bytes = 0;

    FramebufferInfo info = valid_info();
    assert(validate_framebuffer(info, bytes) == ValidationStatus::Ok);
    assert(bytes == 1280ULL * 4ULL * 720ULL);

    info.available = 0;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::Unavailable);

    info = valid_info();
    info.width = 1024;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidMode);

    info = valid_info();
    info.pitch = 1280 * 4 - 1;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidPitch);

    info = valid_info();
    info.physical_address = 0;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidAddress);

    info = valid_info();
    info.red_mask_shift = 8;
    info.green_mask_shift = 8;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidColorMasks);

    info = valid_info();
    info.pitch = UINT32_MAX;
    info.height = UINT32_MAX;
    assert(validate_framebuffer(info, bytes) != ValidationStatus::Ok);

    info = valid_info();
    info.physical_address = UINT64_MAX - 100;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::Overflow);

    const MappingLayout layout = mapping_layout(valid_info());
    assert(layout.physical_base == 0xFD000000ULL);
    assert(layout.offset == 0x123ULL);
    assert(layout.page_count > 0);

    return 0;
}
```

- [ ] **Step 2: Add the host test target and prove RED**

Add to `Makefile`:

```make
$(BUILD)/host-framebuffer-helpers-test: tests/host/framebuffer_helpers_test.cpp kernel/graphics/framebuffer_helpers.hpp kernel/boot_info.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-graphics: $(BUILD)/host-framebuffer-helpers-test
>$(BUILD)/host-framebuffer-helpers-test
```

Add `test-host-graphics` to `.PHONY` and to `test`.

Run:

```bash
make clean
make test-host-graphics
```

Expected: compile failure because `FramebufferInfo` and `framebuffer_helpers.hpp` do not exist.

- [ ] **Step 3: Implement the packed ABI**

Append the framebuffer record inside `namespace linux95` and append it to `BootInfo`:

```cpp
struct __attribute__((packed)) FramebufferInfo {
    uint64_t physical_address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bits_per_pixel;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t available;
};

struct __attribute__((packed)) BootInfo {
    uint32_t magic;
    uint32_t e820_count;
    uint64_t e820_address;
    uint8_t boot_drive;
    FramebufferInfo framebuffer;
};

static_assert(sizeof(FramebufferInfo) == 28, "Linux95 framebuffer ABI changed");
static_assert(sizeof(BootInfo) == 45, "Linux95 BootInfo ABI changed");
```

Keep the existing boot-info validity checks for magic/E820; graphics availability is validated separately.

- [ ] **Step 4: Implement pure validation/mapping helpers**

`framebuffer_helpers.hpp` must define:

```cpp
#pragma once

#include "boot_info.hpp"
#include "memory/address.hpp"

#include <stdint.h>

namespace linux95::graphics {

enum class ValidationStatus : uint8_t {
    Ok,
    Unavailable,
    InvalidMode,
    InvalidAddress,
    InvalidPitch,
    InvalidColorMasks,
    Overflow,
};

struct MappingLayout {
    uint64_t physical_base;
    uint64_t offset;
    uint64_t page_count;
};

ValidationStatus validate_framebuffer(
    const FramebufferInfo& info,
    uint64_t& byte_size);

MappingLayout mapping_layout(const FramebufferInfo& info);

} // namespace linux95::graphics
```

Implement the functions inline in this header so the host test requires no kernel-only link dependencies. Validation rules are exact: `available == 1`, width `1280`, height `720`, bpp `32`, nonzero physical address, pitch at least `5120`, each RGB mask size exactly 8, each shift below 32, masks non-overlapping, `pitch * height` cannot overflow, and `physical_address + byte_size` cannot overflow. `mapping_layout` aligns the physical address down to 4096 bytes and rounds the covered byte range up to full pages.

- [ ] **Step 5: Run GREEN**

```bash
make test-host-graphics
make test
git diff --check
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add Makefile kernel/boot_info.hpp kernel/graphics/framebuffer_helpers.hpp tests/host/framebuffer_helpers_test.cpp
git commit -m "Add framebuffer boot ABI validation"
```

---

### Task 2: Expand kernel image capacity and add BIOS VBE handoff

**Files:**
- Modify: `Makefile`
- Modify: `boot/stage2.asm`
- Modify: `tests/image_checks.py`
- Create: `tests/graphics_source_checks.py`

**Interfaces:**
- Consumes: `BootInfo.framebuffer` layout from Task 1.
- Produces: Stage 2 fills framebuffer fields at BootInfo offset 17 and sets `available=1` only after successful VBE mode set.

- [ ] **Step 1: Write source checks first**

Create `tests/graphics_source_checks.py` that reads `boot/stage2.asm`, `kernel/boot_info.hpp`, and `kernel/arch/interrupts.cpp` and fails unless the source contains:
- VBE controller call `0x4F00`
- VBE mode-info call `0x4F01`
- VBE set-mode call `0x4F02`
- LFB request bit `0x4000`
- an exact `1280` and `720` comparison
- a 32-bpp comparison
- a BootInfo framebuffer available write at offset `17 + 27`
- no `int 0x10` in kernel C++/64-bit assembly files

Use this concrete skeleton:

```python
#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
stage2 = (ROOT / "boot/stage2.asm").read_text()
boot_info = (ROOT / "kernel/boot_info.hpp").read_text()

required_stage2 = [
    "0x4F00",
    "0x4F01",
    "0x4F02",
    "0x4000",
    "1280",
    "720",
    "32",
]
for needle in required_stage2:
    if needle not in stage2:
        raise SystemExit(f"FAIL: stage2 missing graphics marker {needle}")

if "FramebufferInfo framebuffer;" not in boot_info:
    raise SystemExit("FAIL: BootInfo framebuffer field missing")

kernel_root = ROOT / "kernel"
for path in kernel_root.rglob("*"):
    if path.suffix not in {".cpp", ".hpp", ".asm"}:
        continue
    if path.name in {"entry.asm"}:
        continue
    text = path.read_text(errors="replace")
    if "int 0x10" in text.lower():
        raise SystemExit(f"FAIL: BIOS video interrupt found in {path}")

print("graphics source checks: PASS")
```

Add it to `make test`.

- [ ] **Step 2: Prove RED**

```bash
python3 tests/graphics_source_checks.py
```

Expected: FAIL because VBE calls are not present.

- [ ] **Step 3: Grow the kernel image budget before GUI objects arrive**

Change:

```make
KERNEL_SECTORS := 256
IMAGE_SECTORS := 273
```

and in `boot/stage2.asm`:

```asm
KERNEL_SECTORS equ 256
```

Replace the fixed two-read loader with four 64-sector reads to physical:
- `0x20000`
- `0x28000`
- `0x30000`
- `0x38000`

Then copy `256 * 512` bytes from `0x20000` to `0x00100000` in protected mode. Keep every individual EDD transfer at 64 sectors.

`tests/image_checks.py` already derives the expected image size from Makefile values, so preserve that dynamic check; add one assertion that `kernel_sectors % 64 == 0`.

- [ ] **Step 4: Add VBE scratch buffers and framebuffer-default state**

Use non-overlapping low-memory buffers:

```asm
VBE_CTRL_ADDR      equ 0x6000
VBE_MODE_ADDR      equ 0x6200
BOOTINFO_FB_OFFSET equ 17
FB_AVAILABLE       equ BOOTINFO_FB_OFFSET + 27
```

At Stage 2 startup set:

```asm
mov byte [BOOTINFO_ADDR + FB_AVAILABLE], 0
```

- [ ] **Step 5: Implement VBE discovery**

Before leaving BIOS mode, after the kernel disk load succeeds:
1. Call `INT 10h AX=4F00` with `ES:DI = 0000:6000` and signature `VBE2`.
2. Follow the returned far pointer at controller-info offset `0x0E`.
3. Enumerate mode IDs until `0xFFFF`.
4. For each mode call `AX=4F01`, `CX=mode`, `ES:DI=0000:6200`.
5. Require mode attribute bit 0 and bit 7, X=1280, Y=720, bpp=32, memory model=6.
6. Save the selected mode.
7. Call `AX=4F02`, `BX=selected_mode | 0x4000`.
8. Only after `AX == 0x004F`, fill framebuffer fields and set `available=1`.

Use VBE mode-info offsets:
- attributes `0x00`
- bytes/scanline `0x10`
- X `0x12`
- Y `0x14`
- bpp `0x19`
- memory model `0x1B`
- red size/shift `0x1F/0x20`
- green size/shift `0x21/0x22`
- blue size/shift `0x23/0x24`
- physical framebuffer `0x28`

Any VBE failure returns with `available=0` and continues booting; it must not jump to the fatal E3-E6 path.

- [ ] **Step 6: Run build/source/image tests**

```bash
make clean
make all
python3 tests/graphics_source_checks.py
python3 tests/image_checks.py
make test
git diff --check
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add Makefile boot/stage2.asm tests/image_checks.py tests/graphics_source_checks.py
git commit -m "Add VBE framebuffer boot handoff"
```

---

### Task 3: Map the VBE framebuffer into the higher-half kernel

**Files:**
- Create: `kernel/graphics/framebuffer.hpp`
- Create: `kernel/graphics/framebuffer.cpp`
- Modify: `Makefile`
- Modify: `kernel/kernel.cpp`

**Interfaces:**
- Consumes: `FramebufferInfo`, `validate_framebuffer`, `mapping_layout`, `memory::paging::map_page`.
- Produces:
  - `graphics::FramebufferInitResult`
  - `graphics::initialize_framebuffer(const BootInfo&)`
  - `graphics::framebuffer()`

- [ ] **Step 1: Define the API**

Use:

```cpp
namespace linux95::graphics {

constexpr uint64_t kFramebufferVirtualBase = 0xFFFF900000000000ULL;

enum class FramebufferInitResult : uint8_t {
    Ready,
    Unavailable,
    InvalidMetadata,
    MappingFailed,
};

struct PixelFormat {
    uint8_t red_size, red_shift;
    uint8_t green_size, green_shift;
    uint8_t blue_size, blue_shift;
};

struct Framebuffer {
    volatile uint8_t* data;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    PixelFormat format;
};

FramebufferInitResult initialize_framebuffer(const BootInfo& boot_info);
const Framebuffer* framebuffer();

}
```

- [ ] **Step 2: Compile first to prove missing implementation**

Add `framebuffer.o` to `KERNEL_OBJS` and a Makefile compile rule, include the header from `kernel/kernel.cpp`, then run:

```bash
make all
```

Expected: link failure for `initialize_framebuffer`/`framebuffer`.

- [ ] **Step 3: Implement mapping**

Implementation requirements:
- Return `Unavailable` when validation returns `Unavailable`.
- Return `InvalidMetadata` for every other validation error.
- Use `mapping_layout`.
- Map every page at `kFramebufferVirtualBase + i * 4096` to `physical_base + i * 4096`.
- Use `memory::paging::kPageWritable | memory::paging::kPageNoExecute`.
- On any mapping failure, unmap every page mapped by this function before returning `MappingFailed`.
- `Framebuffer.data` is `kFramebufferVirtualBase + layout.offset`.
- Preserve width, height, pitch, and colour-mask metadata exactly.

- [ ] **Step 4: Integrate only enough for diagnostics**

In higher-half startup, after the physical allocator and VM self-test are online, call graphics initialization and write one of:

```text
[PASS] framebuffer_mapped
[INFO] framebuffer_unavailable
[PANIC] framebuffer_invalid
[PANIC] framebuffer_mapping
```

Do not start the GUI yet. If unavailable, continue current VGA flow. Invalid/mapping failures halt because VBE may already own the display.

- [ ] **Step 5: Verify**

```bash
make test
make test-qemu
git diff --check
```

The current QEMU test may still terminate on `[PASS] shell_ready`; that is expected until Task 13 changes the target checkpoint.

- [ ] **Step 6: Commit**

```bash
git add Makefile kernel/graphics/framebuffer.hpp kernel/graphics/framebuffer.cpp kernel/kernel.cpp
git commit -m "Map VBE framebuffer in kernel"
```

---

### Task 4: Software renderer, clipping, colour packing, and bitmap text

**Files:**
- Create: `kernel/graphics/renderer.hpp`
- Create: `kernel/graphics/renderer.cpp`
- Create: `kernel/graphics/font8x8.hpp`
- Create: `tests/host/renderer_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `graphics::Framebuffer`.
- Produces:
  - `Color`
  - `Rect`
  - `pack_color`
  - `clip_rect`
  - `put_pixel`
  - `fill_rect`
  - `draw_rect`
  - `draw_line`
  - `draw_char`
  - `draw_text`

- [ ] **Step 1: Write RED renderer tests**

Test an in-memory 8x8 surface. Required assertions:
- RGB and BGR masks pack differently but correctly.
- negative/out-of-range rectangles clip safely.
- a completely off-screen rectangle writes no bytes.
- `fill_rect` changes exactly the expected pixels.
- `draw_char('A')` changes pixels only within its 8x8 cell.

Representative test setup:

```cpp
uint32_t pixels[64] = {};
Framebuffer fb{
    reinterpret_cast<volatile uint8_t*>(pixels),
    8, 8, 8 * 4,
    PixelFormat{8,16,8,8,8,0},
};

fill_rect(fb, Rect{-2, -2, 4, 4}, Color{255,0,0});
assert(pixels[0] == 0x00FF0000u);
assert(pixels[1] == 0x00FF0000u);
assert(pixels[8] == 0x00FF0000u);
assert(pixels[9] == 0x00FF0000u);
assert(pixels[10] == 0);
```

- [ ] **Step 2: Add host target and prove RED**

Create `host-renderer-test`, add it to `test-host-graphics`, run:

```bash
make test-host-graphics
```

Expected: compile/link failure.

- [ ] **Step 3: Implement fixed geometry and pixel primitives**

Use signed rectangle coordinates:

```cpp
struct Color { uint8_t r, g, b; };
struct Rect { int32_t x, y, width, height; };
```

`pack_color` must use framebuffer mask shifts/sizes rather than hard-coded BGR. `put_pixel` must perform bounds checks before calculating the destination address.

- [ ] **Step 4: Add the font**

`font8x8.hpp` contains a fixed 96-glyph table for printable ASCII `0x20..0x7F`, eight bytes per glyph, one byte per row, MSB-left. Unsupported bytes render as `'?'`. Keep the table `constexpr` so it adds no initialization code.

Pin the font convention with compile-time assertions:

```cpp
static_assert(kFontFirst == 0x20);
static_assert(kFontLast == 0x7F);
static_assert(sizeof(kFont8x8) == 96u * 8u);
```

- [ ] **Step 5: Implement text draw and GREEN**

`draw_char` reads exactly eight rows and eight bits per row. `draw_text` advances X by 8 pixels, handles `\n` by resetting X and adding 8 to Y, and clips through `put_pixel`.

Run:

```bash
make test-host-graphics
make test
git diff --check
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add Makefile kernel/graphics/renderer.hpp kernel/graphics/renderer.cpp kernel/graphics/font8x8.hpp tests/host/renderer_test.cpp
git commit -m "Add software framebuffer renderer"
```

---

### Task 5: PS/2 controller and three-byte mouse decoder

**Files:**
- Create: `kernel/arch/ps2.hpp`
- Create: `kernel/arch/ps2.cpp`
- Create: `kernel/arch/mouse_helpers.hpp`
- Create: `kernel/arch/mouse.hpp`
- Create: `kernel/arch/mouse.cpp`
- Create: `tests/host/mouse_helpers_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces:
  - `ps2::wait_input_clear(uint32_t limit)`
  - `ps2::wait_output_full(uint32_t limit)`
  - `mouse::PacketDecoder::feed(uint8_t, MouseEvent&)`
  - `mouse::initialize()`
  - `mouse::on_irq()`
  - `mouse::has_event()`
  - `mouse::read_event()`

- [ ] **Step 1: Write decoder tests**

Required cases:

```cpp
PacketDecoder decoder;
MouseEvent event{};

assert(!decoder.feed(0x00, event)); // no sync bit, discard
assert(!decoder.feed(0x08, event));
assert(!decoder.feed(0x05, event));
assert(decoder.feed(0xFD, event));
assert(event.dx == 5);
assert(event.dy == -3);
assert(!event.left);

decoder.reset();
assert(!decoder.feed(0x09, event));
assert(!decoder.feed(0x00, event));
assert(decoder.feed(0x00, event));
assert(event.left);

decoder.reset();
assert(!decoder.feed(0x48, event)); // X overflow set
assert(!decoder.feed(0x7F, event));
assert(!decoder.feed(0x00, event)); // complete packet discarded
```

Also test incomplete packet state and resynchronization.

- [ ] **Step 2: Add target and prove RED**

Add `host-mouse-helpers-test` to `test-host-graphics`, then:

```bash
make test-host-graphics
```

Expected: missing decoder.

- [ ] **Step 3: Implement decoder**

Rules:
- first byte must have bit 3 set
- bit 6 or bit 7 marks overflow; consume the whole packet but do not emit an event
- sign-extend X using first-byte bit 4 and Y using bit 5
- report raw PS/2 Y; desktop code will invert Y when applying it to screen coordinates
- expose left/right/middle from bits 0/1/2

- [ ] **Step 4: Implement bounded controller I/O**

All waits use a finite spin count and return `false` on timeout. Mouse command routing uses controller command `0xD4`, then writes the device byte to `0x60`. Initialization sequence:
1. enable auxiliary device with `0xA8`
2. read controller config with `0x20`
3. set IRQ12 bit 1
4. clear auxiliary-clock-disable bit 5
5. write config using `0x60`
6. send mouse `0xF6`, require `0xFA`
7. send mouse `0xF4`, require `0xFA`

`mouse::initialize()` returns `false` on any timeout or wrong ACK.

- [ ] **Step 5: Add event ring**

Use a fixed 64-event ring. On full ring, drop the newest event rather than corrupt indices.

- [ ] **Step 6: GREEN and commit**

```bash
make test-host-graphics
make test
git diff --check
git add Makefile kernel/arch/ps2.hpp kernel/arch/ps2.cpp kernel/arch/mouse_helpers.hpp kernel/arch/mouse.hpp kernel/arch/mouse.cpp tests/host/mouse_helpers_test.cpp
git commit -m "Add PS2 mouse input"
```

---

### Task 6: Wire IRQ12 without making mouse mandatory

**Files:**
- Modify: `kernel/arch/interrupts.cpp`
- Modify: `kernel/kernel.cpp`
- Modify: `tests/graphics_source_checks.py`

**Interfaces:**
- Consumes: `mouse::initialize`, `mouse::on_irq`.
- Produces: IRQ vector 44 dispatch and conditional PIC unmask.

- [ ] **Step 1: Extend source check and prove RED**

Require:

```python
interrupts = (ROOT / "kernel/arch/interrupts.cpp").read_text()
kernel = (ROOT / "kernel/kernel.cpp").read_text()

if "frame->vector == 44" not in interrupts:
    raise SystemExit("FAIL: IRQ12 vector 44 is not dispatched")
if "pic::send_eoi(12)" not in interrupts:
    raise SystemExit("FAIL: IRQ12 EOI missing")
if "pic::unmask_irq(12)" not in kernel:
    raise SystemExit("FAIL: IRQ12 is never unmasked")
```

Run source check; expect FAIL.

- [ ] **Step 2: Wire vector 44**

Before the generic IRQ branch:

```cpp
if (frame->vector == 44) {
    mouse::on_irq();
    pic::send_eoi(12);
    return;
}
```

- [ ] **Step 3: Make initialization optional**

In `kernel.cpp`:

```cpp
const bool mouse_online = mouse::initialize();
if (mouse_online) {
    pic::unmask_irq(12);
    debug::write("[PASS] mouse_initialized\n");
} else {
    debug::write("[INFO] mouse_unavailable\n");
}
```

Do not panic when `mouse_online == false`.

- [ ] **Step 4: Verify and commit**

```bash
python3 tests/graphics_source_checks.py
make test
make test-qemu
git diff --check
git add kernel/arch/interrupts.cpp kernel/kernel.cpp tests/graphics_source_checks.py
git commit -m "Wire optional PS2 mouse IRQ"
```

---

### Task 7: Refactor shell I/O into reusable output and session interfaces

**Files:**
- Create: `kernel/terminal/output.hpp`
- Create: `kernel/terminal/vga_output.hpp`
- Create: `kernel/terminal/vga_output.cpp`
- Create: `kernel/terminal/shell_session.hpp`
- Create: `kernel/terminal/shell_session.cpp`
- Modify: `kernel/terminal/shell.hpp`
- Modify: `kernel/terminal/shell.cpp`
- Create: `tests/host/shell_session_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces:
  - `terminal::Output`
  - `terminal::write`, `write_uint`, `write_hex`
  - `terminal::ShellSession`
  - `shell::execute_command(terminal::Output&, char*)`
  - `shell::run_vga()`

- [ ] **Step 1: Define callback output API**

Use no heap allocation and no virtual dispatch:

```cpp
struct Output {
    void* context;
    void (*put_char)(void*, char);
    void (*clear)(void*);
    void (*set_color)(void*, uint8_t foreground, uint8_t background);
};
```

Provide inline helpers that build `write`, decimal integer, and hexadecimal output entirely from `put_char`.

- [ ] **Step 2: Write session tests**

A fake output records chars in a fixed buffer and a fake executor records the submitted command. Test:
- `begin()` emits `linux95> `
- printable input is echoed
- backspace removes one character and emits backspace only when length > 0
- Enter NUL-terminates and submits the line
- command capacity stays 64 and never writes past the buffer
- after submit, next prompt appears

- [ ] **Step 3: Prove RED**

Add host target and run:

```bash
make test-host-graphics
```

Expected: missing session types.

- [ ] **Step 4: Implement `ShellSession`**

Use:

```cpp
using ExecuteCallback =
    void (*)(void* context, Output& output, char* command);

class ShellSession {
public:
    ShellSession(Output& output, void* context, ExecuteCallback execute);
    void begin();
    void on_char(char c);

private:
    static constexpr size_t kCommandCapacity = 64;
    Output& output_;
    void* execute_context_;
    ExecuteCallback execute_;
    char command_[kCommandCapacity];
    size_t length_;
    void prompt();
};
```

- [ ] **Step 5: Refactor command implementation**

Replace every direct `vga::*` call in command functions with the equivalent `terminal::*` helper on the supplied `Output&`. `clear` calls `output.clear(output.context)`. Preserve command names and behavior.

`run_vga()` creates a VGA adapter, constructs a session with `shell::execute_command`, then keeps the existing `keyboard::has_char/read_char` + `io::halt()` loop.

- [ ] **Step 6: Regression verify**

```bash
make test-host-graphics
make test
make test-qemu
git diff --check
```

QEMU must still reach the VGA shell at this stage.

- [ ] **Step 7: Commit**

```bash
git add Makefile kernel/terminal/output.hpp kernel/terminal/vga_output.hpp kernel/terminal/vga_output.cpp kernel/terminal/shell_session.hpp kernel/terminal/shell_session.cpp kernel/terminal/shell.hpp kernel/terminal/shell.cpp tests/host/shell_session_test.cpp
git commit -m "Decouple shell from VGA output"
```

---

### Task 8: Pure window-manager state machine

**Files:**
- Create: `kernel/gui/geometry.hpp`
- Create: `kernel/gui/window_manager.hpp`
- Create: `kernel/gui/window_manager.cpp`
- Create: `tests/host/window_manager_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces fixed-capacity `WindowManager` with a maximum of 8 windows.

- [ ] **Step 1: Write RED tests**

Use screen `1280x720`, panel height `28`, minimum window `160x100`.

Test:
- first added window is focused
- clicking a lower window raises it to highest z-order
- title-bar drag changes X/Y
- drag clamps X to `[0, screen_width - window_width]`
- drag clamps Y to `[28, screen_height - window_height]`
- resize never goes below `160x100`
- resize never crosses screen right/bottom
- minimize marks window minimized and removes it from hit testing
- restore returns previous geometry and focuses the window
- close removes it from hit testing and task enumeration

- [ ] **Step 2: Define exact model**

```cpp
using WindowId = uint32_t;

enum class WindowState : uint8_t {
    Open,
    Minimized,
    Closed,
};

struct Window {
    WindowId id;
    Rect bounds;
    Rect restore_bounds;
    WindowState state;
    uint8_t z;
    bool resizable;
    bool closable;
};

class WindowManager {
public:
    static constexpr size_t kMaxWindows = 8;
    static constexpr int32_t kPanelHeight = 28;
    static constexpr int32_t kMinWidth = 160;
    static constexpr int32_t kMinHeight = 100;

    bool add_window(WindowId id, Rect bounds, bool resizable, bool closable);
    WindowId focused() const;
    WindowId hit_test(Point p) const;
    bool focus(WindowId id);
    bool begin_drag(WindowId id, Point pointer);
    bool begin_resize(WindowId id, Point pointer);
    void pointer_move(Point pointer);
    void end_pointer_action();
    bool minimize(WindowId id);
    bool restore(WindowId id);
    bool close(WindowId id);
    const Window* find(WindowId id) const;
};
```

- [ ] **Step 3: Implement and GREEN**

Keep all state in a fixed array. Raising a window compacts/reassigns z values `0..N-1`; no duplicate z values.

```bash
make test-host-graphics
make test
git diff --check
```

- [ ] **Step 4: Commit**

```bash
git add Makefile kernel/gui/geometry.hpp kernel/gui/window_manager.hpp kernel/gui/window_manager.cpp tests/host/window_manager_test.cpp
git commit -m "Add Linux95 window manager model"
```

---

### Task 9: Graphical terminal text/history model

**Files:**
- Create: `kernel/gui/terminal_model.hpp`
- Create: `kernel/gui/terminal_model.cpp`
- Create: `tests/host/terminal_model_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces a fixed model used as a `terminal::Output` sink.

- [ ] **Step 1: Write history/clear tests**

Use fixed dimensions:
- 120 columns
- 64 stored lines

Test:
- characters append at cursor
- `\n` advances line
- `\b` removes one visible character when possible
- writing more than 64 lines retains the newest 64 lines
- a line longer than 120 columns wraps
- `clear()` blanks every line and resets row/column to zero
- no operation writes outside the model arrays

- [ ] **Step 2: Define model**

```cpp
class TerminalModel {
public:
    static constexpr size_t kColumns = 120;
    static constexpr size_t kHistoryLines = 64;

    TerminalModel();
    void put_char(char c);
    void clear();
    const char* line(size_t visible_index) const;
    size_t line_count() const;
    size_t cursor_column() const;
};
```

Provide `terminal::Output make_output(TerminalModel&)` whose callbacks call this model.

- [ ] **Step 3: Implement and GREEN**

```bash
make test-host-graphics
make test
git diff --check
```

- [ ] **Step 4: Commit**

```bash
git add Makefile kernel/gui/terminal_model.hpp kernel/gui/terminal_model.cpp tests/host/terminal_model_test.cpp
git commit -m "Add graphical terminal model"
```

---

### Task 10: Built-in app interface and real Terminal app

**Files:**
- Create: `kernel/gui/app.hpp`
- Create: `kernel/gui/terminal_app.hpp`
- Create: `kernel/gui/terminal_app.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces an app callback boundary suitable for replacing kernel-owned apps with process-backed apps later.

- [ ] **Step 1: Define app boundary**

Use function pointers:

```cpp
struct AppContext;

struct AppCallbacks {
    void (*draw)(void* context, graphics::Framebuffer&, graphics::Rect content);
    void (*on_key)(void* context, char c);
    void (*on_close)(void* context);
};

struct AppInstance {
    void* context;
    AppCallbacks callbacks;
};
```

No app may call the window manager directly.

- [ ] **Step 2: Implement Terminal app**

`TerminalApp` owns:
- `TerminalModel`
- its `terminal::Output`
- `terminal::ShellSession`

Construction calls `session.begin()`. `on_key` calls `session.on_char(c)`. `draw` paints a dark content rectangle and renders the newest lines using the bitmap renderer, clipping to the window content rect.

Use the existing `shell::execute_command` callback; do not copy the command `if` chain.

- [ ] **Step 3: Build/regression check**

```bash
make all
make test
git diff --check
```

- [ ] **Step 4: Commit**

```bash
git add Makefile kernel/gui/app.hpp kernel/gui/terminal_app.hpp kernel/gui/terminal_app.cpp
git commit -m "Add interactive graphical terminal app"
```

---

### Task 11: System Info app

**Files:**
- Create: `kernel/gui/system_info_app.hpp`
- Create: `kernel/gui/system_info_app.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes existing read-only APIs from PIT, memory, physical allocator, heap, storage, filesystem, and VFS state.
- Produces `AppInstance make_system_info_app()`.

- [ ] **Step 1: Implement formatting without heap allocation**

Use local fixed buffers and a small decimal helper:

```cpp
static void format_u64(uint64_t value, char* out, size_t capacity);
```

Require NUL termination whenever `capacity > 0`.

- [ ] **Step 2: Draw exact initial fields**

Render:
- `Linux95 Kernel v1.0`
- `Architecture: x86_64`
- uptime seconds
- total RAM MiB
- usable RAM MiB
- physical total/used/free pages
- heap used/capacity KiB
- boot/test disk present state
- FAT32 mounted state
- filesystem mode `read-only`

The app reads subsystem getters at draw/refresh time; it must not probe ATA or remount the filesystem.

- [ ] **Step 3: Build and commit**

```bash
make test
git diff --check
git add Makefile kernel/gui/system_info_app.hpp kernel/gui/system_info_app.cpp
git commit -m "Add graphical system info app"
```

---

### Task 12: Desktop, top panel, dirty regions, cursor, and event loop

**Files:**
- Create: `kernel/gui/desktop.hpp`
- Create: `kernel/gui/desktop.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes framebuffer renderer, mouse event queue, keyboard char queue, PIT, WindowManager, Terminal app, System Info app.
- Produces `[[noreturn]] desktop::run(graphics::Framebuffer&, bool mouse_online)`.

- [ ] **Step 1: Define fixed desktop geometry**

Exact constants:

```cpp
constexpr int32_t kScreenWidth = 1280;
constexpr int32_t kScreenHeight = 720;
constexpr int32_t kPanelHeight = 28;
constexpr int32_t kCursorWidth = 12;
constexpr int32_t kCursorHeight = 18;
constexpr size_t kMaxDirtyRects = 32;
```

Open Terminal at `{40, 70, 760, 430}` and System Info at `{830, 90, 410, 470}`. Terminal starts focused.

- [ ] **Step 2: Implement dirty-region queue**

`invalidate(Rect)` clips to screen. If more than 32 dirty regions accumulate, collapse to exactly one `{0,0,1280,720}` region. This full-screen redraw is a bounded overflow fallback, not the normal mouse path.

`redraw_region` paints, in order:
1. desktop background or panel base
2. each open non-minimized window intersecting the region in z-order
3. panel controls/task buttons intersecting the region
4. cursor if mouse is online and intersects the region

- [ ] **Step 3: Implement cursor movement**

Initial pointer is screen center. For each mouse event:
- invalidate old 12x18 cursor rect
- `x += dx`
- `y -= dy`
- clamp X to `0..1268`
- clamp Y to `28..702`
- dispatch button transition/hit test
- invalidate new cursor rect

No mouse IRQ routine draws pixels.

- [ ] **Step 4: Implement panel controls**

Use fixed hit rectangles:
- Applications: x `0..119`
- Terminal quick launch: `120..219`
- System Info quick launch: `220..339`
- task buttons begin at x `350`
- uptime/status is right-aligned in final 180 pixels

Applications menu opens below the panel with Terminal and System Info rows. Clicking outside closes the menu.

Quick-launch behavior:
- if app window exists minimized: restore + focus
- if app exists visible: focus
- if closed: create it again with default geometry

Task-button click performs restore/focus.

- [ ] **Step 5: Dispatch keyboard to focused app**

When Terminal is focused, pass each available keyboard char to Terminal app. System Info ignores text keys.

- [ ] **Step 6: Implement title bar/buttons/resize hit regions**

Each window:
- title bar height 24
- close button 20x18 at right edge
- minimize button 20x18 immediately left of close
- resize grab region 8 pixels along right/bottom edges
- content starts below title bar

Mouse-down priority: panel controls, title buttons, resize border, title drag, content focus.

- [ ] **Step 7: Main loop**

The loop drains all pending mouse events, drains all keyboard chars, updates the panel uptime only when `pit::uptime_seconds()` changes, redraws all dirty rectangles, then calls `io::halt()` when no work remains.

Write debug marker once after the first complete desktop paint:

```cpp
debug::write("[PASS] desktop_online\n");
```

Terminal/System Info construction write:

```text
[PASS] terminal_app_ready
[PASS] system_info_app_ready
```

- [ ] **Step 8: Build and commit**

```bash
make test
git diff --check
git add Makefile kernel/gui/desktop.hpp kernel/gui/desktop.cpp
git commit -m "Add Linux95 graphical desktop"
```

---

### Task 13: Kernel boot integration, VGA fallback, and QEMU graphics smoke test

**Files:**
- Modify: `kernel/kernel.cpp`
- Modify: `tests/qemu_smoke.py`
- Modify: `tests/graphics_source_checks.py`
- Modify: `Makefile`
- Modify: `README.md`

**Interfaces:**
- Final startup policy:
  - framebuffer unavailable -> VGA shell
  - invalid/mapping failure after VBE handoff -> safe halt with debug marker
  - framebuffer ready -> desktop
  - mouse unavailable -> desktop still starts

- [ ] **Step 1: Change startup control flow**

After storage/VFS initialization and interrupt setup:

```cpp
const graphics::FramebufferInitResult fb_result =
    graphics::initialize_framebuffer(*boot_info);

if (fb_result == graphics::FramebufferInitResult::Unavailable) {
    debug::write("[INFO] graphics_fallback_vga\n");
    shell::run_vga();
}

if (fb_result == graphics::FramebufferInitResult::InvalidMetadata) {
    debug::write("[PANIC] framebuffer_invalid\n");
    panic::halt("Invalid framebuffer handoff");
}

if (fb_result == graphics::FramebufferInitResult::MappingFailed) {
    debug::write("[PANIC] framebuffer_mapping\n");
    panic::halt("Could not map framebuffer");
}

debug::write("[PASS] renderer_online\n");
desktop::run(*graphics::framebuffer(), mouse_online);
```

Do not print the normal startup banner through VGA after `fb_result == Ready`.

- [ ] **Step 2: Update QEMU smoke command**

Add:

```python
"-vga", "std",
```

to the QEMU command. Change the success condition from only `shell_ready` to `desktop_online`.

Required graphics markers:

```python
required_graphics = [
    "[PASS] framebuffer_mapped",
    "[PASS] renderer_online",
    "[PASS] terminal_app_ready",
    "[PASS] system_info_app_ready",
    "[PASS] desktop_online",
]
```

Mouse status accepts either `[PASS] mouse_initialized` or `[INFO] mouse_unavailable`; QEMU `-vga std` should normally exercise the successful graphics path.

Preserve all existing memory/storage/FAT32/VFS required markers.

- [ ] **Step 3: Add a separate VGA-fallback smoke path**

Add a second short QEMU invocation using a mode where the exact VBE target is deliberately unavailable only if QEMU can express that deterministically. If not, preserve fallback as source-checked behavior and do not invent an unreliable runtime switch.

The source check must require:
- `FramebufferInitResult::Unavailable`
- call to `shell::run_vga()`
- `[INFO] graphics_fallback_vga`

This makes fallback an explicit verified branch even when the emulator cannot force the condition cleanly.

- [ ] **Step 4: Update build label and README**

Change the image banner to:

```text
Linux95 Kernel v1.0 Graphics Desktop Foundation image built:
```

README must document:
- VBE 1280x720x32
- software framebuffer renderer
- PS/2 mouse
- top-panel desktop/window manager
- interactive GUI Terminal
- System Info
- VGA fallback
- QEMU-first scope and future UEFI/GOP direction

- [ ] **Step 5: Full fresh verification**

Run exactly:

```bash
make clean
make test
make test-qemu
git diff --check
git status --short
```

Expected:
- `make test`: PASS
- `make test-qemu`: PASS and reaches `[PASS] desktop_online`
- `git diff --check`: no output
- `git status --short`: only intentional tracked changes before the final commit; pre-existing untracked `release/` remains untouched

- [ ] **Step 6: Commit final integration**

```bash
git add Makefile README.md kernel/kernel.cpp tests/qemu_smoke.py tests/graphics_source_checks.py
git commit -m "Boot Linux95 into graphical desktop"
```

- [ ] **Step 7: Whole-branch verification before any merge/push**

```bash
git log --oneline --decorate -15
git diff --check HEAD~13..HEAD
make clean
make test
make test-qemu
```

Do not push or merge until the user explicitly approves the reviewed branch.
'''

path = Path("/mnt/data/2026-09-25-linux95-graphics-desktop-implementation-plan.md")
path.write_text(plan)
print(f"Wrote {path} ({len(plan.splitlines())} lines, {len(plan)} bytes)")
from pathlib import Path
p = Path("/mnt/data/2026-09-25-linux95-graphics-desktop-implementation-plan.md")
p.write_text(r'''# Linux95 Graphics Desktop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot Linux95 into a 1280x720x32 VBE software-rendered desktop with PS/2 mouse input, click-to-focus windows, an interactive graphical terminal using the existing shell commands, a System Info app, and VGA text fallback when graphics is unavailable.

**Architecture:** Stage 2 discovers and sets an exact VBE 1280x720x32 linear-framebuffer mode and passes a firmware-neutral framebuffer description through `BootInfo`. The higher-half kernel maps that MMIO framebuffer into a dedicated virtual range, renders directly with dirty-region repainting, drives a PS/2 mouse through IRQ12, and runs a desktop event loop that owns the top panel, window manager, Terminal, and System Info. Existing shell logic is refactored behind an output/session interface so the same commands run in both VGA fallback and the GUI terminal.

**Tech Stack:** x86_64, legacy BIOS, VBE, NASM, freestanding C++17, PS/2 controller, 8259 PIC, PIT, QEMU `-vga std`, host-side C++17 tests, Python source/image/QEMU smoke checks, GNU Make.

**Spec:** `docs/superpowers/specs/2026-09-25-linux95-graphics-desktop-design.md`

## Global Constraints

- Initial graphics target is QEMU using legacy BIOS VBE.
- Required graphical mode is exactly 1280x720 at 32 bits per pixel.
- Require a direct-colour VBE mode with linear framebuffer support.
- Do not hard-code a VBE mode number; enumerate the mode list.
- Graphics rendering is CPU-only and writes directly to the visible framebuffer.
- Do not add a full-screen back buffer in this milestone.
- Use dirty-region redraws rather than unconditional full-screen repainting on mouse movement or typing.
- The top panel is at the top of the screen.
- Window focus is click-to-focus.
- Windows support z-order, dragging, resizing, minimize, restore, and close.
- Terminal and System Info are kernel-resident built-in apps for this milestone.
- The graphical Terminal must execute the existing real shell commands; do not duplicate command implementations.
- Keep VGA text mode as the fallback when VBE cannot be established before graphics-mode activation.
- Once VBE mode is active and Linux95 is in long mode, do not attempt BIOS calls to restore VGA.
- PS/2 mouse v1 uses three-byte packets and supports X/Y movement plus left, right, and middle buttons.
- Scroll-wheel, USB input, GPU acceleration, compositing, animations, transparency, user mode, processes, GUI file manager, networking, audio, UEFI, and GOP are outside this milestone.
- Mouse initialization failure alone must not prevent the graphical desktop from booting.
- Existing `make test` and `make test-qemu` regressions must remain green.
- Do not push, merge, or publish without explicit user approval.

## File Structure

**Boot/ABI:** modify `boot/stage2.asm`, `kernel/boot_info.hpp`, `Makefile`, `tests/image_checks.py`; create `tests/graphics_source_checks.py`.

**Graphics:** create `kernel/graphics/framebuffer_helpers.hpp`, `framebuffer.hpp/.cpp`, `renderer.hpp/.cpp`, `font8x8.hpp`, and host tests for framebuffer helpers and renderer.

**PS/2:** create `kernel/arch/ps2.hpp/.cpp`, `mouse_helpers.hpp`, `mouse.hpp/.cpp`; modify `kernel/arch/interrupts.cpp` and `kernel/kernel.cpp`; add a host mouse decoder test.

**Terminal:** create `kernel/terminal/output.hpp`, `vga_output.hpp/.cpp`, `shell_session.hpp/.cpp`; modify `shell.hpp/.cpp`; add a host shell-session test.

**GUI:** create `kernel/gui/geometry.hpp`, `window_manager.hpp/.cpp`, `terminal_model.hpp/.cpp`, `app.hpp`, `terminal_app.hpp/.cpp`, `system_info_app.hpp/.cpp`, `desktop.hpp/.cpp`; add host window-manager and terminal-model tests.

## Review Focus

1. Malformed framebuffer arithmetic must be rejected before multiplication/addition/page rounding can wrap; Task 1 tests this.
2. Missing exact VBE mode or mode-set failure must keep `framebuffer.available == 0` and continue to VGA; Task 2 source-checks the branch and Task 13 preserves it.
3. PS/2 desynchronization/overflow packets must be consumed safely without pointer motion; Task 5 tests this.
4. Window drag/resize must stay inside the 1280x692 work area under the 28-pixel panel and honor minimum dimensions; Task 8 tests every boundary.
5. Long terminal output must scroll deterministically and `clear` must reset history/cursor without out-of-bounds writes; Task 9 tests this.

---

### Task 1: Framebuffer ABI and pure validation helpers

**Files:**
- Modify: `kernel/boot_info.hpp`
- Create: `kernel/graphics/framebuffer_helpers.hpp`
- Create: `tests/host/framebuffer_helpers_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces `linux95::FramebufferInfo`.
- Produces `graphics::validate_framebuffer(const FramebufferInfo&, uint64_t&)`.
- Produces `graphics::mapping_layout(const FramebufferInfo&)`.

- [ ] **Step 1: Write the failing host test**

```cpp
#include "boot_info.hpp"
#include "graphics/framebuffer_helpers.hpp"
#include <assert.h>
#include <stdint.h>

using namespace linux95;
using namespace linux95::graphics;

static FramebufferInfo valid_info()
{
    return FramebufferInfo{
        0xFD000123ULL, 1280, 720, 1280 * 4, 32,
        8, 16, 8, 8, 8, 0, 1,
    };
}

int main()
{
    uint64_t bytes = 0;
    FramebufferInfo info = valid_info();

    assert(validate_framebuffer(info, bytes) == ValidationStatus::Ok);
    assert(bytes == 1280ULL * 4ULL * 720ULL);

    info.available = 0;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::Unavailable);

    info = valid_info();
    info.width = 1024;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidMode);

    info = valid_info();
    info.pitch = 5119;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidPitch);

    info = valid_info();
    info.physical_address = 0;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidAddress);

    info = valid_info();
    info.red_mask_shift = 8;
    info.green_mask_shift = 8;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::InvalidColorMasks);

    info = valid_info();
    info.physical_address = UINT64_MAX - 100;
    assert(validate_framebuffer(info, bytes) == ValidationStatus::Overflow);

    MappingLayout layout = mapping_layout(valid_info());
    assert(layout.physical_base == 0xFD000000ULL);
    assert(layout.offset == 0x123ULL);
    assert(layout.page_count > 0);
    return 0;
}
```

- [ ] **Step 2: Add the host target and prove RED**

```make
$(BUILD)/host-framebuffer-helpers-test: tests/host/framebuffer_helpers_test.cpp kernel/graphics/framebuffer_helpers.hpp kernel/boot_info.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-graphics: $(BUILD)/host-framebuffer-helpers-test
>$(BUILD)/host-framebuffer-helpers-test
```

Add `test-host-graphics` to `.PHONY` and `test`.

Run:

```bash
make clean
make test-host-graphics
```

Expected: compile failure because the new ABI/helpers do not exist.

- [ ] **Step 3: Implement the packed ABI**

Inside `namespace linux95`:

```cpp
struct __attribute__((packed)) FramebufferInfo {
    uint64_t physical_address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bits_per_pixel;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t available;
};

struct __attribute__((packed)) BootInfo {
    uint32_t magic;
    uint32_t e820_count;
    uint64_t e820_address;
    uint8_t boot_drive;
    FramebufferInfo framebuffer;
};

static_assert(sizeof(FramebufferInfo) == 28, "Linux95 framebuffer ABI changed");
static_assert(sizeof(BootInfo) == 45, "Linux95 BootInfo ABI changed");
```

Keep `valid_boot_info` focused on magic/E820; graphics has separate validation.

- [ ] **Step 4: Implement pure helpers**

```cpp
enum class ValidationStatus : uint8_t {
    Ok, Unavailable, InvalidMode, InvalidAddress,
    InvalidPitch, InvalidColorMasks, Overflow,
};

struct MappingLayout {
    uint64_t physical_base;
    uint64_t offset;
    uint64_t page_count;
};
```

Exact validation: available=1, width=1280, height=720, bpp=32, physical address nonzero, pitch>=5120, RGB mask sizes each 8, shifts<32, masks non-overlapping, no `pitch*height` overflow, no `physical_address+byte_size` overflow. `mapping_layout` aligns down to 4096 and rounds coverage up to pages.

- [ ] **Step 5: GREEN and commit**

```bash
make test-host-graphics
make test
git diff --check
git add Makefile kernel/boot_info.hpp kernel/graphics/framebuffer_helpers.hpp tests/host/framebuffer_helpers_test.cpp
git commit -m "Add framebuffer boot ABI validation"
```

---

### Task 2: Expand kernel image capacity and add BIOS VBE handoff

**Files:**
- Modify: `Makefile`
- Modify: `boot/stage2.asm`
- Modify: `tests/image_checks.py`
- Create: `tests/graphics_source_checks.py`

**Interfaces:** Stage 2 fills framebuffer fields at BootInfo offset 17 and sets available at offset 44 only after successful mode set.

- [ ] **Step 1: Write source checks first**

`tests/graphics_source_checks.py` must require `0x4F00`, `0x4F01`, `0x4F02`, `0x4000`, `1280`, `720`, and `32` in Stage 2; require `FramebufferInfo framebuffer;` in the ABI; and reject `int 0x10` from kernel C++/64-bit graphics code.

Run:

```bash
python3 tests/graphics_source_checks.py
```

Expected: FAIL because VBE is absent.

- [ ] **Step 2: Grow kernel budget**

Set:

```make
KERNEL_SECTORS := 256
IMAGE_SECTORS := 273
```

and:

```asm
KERNEL_SECTORS equ 256
```

Load four 64-sector chunks into physical `0x20000`, `0x28000`, `0x30000`, `0x38000`, then copy `256*512` bytes to `0x00100000`. Add `kernel_sectors % 64 == 0` to `tests/image_checks.py`.

- [ ] **Step 3: Add VBE buffers/default**

```asm
VBE_CTRL_ADDR      equ 0x6000
VBE_MODE_ADDR      equ 0x6200
BOOTINFO_FB_OFFSET equ 17
FB_AVAILABLE       equ BOOTINFO_FB_OFFSET + 27
```

Initialize:

```asm
mov byte [BOOTINFO_ADDR + FB_AVAILABLE], 0
```

- [ ] **Step 4: Enumerate and set exact VBE mode**

After kernel disk load, before leaving BIOS mode:
1. `AX=4F00`, `ES:DI=0000:6000`, signature `VBE2`.
2. Follow controller mode-list far pointer at offset `0x0E`.
3. For each mode ID until `0xFFFF`, call `AX=4F01`, `CX=mode`, `ES:DI=0000:6200`.
4. Require mode attributes bit0+bit7, X=1280, Y=720, bpp=32, memory model=6.
5. Set with `AX=4F02`, `BX=mode|0x4000`.
6. Only if AX returns `0x004F`, copy VBE mode-info fields into BootInfo and set available=1.

Use offsets: attributes `0x00`, pitch `0x10`, X `0x12`, Y `0x14`, bpp `0x19`, memory model `0x1B`, RGB sizes/shifts `0x1F..0x24`, physical base `0x28`.

Every VBE failure returns to normal boot with available=0; no fatal E3-E6 branch.

- [ ] **Step 5: Verify and commit**

```bash
make clean
make all
python3 tests/graphics_source_checks.py
python3 tests/image_checks.py
make test
git diff --check
git add Makefile boot/stage2.asm tests/image_checks.py tests/graphics_source_checks.py
git commit -m "Add VBE framebuffer boot handoff"
```

---

### Task 3: Map the framebuffer in the higher-half kernel

**Files:**
- Create: `kernel/graphics/framebuffer.hpp`
- Create: `kernel/graphics/framebuffer.cpp`
- Modify: `Makefile`
- Modify: `kernel/kernel.cpp`

**Interfaces:**

```cpp
constexpr uint64_t kFramebufferVirtualBase = 0xFFFF900000000000ULL;

enum class FramebufferInitResult : uint8_t {
    Ready, Unavailable, InvalidMetadata, MappingFailed,
};

struct PixelFormat {
    uint8_t red_size, red_shift;
    uint8_t green_size, green_shift;
    uint8_t blue_size, blue_shift;
};

struct Framebuffer {
    volatile uint8_t* data;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    PixelFormat format;
};

FramebufferInitResult initialize_framebuffer(const BootInfo&);
const Framebuffer* framebuffer();
```

- [ ] **Step 1: Add declarations/object and prove RED**

Add `framebuffer.o` to `KERNEL_OBJS`, include the API from `kernel.cpp`, call it after memory/VM initialization, then:

```bash
make all
```

Expected: linker failure until implementation exists.

- [ ] **Step 2: Implement mapping**

Use Task 1 validation/layout. Map each page at `kFramebufferVirtualBase + i*4096` to `physical_base + i*4096` with `kPageWritable | kPageNoExecute`. If one mapping fails, unmap pages already mapped by this function and return `MappingFailed`. Live data pointer is `kFramebufferVirtualBase + layout.offset`.

- [ ] **Step 3: Add diagnostics**

Emit exactly one:
- `[PASS] framebuffer_mapped`
- `[INFO] framebuffer_unavailable`
- `[PANIC] framebuffer_invalid`
- `[PANIC] framebuffer_mapping`

Unavailable continues current VGA flow; invalid/mapping failure halts safely.

- [ ] **Step 4: Verify and commit**

```bash
make test
make test-qemu
git diff --check
git add Makefile kernel/graphics/framebuffer.hpp kernel/graphics/framebuffer.cpp kernel/kernel.cpp
git commit -m "Map VBE framebuffer in kernel"
```

---

### Task 4: Software renderer and bitmap text

**Files:**
- Create: `kernel/graphics/renderer.hpp`
- Create: `kernel/graphics/renderer.cpp`
- Create: `kernel/graphics/font8x8.hpp`
- Create: `tests/host/renderer_test.cpp`
- Modify: `Makefile`

**Interfaces:**

```cpp
struct Color { uint8_t r, g, b; };
struct Rect { int32_t x, y, width, height; };

uint32_t pack_color(const PixelFormat&, Color);
Rect clip_rect(Rect, int32_t width, int32_t height);
void put_pixel(Framebuffer&, int32_t x, int32_t y, Color);
void fill_rect(Framebuffer&, Rect, Color);
void draw_rect(Framebuffer&, Rect, Color);
void draw_line(Framebuffer&, int32_t x0, int32_t y0, int32_t x1, int32_t y1, Color);
void draw_char(Framebuffer&, int32_t x, int32_t y, char c, Color);
void draw_text(Framebuffer&, int32_t x, int32_t y, const char* text, Color);
```

- [ ] **Step 1: Write RED host tests**

Use an 8x8 in-memory framebuffer and assert RGB packing, clipping, offscreen no-write behavior, exact fill pixels, and `'A'` writes only within one 8x8 cell.

- [ ] **Step 2: Implement pixel/geometry primitives**

All bounds checks happen before address arithmetic. Packing uses supplied colour mask shifts, never hard-coded byte order.

- [ ] **Step 3: Add fixed printable-ASCII font**

`font8x8.hpp` contains 96 glyphs for `0x20..0x7F`, eight bytes per glyph, MSB-left:

```cpp
constexpr uint8_t kFontFirst = 0x20;
constexpr uint8_t kFontLast = 0x7F;
extern const uint8_t kFont8x8[96][8];
static_assert(kFontLast - kFontFirst + 1 == 96);
```

Unsupported characters render as `?`. The table is fixed data with no runtime allocation.

- [ ] **Step 4: Implement text, GREEN, commit**

`\n` resets X to the initial text X and adds 8 to Y. Every glyph pixel clips through `put_pixel`.

```bash
make test-host-graphics
make test
git diff --check
git add Makefile kernel/graphics/renderer.hpp kernel/graphics/renderer.cpp kernel/graphics/font8x8.hpp tests/host/renderer_test.cpp
git commit -m "Add software framebuffer renderer"
```
''')
print(f"Created first plan chunk: {len(p.read_text().splitlines())} lines")
from pathlib import Path
p = Path("/mnt/data/2026-09-25-linux95-graphics-desktop-implementation-plan.md")
with p.open("a") as f:
    f.write(r'''

---

### Task 5: PS/2 controller and three-byte mouse decoder

**Files:**
- Create: `kernel/arch/ps2.hpp`
- Create: `kernel/arch/ps2.cpp`
- Create: `kernel/arch/mouse_helpers.hpp`
- Create: `kernel/arch/mouse.hpp`
- Create: `kernel/arch/mouse.cpp`
- Create: `tests/host/mouse_helpers_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- `ps2::wait_input_clear(uint32_t limit)`
- `ps2::wait_output_full(uint32_t limit)`
- `mouse::PacketDecoder::feed(uint8_t, MouseEvent&)`
- `mouse::initialize()`
- `mouse::on_irq()`
- `mouse::has_event()`
- `mouse::read_event()`

- [ ] **Step 1: Write decoder tests**

```cpp
PacketDecoder decoder;
MouseEvent event{};

assert(!decoder.feed(0x00, event)); // bad first-byte sync

assert(!decoder.feed(0x08, event));
assert(!decoder.feed(0x05, event));
assert(decoder.feed(0xFD, event));
assert(event.dx == 5);
assert(event.dy == -3);
assert(!event.left);

decoder.reset();
assert(!decoder.feed(0x09, event));
assert(!decoder.feed(0x00, event));
assert(decoder.feed(0x00, event));
assert(event.left);

decoder.reset();
assert(!decoder.feed(0x48, event)); // X overflow
assert(!decoder.feed(0x7F, event));
assert(!decoder.feed(0x00, event)); // packet consumed but discarded
```

Also assert incomplete packets do not emit events and a later valid sync byte starts a fresh packet.

- [ ] **Step 2: Add host target and prove RED**

Add `host-mouse-helpers-test` to `test-host-graphics`.

```bash
make test-host-graphics
```

Expected: missing decoder types.

- [ ] **Step 3: Implement decoder**

Rules:
- first byte requires bit 3
- first-byte bit 6/7 means X/Y overflow; consume all three bytes and emit nothing
- sign-extend X with first-byte bit 4 and Y with bit 5
- event buttons come from bits 0/1/2
- report raw PS/2 Y; desktop applies `screen_y -= dy`

- [ ] **Step 4: Implement bounded PS/2 controller access**

Every wait has a finite iteration limit and returns false on timeout.

Mouse initialization sequence:
1. controller command `0xA8`
2. controller command `0x20`, read config
3. set config bit 1 (IRQ12)
4. clear config bit 5 (aux clock enabled)
5. controller command `0x60`, write config
6. controller command `0xD4`, mouse byte `0xF6`, require ACK `0xFA`
7. controller command `0xD4`, mouse byte `0xF4`, require ACK `0xFA`

`mouse::initialize()` returns false on timeout/wrong ACK.

- [ ] **Step 5: Add fixed event ring**

Use 64 `MouseEvent` slots. When full, drop the newest event and keep existing queued events/indices intact.

- [ ] **Step 6: GREEN and commit**

```bash
make test-host-graphics
make test
git diff --check
git add Makefile kernel/arch/ps2.hpp kernel/arch/ps2.cpp kernel/arch/mouse_helpers.hpp kernel/arch/mouse.hpp kernel/arch/mouse.cpp tests/host/mouse_helpers_test.cpp
git commit -m "Add PS2 mouse input"
```

---

### Task 6: Wire IRQ12 without making mouse mandatory

**Files:**
- Modify: `kernel/arch/interrupts.cpp`
- Modify: `kernel/kernel.cpp`
- Modify: `tests/graphics_source_checks.py`

**Interfaces:**
- Consumes `mouse::initialize()` and `mouse::on_irq()`.
- Produces vector 44 dispatch and conditional PIC unmask.

- [ ] **Step 1: Extend source check and prove RED**

```python
interrupts = (ROOT / "kernel/arch/interrupts.cpp").read_text()
kernel = (ROOT / "kernel/kernel.cpp").read_text()

if "frame->vector == 44" not in interrupts:
    raise SystemExit("FAIL: IRQ12 vector 44 is not dispatched")
if "pic::send_eoi(12)" not in interrupts:
    raise SystemExit("FAIL: IRQ12 EOI missing")
if "pic::unmask_irq(12)" not in kernel:
    raise SystemExit("FAIL: IRQ12 is never unmasked")
```

Run:

```bash
python3 tests/graphics_source_checks.py
```

Expected: FAIL.

- [ ] **Step 2: Add IRQ12 dispatch**

Before the generic IRQ branch:

```cpp
if (frame->vector == 44) {
    mouse::on_irq();
    pic::send_eoi(12);
    return;
}
```

- [ ] **Step 3: Initialize mouse as optional hardware**

```cpp
const bool mouse_online = mouse::initialize();
if (mouse_online) {
    pic::unmask_irq(12);
    debug::write("[PASS] mouse_initialized\n");
} else {
    debug::write("[INFO] mouse_unavailable\n");
}
```

Do not panic on `false`.

- [ ] **Step 4: Verify and commit**

```bash
python3 tests/graphics_source_checks.py
make test
make test-qemu
git diff --check
git add kernel/arch/interrupts.cpp kernel/kernel.cpp tests/graphics_source_checks.py
git commit -m "Wire optional PS2 mouse IRQ"
```

---

### Task 7: Refactor shell I/O into reusable output and session interfaces

**Files:**
- Create: `kernel/terminal/output.hpp`
- Create: `kernel/terminal/vga_output.hpp`
- Create: `kernel/terminal/vga_output.cpp`
- Create: `kernel/terminal/shell_session.hpp`
- Create: `kernel/terminal/shell_session.cpp`
- Modify: `kernel/terminal/shell.hpp`
- Modify: `kernel/terminal/shell.cpp`
- Create: `tests/host/shell_session_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- `terminal::Output`
- `terminal::write`, `write_uint`, `write_hex`
- `terminal::ShellSession`
- `shell::execute_command(terminal::Output&, char*)`
- `shell::run_vga()`

- [ ] **Step 1: Define callback output API**

```cpp
struct Output {
    void* context;
    void (*put_char)(void*, char);
    void (*clear)(void*);
    void (*set_color)(void*, uint8_t foreground, uint8_t background);
};
```

Provide inline `write`, decimal integer, and hexadecimal helpers using only `put_char`.

- [ ] **Step 2: Write session tests**

Fake output stores chars in a fixed array and fake executor copies submitted command into a fixed array.

Assert:
- `begin()` emits `linux95> `
- printable chars are echoed
- backspace edits only when length>0
- Enter NUL-terminates and submits
- maximum command buffer is 64 bytes including NUL
- after submit a new prompt appears

- [ ] **Step 3: Add host target and prove RED**

```bash
make test-host-graphics
```

Expected: missing session/output types.

- [ ] **Step 4: Implement exact session API**

```cpp
using ExecuteCallback =
    void (*)(void* context, Output& output, char* command);

class ShellSession {
public:
    ShellSession(Output& output, void* context, ExecuteCallback execute);
    void begin();
    void on_char(char c);

private:
    static constexpr size_t kCommandCapacity = 64;
    Output& output_;
    void* execute_context_;
    ExecuteCallback execute_;
    char command_[kCommandCapacity];
    size_t length_;
    void prompt();
};
```

- [ ] **Step 5: Refactor existing commands**

Every current command writes through the passed `Output&`. `clear` calls `output.clear(output.context)`. Preserve current command names/semantics.

`run_vga()` creates the VGA adapter and a `ShellSession`, then retains the current keyboard polling + `io::halt()` loop.

- [ ] **Step 6: Regression verify and commit**

```bash
make test-host-graphics
make test
make test-qemu
git diff --check
git add Makefile kernel/terminal/output.hpp kernel/terminal/vga_output.hpp kernel/terminal/vga_output.cpp kernel/terminal/shell_session.hpp kernel/terminal/shell_session.cpp kernel/terminal/shell.hpp kernel/terminal/shell.cpp tests/host/shell_session_test.cpp
git commit -m "Decouple shell from VGA output"
```

---

### Task 8: Pure window-manager state machine

**Files:**
- Create: `kernel/gui/geometry.hpp`
- Create: `kernel/gui/window_manager.hpp`
- Create: `kernel/gui/window_manager.cpp`
- Create: `tests/host/window_manager_test.cpp`
- Modify: `Makefile`

**Interfaces:** fixed-capacity manager with max 8 windows.

- [ ] **Step 1: Write RED tests**

Use screen `1280x720`, panel `28`, minimum window `160x100`.

Assert:
- first window becomes focused
- clicking lower window raises it
- title drag changes X/Y
- X clamps to `0..screen_width-window_width`
- Y clamps to `28..screen_height-window_height`
- resize never below `160x100`
- resize never crosses right/bottom screen edge
- minimized window is absent from hit testing
- restore uses previous geometry and focuses
- closed window is absent from hit testing/task enumeration

- [ ] **Step 2: Define exact model**

```cpp
using WindowId = uint32_t;

enum class WindowState : uint8_t {
    Open, Minimized, Closed,
};

struct Window {
    WindowId id;
    Rect bounds;
    Rect restore_bounds;
    WindowState state;
    uint8_t z;
    bool resizable;
    bool closable;
};

class WindowManager {
public:
    static constexpr size_t kMaxWindows = 8;
    static constexpr int32_t kPanelHeight = 28;
    static constexpr int32_t kMinWidth = 160;
    static constexpr int32_t kMinHeight = 100;

    bool add_window(WindowId, Rect, bool resizable, bool closable);
    WindowId focused() const;
    WindowId hit_test(Point) const;
    bool focus(WindowId);
    bool begin_drag(WindowId, Point);
    bool begin_resize(WindowId, Point);
    void pointer_move(Point);
    void end_pointer_action();
    bool minimize(WindowId);
    bool restore(WindowId);
    bool close(WindowId);
    const Window* find(WindowId) const;
};
```

- [ ] **Step 3: Implement and GREEN**

Keep windows in a fixed array. Raising compacts z values to unique `0..N-1`.

```bash
make test-host-graphics
make test
git diff --check
```

- [ ] **Step 4: Commit**

```bash
git add Makefile kernel/gui/geometry.hpp kernel/gui/window_manager.hpp kernel/gui/window_manager.cpp tests/host/window_manager_test.cpp
git commit -m "Add Linux95 window manager model"
```

---

### Task 9: Graphical terminal history model

**Files:**
- Create: `kernel/gui/terminal_model.hpp`
- Create: `kernel/gui/terminal_model.cpp`
- Create: `tests/host/terminal_model_test.cpp`
- Modify: `Makefile`

**Interfaces:** fixed 120-column, 64-line text/history model exposed as a `terminal::Output` sink.

- [ ] **Step 1: Write RED tests**

Assert:
- chars append at cursor
- newline advances row
- backspace removes one visible char when possible
- >64 lines retains newest 64
- >120 chars wraps
- `clear()` blanks all rows and resets cursor
- canary bytes around a test instance remain unchanged during wrap/scroll stress

- [ ] **Step 2: Define model**

```cpp
class TerminalModel {
public:
    static constexpr size_t kColumns = 120;
    static constexpr size_t kHistoryLines = 64;

    TerminalModel();
    void put_char(char c);
    void clear();
    const char* line(size_t visible_index) const;
    size_t line_count() const;
    size_t cursor_column() const;
};

terminal::Output make_output(TerminalModel&);
```

- [ ] **Step 3: Implement and GREEN**

```bash
make test-host-graphics
make test
git diff --check
```

- [ ] **Step 4: Commit**

```bash
git add Makefile kernel/gui/terminal_model.hpp kernel/gui/terminal_model.cpp tests/host/terminal_model_test.cpp
git commit -m "Add graphical terminal model"
```
''')
print(f"Plan now has {len(p.read_text().splitlines())} lines")
from pathlib import Path
p = Path("/mnt/data/2026-09-25-linux95-graphics-desktop-implementation-plan.md")
with p.open("a") as f:
    f.write(r'''

---

### Task 10: Built-in app interface and real Terminal app

**Files:**
- Create: `kernel/gui/app.hpp`
- Create: `kernel/gui/terminal_app.hpp`
- Create: `kernel/gui/terminal_app.cpp`
- Modify: `Makefile`

**Interfaces:** callback-based app boundary that can later be backed by user-space processes.

- [ ] **Step 1: Define app boundary**

```cpp
struct AppCallbacks {
    void (*draw)(
        void* context,
        graphics::Framebuffer& framebuffer,
        graphics::Rect content);
    void (*on_key)(void* context, char c);
    void (*on_close)(void* context);
};

struct AppInstance {
    void* context;
    AppCallbacks callbacks;
};
```

Apps do not manipulate z-order or window state directly.

- [ ] **Step 2: Implement Terminal app**

`TerminalApp` owns:
- `TerminalModel`
- its `terminal::Output`
- one `terminal::ShellSession`

Construction calls `session.begin()`.

`on_key` calls `session.on_char(c)`.

`draw`:
1. fills the content area with the terminal background
2. calculates visible rows from `content.height / 8`
3. renders the newest model lines with `graphics::draw_text`
4. clips all drawing to the window content rectangle

Create the session with the existing `shell::execute_command` callback; never copy the shell command dispatch chain.

- [ ] **Step 3: Build/regression check**

```bash
make all
make test
git diff --check
```

- [ ] **Step 4: Commit**

```bash
git add Makefile kernel/gui/app.hpp kernel/gui/terminal_app.hpp kernel/gui/terminal_app.cpp
git commit -m "Add interactive graphical terminal app"
```

---

### Task 11: System Info app

**Files:**
- Create: `kernel/gui/system_info_app.hpp`
- Create: `kernel/gui/system_info_app.cpp`
- Modify: `Makefile`

**Interfaces:** consumes existing PIT, memory, allocator, heap, storage, and filesystem getters; produces an `AppInstance`.

- [ ] **Step 1: Add fixed-buffer number formatting**

Use:

```cpp
static void format_u64(uint64_t value, char* out, size_t capacity)
{
    if (capacity == 0) return;

    char reversed[21];
    size_t count = 0;

    do {
        reversed[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < sizeof(reversed));

    size_t out_count = 0;
    while (count > 0 && out_count + 1 < capacity) {
        out[out_count++] = reversed[--count];
    }
    out[out_count] = '\0';
}
```

- [ ] **Step 2: Render exact initial fields**

Draw:
- `Linux95 Kernel v1.0`
- `Architecture: x86_64`
- uptime seconds
- total RAM MiB
- usable RAM MiB
- physical total/used/free pages
- heap used/capacity KiB
- boot disk present yes/no
- test disk present yes/no
- FAT32 mounted yes/no
- `Filesystem: read-only`

Read getters at draw/refresh time; do not issue ATA IDENTIFY again and do not remount FAT32.

- [ ] **Step 3: Build and commit**

```bash
make test
git diff --check
git add Makefile kernel/gui/system_info_app.hpp kernel/gui/system_info_app.cpp
git commit -m "Add graphical system info app"
```

---

### Task 12: Desktop, top panel, dirty regions, cursor, and event loop

**Files:**
- Create: `kernel/gui/desktop.hpp`
- Create: `kernel/gui/desktop.cpp`
- Modify: `Makefile`

**Interfaces:** produces `[[noreturn]] desktop::run(graphics::Framebuffer&, bool mouse_online)`.

- [ ] **Step 1: Lock desktop geometry**

```cpp
constexpr int32_t kScreenWidth = 1280;
constexpr int32_t kScreenHeight = 720;
constexpr int32_t kPanelHeight = 28;
constexpr int32_t kCursorWidth = 12;
constexpr int32_t kCursorHeight = 18;
constexpr size_t kMaxDirtyRects = 32;
```

Default windows:
- Terminal `{40, 70, 760, 430}`
- System Info `{830, 90, 410, 470}`

Terminal starts focused.

- [ ] **Step 2: Implement dirty-region queue**

`invalidate(Rect)` clips to the screen. If a 33rd region would be added, replace the queue with one `{0,0,1280,720}` region.

`redraw_region` paints in this exact order:
1. desktop background/panel base
2. open non-minimized windows intersecting the region in ascending z
3. panel controls and task buttons
4. cursor when mouse is online

- [ ] **Step 3: Implement cursor updates**

Initial pointer: screen center.

For each mouse event:

```cpp
invalidate(cursor_rect());
mouse_x += event.dx;
mouse_y -= event.dy;
mouse_x = clamp(mouse_x, 0, 1280 - kCursorWidth);
mouse_y = clamp(mouse_y, kPanelHeight, 720 - kCursorHeight);
dispatch_mouse_buttons(event);
invalidate(cursor_rect());
```

No IRQ routine calls renderer functions.

- [ ] **Step 4: Implement top-panel hit regions**

Use:
- Applications x `0..119`
- Terminal quick launch `120..219`
- System Info quick launch `220..339`
- task buttons start at x `350`
- final 180 pixels reserved for uptime/status

Applications menu appears directly below the panel and contains exactly Terminal and System Info. Clicking outside closes it.

Quick-launch behavior:
- minimized existing app -> restore + focus
- visible existing app -> focus
- closed app -> create with default geometry + focus

Clicking a task button restores if minimized, then focuses.

- [ ] **Step 5: Implement window chrome interaction**

Exact chrome:
- title bar height 24
- close button 20x18 at right
- minimize button 20x18 immediately left
- resize grip is 8 pixels on right/bottom
- content begins below title bar

Mouse-down priority:
1. panel/menu
2. close/minimize
3. resize border
4. title-bar drag
5. content focus

- [ ] **Step 6: Route keyboard**

Drain existing keyboard chars. Send chars only to focused app. Terminal accepts chars; System Info ignores them.

- [ ] **Step 7: Main loop**

Track last uptime second. Only invalidate uptime/status when it changes.

Loop:
1. drain mouse queue
2. drain keyboard queue
3. process timer-derived uptime change
4. redraw all dirty regions
5. `io::halt()` if no events/dirty work remain

After first complete paint:

```cpp
debug::write("[PASS] desktop_online\n");
```

At app construction:

```cpp
debug::write("[PASS] terminal_app_ready\n");
debug::write("[PASS] system_info_app_ready\n");
```

- [ ] **Step 8: Build and commit**

```bash
make test
git diff --check
git add Makefile kernel/gui/desktop.hpp kernel/gui/desktop.cpp
git commit -m "Add Linux95 graphical desktop"
```

---

### Task 13: Final boot integration and graphics QEMU smoke test

**Files:**
- Modify: `kernel/kernel.cpp`
- Modify: `tests/qemu_smoke.py`
- Modify: `tests/graphics_source_checks.py`
- Modify: `Makefile`
- Modify: `README.md`

**Interfaces:** final startup policy.

- [ ] **Step 1: Switch startup flow**

After storage/VFS and interrupt/input setup:

```cpp
const graphics::FramebufferInitResult fb_result =
    graphics::initialize_framebuffer(*boot_info);

if (fb_result == graphics::FramebufferInitResult::Unavailable) {
    debug::write("[INFO] graphics_fallback_vga\n");
    shell::run_vga();
}

if (fb_result == graphics::FramebufferInitResult::InvalidMetadata) {
    debug::write("[PANIC] framebuffer_invalid\n");
    panic::halt("Invalid framebuffer handoff");
}

if (fb_result == graphics::FramebufferInitResult::MappingFailed) {
    debug::write("[PANIC] framebuffer_mapping\n");
    panic::halt("Could not map framebuffer");
}

debug::write("[PASS] renderer_online\n");
desktop::run(*graphics::framebuffer(), mouse_online);
```

Do not print the normal VGA startup banner after `Ready`.

- [ ] **Step 2: Update QEMU graphics path**

Add to `tests/qemu_smoke.py` command:

```python
"-vga", "std",
```

Change success wait to `[PASS] desktop_online`.

Preserve all current memory/storage/FAT32/VFS required markers, and add:

```python
required_graphics = [
    "[PASS] framebuffer_mapped",
    "[PASS] renderer_online",
    "[PASS] terminal_app_ready",
    "[PASS] system_info_app_ready",
    "[PASS] desktop_online",
]
```

Accept either `[PASS] mouse_initialized` or `[INFO] mouse_unavailable`; mouse absence is not a boot failure.

- [ ] **Step 3: Pin fallback branch in source checks**

Require all three strings in `kernel/kernel.cpp`:

```text
FramebufferInitResult::Unavailable
shell::run_vga()
[INFO] graphics_fallback_vga
```

Do not invent an unreliable emulator trick solely to force the exact VBE mode to disappear.

- [ ] **Step 4: Update milestone text**

Make build banner:

```text
Linux95 Kernel v1.0 Graphics Desktop Foundation image built:
```

README documents VBE 1280x720x32, software renderer, PS/2 mouse, top panel/window manager, interactive GUI Terminal, System Info, VGA fallback, QEMU-first status, and later UEFI/GOP direction.

- [ ] **Step 5: Fresh full verification**

```bash
make clean
make test
make test-qemu
git diff --check
git status --short
```

Expected:
- `make test` PASS
- `make test-qemu` PASS and reaches `[PASS] desktop_online`
- `git diff --check` has no output
- pre-existing untracked `release/` remains untouched

- [ ] **Step 6: Final integration commit**

```bash
git add Makefile README.md kernel/kernel.cpp tests/qemu_smoke.py tests/graphics_source_checks.py
git commit -m "Boot Linux95 into graphical desktop"
```

- [ ] **Step 7: Whole-branch verification before publication**

```bash
git log --oneline --decorate -15
git diff --check
make clean
make test
make test-qemu
```

Do not push or merge until the user explicitly approves the reviewed branch.

## Plan Self-Review

- **Spec coverage:** VBE handoff, generic framebuffer mapping, direct software rendering, exact mode, VGA fallback, optional mouse, IRQ12, click-to-focus windows, drag/resize/minimize/restore/close, top panel, Applications + quick launch, task buttons, real graphical shell, System Info, dirty redraw, QEMU-first scope, and future GOP-compatible framebuffer description are all assigned to tasks.
- **Placeholder scan:** no TBD/TODO/“implement later” steps remain.
- **Type consistency:** `FramebufferInfo`, `FramebufferInitResult`, `Framebuffer`, `PixelFormat`, `Output`, `ShellSession`, `WindowManager`, `TerminalModel`, `AppInstance`, and `desktop::run` have one canonical spelling/interface in this plan.
- **Review-focus coverage:** framebuffer overflow (Task 1), VBE fallback (Task 2/13), PS/2 desync/overflow (Task 5), window bounds (Task 8), terminal history overflow/clear (Task 9).
- **Scope:** all tasks form one dependency chain ending in one bootable desktop milestone; UEFI/GOP and user processes remain separate future milestones.
''')
print(f"Plan complete: {len(p.read_text().splitlines())} lines, {p.stat().st_size} bytes")
