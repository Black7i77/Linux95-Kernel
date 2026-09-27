# Linux95 Terminal Editor v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded Nano-style text editor inside the existing graphical TerminalApp, backed by Linux95's writable Test-disk filesystem API.

**Architecture:** Upgrade keyboard input to structured events and route those through the existing focused-app callback while preserving ShellSession's character interface. Add a separate fixed-capacity editor model and file adapter, then connect the shared shell parser, TerminalApp mode lifecycle, rendering, tests, and (when deterministic QEMU key injection is supported) an integrated persistence smoke test.

**Tech Stack:** Freestanding C++17 kernel, PS/2 Set 1 keyboard, existing framebuffer/8x8 renderer, TerminalApp/ShellSession, FAT32/VFS, host C++ tests, Python QEMU harness.

**Spec:** docs/superpowers/specs/2026-09-27-linux95-terminal-editor-design.md

## Global Constraints

- Work only in the isolated `/tmp/linux95-terminal-editor` worktree on branch `terminal-editor`; do not modify the main checkout or other worktrees.
- The approved design spec is binding; this plan only decomposes its requirements.
- Use strict TDD RED -> GREEN in every implementation task: add/extend a focused test, run it and record the expected failure before production changes, implement the minimum, then rerun to green.
- Preserve shell printable typing, Enter, Backspace, existing command dispatch, desktop focus, network/DNS polling, scheduler/preemption, mouse, and all existing QEMU modes.
- The editor remains one mode of the existing graphical TerminalApp; it is not a new window or Ring 3 program.
- Text payload capacity is exactly 65,536 bytes, with no NUL byte reserved; the fixed text array is BSS/static-backed, not inline in stack-resident TerminalApp/runtime state.
- Existing files may contain only LF `0x0A` and printable ASCII `0x20`–`0x7E`. Reject CR, TAB, NUL, other controls, and bytes `>= 0x7F` without normalization or source mutation.
- Opening a valid missing path is clean and non-mutating. Only successful Ctrl+S may create it; clean Ctrl+Q leaves it absent.
- Saves use only the public filesystem/VFS `write_file` operation. All writes remain Test/slave-only; do not weaken Boot/master protection or write sectors from editor code.
- Keep the shared shell parser as the only command parser; use an explicit bounded command result for editor entry, never a global flag.
- The implementation must not add features excluded by the spec or alter unrelated files. Do not stage `release/`, push, merge, publish, or delete branches/worktrees.
- Preserve existing Makefile source-check/test ordering contracts when registering tests or modes.

## Review Focus

- E0 and modifier state when the bounded queue is full: dropping a key event must not leave left/right Shift or Ctrl stuck; pin in Task 1's full-queue modifier-release test.
- Exactly-full 65,536-byte content and cursor-at-end insertion/rendering: no terminator reservation, overflow, or out-of-bounds viewport read; pin in Tasks 3–4.
- New-file path versus missing parent and first-save publication: only a missing leaf under a valid directory may open as a new clean buffer, and no file exists before successful save; pin in Tasks 6–7.
- Ctrl+Q consecutiveness and disarming: key release/modifier-only events preserve the two-press sequence, while navigation/edit/unknown key press or either successful/failed Ctrl+S disarms it; pin in Task 7.
- Editor mode with pending ShellSession ping/DNS state: shell history/session must remain intact, shell polling must not draw into the editor, and desktop-level network/DNS polling must continue; pin in Tasks 8–9.

---

### Task 1: Structured PS/2 Key Events and Testable Decoder

**Files:**
- Modify: `kernel/arch/keyboard.hpp`
- Modify: `kernel/arch/keyboard.cpp`
- Create: `kernel/arch/keyboard_helpers.hpp` (pure Set 1 decoder and bounded event queue seam)
- Create: `tests/host/keyboard_test.cpp`
- Modify: `Makefile` (focused build/run target; attach its host target to the existing `test-host-graphics` aggregate, which is already in `make test`)

