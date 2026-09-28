import unittest

from file_manager_screen_checks import contains_text, render_text_fixture


class FileManagerScreenChecksTests(unittest.TestCase):
    def test_finds_linux95_font_text_in_a_ppm_frame(self):
        frame = render_text_fixture(240, 32, "delete: directory is not empty")
        self.assertTrue(contains_text(frame, "delete: directory is not empty"))
        self.assertFalse(contains_text(frame, "directory was deleted"))

    def test_optional_region_limits_search_to_the_expected_ui_area(self):
        frame = render_text_fixture(240, 32, "OK")
        self.assertTrue(contains_text(frame, "OK", region=(0, 8, 240, 16)))
        self.assertFalse(contains_text(frame, "OK", region=(0, 0, 240, 8)))

    def test_rejects_invalid_or_nonpositive_ppm_dimensions(self):
        with self.assertRaisesRegex(ValueError, "invalid QEMU PPM"):
            contains_text(b"P6\n0 16\n255\n", "x")
        with self.assertRaisesRegex(ValueError, "invalid QEMU PPM"):
            contains_text(b"P6\n16 -1\n255\n", "x")

    def test_rejects_text_when_a_background_pixel_inside_a_glyph_differs(self):
        frame = bytearray(render_text_fixture(16, 24, "X"))
        header_size = frame.index(b"\n", frame.index(b"255")) + 1
        # The upper-left pixel of the Linux95 X glyph is clear/background.
        frame[header_size + 8 * 16 * 3:header_size + 8 * 16 * 3 + 3] = b"\x00\x00\x00"
        self.assertFalse(contains_text(bytes(frame), "X"))


if __name__ == "__main__":
    unittest.main()
