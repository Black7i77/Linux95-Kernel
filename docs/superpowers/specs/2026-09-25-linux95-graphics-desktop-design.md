# Linux95 Graphics Desktop Design

Date: 2026-09-25
Target branch: v1.0-dev

## Goal

Add the first graphical desktop subsystem to Linux95 Kernel while preserving
the existing text-mode environment as a recovery path.

The first graphical milestone will provide:

- 1280x720 32-bpp graphics.
- Legacy BIOS VBE support, initially tested in QEMU.
- A hardware-independent framebuffer interface.
- Pure software rendering directly to the framebuffer.
- PS/2 mouse support.
- Existing PS/2 keyboard support.
- A Linux95 graphical desktop.
- A top panel.
- Click-to-focus windows.
- Window dragging and resizing.
- Minimize, restore, and close controls.
- Applications menu.
- Terminal and System Info quick-launch buttons.
- Top-panel task buttons for minimized windows.
- A fully interactive graphical terminal.
- A graphical System Info application.
- VGA text fallback when VBE graphics cannot be established.

The first implementation targets QEMU. The architecture must allow later
legacy-PC hardening and a future UEFI/GOP boot path without replacing the
desktop or renderer.

## Graphics Boot Architecture

Linux95 Stage 2 already executes while BIOS services are available.

Stage 2 will therefore own VBE mode discovery and selection.

The bootloader will:

1. Query VBE controller information.
2. Enumerate VBE display modes.
3. Locate an exact 1280x720, 32-bpp direct-colour mode.
4. Require linear framebuffer support.
5. Record framebuffer address, pitch, dimensions, colour masks, and pixel
   format information.
6. Set the graphics mode.
7. Pass framebuffer information to the kernel through BootInfo.

Linux95 must not depend on a hard-coded VBE mode number.

If VBE discovery or mode selection fails before graphics mode is activated,
Linux95 will remain in VGA text mode and start the existing text shell.

Once VBE graphics mode has successfully replaced VGA text mode, failures in
higher GUI layers will use a framebuffer text recovery screen rather than
attempting unsafe BIOS calls from x86_64 long mode.

## BootInfo Extension

BootInfo will gain framebuffer information.

The ABI will use fixed-width integer fields. It will not depend on C++ bool
layout.

Conceptually:

    FramebufferInfo
        physical_address
        width
        height
        pitch
        bits_per_pixel
        red_mask_size
        red_mask_shift
        green_mask_size
        green_mask_shift
        blue_mask_size
        blue_mask_shift
        available

The kernel must validate every framebuffer field before use.

The renderer will construct colours from the supplied colour masks instead of
assuming RGB or BGR byte order.

This framebuffer description is intentionally firmware-independent.

A future UEFI GOP loader will populate the same logical framebuffer
information.

## Framebuffer Mapping

The framebuffer is device/MMIO memory and must not be treated as ordinary
E820 usable RAM.

The physical allocator must not allocate framebuffer pages.

After Linux95 establishes its normal higher-half memory environment, the
graphics subsystem will map the framebuffer into a dedicated kernel virtual
address range with writable permissions.

The mapping must cover:

    pitch * height

rounded to complete 4 KiB pages.

The graphics subsystem must validate overflow and page alignment calculations
before creating mappings.

## Renderer

Linux95 will use CPU-based software rendering.

Version 1 deliberately has no GPU acceleration and no full-screen back buffer.

The renderer will provide small hardware-independent operations such as:

- put pixel
- fill rectangle
- draw rectangle/border
- draw line
- clipping
- bitmap character drawing
- text drawing
- simple UI primitives

A built-in bitmap font will be used.

The renderer operates through the framebuffer abstraction rather than directly
depending on VBE.

Because rendering is directly into the visible framebuffer, Linux95 will use
dirty-region redraws instead of continuously repainting the complete screen.

## Desktop

When graphics initialization succeeds, Linux95 boots directly into the
desktop.

The desktop style is "Hybrid Linux95":

- classic desktop structure
- dark and clean appearance
- retro influence
- original Linux95 identity
- top panel rather than a Windows-style bottom taskbar

The desktop background occupies the area below the top panel.

## Top Panel

The top panel contains:

- Applications menu
- Terminal quick-launch button
- System Info quick-launch button
- running/minimized window task buttons
- basic system status
- uptime/status area on the right

Clicking a minimized window's task button restores and focuses it.

## Window Manager

The window manager owns:

- window position
- window size
- z-order
- keyboard focus
- mouse hit testing
- dragging
- resizing
- minimize state
- close state
- task-button state
- dirty regions

Focus uses click-to-focus.

Clicking a visible window raises it to the front.

Dragging its title bar moves it.

Dragging supported window borders resizes it.

Minimize removes the window from the desktop work area and leaves a task
button in the top panel.

Restore returns the window to its previous geometry and focuses it.

Close removes the built-in window instance.

Version 1 windows are kernel-resident. The window/app boundary must remain
separate enough that future user-space processes can own windows without
replacing the complete window manager.

## Input Architecture

Keyboard and mouse IRQ handlers must remain small.

Interrupt handlers collect input and place events into queues.

They must not render UI directly.

The desktop event loop consumes those events.

The existing keyboard implementation will be adapted so keyboard input can be
routed to the currently focused application.