**Interfaces:**
- Produce in `keyboard.hpp`: `enum class KeyCode : uint8_t { Unknown, Character, Enter, Backspace, ArrowLeft, ArrowRight, ArrowUp, ArrowDown };`, `struct KeyEvent { KeyCode key; char character; bool ctrl; bool shift; bool pressed; };`, `bool has_event();`, and `KeyEvent read_event();`.
- Produce in `keyboard_helpers.hpp`: `class Set1Decoder` with `bool feed(uint8_t scancode, KeyEvent& out)`, a fixed-capacity `class EventQueue` exposing `bool push(const KeyEvent&)`, `bool empty() const`, and `KeyEvent pop()`, and `void decode_and_queue(Set1Decoder&, EventQueue&, uint8_t scancode)`. Queue capacity is 128 events and overflow drops the newest event; `decode_and_queue` always feeds the decoder before attempting queue insertion.
- Decoder state tracks E0 prefix, left/right Shift, and left/right Ctrl; modifier state is updated before queue insertion so queue-full drops cannot corrupt it.

- [ ] **Step 1: Write failing decoder/queue tests first.** Cover ordinary US character, shifted letter and punctuation, independent left/right Shift make/break, left Ctrl make/break, right Ctrl E0 make/break, each arrow make/break, unsupported E0 key, prefix/modifier non-character behavior, 128-slot queue overflow/drop-newest, and a full queue followed by Ctrl/Shift release then a decoded character with modifiers cleared.
- [ ] **Step 2: Prove RED.** Run `make build/host-keyboard-test`; expect compilation/test failure because the structured event/decoder/queue API is not implemented.
- [ ] **Step 3: Implement the minimum decoder and adapt the IRQ handler.** Keep port I/O in `keyboard.cpp`; feed each byte to the pure decoder, update modifiers on make/break, and enqueue only produced events. Remove the character queue API once callers migrate in Task 2; do not allocate dynamically.
- [ ] **Step 4: Prove GREEN and preserve the focused result.** Run `make build/host-keyboard-test && ./build/host-keyboard-test`; expect PASS, including queue-full modifier release and no stuck modifier after subsequent key decoding.
- [ ] **Step 5: Commit.** `git add kernel/arch/keyboard.hpp kernel/arch/keyboard.cpp kernel/arch/keyboard_helpers.hpp tests/host/keyboard_test.cpp Makefile && git commit -m "Add structured keyboard events"`.

### Task 2: Route KeyEvent Through GUI Callbacks and Preserve Shell Typing

**Files:**
- Modify: `kernel/gui/app.hpp`
- Modify: `kernel/gui/desktop.hpp`
- Modify: `kernel/gui/desktop.cpp`
- Create: `kernel/terminal/key_event_adapter.hpp` (pure Shell-mode key-to-char adapter)
- Modify: `kernel/gui/terminal_app.hpp`
- Modify: `kernel/gui/terminal_app.cpp`
- Modify: `kernel/terminal/shell.cpp` (VGA loop drains KeyEvent but still feeds chars to ShellSession)
- Create: `tests/host/terminal_key_event_test.cpp`
- Modify: `Makefile` (register the focused target under the existing `test-host-graphics` aggregate; do not add/reorder `test` source-check prerequisites)

**Interfaces:**
- Consume Task 1 `keyboard::KeyEvent`, `keyboard::has_event()`, and `keyboard::read_event()`.
- Produce `AppCallbacks::on_key(void* context, const keyboard::KeyEvent& event)` and `desktop::route_key(..., const keyboard::KeyEvent& event)`.
- Produce `bool terminal::shell_character_for_key(const keyboard::KeyEvent&, char& out)` in `key_event_adapter.hpp`; it returns true only for pressed, non-Ctrl Character/Enter/Backspace events, translating Enter and Backspace to the existing shell control characters.
- In Shell mode, TerminalApp uses that adapter to keep `ShellSession::on_char(char)` unchanged. Arrows, releases, Unknown, and all Ctrl-modified characters are not sent as ordinary shell characters.

