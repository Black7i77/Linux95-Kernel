# Linux95 Terminal Editor v1 Design

**Status:** Proposed architectural design for review
**Date:** 2026-09-27
**Scope:** Full-screen text editing inside the existing graphical Terminal application

## 1. Goal and user-visible behavior

Linux95 will provide a small retro full-screen text editor, inspired by Nano, opened with:

```text
edit NOTES.TXT
```

The editor takes over the content area of the existing Terminal application. It is a second logical mode of `TerminalApp`, not a new desktop window and not a Ring 3 program. Quitting returns to the same shell session.

The editor supports simple byte-oriented text entry, arrow navigation, Backspace, Enter, Ctrl+S save, and Ctrl+Q quit. Its text capacity is exactly 65,536 bytes. It uses the existing writable filesystem API and never creates a missing file merely by opening it.

## 2. Current architecture and background

The graphical desktop owns a `TerminalApp` in its runtime state. `TerminalApp` currently owns a `TerminalModel`, output callbacks, and a `ShellSession`; its `poll()` forwards to `ShellSession::poll()`, its draw callback renders terminal history, and its key callback forwards a character to `ShellSession::on_char(char)`.

The keyboard driver currently decodes PS/2 Set 1 printable US keyboard input, Shift, Enter, and Backspace into a fixed 128-entry character ring queue. The desktop drains that queue and routes characters to the focused application through `AppCallbacks::on_key(void*, char)`. There is no Ctrl or E0-prefixed arrow-key event model yet.

`ShellSession` owns a bounded 260-byte command buffer and handles ordinary character editing, Enter, Backspace, ping, and asynchronous DNS command state. Other commands go through its execute callback into the shared shell command dispatcher. This shared dispatcher is also used by the VGA shell, so the editor integration must not duplicate command parsing or assume every shell caller can enter a graphical editor.

The Terminal renderer uses 8-by-8 character cells. The desktop supplies the Terminal window content rectangle, excluding its title bar, to the app draw callback. Resizing changes this rectangle.

The filesystem already exposes `filesystem::vfs::kPathCapacity == 128`, `stat`, `open`, `read`, `close`, and `write_file`. `open` distinguishes directories from regular files, and reads can be performed incrementally. Writable filesystem operations target the Test/slave FAT32 volume; the storage layer's `write_sector` policy independently permits writes only to `storage::DiskId::Test`. The editor must use these public filesystem/VFS interfaces and must not introduce a raw FAT writer.

## 3. Scope

Version 1 includes:

- `edit <path>` from the existing graphical Terminal shell.
- Existing-file loading, new-file editing without creation on open, saving, and the specified quit behavior.
- A fixed 65,536-byte text buffer with bounded editing operations.
- A structured keyboard-event path for characters, Enter, Backspace, arrows, Ctrl, Shift, and press/release state.
- A retro editor view rendered in the existing Terminal content area.
- Host tests and a dedicated QEMU editor smoke/persistence test where the existing harness supports it cleanly.

## 4. Non-goals

Version 1 is not:

- A Unix tty subsystem or a general terminal escape-sequence parser.
- A full Nano clone or a separate userspace/Ring 3 editor process.
- Undo/redo, search, replace, selection, clipboard, copy/paste, syntax highlighting, mouse editing, Home/End, Page Up/Page Down, multiple files, or tabs.
- A long-filename editor, a binary/hex editor, or an editor with UTF-8 editing features beyond the existing simple byte/ASCII behavior.
- An autosaving editor.

## 5. Structured keyboard-event architecture

Replace or supersede the character-only event pipeline with a fixed-size structured event API in the keyboard layer. The event model will include semantics equivalent to:

```cpp
enum class KeyCode : uint8_t {
    Unknown,
    Character,
    Enter,
    Backspace,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,
};

struct KeyEvent {
    KeyCode key;
    char character;
    bool ctrl;
    bool shift;
    bool pressed;
};
```

Names may follow the repository's exact conventions, but the fields' meanings are fixed. Character events contain the printable character after Shift translation. Ctrl accelerator matching must work for Ctrl+S and Ctrl+Q and must not insert `s` or `q` into the editor. Modifier-only events may use `Unknown`; they update driver state and are not editor actions.