## PS/2 Mouse

Version 1 uses a standard PS/2 mouse.

The initial mouse driver supports:

- three-byte PS/2 packets
- X movement
- Y movement
- left button
- right button
- middle button

Scroll-wheel support is outside this milestone.

The pointer is clamped to the 1280x720 screen.

PS/2 controller handling must coordinate keyboard and mouse access to I/O ports
0x60 and 0x64.

The mouse IRQ uses IRQ12.

The PIC will unmask IRQ12 only after successful mouse initialization.

## Cursor

The software cursor is rendered through the graphics layer.

Because there is no full back buffer, moving the cursor must repair the region
previously covered by the cursor before drawing it at its new position.

Cursor movement must not trigger an unconditional full-screen redraw.

## Desktop Event Loop

The GUI replaces the current shell-owned infinite loop as the normal system
event loop.

Conceptually:

    keyboard queue ----\
    mouse queue --------> desktop event loop
    PIT/timer ----------/
                            |
                            +--> window manager
                            +--> focused application
                            +--> top panel
                            +--> dirty-region rendering

Interrupt handlers never draw windows directly.

## Shell Refactor

The existing Linux95 command implementation must not be duplicated.

Currently the shell is coupled directly to VGA output and owns an infinite
input loop.

The shell will be separated into reusable command/session logic and an output
interface.

The same shell engine will support:

1. VGA text terminal recovery mode.
2. Graphical terminal window.

Existing commands remain real commands, including:

- help
- clear
- version
- mem
- diskinfo
- fsinfo
- ls
- cat
- uptime
- reboot

The graphical terminal receives keyboard input only when its window has focus.

## Terminal Application

Terminal is a built-in kernel application for this milestone.

It provides:

- real keyboard input
- Linux95 prompt
- command history area
- scrolling text area as required for visible output
- existing shell commands
- repaint through the software renderer

It is not a visual mock-up.

## System Info Application

System Info is the second built-in application.

It presents kernel information using graphical text/widgets.

Its initial information includes:

- Linux95/kernel identity
- architecture
- uptime
- total RAM
- usable RAM
- physical page statistics
- heap statistics
- disk information
- FAT32 mount information
- VFS/filesystem state

The application reads existing kernel subsystem information rather than
duplicating hardware probing logic.

## Failure Handling

Before VBE mode activation:

- VBE unsupported -> remain in VGA text mode
- exact 1280x720x32 mode unavailable -> remain in VGA text mode
- linear framebuffer unavailable -> remain in VGA text mode
- VBE set-mode failure -> remain/recover to VGA text mode

After VBE activation:

- invalid framebuffer handoff -> show framebuffer recovery text if possible
  and emit debug-port diagnostics
- mapping failure -> emit a clear debug failure marker and halt safely
- PS/2 mouse unavailable -> desktop may still boot with keyboard input
- malformed mouse packets -> discard and resynchronize
- application/window error -> contain failure to the GUI component where
  possible

Loss of mouse support alone must not prevent the kernel from booting.

## Hardware Roadmap

Stage 1 of this graphics subsystem targets:

- QEMU
- legacy BIOS
- VBE linear framebuffer
- PS/2 keyboard
- PS/2 mouse

Later work may harden VBE support for physical legacy BIOS PCs.

A separate future boot milestone may add UEFI and GOP.

UEFI/GOP must feed the same generic framebuffer description so the renderer,
desktop, window manager, and applications remain firmware-independent.

UEFI support is not part of this first implementation.

## Testing

Existing tests remain mandatory:

    make test
    make test-qemu

New host-side tests should cover pure logic where possible:

- framebuffer validation
- framebuffer size/overflow calculations
- clipping
- rectangle geometry
- colour packing
- PS/2 mouse packet decoding
- mouse coordinate clamping
- window hit testing
- focus changes
- z-order changes
- drag geometry
- resize geometry
- minimize/restore state
- shell session input behavior

QEMU smoke tests will add debug-port checkpoints for major initialization
stages such as:

- framebuffer handoff valid
- framebuffer mapped
- renderer online
- mouse initialized or unavailable
- desktop online
- terminal application ready
- system-info application ready

The tests will not depend on image OCR.

## Scope Exclusions

This milestone does not add:

- GPU acceleration
- compositing
- full-screen double buffering
- animations
- transparency
- PS/2 scroll wheel
- USB mouse
- USB keyboard
- user mode
- processes
- application isolation
- writable FAT
- a GUI file manager
- networking
- audio
- UEFI boot
- GOP implementation

These may be separate future milestones.

## Success Criteria

The milestone is successful when:

1. Linux95 boots in QEMU at 1280x720x32 through VBE.
2. The graphical desktop becomes the normal interface.
3. The mouse pointer moves correctly.
4. Windows can be focused, dragged, resized, minimized, restored, and closed.
5. The Applications menu and quick-launch controls work.
6. Terminal and System Info can exist as real windows.
7. Terminal runs the existing Linux95 shell commands.
8. System Info displays live information from existing kernel subsystems.
9. VGA text mode remains the fallback when VBE cannot be established.
10. Existing memory, storage, FAT32, VFS, keyboard, timer, and shell behavior
    remain regression-tested.
11. make test passes.
12. make test-qemu passes.
