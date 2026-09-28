# Linux95 File Manager v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a compact graphical File Manager with shared Icon/Details views and safe Test-volume create, rename, and delete operations.

**Architecture:** A File Manager model/controller owns the root-based path, bounded VFS listing, selected name, view, and operation state. A File Manager app renders that model and owns its small dialogs; desktop routes generic app pointer events, while all filesystem calls go through VFS and its existing Test-disk FAT32 backend.

**Tech Stack:** Freestanding C++17 Linux95 kernel, PS/2 Set 1 keyboard/mouse input, existing GUI framebuffer renderer and WindowManager, VFS/FAT32, host C++ tests, Python/QEMU image harness.

**Spec:** `docs/superpowers/specs/2026-09-28-linux95-file-manager-design.md`

## Global Constraints

- Source baseline: v1.0-dev commit `9d843e9877ef40bea820fba0b50cbc0d91c491e9`.
- All filesystem listing and mutations use the public VFS; GUI code must not access FAT32 internals or sectors.
- Writable operations target only the Test/slave volume; Boot/master remains read-only and its image must remain unchanged.
- Paths are bounded by `vfs::kPathCapacity` (128 bytes) and root is `/`; path components use the existing FAT32 8.3 rules.
- Directory model capacity is exactly 128 entries and overflow/truncation is explicitly reported.
- Directory rename is same-parent only; no cross-parent directory moves or recursive deletion.
- Delete always requires confirmation; non-empty-directory removal is refused.
- Icon View and Details View share one model, current path, and selected entry.
- Existing Terminal Editor, desktop, networking, Ring 3, preemptive scheduling, and no-network behavior must remain intact.
- Do not change unrelated files, touch `release/`, or push/merge/publish/delete branches/worktrees.
- Preserve existing Makefile source-check and test ordering contracts.

## Review Focus

- **Mouse edge routing versus desktop chrome:** a press/release over content reaches only the focused app; titlebar drag, close/minimize, panel, and resize remain desktop-owned. Test in Task 1.
- **Bounded listing and directory-handle cleanup:** the 129th entry causes an explicit truncation indication, not silent omission or invalid selection; VFS errors still close the handle and preserve the prior model. Test in Task 3.
- **Case-insensitive selection/path identity:** mixed-case 8.3 names resolve and survive view changes/refresh without selecting a different entry. Test in Tasks 2–3.
- **Filesystem result versus UI publication:** duplicate/collision, `NoSpace`, `DirectoryNotEmpty`, and I/O errors never display success or falsely update selection. Test in Task 4.
- **Disposable-image and Boot-image safety:** QEMU GUI operations mutate only the disposable Test image and the Boot image digest is unchanged. Test in Task 8.

---

## File and Interface Map

- `kernel/gui/app.hpp`: add an optional generic app mouse callback and a content-relative event carrying pointer coordinates plus primary-button pressed/released/down state. Existing apps may leave it null.
- `kernel/gui/desktop.hpp/.cpp`: expose/test the generic routing seam; register/activate the File Manager window, route keyboard and pointer input, and add its Applications/task/title entries without special-casing clicks inside desktop code.
- `kernel/arch/keyboard.hpp/.cpp`: add `KeyCode::Escape` and `KeyCode::Delete` if still absent; preserve existing Set 1 decoding and modifiers.
- `kernel/filesystem/filesystem.hpp/.cpp`, `kernel/filesystem/vfs.hpp/.cpp`: expose `filesystem::Status vfs::validate_name(const char*)`, delegating to the existing 8.3 validator.
- `kernel/gui/file_manager_model.hpp/.cpp`: fixed model capacity 128; path/listing/view/selection/navigation and single implementation of VFS operations.
- `kernel/gui/file_manager_app.hpp/.cpp`: `AppInstance`, app drawing/input callbacks, toolbar/dialog state, user messages; no direct FAT/storage calls.
- `tests/host/file_manager_model_test.cpp`, `tests/host/file_manager_app_test.cpp`, `tests/host/desktop_mouse_routing_test.cpp`, `tests/host/file_manager_fake_vfs.hpp/.cpp`, and relevant filesystem/VFS tests: pure model, interaction, routing, and deterministic VFS test-double coverage.
- `tests/prepare_fat32_image.py`, `tests/qemu_smoke.py`, plus a focused independent verifier if needed: disposable Test-image GUI/persistence test; Boot-image digest guard.
- `Makefile`: kernel object inclusion, focused host-test targets, and QEMU image/mode wiring only as required.