- [ ] **Step 1: Write failing routing/adapter tests first.** In `terminal_key_event_test.cpp`, assert the shell adapter maps ordinary `a`, Enter, and Backspace to the existing shell characters and rejects arrows, releases, Unknown, and Ctrl+S/Ctrl+Q. Also test `desktop::route_key` callback receives the complete event for the focused Terminal and system-info app and retains existing focus/open-window behavior.
- [ ] **Step 2: Prove RED.** Run `make build/host-terminal-key-event-test`; expect missing event callback/adapter API or failed event-routing assertions.
- [ ] **Step 3: Change the callback and desktop drain to KeyEvent.** Keep key delivery to the currently focused app and current invalidation behavior. TerminalApp's adapter handles only pressed events; ShellSession remains char-based.
- [ ] **Step 4: Prove GREEN and shell compatibility.** Run `make build/host-terminal-key-event-test && ./build/host-terminal-key-event-test`, `make test-host-shell-session`, and `make test-host-graphics`; expect PASS with existing shell tests unchanged.
- [ ] **Step 5: Commit.** `git add kernel/gui/app.hpp kernel/gui/desktop.hpp kernel/gui/desktop.cpp kernel/gui/terminal_app.hpp kernel/gui/terminal_app.cpp kernel/terminal/key_event_adapter.hpp kernel/terminal/shell.cpp tests/host/terminal_key_event_test.cpp Makefile && git commit -m "Route structured keys to terminal apps"`.

### Task 3: Fixed-Capacity Editor Model and Basic Byte Editing

**Files:**
- Create: `kernel/gui/editor_model.hpp`
- Create: `kernel/gui/editor_model.cpp`
- Create: `tests/host/editor_model_test.cpp`
- Modify: `Makefile` (register the focused target under the existing `test-host-graphics` aggregate)

**Interfaces:**
- Produce `gui::editor::kTextCapacity == 65536` and a small metadata-only `EditorModel` constructed with caller-provided static/BSS storage: `EditorModel(uint8_t* storage, size_t capacity)`.
- Produce `constexpr size_t kStatusCapacity = 96`, `bool initialize(const char* path, const uint8_t* data, size_t length, bool file_exists)`, `bool insert(uint8_t byte)`, `bool insert_newline()`, `bool backspace()`, `size_t length() const`, `size_t cursor() const`, `const uint8_t* data() const`, `const char* path() const`, `bool file_exists() const`, `bool modified() const`, and `const char* status_message() const`.
- The model rejects input except `0x20`–`0x7E` for character insertion and LF through `insert_newline`; initialization accepts only already-validated content no larger than 65,536 bytes. The model does not reserve a NUL terminator.

- [ ] **Step 1: Write failing model tests first.** Cover empty state/one empty line, insertion at end and middle, exact 65,536-byte capacity, rejected 65,537th insertion leaving content/cursor/Modified valid, Enter as one LF, Backspace, and Backspace at position zero.
- [ ] **Step 2: Prove RED.** Run `make build/host-editor-model-test`; expect missing model API/build failure.
- [ ] **Step 3: Implement the bounded model.** Require storage capacity exactly at least `kTextCapacity`; use bounded overlapping byte moves for middle insertion/deletion, and maintain `0 <= cursor <= length <= 65536` after every operation.
- [ ] **Step 4: Prove GREEN.** Run `make build/host-editor-model-test && ./build/host-editor-model-test`; expect PASS. Also run `make all` to catch freestanding compiler/link issues.
- [ ] **Step 5: Commit.** `git add kernel/gui/editor_model.hpp kernel/gui/editor_model.cpp tests/host/editor_model_test.cpp Makefile && git commit -m "Add fixed-capacity terminal editor model"`.

### Task 4: Logical-Line Navigation, Desired Column, and Viewport

**Files:**
- Modify: `kernel/gui/editor_model.hpp`
- Modify: `kernel/gui/editor_model.cpp`
- Modify: `tests/host/editor_model_test.cpp`
- Modify: `Makefile` only if the test target dependencies need it; attach the focused target under existing `test-host-graphics`

**Interfaces:**
- Extend `EditorModel` with `move_left()`, `move_right()`, `move_up()`, `move_down()`, `update_viewport(size_t visible_rows, size_t visible_columns)`, `line_count()`, `line_bounds(size_t line, size_t& begin, size_t& end) const`, and read-only getters for one-based line/column plus viewport top-line/left-column.
- Lines are LF-delimited; empty text is one empty line; trailing LF creates a final empty line. Vertical motion retains desired zero-based column through short-line clamps. Any edit and Left/Right reset desired column.
- View layout helper (if separated) consumes pixel `graphics::Rect` and returns cell counts using 8x8 cells, three chrome rows, and `max(total_rows - 3, 0)` text rows.

