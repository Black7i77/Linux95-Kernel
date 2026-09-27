# Linux95 File Manager v1 — Toolbar File Operations

**Status:** Proposed for human review; no implementation is authorized until approved.

## 1. Goal and user-visible behavior

Linux95 v1.0-dev has a graphical desktop, Terminal, and System Info applications, but no graphical File Manager. This milestone adds a compact File Manager window that lists the mounted Test/slave FAT32 volume and supports creating folders/files, renaming entries, deleting files or empty folders, and switching between Icon and Details views.

The toolbar is approximately:

`[ New Folder ] [ New File ] [ Rename ] [ Delete ] [ View ]`

The two views are presentations of one File Manager model/controller. They share the same current directory, listing, and single selected entry. Opening a directory navigates into it; a parent control navigates upward. Operations refresh the current listing and preserve or update selection as described below.

## 2. Current architecture and constraints

The approved source baseline is v1.0-dev commit `9d843e9877ef40bea820fba0b50cbc0d91c491e9`. There is no File Manager application or dialog framework in this baseline. The desktop currently registers Terminal and System Info in a fixed-capacity `WindowManager`; app callbacks include drawing, keyboard input, and close notification. The desktop routes mouse input for window chrome, focus, dragging, and resizing, but does not deliver pointer events to apps.

Keyboard delivery already uses `keyboard::KeyEvent`; this milestone must preserve it. GUI drawing uses the existing framebuffer renderer and compact retro window conventions. The desktop's window table has a fixed maximum of eight windows.

The VFS provides fixed-capacity paths (`vfs::kPathCapacity`, 128 bytes), `DirectoryEntry` records with 8.3 display names, `stat`, `opendir`, `readdir`, and `closedir`, plus `touch`, `mkdir`, `move`, and `remove`. Directory iteration is handle-based and reports errors separately from end-of-directory. The GUI must close every successfully opened directory handle, including on read errors.

Writable filesystem operations already target the Test/slave volume through the public filesystem/VFS path. `storage::write_sector` independently denies writes to the Boot/master disk. Existing FAT32 rules allow moving files across directories, but directory moves are supported only as same-parent renames; non-empty directory removal is refused. The File Manager must not bypass those rules or access FAT32 internals.

## 3. Scope

The milestone includes:

- A File Manager application/window, discoverable through the existing Applications UI and represented in the existing window/task UI.
- A current-directory model and bounded directory listing populated through VFS.
- One shared selected-entry identity and one controller for both views.
- Icon View, Details View, and a view toggle.
- Basic child-directory and parent-directory navigation.
- Toolbar actions: New Folder, New File, Rename, Delete, and View.
- Small File Manager-owned name and delete-confirmation dialogs.
- Shared public 8.3 name validation exposed through the filesystem/VFS API; GUI code does not include or call FAT32 helper internals.
- Host tests and a QEMU integration/persistence proof using a disposable Test image, while proving the Boot image remains unchanged.

No content editor is launched when a file is created.

## 4. Explicit non-goals

V1 does not add right-click menus, drag and drop, recursive deletion, recursive copy, clipboard or GUI copy/paste, multiple selection, cross-parent directory moves, cross-volume moves, long filenames, a general-purpose desktop dialog framework, oversized modern controls, or unrelated desktop redesign. It does not add a second filesystem implementation or GUI-specific disk access.

## 5. Application and controller architecture

Add a File Manager app/model component following the existing `AppInstance` and `AppCallbacks` pattern. The File Manager owns:

- A bounded absolute current-directory path rooted at the Test volume root.
- A fixed array of exactly 128 displayed VFS entries, including name, directory flag, and size.
- A selected-entry identity represented by its case-insensitive 8.3 name, not a view-specific icon or row index.
- Current view mode (`Icons` or `Details`).
- Navigation, dialog, confirmation, and transient status state.

Both views render the same entry array and selection. Click, keyboard activation, toolbar commands, and view switching operate through the same controller methods. Filesystem operations are each implemented once in that controller and call only the public VFS API. The renderer contains no mutation logic.

The initial view shows the volume root. Child navigation joins the selected directory name to the current absolute path. Parent navigation is computed locally by removing the final path component (at root it stays at root); it does not send `..` as a VFS path component because FAT path validation rejects dot components.

Directory loading opens the current path with `vfs::opendir`, reads until end or the 128-entry model capacity, and always closes the handle. A VFS error leaves the prior directory/model intact and reports the error. If the 128th entry is loaded and the next read shows more entries, the app reports that the listing is truncated; it must not imply the omitted entries do not exist or allow destructive operations on an unlisted item. Selecting entries is limited to entries actually present in the model. A successful operation reloads the listing.