## Task 1: Generic App Mouse Events and Desktop Routing

**Files:**
- Modify: `kernel/gui/app.hpp`
- Modify: `kernel/gui/desktop.hpp`, `kernel/gui/desktop.cpp`
- Create: `tests/host/desktop_mouse_routing_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produce `gui::AppMouseEvent { int32_t x; int32_t y; bool left_pressed; bool left_released; bool left_down; }`, with coordinates relative to app content.
- Produce optional `AppCallbacks::on_mouse(void*, const AppMouseEvent&)`.
- Produce `bool desktop::route_mouse(const gui::WindowManager&, gui::WindowId, gui::AppInstance&, gui::Rect content, gui::Point screen_point, bool left_pressed, bool left_released, bool left_down)`; invoke only for the focused app and only when the screen point is inside its content rectangle, translating coordinates to content-relative values.
- Consume existing `mouse::MouseEvent`, desktop cursor position, chrome hit-test, and focused-window state.

- [ ] **Step 1: Write failing routing tests and the focused Makefile build rule.** Assert content press/release coordinates are translated correctly; null callback is harmless; points in chrome/panel do not route; existing drag/resize/chrome events are not routed.
- [ ] **Step 2: Prove RED.** Run `make build/host-desktop-mouse-routing-test && ./build/host-desktop-mouse-routing-test`; expected compile failure because the generic event/callback/routing API is absent.
- [ ] **Step 3: Implement the optional event and narrow generic router.** Route app events only after desktop chrome/panel/focus handling, preserving current drag and focus behavior. Update Terminal/System Info callback initialization for the added field.
- [ ] **Step 4: Prove GREEN.** Run `make build/host-desktop-mouse-routing-test && ./build/host-desktop-mouse-routing-test`; expected all routing assertions pass.
- [ ] **Step 5: Regression and commit.** Run `make test-host-graphics` and `make test`; commit only Task 1 files as `Add generic GUI app mouse routing`.

## Task 2: Public Shared 8.3 Name Validation

**Files:**
- Modify: `kernel/filesystem/filesystem.hpp`, `kernel/filesystem/filesystem.cpp`
- Modify: `kernel/filesystem/vfs.hpp`, `kernel/filesystem/vfs.cpp`
- Create: `tests/host/vfs_name_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produce `filesystem::Status vfs::validate_name(const char* name)`; returns `Status::Ok` for one valid existing short-name component and `Status::InvalidName` for invalid syntax, with no mount or mutation requirement.
- Produce the matching filesystem-facade `filesystem::Status filesystem::validate_name(const char* name)`; the VFS wrapper delegates to it, and the facade delegates to the FAT32 canonical helper.
- Consume `fat32::helpers::valid_path_component` as the single canonical rule source; GUI code will consume only the VFS API.

- [ ] **Step 1: Write failing tests.** Cover valid uppercase/lowercase/mixed-case 8.3 names, maximum 8+3 limits, invalid characters/dot forms, `.`/`..`, separators, null/empty input, and prove validation does not require a mounted disk.
- [ ] **Step 2: Prove RED.** Run `make build/host-vfs-name-test && ./build/host-vfs-name-test`; expected missing VFS validator declaration/definition.
- [ ] **Step 3: Implement the filesystem facade and VFS delegation.** Do not copy the FAT32 name-character rules into GUI or VFS code.
- [ ] **Step 4: Prove GREEN.** Run the focused target; expected all valid/invalid cases pass.
- [ ] **Step 5: Regression and commit.** Run `make test-host-filesystem`; commit as `Expose shared VFS short-name validation`.