- [ ] **Step 1: Extend tests first.** Cover left/right bounds, cursor before/after LF, up/down, short-line clamp with desired-column retention over repeated vertical moves, empty/trailing-newline lines, horizontal and vertical cursor visibility, exact-capacity cursor-at-end viewport, and resize without text/cursor mutation.
- [ ] **Step 2: Prove RED.** Run `make build/host-editor-model-test`; expect missing movement/viewport assertions to fail.
- [ ] **Step 3: Implement line scanning/navigation and viewport clamping.** Do not soft-wrap; treat each supported printable byte as one cell. If either viewport dimension is zero, avoid division/clamping underflow and keep model state unchanged.
- [ ] **Step 4: Prove GREEN.** Run `make build/host-editor-model-test && ./build/host-editor-model-test` and the focused renderer test `make test-host-renderer`; expect PASS.
- [ ] **Step 5: Commit.** `git add kernel/gui/editor_model.hpp kernel/gui/editor_model.cpp tests/host/editor_model_test.cpp && git commit -m "Add editor navigation and scrolling model"`.

### Task 5: Shared Shell `edit` Result Boundary

**Files:**
- Modify: `kernel/terminal/shell.hpp`
- Create: `kernel/terminal/shell_edit.hpp` and `kernel/terminal/shell_edit.cpp` (bounded `edit` argument parser and VGA-unavailable result helper)
- Modify: `kernel/terminal/shell.cpp` (command result return path and VGA ShellSession callback)
- Modify: `kernel/terminal/shell_session.hpp`
- Modify: `kernel/terminal/shell_session.cpp`
- Modify: `tests/host/shell_session_test.cpp`
- Create: `tests/host/shell_edit_test.cpp`
- Modify: `Makefile` (focused shell-edit test target; register it under `test-host-graphics`)

**Interfaces:**
- In namespace `linux95::shell`, produce `enum class CommandAction : uint8_t { Continue, OpenEditor };` and `struct CommandResult { CommandAction action; char path[filesystem::vfs::kPathCapacity]; };` in `kernel/terminal/shell.hpp` (include the existing VFS path-capacity declaration).
- Change `shell::execute_command(terminal::Output&, char*)` to return `shell::CommandResult`; change `terminal::ExecuteCallback` to return that result and make `ShellSession::on_char(char)` return `shell::CommandResult` (default `Continue`, `OpenEditor` only when an Enter-submitted command requests it). Include the command-result declaration in ShellSession without creating an include cycle.
- Produce `enum class EditArgumentStatus : uint8_t { Ok, Missing, Extra, TooLong };` and `EditArgumentStatus parse_edit_argument(const char*, char (&path)[filesystem::vfs::kPathCapacity])` in `shell_edit.hpp`; use it from the sole shared dispatcher after command tokenization. Produce `shell::CommandResult report_editor_unavailable(Output&, shell::CommandResult)` for the VGA callback; it prints a clear unavailable message and consumes `OpenEditor` as `Continue`.
- `edit` accepts one bounded path only. Missing/extra arguments print usage/error and return `Continue`. Parsing validates bounded copy/no truncation and does no filesystem mutation. Non-graphical VGA caller consumes `OpenEditor` by printing an editor-unavailable message.

- [ ] **Step 1: Add failing shell tests first.** Extend `shell_session_test.cpp` with callback-result propagation and unchanged ordinary command behavior. Create `tests/host/shell_edit_test.cpp` for missing, extra, exact path, `kPathCapacity` boundary/no truncation, and VGA-unavailable output/result behavior.
- [ ] **Step 2: Prove RED.** Run `make build/host-shell-session-test` and `make build/host-shell-edit-test`; expect callback/result/parser API failures.
- [ ] **Step 3: Implement the minimal result propagation and `edit` parser branch.** Do not duplicate parsing in TerminalApp; no globals or file creation occur here.
- [ ] **Step 4: Prove GREEN.** Run `make build/host-shell-session-test && ./build/host-shell-session-test`, `make build/host-shell-edit-test && ./build/host-shell-edit-test`, and `make test-host-filesystem` to ensure shared shell dispatcher/filesystem commands remain compatible.
- [ ] **Step 5: Commit.** `git add kernel/terminal/shell.hpp kernel/terminal/shell.cpp kernel/terminal/shell_edit.hpp kernel/terminal/shell_edit.cpp kernel/terminal/shell_session.hpp kernel/terminal/shell_session.cpp tests/host/shell_session_test.cpp tests/host/shell_edit_test.cpp Makefile && git commit -m "Add terminal editor shell request"`.

