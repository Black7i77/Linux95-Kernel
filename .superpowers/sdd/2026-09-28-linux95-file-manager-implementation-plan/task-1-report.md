# Task 1 Report: Generic App Mouse Events and Desktop Routing

Base commit: `266142d49cff6609fd6009b22479a62f5755c6a1`

## RED

Added the focused host test and Makefile build rule before production changes, then ran:

```text
$ make build/host-desktop-mouse-routing-test
g++ ... tests/host/desktop_mouse_routing_test.cpp kernel/gui/desktop.cpp kernel/gui/window_manager.cpp ...
tests/host/desktop_mouse_routing_test.cpp:10:10: error: ‘AppMouseEvent’ in namespace ‘linux95::gui’ does not name a type
...
tests/host/desktop_mouse_routing_test.cpp:46:21: error: ‘route_mouse’ is not a member of ‘linux95::desktop’
...
make: *** [Makefile:906: build/host-desktop-mouse-routing-test] Error 1
```

This was the expected RED: the test could not compile because the requested event type, callback field, and router API did not yet exist. It also showed the old three-field callback aggregate could not accept a fourth mouse callback.

## Implementation and GREEN

Added `gui::AppMouseEvent` and optional `AppCallbacks::on_mouse`, plus `desktop::route_mouse`. The router checks the target is focused and open, requires the point to be in the supplied content rectangle, translates screen coordinates to content-relative coordinates, and safely declines a missing callback. Desktop mouse processing calls it after existing panel/menu and chrome hit handling. A window drag or resize in progress retains pointer ownership, so those events are not sent to an app. Terminal and System Info explicitly initialize the new callback to null.

The focused test covers content press/release coordinates and button state, a null callback, points in the title bar and panel, and a non-focused window.

```text
$ make build/host-desktop-mouse-routing-test && ./build/host-desktop-mouse-routing-test
g++ ... tests/host/desktop_mouse_routing_test.cpp kernel/gui/desktop.cpp kernel/gui/window_manager.cpp ...
# exit 0; all assertions passed
```

## Regressions

- `make test-host-graphics`: PASS. The first run exposed `-Werror=missing-field-initializers` in `tests/host/terminal_key_event_test.cpp`; added the explicit null callback and reran successfully.
- `make test`: PASS. This rebuilt and linked the kernel and passed host memory, storage, filesystem, graphics, desktop/key, process/scheduler, networking, DNS, preemption source, QEMU smoke policy, source, image, memory/storage/filesystem source, and relocation checks.
- `git diff --check`: PASS.

## Files changed

- `kernel/gui/app.hpp`
- `kernel/gui/desktop.hpp`
- `kernel/gui/desktop.cpp`
- `kernel/gui/terminal_app.cpp`
- `kernel/gui/system_info_app.cpp`
- `tests/host/desktop_mouse_routing_test.cpp`
- `tests/host/terminal_key_event_test.cpp` (explicit initializer required by warnings-as-errors)
- `Makefile`

## Commit

`250e146` — `Add generic GUI app mouse routing`.

## Concerns

The host test directly exercises the public router and its focus/content checks. The drag/resize suppression is enforced at the desktop event integration point and compiled by the full kernel build, but the private runtime mouse loop is not simulated in a host test.