## Task 3: Shared File Manager Directory Model

**Files:**
- Create: `kernel/gui/file_manager_model.hpp`, `kernel/gui/file_manager_model.cpp`
- Create: `tests/host/file_manager_model_test.cpp`, `tests/host/file_manager_fake_vfs.hpp`, `tests/host/file_manager_fake_vfs.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produce `gui::file_manager::ViewMode { Icons, Details }`.
- Produce `constexpr size_t kMaxEntries = 128` and a model entry array based on `filesystem::vfs::DirectoryEntry`.
- Produce `class FileManagerModel` with `filesystem::Status load_root()`, `filesystem::Status refresh()`, `filesystem::Status navigate_into(size_t entry_index)`, `filesystem::Status navigate_parent()`, `bool select(size_t entry_index)`, `void set_view(ViewMode)`, and const accessors for path, count, entry, selection, view, and truncation.
- Exact result/accessor types: each operation returns `filesystem::Status`; accessors are `const char* current_path() const`, `size_t entry_count() const`, `const filesystem::vfs::DirectoryEntry* entry(size_t index) const`, `int selected_index() const` (`-1` means none), `ViewMode view_mode() const`, and `bool truncated() const`.
- Model path starts at `/`. Selection identity is a case-insensitive entry name; selection access returns no selection when absent.
- Consume `vfs::opendir/readdir/closedir` and the Task 2 `vfs::validate_name` for child path construction.

- [ ] **Step 1: Write failing host tests plus fixed VFS test doubles and the focused Makefile rule.** Cover initial root, listing and directory/file metadata, child and parent navigation, root-parent no-op, path overflow rejection, missing/non-directory child, view toggle preserving path/selection, refresh preserving extant selection case-insensitively and clearing a vanished selection, error preserving old model, and handle close on success/error.
- [ ] **Step 2: Prove RED.** Run `make build/host-file-manager-model-test && ./build/host-file-manager-model-test`; expected missing model APIs.
- [ ] **Step 3: Implement the fixed-capacity model.** Read through VFS only; distinguish `end` from error; after reading 128 entries, probe one further entry to detect truncation, then close the handle. Commit no partially read replacement listing on error.
- [ ] **Step 4: Prove GREEN.** Run focused model tests; expected all state, navigation, capacity, and cleanup cases pass.
- [ ] **Step 5: Regression and commit.** Run the focused test and `make test-host-filesystem`; commit as `Add shared File Manager directory model`.

## Task 4: Single VFS Operation Controller

**Files:**
- Modify: `kernel/gui/file_manager_model.hpp`, `kernel/gui/file_manager_model.cpp`
- Modify: `tests/host/file_manager_model_test.cpp`
- Modify: `Makefile` only if test linkage changes

**Interfaces:**
- Produce controller methods `Status create_folder(const char* name)`, `Status create_file(const char* name)`, `Status rename_selected(const char* name)`, and `Status remove_selected()` on the shared model/controller.
- Each method returns `filesystem::Status`; name methods accept one bounded NUL-terminated 8.3 component. `remove_selected()` returns `Unsupported` when no selection exists; UI may intercept that case to show its no-selection status without calling VFS.
- Every name method validates through `vfs::validate_name` and constructs bounded absolute paths. `create_file` first calls `vfs::stat`: existing path returns `AlreadyExists`; only `NotFound` proceeds to `vfs::touch`; other statuses abort. This is required because `touch` itself is idempotent for existing files. Each successful operation calls exactly one VFS mutation (`mkdir`, `touch`, `move`, or `remove`), then refreshes and updates selection; failed mutations do not claim success or invent selection changes.
- Consume Task 2 validation and Task 3 path/list/selection behavior.

- [ ] **Step 1: Write failing operation tests.** Cover folder/file create and selected result, duplicate collision with existing-file `touch` semantics (assert no touch call occurs), invalid 8.3 name without mutation, file rename, same-parent directory rename, rename collision, no-selection rename/delete no-op, delete file, delete empty directory, refuse non-empty directory, and failed VFS status preserving model/selection.
- [ ] **Step 2: Prove RED.** Run `make build/host-file-manager-model-test && ./build/host-file-manager-model-test`; expected missing controller operations.
- [ ] **Step 3: Implement one controller path for both views.** Use only VFS public mutation calls. Do not implement recursive removal or directory relocation.
- [ ] **Step 4: Prove GREEN.** Run focused model tests; expected all operations and failure mappings match the VFS return values.
- [ ] **Step 5: Regression and commit.** Run `make test-host-filesystem` and the focused model target; commit as `Add File Manager VFS operations`.

## Task 5: Escape/Delete Keyboard Events

**Files:**
- Modify: `kernel/arch/keyboard.hpp`, `kernel/arch/keyboard.cpp`
- Modify: `tests/host/keyboard_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produce Set 1 `KeyCode::Escape` and `KeyCode::Delete` decoding, including correct make/break and E0 handling; preserve existing `KeyEvent` modifier/pressed contract.
- PS/2 Set 1 mapping: Escape is non-E0 scan `0x01`; Delete is E0-prefixed scan `0x53`; break codes produce release events and E0 prefix bytes alone produce no event.
- Consume the existing decoder/key-event queue and retain current Shift/Ctrl/character behavior.