### Task 6: Editor File Open and Missing-File Initialization

**Files:**
- Create: `kernel/gui/editor_file.hpp`
- Create: `kernel/gui/editor_file.cpp`
- Modify: `kernel/gui/editor_model.hpp` and `kernel/gui/editor_model.cpp` for load initialization/accessors only
- Modify: `tests/host/editor_file_test.cpp`
- Modify: `Makefile` (register both focused targets under existing `test-host-graphics`)

**Interfaces:**
- Produce `filesystem::Status gui::editor::open_file(EditorModel& model, const char* path)` and `filesystem::Status gui::editor::save_file(EditorModel& model)` in `editor_file.hpp`; Task 7 implements/extends save behavior using this same interface.
- `open_file` uses `filesystem::vfs::stat/open/read/close`; metadata precedes reads. It returns `Status::Ok` only when the model is ready in Editor mode. Missing leaf is accepted only after confirming the parent exists and is a directory; distinguish invalid name, missing parent, and non-directory parent using existing VFS status semantics.
- Existing file length `<= 65536` is loaded byte-exact only if every byte is LF or printable ASCII; larger files and files with unsupported bytes return `filesystem::Status::Unsupported` and leave model/app mode unchanged. Directory open returns `IsDirectory`; invalid paths retain the existing VFS `InvalidName`/`NotFound`/`NotDirectory` distinction. Do not write on open.

- [ ] **Step 1: Write failing file lifecycle tests first.** Cover supported ASCII/LF byte-exact open, a 65,536-byte file, >65,536 refusal before content read, CRLF refusal without mutation, TAB, embedded NUL, other control, high byte refusal, directory refusal, invalid 8.3, missing parent and non-directory parent refusal, and missing valid path opening clean without creation.
- [ ] **Step 2: Prove RED.** Run `make build/host-editor-file-test`; expect missing open adapter and validation failures.
- [ ] **Step 3: Implement open through public VFS only.** Use host stubs for VFS calls; close descriptors on every path after successful open. Preserve source bytes and do not call `touch`/`write_file` during open.
- [ ] **Step 4: Prove GREEN.** Run `make build/host-editor-file-test && ./build/host-editor-file-test`; expect all open, bounded-size, ASCII/LF, and non-creation cases to pass.
- [ ] **Step 5: Commit.** `git add kernel/gui/editor_file.hpp kernel/gui/editor_file.cpp kernel/gui/editor_model.hpp kernel/gui/editor_model.cpp tests/host/editor_file_test.cpp Makefile && git commit -m "Load supported files into terminal editor"`.

### Task 7: Save, Status Mapping, Modified State, and Quit State Machine

**Files:**
- Modify: `kernel/gui/editor_model.hpp`
- Modify: `kernel/gui/editor_model.cpp`
- Modify: `kernel/gui/editor_file.cpp`
- Modify: `tests/host/editor_file_test.cpp`
- Create: `tests/host/editor_state_test.cpp`
- Modify: `Makefile` (register focused target under existing `test-host-graphics`)

**Interfaces:**
- `save_file(EditorModel&)` calls `filesystem::vfs::write_file(path, data, length)` exactly once; no touch-then-write. On `Ok`, mark present, clear Modified, and set `Saved <path>`; otherwise preserve bytes, set/retain Modified, and map the returned filesystem status to a useful message.
- Produce `enum class EditorAction : uint8_t { None, Save, Quit };` and `EditorAction EditorModel::handle_key(const keyboard::KeyEvent&)`; it performs supported normal edits/navigation, reports Save for Ctrl+S, and implements the Ctrl+Q warning/quit state machine. With Ctrl held, match `s`/`S` as Save and `q`/`Q` as Quit; never insert those shortcut characters. It ignores releases/modifier-only events without disarming. Add `void save_succeeded()` and `void save_failed(filesystem::Status)` to update dirty/present/status state.
- All status text is stored in the Task 3 fixed 96-byte status buffer, NUL-terminated and clipped on assignment; line/column text remains visible ahead of the status message.
- Save always disarms pending forced quit, including failure. An intervening press disarms; releases/modifier-only do not. Failed save is non-success regardless of uncertain on-disk publication.