Use a bounded, statically allocated event queue. On overflow, drop the newest event; do not overwrite unread entries or corrupt indices. Modifier state is updated from every make/break code even when an event is dropped. No dynamic allocation is introduced.

Provide a pure/testable Set 1 decoding component or equivalent seam so host tests can exercise scancode decoding without reading hardware ports.

## 6. PS/2 decoding behavior

Continue US printable-key translation and current Shift punctuation/case behavior. Track left and right Shift independently so releasing one does not clear the other while it remains held. Track left and right Ctrl independently; `ctrl` in a `KeyEvent` is true while either is held.

Decode the Set 1 E0 prefix as state, not as a key. Recognize E0 make and break codes for the four arrow keys. Do not emit printable events for E0, modifier keys, or unsupported extended keys. Make codes produce `pressed=true`; break codes produce `pressed=false` for supported non-modifier keys if emitted. Modifier break codes always clear the corresponding state. Only key-press events trigger editor actions; key releases and modifier-only events do not.

Enter and Backspace remain structured keys and retain their current shell behavior when adapted by TerminalApp. Queue overflow drops an event safely.

## 7. Event routing through desktop and app callbacks

The keyboard API exposes event availability/read operations instead of requiring callers to drain characters. The desktop drains `KeyEvent` values and routes each to the focused app. `AppCallbacks` receives a `KeyEvent`, and `TerminalApp` handles it according to its current mode.

The desktop's normal event loop, network/DNS polling, scheduler call, mouse processing, redraw behavior, and app focus rules remain unchanged. Routing a key invalidates/redraws the Terminal as it does today.

## 8. Shell compatibility

In Shell mode, `TerminalApp` adapts pressed `Character`, `Enter`, and `Backspace` events to the existing `ShellSession::on_char(char)` contract (`\n` and `\b` for Enter and Backspace). Arrow and unsupported key events are ignored by the shell. Ctrl+S/Ctrl+Q are not translated into ordinary shell characters.

The existing shared shell command parser remains the sole parser. Add a narrow explicit command-result boundary for `edit`: the command dispatcher returns either the normal “continue in shell” result or an `OpenEditor` result containing a NUL-terminated path in a buffer of `filesystem::vfs::kPathCapacity` bytes. `ShellSession` propagates that result from command submission to `TerminalApp`; it does not own editor state. `TerminalApp` consumes the result, attempts to open the file, and changes mode only after successful editor initialization. Other shell commands keep their current output and behavior.

The `edit` command accepts exactly one path argument. `edit` with no path prints `usage: edit <path>` and remains in Shell mode. Extra unsupported arguments are rejected with a clear usage/error message. Command parsing failure never creates a file. If the shared dispatcher is called by the VGA shell, it must handle an editor request as unavailable in that non-graphical context rather than silently pretending an editor opened.

## 9. Editor mode lifecycle

`TerminalApp` has explicit `Shell` and `Editor` modes.

On a valid `OpenEditor(path)` request:

1. Validate and copy the path into a bounded editor-owned path buffer no larger than `vfs::kPathCapacity`.
2. Inspect the path through the public filesystem/VFS API.
3. If it is an existing regular file, load it only if its reported size is at most 65,536 bytes.
4. If it is a valid missing file path, initialize an empty, clean editor state and record that it is not yet present.
5. If initialization succeeds, switch the existing TerminalApp to Editor mode. Otherwise remain in Shell mode and print a clear error.

On quit, discard only the editor's in-memory state, return to Shell mode, and continue using the existing `ShellSession`; do not reconstruct/reinitialize the Terminal or lose shell history/session state.

Because the desktop runtime state currently embeds `TerminalApp` and is constructed on the desktop run stack, the 65,536-byte text array must not be added as a large inline member of `TerminalApp` or another stack-allocated runtime object. Store the fixed editor buffer in static/BSS-backed editor-model storage (with one editor instance, matching the one TerminalApp), while keeping cursor, path, viewport, dirty, and status state logically owned by the editor model. No heap allocation is required for editor text.

## 10. Editor model and invariants

Create a focused editor model/component separate from `TerminalModel` and shell command handling. It owns or logically manages:

- Exactly 65,536 bytes of text storage and a current length in `[0, 65536]`.
- A byte cursor in `[0, length]`.
- Logical line/column calculations, desired column for vertical movement, and viewport origin.
- Modified state, pending forced-quit warning state, current bounded path, and whether the path is logically saved/present.
- A bounded transient status/error message.