On navigation or refresh, selection is preserved by case-insensitive name if that entry remains present. Successful create selects the created entry; successful rename selects the new name; successful delete clears selection. View toggling does not reload or mutate the model and preserves both current path and selection.

## 6. Window registration and app mouse-event seam

Add the File Manager as an ordinary desktop app/window using a new fixed window ID, title, default bounds, and Applications-menu entry. Respect `WindowManager::kMaxWindows`; opening an already registered File Manager activates its existing window rather than creating duplicates. Closing/minimizing/focusing it follows existing desktop behavior.

Add a general optional app mouse callback to `gui::AppCallbacks` using a GUI-level event with content-relative x/y coordinates and primary-button pressed, released, and down state. Desktop remains responsible for window chrome, focus, drag/resize, and panel handling. It routes pointer events only inside the focused app's content rectangle; events over chrome/panel are not delivered to the app. No File Manager-specific click handling is embedded in desktop code. Existing apps with a null mouse callback remain unchanged.

File Manager toolbar and dialog buttons are compact, visibly bounded controls with pressed/hover feedback only where existing redraw mechanisms make it practical. Keyboard bindings are fixed as follows: Up/Down move the listing selection; Enter activates a selected directory; Backspace navigates to the parent; Ctrl+N opens New Folder; Ctrl+F opens New File; Ctrl+R opens Rename; Delete opens the same confirmation as the Delete toolbar button; Ctrl+V toggles views. In a name dialog, printable characters edit the bounded name, Backspace deletes one character, Enter confirms, and Escape cancels. In the delete confirmation, Enter confirms and Escape cancels. Add Escape and Delete key codes to structured PS/2 input decoding if absent. Modifier and key-release events do not trigger commands. No general dialog service is introduced.

## 7. Shared short-name validation

The existing shared FAT32 8.3 validation/encoding rules live in FAT32 helper internals and are not an appropriate GUI dependency. Expose `filesystem::Status vfs::validate_name(const char* name)` for a single user-entered short-name component. It delegates to the existing canonical FAT32 validator; it does not duplicate or broaden rules and performs no disk mutation. `Status::Ok` means valid; invalid text returns the existing `InvalidName` status. This validation is independent of mount state because it validates syntax only.

The validation contract follows existing v1 FAT32 behavior: classic 8.3 only, ASCII case-insensitive identity, lower/mixed/uppercase input accepted and canonicalized by the filesystem, no path separators in a name, and `.`/`..` components rejected. A full destination path is formed only after name validation and checked against `vfs::kPathCapacity`, including its terminating NUL. Invalid or overlong paths fail before any mutation.

## 8. Toolbar operations and dialog behavior

### New Folder

Open a File Manager-owned name dialog. On confirmation, validate the single name, join it to the current directory, and call `vfs::mkdir`. Never overwrite. On success refresh and select the new directory. On failure keep the dialog or show its error without changing the listing; display a useful status for `AlreadyExists`, `InvalidName`, `NoSpace`, `ReadOnly`, `NotMounted`, I/O, and other filesystem failures.

### New File

Use the same name dialog and validation path, then call `vfs::touch` on the joined path. Duplicate names are rejected by the filesystem and never overwritten. On success refresh and select the new empty file. Do not launch Terminal Editor automatically.

### Rename

With no selection, perform no filesystem operation and show feedback where the app can display it. Otherwise open a name dialog initialized to the selected entry's current display name. Validate the new component and call `vfs::move(old_path, new_path)` with both paths in the same current directory. An identical source/destination is treated as a destination collision and must not be presented as a successful rename. Existing destinations are never overwritten. Files may be renamed; directories may be renamed only within this same parent, as already supported. The UI exposes no cross-parent directory move. On success refresh and select the renamed entry.

### Delete

With no selection, do nothing destructive and display feedback. Otherwise always show a confirmation dialog containing the selected name and explicit Delete/Cancel choices. Cancel performs no VFS call. Delete calls `vfs::remove` once. Regular files and empty directories can be removed; the VFS/FAT32 layer refuses non-empty directories. The app never retries recursively and never directly traverses or deletes directory contents. On success refresh and clear selection; on failure retain selection and show a filesystem-derived message.

All actions target the Test/slave volume implicitly through the existing VFS. There is no volume chooser and no Boot/master mutation path.