- [ ] **Step 1: Write failing tests first.** Cover first save creating a new file, empty zero-byte save, existing save/reopen byte-exact, success clears dirty/present=true, every filesystem status listed in Spec Section 14 maps to a useful message, failure retains exact text and Modified, no raw disk seam, clean Ctrl+Q, first dirty warning exact text, second consecutive Ctrl+Q quits, navigation/edit/unknown press disarms, release/modifier-only preserves, and Ctrl+S disarms on success and failure.
- [ ] **Step 2: Prove RED.** Run `make build/host-editor-state-test`; expect missing save/quit state behavior to fail.
- [ ] **Step 3: Implement save and key state transitions.** Ensure clean missing-file quit does not invoke filesystem mutation; failed Ctrl+S leaves the model modified and editor active. `handle_key` returns `Save` on Ctrl+S and `Quit` only when the clean/two-press rules permit; all other presses return `None` after the appropriate model operation.
- [ ] **Step 4: Prove GREEN.** Run `make build/host-editor-state-test && ./build/host-editor-state-test` and `make build/host-editor-file-test && ./build/host-editor-file-test`; expect PASS.
- [ ] **Step 5: Commit.** `git add kernel/gui/editor_model.hpp kernel/gui/editor_model.cpp kernel/gui/editor_file.cpp tests/host/editor_file_test.cpp tests/host/editor_state_test.cpp Makefile && git commit -m "Add editor save and quit behavior"` (stage only created/modified tests).

### Task 8: TerminalApp Modes, File Lifecycle Integration, and Rendering

**Files:**
- Modify: `kernel/gui/terminal_app.hpp`
- Modify: `kernel/gui/terminal_app.cpp`
- Modify: `kernel/gui/app.hpp` only if Task 2 left a routing seam incomplete
- Modify: `tests/host/terminal_editor_integration_test.cpp`
- Modify: `Makefile` (keep all existing `test` and source-check recipe ordering unchanged)

**Interfaces:**
- `TerminalApp` owns `Mode::{Shell, Editor}`, a small `EditorModel` referencing one fixed 65,536-byte BSS-backed array (not an inline TerminalApp member), and one shared ShellSession retained for the app's lifetime.
- Expose a read-only `Mode mode() const` seam for host integration tests; do not add test-only mode mutation.
- Consume Task 2 `keyboard::KeyEvent` callbacks, Task 5 `shell::CommandResult`, Task 6 `gui::editor::open_file`, and Task 7 save/quit actions.
- `TerminalApp::poll()` calls `session_.poll()` only in Shell mode. In Editor mode, shell input/poll output is suspended; desktop-level network/DNS/scheduler polling is unchanged.
- Draw editor title/path/Modified, non-wrapped text viewport, visible caret, help, one-based line/column, and clipped bounded status inside supplied content rect using 8x8 cell layout with three reserved chrome rows.
- Because editor storage is not NUL-terminated but the existing renderer's `draw_text` consumes C strings, render each visible cell with a two-byte `{character, '\\0'}` scratch or another equally bounded method; never pass a raw editor-buffer span as a C string. Keep any large row scratch out of the runtime stack.