The text is byte-oriented simple text. V1 inserts only printable ASCII bytes from the keyboard and LF for Enter. The buffer is not NUL-terminated and capacity counts text bytes, not a terminator. Existing file bytes are loaded as-is, subject to the size limit; this does not make the editor a binary editor or add UTF-8-aware cursor semantics.

All insertion, deletion, cursor movement, line scanning, and rendering are bounds-checked. An operation that cannot fit or cannot move leaves buffer/cursor state valid and does not change content or Modified state.

## 11. Text editing semantics

- A printable character inserts one byte at the cursor, shifts later bytes right, and advances the cursor. At length 65,536, insertion is refused safely.
- Enter inserts exactly one LF byte (`'\n'`), never CRLF.
- Backspace removes the byte immediately before the cursor and moves the cursor left. Removing LF joins the adjacent logical lines. At position zero, it does nothing.
- Left and Right move one byte position, clamped to `[0, length]`.
- Logical lines are ranges separated by LF. A trailing LF creates a final empty logical line; an empty buffer has one empty line. A cursor immediately before LF belongs to the preceding line; a cursor immediately after LF belongs to the next line.
- Up/Down move to the adjacent logical line, retaining a preferred zero-based byte column from the last nonvertical cursor/edit action. The destination column is `min(preferred_column, destination_line_length)`. Repeated vertical movement retains the preferred column even when temporarily clamped by a short line. At the first/last line, movement does nothing. An edit or Left/Right action resets the preferred column to the resulting cursor column.
- Content changes set Modified. Navigation does not. A refused capacity insertion does not mark content modified.

Displayed line and column numbers are one-based.

## 12. Cursor and scrolling semantics

Text is not soft-wrapped: LF alone separates logical lines, and each displayed ASCII byte occupies one 8-pixel cell. Long lines use horizontal scrolling.

The renderer derives whole visible cell rows/columns from the clipped Terminal content rectangle. Reserve three rows for editor chrome: one title row, one help row, and one status row. The remaining rows are the text viewport; a rectangle too small for text remains safe and simply shows no text rows. Visible columns are `floor(content.width / 8)`; visible total rows are `floor(content.height / 8)`.

The viewport tracks the cursor and adjusts vertically and horizontally so its cell is visible whenever the corresponding dimension has at least one cell. Cursor-at-end positions are valid; scrolling must make the insertion point visible without reading text at `length`. Resizing recomputes row/column counts and clamps/repositions the viewport around the same text and cursor; it never alters the buffer or cursor.

## 13. File open and new-file behavior

Use existing root-based FAT32/VFS path semantics and the existing 8.3 validation rules. Do not add an editor-specific path grammar. Respect the 128-byte VFS path capacity, including termination, and reject overlong paths without truncation. Leading-slash and nested-path behavior is exactly the behavior already provided by the filesystem API.

For an existing regular file, obtain metadata first, reject files larger than 65,536 bytes without reading/truncating them, then read exactly the reported contents through VFS `open/read/close` (or the equivalent existing public read API). A successful open begins clean. Directory paths are rejected with a useful message. Invalid names, missing parents, non-directory parents, I/O errors, and corrupt/unsupported filesystem results are reported and leave the app in Shell mode.

For a missing valid filename/path, enter Editor mode with length zero, Modified false, and a “not yet present” logical state. Opening does not call `touch`, `write_file`, or any mutating operation. In particular, clean Ctrl+Q from this state returns to Shell mode and leaves the path absent. If a parent is missing or not a directory, this is not a valid new-file case and opening fails in Shell mode.

## 14. Save semantics

Ctrl+S calls the existing public `filesystem::write_file(path, buffer, length)` (or its VFS wrapper) exactly once for the current complete byte contents. Do not implement touch-then-write. The filesystem layer remains responsible for Test-disk targeting, FAT32 mutation ordering, and its established failure semantics.

On `Status::Ok`, clear Modified, mark the path logically present/saved, and show a success message such as `Saved NOTES.TXT`. This includes a zero-length buffer: first Ctrl+S on a new blank file creates an empty file.