## 9. Listing, selection, and view semantics

Icon View presents directory and file icons with compact names; Details View presents the same model entries in rows with type and size. The selected entry is visually distinguishable in both views. Clicking an entry selects it; activating a directory navigates into it; activating a file only selects it in v1. Switching views preserves current directory and selected name if it remains present. Single selection only.

The parent-navigation control is separate from the entries and cannot be renamed or deleted. The VFS listing is the authority for actual children. Error versus end-of-directory is never conflated.

## 10. Status and error mapping

The app reports operation names and useful status text for at least `NotMounted`, `NotFound`, `NotDirectory`, `IsDirectory`, `AlreadyExists`, `InvalidName`, `NoSpace`, `DirectoryNotEmpty`, `ReadOnly`, `IoError`, `Corrupt`, and `Unsupported`. Unknown statuses receive a generic failure message. Errors do not clear or falsify the current selection/model; after an operation whose result may be uncertain, the app may refresh from VFS while retaining an error message. No operation reports success unless the corresponding VFS call returned `Status::Ok`.

## 11. Testing and verification strategy

Implementation must use strict TDD RED → GREEN. Focused host coverage should exercise:

- Public shared 8.3 validation and invalid-name behavior without GUI access to FAT32 internals.
- Directory model loading, handle closure/error propagation, fixed-capacity truncation, root/child/parent navigation, and safe path joining/capacity checks.
- Shared selection and current-directory preservation across view switches and refresh, including selection clearing/preservation rules.
- New file creation, new folder creation, duplicate rejection, and invalid name rejection.
- File rename, same-parent directory rename, and collision rejection.
- File deletion, empty-directory deletion, mandatory confirmation/cancel no-op, and non-empty-directory refusal without recursion.
- Both view presentations using the same model/controller operation path.
- Generic app mouse routing: content pointer events reach the focused app; desktop chrome/panel/drag behavior is not misrouted; null app callbacks are safe.
- Existing desktop, terminal editor, writable FAT32, and VFS behavior remains intact.

QEMU integration must operate only on a disposable copy/fixture of the FAT32 Test/slave disk. It should exercise toolbar/dialog operations through the real GUI input route where deterministic input is supported, then inspect persisted results after QEMU exits without recreating the Test image. At minimum prove create folder, create file, rename, delete/empty-directory behavior, and survival of the resulting expected state. Compare the Boot/master image digest before and after and require it unchanged. If the current harness cannot safely inject deterministic GUI input, document the exact limitation and use the strongest real app/controller host integration plus independent persisted-image verification; do not fake GUI input. Existing QEMU suites remain mandatory.

Final implementation verification includes:

```text
make clean
make all
make test
make test-qemu
python3 tests/qemu_smoke.py --fat32-write-test
python3 tests/qemu_smoke.py --process-self-test
python3 tests/qemu_smoke.py --process-fault-test
python3 tests/qemu_smoke.py --without-network
python3 tests/qemu_smoke.py --without-user-programs
nm -u build/kernel.elf
git diff --check
git status --short --branch
```

Also run any File Manager-specific host/QEMU targets added by implementation. Existing Terminal Editor behavior must remain covered by its current test target/mode. `nm -u` must show no unresolved symbols, and the Boot image must remain unchanged.

## 12. Acceptance criteria

1. Linux95 exposes a compact File Manager window from the desktop Applications UI.
2. The current-directory model is populated only through VFS and supports child/parent navigation.
3. Icon and Details views show the same entries and share one selected-entry model.
4. Toggling views preserves directory and selection.
5. New Folder and New File validate names through the shared public filesystem validation seam, mutate through VFS, refresh, and select the new entry.
6. Rename uses VFS move, rejects collisions, supports files and same-parent directory renames, and does not offer cross-parent directory moves.
7. Delete always confirms, removes files/empty directories through VFS, refuses non-empty directories, and never recurses.
8. A generic app mouse callback routes content input without regressing existing desktop chrome or apps.
9. No GUI code accesses FAT32 structures or writes sectors directly; mutations remain limited to the Test/slave disk and Boot/master remains protected.
10. Host, QEMU persistence, Boot-image-integrity, and existing regression checks pass; no unapproved files or unrelated changes are included.

## 13. Design boundaries

This document authorizes design review only. It does not authorize implementation. The later implementation plan may refine file/task boundaries and test seams, but the window behavior, keyboard bindings, model capacity, public validator contract, VFS-only operations, and safety boundaries above are design decisions rather than unresolved choices.