- [ ] **Step 1: Write failing integrated TerminalApp tests first.** Compile the actual TerminalApp/model/file/session sources and provide test link doubles for shared `shell::execute_command`, VFS entry points, and network/DNS callbacks. Verify shell `OpenEditor` enters editor only on successful file load; load failure leaves Shell mode; keys are isolated by mode; Ctrl+Q returns to the same ShellSession; shell polling is suspended/resumed correctly; rendering/layout handles zero rows/columns and resize without mutation; ShellSession history survives the round trip.
- [ ] **Step 2: Prove RED.** Run `make build/host-terminal-editor-integration-test`; expect TerminalApp mode/result/render integration assertions to fail.
- [ ] **Step 3: Implement TerminalApp coordination and view.** Keep editor text in one static/BSS-backed 65,536-byte store. No second shell parser, no second window, and no editor raw filesystem/disk writes.
- [ ] **Step 4: Prove GREEN.** Run `make build/host-terminal-editor-integration-test && ./build/host-terminal-editor-integration-test`, then `make test-host-graphics && make test-host-shell-session`; expect PASS.
- [ ] **Step 5: Commit.** `git add kernel/gui/terminal_app.hpp kernel/gui/terminal_app.cpp tests/host/terminal_editor_integration_test.cpp Makefile && git commit -m "Integrate editor mode into terminal app"` (include `app.hpp` only if it is actually modified in this task).

### Task 9: Integrated Host Coverage and Boundary Hardening

**Files:**
- Modify: `tests/host/keyboard_test.cpp`
- Modify: `tests/host/shell_session_test.cpp`
- Modify: `tests/host/editor_model_test.cpp`
- Modify: `tests/host/editor_file_test.cpp`
- Modify: `tests/host/editor_state_test.cpp`
- Modify: `tests/host/terminal_editor_integration_test.cpp`
- Modify: `Makefile`
- Modify production files only if a focused test exposes a real defect; add the failing assertion first.

**Interfaces:**
- Consume the established APIs from Tasks 1–8; do not introduce a parallel keyboard, shell, editor, or filesystem API for tests.
- Add all new host tests to `make test` through the existing host-test aggregation while preserving the current preemption/source-check prerequisite and source-check command ordering.

- [ ] **Step 1: Add any missing cross-boundary failing tests.** Specifically pin queue-full modifier release, shell typing after editor exit, pending ping while editor is active (ShellSession polling pauses and resumes without losing the result), pending DNS blocking shell input so `edit` cannot enter until that lookup completes, exact capacity/cursor-end viewport, unsupported file byte variants leaving source unchanged, and save/quit API calls at most once per key event.
- [ ] **Step 2: Run the relevant focused targets and record RED.** Use each relevant host binary target and expect the new assertions to fail before any fix.
- [ ] **Step 3: Make only test-proven minimal fixes.** Do not broaden shell parser, keyboard, filesystem, or renderer scope.
- [ ] **Step 4: Prove GREEN.** Run every new focused host binary and `make test-host-graphics && make test-host-filesystem`; expect PASS.
- [ ] **Step 5: Commit.** `git add` only the named host tests, any specifically justified production files, and `Makefile`; commit as `Harden terminal editor integration tests`.

### Task 10: QEMU Editor Keyboard and Persistence Proof

**Files:**
- Modify: `tests/qemu_smoke.py`
- Modify: `tests/prepare_fat32_image.py`
- Modify: `Makefile`
- Modify: `kernel/gui/terminal_app.cpp` for success markers compiled only into the dedicated editor-test kernel variant; markers must not be emitted by fixture setup or test-mode selection.

**Interfaces:**
- Add a separate `--editor-test` boot image built from the normal desktop kernel with only editor-test markers enabled in `terminal_app.cpp`; use a separate freshly prepared disposable Test image and preserve the canonical FAT fixture. The current FAT32-write kernel self-test image is not the editor boot image.
- Extend `tests/prepare_fat32_image.py` with an editor fixture mode that formats/populates a disposable FAT32 image and deliberately leaves both editor test targets absent. Never recreate the Test image between QEMU completion and persistence inspection.
- Use a test-only QMP/HMP input channel only for this mode; normal QEMU invocations and monitor configuration remain unchanged. Markers must be emitted only after real mode entry, successful filesystem save, actual quit to Shell, and a subsequent key delivered to Shell.
- If real deterministic key injection succeeds, require actual shell command, editor entry, inserted text, successful save, quit, returned-shell activity, exact post-run Test-image contents, clean missing-file quit/no creation, and unchanged Boot image.