On any non-Ok status, stay in Editor mode, preserve the full in-memory buffer, set/retain Modified, and show an error derived from the filesystem status. Never show success or clear Modified on failure. A failure status does not claim whether an ambiguous lower-level I/O operation did or did not publish data on disk; the UI reports only that save did not return Ok and remains retryable.

Map at least `NotFound`, `NotDirectory`, `IsDirectory`, `IoError`, `Corrupt`, `Unsupported`, `AlreadyExists`, `InvalidName`, `NoSpace`, `DirectoryNotEmpty`, `NotMounted`, and `ReadOnly` to useful human-readable status text where those statuses can reach the editor. No error path may fall back to raw disk access.

## 15. Quit semantics

- If Modified is false, Ctrl+Q immediately leaves Editor mode. This includes a newly opened blank, nonexistent file; no file is created.
- If Modified is true and no quit warning is pending, the first Ctrl+Q stays in Editor mode, arms the warning, and displays exactly: `Unsaved changes! Press Ctrl+Q again to quit without saving.`
- A second Ctrl+Q key-press before any intervening non-modifier key-press discards the in-memory changes and returns to Shell mode without saving.
- Any intervening key-press other than Ctrl+Q, including navigation, editing, or an unrecognized key, disarms the pending forced quit. Modifier-only events and key releases do not count as intervening actions. Ctrl+S always disarms the pending forced quit, whether save succeeds or fails.
- Successful save clears Modified. Failed save leaves Modified true. After a failed save, Ctrl+Q therefore still requires the warning/second-press sequence.

## 16. Rendering and layout

The editor fills and draws only inside the existing clipped Terminal content rectangle using the framebuffer/text renderer and existing retro terminal palette conventions.

- Title row: `Linux95 Editor - <path>` and a visible `Modified` indicator when dirty.
- Middle: the horizontally/vertically scrolled text viewport, one byte per 8x8 cell.
- Help row: `^S Save    ^Q Quit`.
- Status row: one-based `Ln <line>, Col <column>` first, followed by the current transient status/error in the remaining width when present; clip the message rather than hiding the line/column indicator.

Status/error messages remain visible until the next editor key-press or mode exit, avoiding time-dependent model behavior. Draw a visible caret/block at the insertion point when it is inside the visible viewport; the cursor-at-end indicator may occupy the cell immediately after the final byte. Rendering a cursor must not mutate editor state. Mouse editing is not supported; existing desktop mouse/window interactions remain as they are, and the editor draws within whatever content rectangle it is given.

## 17. Error handling and integrity guarantees

- No editor buffer, cursor, path, render, or event-queue operation may overrun its fixed storage.
- A file larger than 65,536 bytes is refused before content is loaded; the source file is never truncated by open.
- A missing file is not created until Ctrl+S succeeds.
- Existing files are not changed by open or by a clean quit.
- Failed save preserves the in-memory text and Modified state and displays a non-success status.
- Path and filesystem errors are propagated without truncating paths or treating directories as files.
- All mutations go through the approved writable filesystem API, which targets only the Test/slave disk. Boot/master remains read-only through both the filesystem boundary and `storage::write_sector` defense in depth.
- Queue overflow drops input safely. Existing desktop, network, DNS, scheduler, keyboard/mouse, and shell behavior must not be compromised by editor mode.

These guarantees do not add journaling or strengthen the filesystem's crash-consistency contract.

## 18. Polling and runtime interaction

The desktop continues its normal network/DNS polling, scheduler work, input dispatch, and redraw loop while Editor mode is active. `TerminalApp::poll()` does not call `ShellSession::poll()` in Editor mode, preventing asynchronous ping/DNS output or shell prompt state from being rendered into or interfering with the editor. The desktop-level network and DNS polling continues normally. On return to Shell mode, the existing ShellSession resumes polling any pending shell operation and retains its command/session state.

Shell input is not delivered to `ShellSession` while Editor mode is active. Editor mode consumes only its supported key presses; unsupported keys do not become shell input. The editor does not pause preemptive scheduling or unrelated desktop services.

## 19. Testing strategy

Implementation uses strict TDD RED -> GREEN for each plan task. Add focused host-test seams for keyboard decoding, editor-model behavior, shell command-result integration, and filesystem/editor lifecycle. Preserve every existing test and QEMU mode.

Host tests cover at least:

- Set 1 character decoding; Shift letters/punctuation regression; independent Shift state; left/right Ctrl press/release; E0 arrow make/break decoding; no bogus character for modifiers/prefixes; bounded queue overflow drops safely.
- Shell ordinary character, Enter, Backspace, command editing, and command execution compatibility through `KeyEvent` adaptation; `edit` missing argument and extra-argument rejection; explicit editor request propagation; ordinary shell commands unchanged.
- Empty editor model and one logical line; insertion at end and middle; exact 65,536-byte capacity; refusal at 65,537th byte; Backspace and LF; left/right bounds; up/down; shorter-line clamp; retained desired column; viewport vertical/horizontal movement and resize with cursor visible.
- Dirty state; clean Ctrl+Q; first modified Ctrl+Q warning; second consecutive Ctrl+Q discard; intervening press disarms; modifier release does not disarm; Ctrl+S disarms; successful save clears dirty; failed save keeps dirty and buffer.
- Existing-file open and exact contents; nonexistent-file open without creation; clean Ctrl+Q on a missing file without creation; first Ctrl+S creates it; immediate Ctrl+S creates a zero-byte file; edit/save/reopen byte-exact contents; >65,536-byte refusal; directory refusal; invalid 8.3 name and missing-parent errors.
- Filesystem statuses mapped to clear errors; failed save does not claim success; all test doubles verify writes use the public filesystem seam rather than raw FAT/storage access.

A dedicated QEMU editor smoke test should use the disposable writable Test-disk image and real keyboard input injection through the harness (for example, a QMP/HMP monitor in this mode only). It should boot Linux95, issue `edit` through the actual Terminal shell, exercise editor entry, insert text, Ctrl+S, Ctrl+Q, and prove shell mode remains usable afterward. Use test-only markers only at actual mode/save/return success boundaries. Inspect/remount the same disposable Test image after the run (without recreating it before verification) and verify exact saved contents; also verify a clean quit of a missing blank file leaves it absent. Confirm the Boot/master image remains unchanged. If the existing QEMU setup cannot safely inject keys, keep model/integration host tests mandatory and document the exact harness limitation rather than claiming an unexercised QEMU interaction.

## 20. Acceptance criteria

The milestone is accepted when:

1. `edit <path>` is parsed by the shared shell command path and opens the existing TerminalApp in Editor mode without creating a missing file.
2. Structured keyboard events preserve shell typing and correctly support Shift, Ctrl, E0 arrows, Enter, and Backspace without queue corruption.
3. The fixed editor model supports the specified 65,536-byte bound, exact editing/navigation semantics, cursor-visible scrolling, and resize safety.
4. Existing regular files load exactly; oversized files/directories/invalid paths fail clearly and remain in Shell mode.
5. Ctrl+S uses the existing writable filesystem API; successful and failed saves follow the dirty-state rules, including creating an empty file on save and preserving retryable memory on failure.
6. Ctrl+Q follows the clean/modified two-press rules, and returning to Shell mode preserves the existing terminal session.
7. The editor renders in the existing Terminal content area with title, Modified state, text, help, line/column, and status.
8. The editor cannot write the Boot/master disk, and all writes remain subject to the existing Test/slave storage policy.
9. Focused host tests and the dedicated editor QEMU persistence proof pass, along with the existing regression matrix required by the implementation plan: clean build, `make all`, `make test`, `make test-qemu`, writable FAT32 smoke, process self-test, process fault test, without-user-programs test, unresolved-symbol check, and clean `git diff --check`.

## 21. Design decisions and boundaries

- The editor is one logical mode of the existing graphical TerminalApp; no new window or Ring 3 program is created.
- The editor model is separate from terminal output history and shell parsing. Its fixed text array resides in static/BSS-backed storage to avoid expanding the desktop runtime stack.
- The shell dispatcher reports an explicit bounded editor-open result; TerminalApp alone owns mode switching and filesystem loading.
- A valid missing path starts as clean and nonexistent. Only a successful Ctrl+S creates it.
- The text limit is 65,536 payload bytes, not 65,535 plus a terminator.
- Vertical movement uses logical LF-delimited lines with retained desired column; horizontal scrolling is used instead of wrapping.
- The editor's only persistence path is the existing writable filesystem API, preserving Test-disk-only writes and the Boot-disk prohibition.
- The desktop continues system polling in Editor mode, while shell-session polling is suspended until the shell is active again.