- [ ] **Step 1: Write failing decoder assertions** for Escape/Delete make and release (including Delete's E0 prefix), and no event for prefix-only bytes.
- [ ] **Step 2: Prove RED.** Run `make build/host-keyboard-test && ./build/host-keyboard-test`; expected failure on the missing key codes/events.
- [ ] **Step 3: Implement the two Set 1 key mappings** without changing modifier state or existing queue behavior.
- [ ] **Step 4: Prove GREEN.** Run `make build/host-keyboard-test && ./build/host-keyboard-test`; expected all existing and new keyboard assertions pass.
- [ ] **Step 5: Regression and commit.** Run `make test-host-graphics`; commit as `Decode File Manager keyboard controls`.

## Task 6: File Manager App and Desktop Registration

**Files:**
- Create: `kernel/gui/file_manager_app.hpp`, `kernel/gui/file_manager_app.cpp`
- Modify: `kernel/gui/desktop.hpp`, `kernel/gui/desktop.cpp`
- Create: `tests/host/file_manager_app_test.cpp` (reuse `file_manager_fake_vfs`)
- Modify: `Makefile`

**Interfaces:**
- Produce `FileManagerApp`, whose `instance()` returns ordinary app callbacks and whose `open()` loads the model root when a new window is created.
- Produce `desktop::kFileManagerWindowId = 3`; `activate_window` creates once, then focuses/restores the existing File Manager window. Runtime state owns one app and one `AppInstance`.
- Extend desktop app lookup/key routing to dispatch the third app; add its Applications menu item, title, and task button.
- Consume Task 1 generic pointer routing and Task 3–4 model/controller.

- [ ] **Step 1: Write failing app/desktop tests** for FileManagerApp callbacks, opening root on first window creation, adding/focusing/restoring window ID 3 once, and app lookup/key delivery to the third app.
- [ ] **Step 2: Prove RED.** Run `make build/host-file-manager-app-test && ./build/host-file-manager-app-test`; expected missing FileManagerApp/registration behavior.
- [ ] **Step 3: Implement app ownership and registration** in runtime state and the existing Applications/menu/task routing. Respect fixed window capacity and do not create duplicate windows.
- [ ] **Step 4: Prove GREEN.** Run the focused app/desktop test; expected registration and callback routing assertions pass.
- [ ] **Step 5: Regression and commit.** Run `make test-host-graphics`; commit as `Register File Manager application`.

## Task 7: Toolbar, Dialogs, and Shared Interaction

**Files:**
- Modify: `kernel/gui/file_manager_app.hpp`, `kernel/gui/file_manager_app.cpp`
- Modify: `tests/host/file_manager_app_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produce dialog mode (`None`, `Name`, `DeleteConfirm`), 13-byte short-name buffer (12 display bytes plus NUL), pending operation, and bounded status message in `FileManagerApp`.
- App consumes Task 1 `AppMouseEvent` and calls Task 4 model/controller operations. Mouse and keyboard activate the same action methods.

- [ ] **Step 1: Write failing interaction tests.** Cover toolbar and keyboard opening of New Folder/New File/Rename/Delete; name input/backspace/Enter/Escape; Rename prefill; Delete confirmation and cancel making zero VFS calls; no-selection feedback; safe clicks outside controls.
- [ ] **Step 2: Prove RED.** Run `make build/host-file-manager-app-test && ./build/host-file-manager-app-test`; expected absent dialog/action transitions.
- [ ] **Step 3: Implement compact app-owned dialogs and dispatch.** Bind Ctrl+N folder, Ctrl+F file, Ctrl+R rename, Delete confirmation, Ctrl+V view; Up/Down select, Enter activates directory, Backspace navigates parent. Accept printable name characters only.
- [ ] **Step 4: Prove GREEN.** Run focused app tests; expect all dialog, keyboard/mouse dispatch, and cancel/no-mutation assertions pass.
- [ ] **Step 5: Regression and commit.** Run `make test-host-graphics`; commit as `Add File Manager toolbar interactions`.

## Task 8: Icon/Details Presentation and Navigation UI

**Files:**
- Modify: `kernel/gui/file_manager_app.cpp`
- Modify: `tests/host/file_manager_app_test.cpp`
- Modify: `Makefile`

**Interfaces:**
- Both renderers consume the same model entries, selected identity, current path, and `ViewMode`.
- Toolbar order is New Folder, New File, Rename, Delete, View. Parent navigation is a separate control. Hit mapping returns model entry indices independent of view mode.
- Produce pure layout/hit-test helpers where needed so compact bounds, clipping, and selection mapping are host-testable.

- [ ] **Step 1: Write failing layout/hit tests.** Assert both views display the same entries/selection; toggle preserves path and selection; child/parent controls navigate; icon/row hits select the corresponding index; clipping/small content bounds do not produce out-of-range indices.
- [ ] **Step 2: Prove RED.** Run `make build/host-file-manager-render-test && ./build/host-file-manager-render-test`; expected missing view layout/hit behavior.
- [ ] **Step 3: Implement compact retro rendering.** Use renderer primitives, clip to content, draw selection/path/status/truncation, keep dialogs within content, and do not mutate model during drawing.
- [ ] **Step 4: Prove GREEN.** Run the focused render test; expected both view modes use identical model data and bounded hit/layout behavior.
- [ ] **Step 5: Regression and commit.** Run `make test-host-graphics` and `make test`; commit as `Render File Manager icon and details views`.

## Task 9: Disposable-Test-Disk QEMU Operations and Persistence

**Files:**
- Modify: `tests/prepare_fat32_image.py`
- Modify: `tests/qemu_smoke.py`
- Create: `tests/file_manager_image_checks.py` if independent post-run inspection is needed
- Create: `tests/file_manager_image_checks_test.py`
- Modify: `Makefile` for a dedicated mode/fixture only as needed

**Interfaces:**
- Produce a dedicated `--file-manager-test` QEMU mode/target using a disposable Test/slave FAT32 image; never reuse or mutate the canonical source fixture.
- Record Boot-image digest before guest launch and check afterward. After QEMU exits, inspect the same modified Test image without reformatting/recreating it.

- [ ] **Step 1: Test the image verifier/harness contract first.** Add `tests/file_manager_image_checks_test.py` proving expected names/content and unchanged Boot digest are required; run `python3 tests/file_manager_image_checks_test.py` and observe RED because the verifier/test mode is absent or accepts an invalid fixture.
- [ ] **Step 2: Prove RED.** Run the focused Python harness/verifier test and `python3 tests/qemu_smoke.py --file-manager-test`; expected failure because the File Manager action flow/results are not yet integrated in the test guest.
- [ ] **Step 3: Inspect deterministic input support before choosing injection.** Use existing QEMU monitor/QMP infrastructure only if it safely supports deterministic input to the real app. If not, record the exact harness limitation and use the strongest real app/controller integration with independent inspection of Test-image changes produced by the guest; do not synthesize GUI events or pass markers.
- [ ] **Step 4: Implement the disposable fixture and persistence proof.** Exercise New Folder, New File, rename, file deletion, empty-directory deletion, and non-empty-directory refusal through GUI controls when deterministic injection is safe; otherwise the limitation/fallback must be explicit. Verify final contents from the same Test image after QEMU exits and compare Boot digest unchanged.
- [ ] **Step 5: Prove GREEN.** Run `python3 tests/qemu_smoke.py --file-manager-test`; expected actual guest VFS operations and post-run image assertions PASS. Existing QEMU regression remains mandatory either way.
- [ ] **Step 6: Regression and commit.** Run focused host File Manager targets and `python3 tests/qemu_smoke.py --fat32-write-test`; commit as `Verify File Manager operations on disposable FAT32`.

## Task 10: Full Regression, Hygiene, and Whole-Branch Review

**Files:**
- No production changes expected; fix only defects reproduced by a failing test.
- Modify only review ledger/test metadata if the workflow requires it.

**Interfaces:**
- All previous task interfaces remain unchanged. This task consumes the complete branch and approved design spec.

- [ ] **Step 1: Fresh clean verification.** Run `make clean`, `make all`, `make test`, `make test-qemu`, `python3 tests/qemu_smoke.py --fat32-write-test`, `python3 tests/qemu_smoke.py --file-manager-test`, `python3 tests/qemu_smoke.py --process-self-test`, `python3 tests/qemu_smoke.py --process-fault-test`, `python3 tests/qemu_smoke.py --without-network`, `python3 tests/qemu_smoke.py --without-user-programs`, and `python3 tests/qemu_smoke.py --editor-test`.
- [ ] **Step 2: Binary and hygiene checks.** Run `nm -u build/kernel.elf`, `git diff --check`, and `git status --short --branch`; require no unresolved symbols, clean diff check, and only intended milestone files.
- [ ] **Step 3: Whole-branch review.** Review the complete feature range against the spec, emphasizing shared view state, VFS-only mutation, generic pointer routing, Test/Boot disk boundary, and regression safety. Reproduce and TDD-fix any Critical/Important finding, then rerun the full verification matrix.
- [ ] **Step 4: Commit any narrowly required final fix and record deferred minors.** No empty commit and no unrelated cleanup.

## Cross-Task Contract Check

| Consumer | Required producer contract | Owner |
|---|---|---|
| File Manager app pointer handling | `AppCallbacks::on_mouse` with content-relative `AppMouseEvent` | Task 1 |
| GUI name dialog | `vfs::validate_name(const char*) -> filesystem::Status` | Task 2 |
| Both views and all actions | `FileManagerModel` path/list/selection/view/accessor contract; capacity 128 | Task 3 |
| Toolbar mutation actions | `create_folder`, `create_file`, `rename_selected`, `remove_selected` through VFS | Task 4 |
| Desktop registration/input | `FileManagerApp::instance()` and fixed window activation | Task 6 |
| App actions and dialogs | Dialog modes/name storage/status, model methods, generic pointer callback | Task 7 |
| View renderer/hit testing | shared model entry indices and `ViewMode` | Task 8 |
| QEMU verification | disposable image path + independent persisted-result and Boot-digest checks | Task 9 |