- [ ] **Step 1: Probe the existing QEMU harness's deterministic input capability before changing production code.** The current harness uses `-monitor none`; confirm QEMU availability and test a dedicated temporary QMP/HMP channel with `sendkey` make/release timing on a disposable boot. Retain exact evidence and ensure monitor sockets/processes are isolated and closed. Do not alter other modes.
- [ ] **Step 2: Write the editor smoke test first.** Add the dedicated flag/fixture/test assertions and run `python3 tests/qemu_smoke.py --editor-test`; expected RED is absent editor markers/content or unsupported harness injection, never synthetic success.
- [ ] **Step 3: If the injection probe is safe and deterministic, implement only the dedicated editor-test kernel image/harness, fixture, and genuine success-boundary markers needed to exercise the real path.** Create the disposable fixture with `NOSAVE.TXT` and `SAVED.TXT` absent. Inject `edit NOSAVE.TXT`, Enter, clean Ctrl+Q; then `edit SAVED.TXT`, Enter, type a fixed payload, Ctrl+S, Ctrl+Q, and a normal shell command. Require two `[PASS] editor opened` markers, two `[PASS] editor returned to shell` markers, exactly one `[PASS] editor saved`, and exactly one `[PASS] shell accepted input`, in operation order and emitted only at those real operations. Stop QEMU and inspect the same Test image without reformat/recreation: `NOSAVE.TXT` must remain absent and `SAVED.TXT` must match exactly. Confirm the Boot image digest is unchanged.
- [ ] **Step 4: If safe deterministic injection genuinely cannot be supported, do not fake or claim the QEMU editor test.** Record the exact harness limitation and add/strengthen the strongest feasible host-level keyboard + TerminalApp + editor + filesystem integration test; that test must prove the same save/return/non-creation contracts through real production components and test filesystem adapters.
- [ ] **Step 5: Prove the selected path GREEN.** Supported-injection path: run `python3 tests/qemu_smoke.py --editor-test` and verify ordered markers, persisted bytes, absent clean-quit target, and Boot digest. Fallback path: run the documented host integration target and retain evidence/limitation in the final verification report. In either path run `python3 tests/qemu_smoke.py --fat32-write-test` and confirm it still passes.
- [ ] **Step 6: Commit.** Commit only QEMU/fixture/Makefile and any genuinely necessary editor success-marker changes as `Add terminal editor integration smoke test`.

### Task 11: Full Regression, Hygiene, and Final Review

**Files:**
- Modify source/test/build files only if a defect is reproduced and fixed test-first.
- No documentation-only or empty commit is required if this task finds no defect.

**Interfaces:**
- Verify the complete approved spec against the final branch; preserve all prior Linux95 behaviors and existing QEMU mode semantics.
- QEMU editor interaction follows Task 10's recorded supported-injection result; all existing regression modes are unconditional.

- [ ] **Step 1: Run a clean build.** `make clean && make all`; confirm kernel image and userspace ELFs build from scratch.
- [ ] **Step 2: Run the complete host/source/image suite.** `make test`; every existing and new test must pass.
- [ ] **Step 3: Run the full QEMU regression matrix.** `make test-qemu`, `python3 tests/qemu_smoke.py --fat32-write-test`, `python3 tests/qemu_smoke.py --process-self-test`, `python3 tests/qemu_smoke.py --process-fault-test`, and `python3 tests/qemu_smoke.py --without-user-programs`; also run `python3 tests/qemu_smoke.py --editor-test` only when Task 10 established safe deterministic keyboard injection. Do not use one mode as a substitute for another.
- [ ] **Step 4: Run binary/hygiene checks.** `nm -u build/kernel.elf` must print no unresolved symbols; `git diff --check` must be clean; `git status --short --branch` must show only intended milestone files and no unrelated/main-checkout changes. Confirm `release/` and all historical worktrees are untouched.
- [ ] **Step 5: Review the complete branch against the approved spec.** Check editor event routing and bounds, shell compatibility, no implicit creation, unsupported-byte preservation, save/quit semantics, polling, and Test-disk-only writes. Resolve any Critical/Important issue with a failing test first and rerun the affected tests plus the complete final matrix.
- [ ] **Step 6: Commit only any verified in-scope fix.** If verification requires a fix, commit that fix narrowly after green checks. If no fix is required, do not create an empty commit.
