"""Checks for locating the Linux95 pointer in a QEMU PPM screenshot."""

import unittest

import qemu_cursor
from qemu_cursor import locate_cursor, movement_steps


class CursorScreenshotTests(unittest.TestCase):
    def test_waits_for_cursor_when_desktop_marker_precedes_finished_draw(self):
        width, height = 64, 48
        blank = f"P6\n{width} {height}\n255\n".encode() + bytes((12, 16, 22)) * width * height
        pixels = bytearray(bytes((12, 16, 22)) * width * height)
        for row in range(18):
            offset = ((12 + row) * width + 25) * 3
            pixels[offset:offset + 3] = bytes((255, 255, 255))
        for row in range(1, 13):
            offset = ((12 + row) * width + 26) * 3
            pixels[offset:offset + 3] = bytes((0, 0, 0))
        for dx, dy, color in (
                (11, 10, (255, 255, 255)), (10, 10, (0, 0, 0)),
                (6, 5, (255, 255, 255)), (5, 5, (0, 0, 0)),
                (4, 13, (255, 255, 255))):
            offset = ((12 + dy) * width + 25 + dx) * 3
            pixels[offset:offset + 3] = bytes(color)
        drawn = f"P6\n{width} {height}\n255\n".encode() + pixels
        frames = iter((blank, drawn))
        self.assertEqual(qemu_cursor.wait_for_cursor(lambda: next(frames), timeout=1), (25, 12))

    def test_relative_qmp_steps_use_screen_y_direction_and_reach_target(self):
        steps = movement_steps((640, 360), (60, 14))
        self.assertTrue(steps)
        self.assertTrue(all(abs(dx) <= 40 and abs(dy) <= 40 for dx, dy in steps))
        self.assertEqual(sum(dx for dx, _ in steps), -580)
        self.assertEqual(sum(dy for _, dy in steps), -346)

    def test_locates_drawn_pointer_without_confusing_plain_background(self):
        width, height = 64, 48
        pixels = bytearray(bytes((12, 16, 22)) * width * height)
        x, y = 25, 12

        def pixel(px, py, color):
            offset = (py * width + px) * 3
            pixels[offset:offset + 3] = color

        for row in range(18):
            pixel(x, y + row, bytes((255, 255, 255)))
        for row in range(1, 13):
            pixel(x + 1, y + row, bytes((0, 0, 0)))
        pixel(x + 11, y + 10, bytes((255, 255, 255)))
        pixel(x + 10, y + 10, bytes((0, 0, 0)))
        pixel(x + 6, y + 5, bytes((255, 255, 255)))
        pixel(x + 5, y + 5, bytes((0, 0, 0)))
        pixel(x + 4, y + 13, bytes((255, 255, 255)))
        image = f"P6\n{width} {height}\n255\n".encode() + pixels

        self.assertEqual(locate_cursor(image), (x, y))

    def test_expected_location_disambiguates_repeated_cursor_like_pixels(self):
        width, height = 64, 48
        pixels = bytearray(bytes((12, 16, 22)) * width * height)
        x, y = 25, 12

        def pixel(px, py, color):
            offset = (py * width + px) * 3
            pixels[offset:offset + 3] = bytes(color)

        for cursor_x in (5, x):
            for row in range(18):
                pixel(cursor_x, y + row, (255, 255, 255))
            for row in range(1, 13):
                pixel(cursor_x + 1, y + row, (0, 0, 0))
            for dx, dy, color in (
                    (11, 10, (255, 255, 255)), (10, 10, (0, 0, 0)),
                    (6, 5, (255, 255, 255)), (5, 5, (0, 0, 0)),
                    (4, 13, (255, 255, 255))):
                pixel(cursor_x + dx, y + dy, color)
        image = f"P6\n{width} {height}\n255\n".encode() + pixels

        with self.assertRaisesRegex(ValueError, "found 2"):
            locate_cursor(image)
        self.assertEqual(locate_cursor(image, expected=(x, y)), (x, y))

    def test_long_window_edge_is_not_a_mouse_pointer(self):
        width, height = 64, 48
        pixels = bytearray(bytes((12, 16, 22)) * width * height)
        for row in range(4, 42):
            white = (row * width + 5) * 3
            black = (row * width + 6) * 3
            pixels[white:white + 3] = bytes((255, 255, 255))
            pixels[black:black + 3] = bytes((0, 0, 0))
        image = f"P6\n{width} {height}\n255\n".encode() + pixels
        with self.assertRaisesRegex(ValueError, "found 0"):
            locate_cursor(image)


if __name__ == "__main__":
    unittest.main()
