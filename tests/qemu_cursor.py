"""Small, dependency-free checks for Linux95's QEMU mouse test."""

import time


def movement_steps(origin, target, maximum=40):
    """Split a relative QMP move into small screen-coordinate deltas."""
    dx = target[0] - origin[0]
    dy = target[1] - origin[1]
    steps = []
    while dx or dy:
        step_x = max(-maximum, min(maximum, dx))
        step_y = max(-maximum, min(maximum, dy))
        steps.append((step_x, step_y))
        dx -= step_x
        dy -= step_y
    return steps


def locate_cursor(ppm, expected=None):
    """Find the Linux95 cursor, optionally at a known QMP screen position."""
    try:
        magic, dimensions, maximum, pixels = ppm.split(b"\n", 3)
        width, height = map(int, dimensions.split())
    except (ValueError, TypeError) as error:
        raise ValueError("invalid QEMU PPM screenshot") from error
    if magic != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
        raise ValueError("invalid QEMU PPM screenshot")

    white = b"\xff\xff\xff"
    black = b"\x00\x00\x00"
    stride = width * 3
    found = []
    if expected is None:
        ys = range(height - 17)
        xs = range(width - 11)
    else:
        expected_x, expected_y = expected
        if expected_x < 0 or expected_y < 0 or expected_x >= width - 11 or expected_y >= height - 17:
            raise ValueError("expected cursor position is outside screenshot bounds")
        ys = (expected_y,)
        xs = (expected_x,)
    for y in ys:
        for x in xs:
            top = y * stride + x * 3
            if pixels[top:top + 3] != white:
                continue
            if all(pixels[top + row * stride:top + row * stride + 3] == white
                   for row in range(18)) and all(
                       pixels[top + row * stride + 3:top + row * stride + 6] == black
                       for row in range(1, 13)) and (
                       pixels[top + 10 * stride + 11 * 3:top + 10 * stride + 12 * 3] == white
                       and pixels[top + 10 * stride + 10 * 3:top + 10 * stride + 11 * 3] == black
                       and pixels[top + 5 * stride + 6 * 3:top + 5 * stride + 7 * 3] == white
                       and pixels[top + 5 * stride + 5 * 3:top + 5 * stride + 6 * 3] == black
                       and pixels[top + 13 * stride + 4 * 3:top + 13 * stride + 5 * 3] == white):
                found.append((x, y))
    if len(found) != 1:
        raise ValueError(f"expected one Linux95 cursor, found {len(found)}")
    return found[0]


def wait_for_cursor(capture, timeout=60.0, expected=None):
    """Wait until the desktop has finished drawing its pointer overlay."""
    deadline = time.monotonic() + timeout
    while True:
        try:
            return locate_cursor(capture(), expected=expected)
        except ValueError as error:
            if str(error) != "expected one Linux95 cursor, found 0":
                raise
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.5)
