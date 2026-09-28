"""Pixel-level assertions for text rendered by the Linux95 File Manager."""

from functools import lru_cache
from pathlib import Path
import re
import time


@lru_cache(maxsize=1)
def _font_rows():
    source = Path(__file__).resolve().parents[1] / "kernel/graphics/renderer.cpp"
    contents = source.read_text()
    start = contents.index("const uint8_t kFont8x8[96][8] = {")
    end = contents.index("\n};", start)
    glyphs = re.findall(r"\{\s*((?:0x[0-9A-Fa-f]{2}\s*,?\s*){8})\}",
                        contents[start:end])
    if len(glyphs) != 96:
        raise RuntimeError("Linux95 8x8 font table format changed")
    rows = []
    for glyph in glyphs:
        values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", glyph)]
        if len(values) != 8:
            raise RuntimeError("Linux95 8x8 glyph format changed")
        rows.append(values)
    return rows


def _parse_ppm(ppm):
    try:
        magic, dimensions, maximum, pixels = ppm.split(b"\n", 3)
        width, height = map(int, dimensions.split())
    except (ValueError, TypeError) as error:
        raise ValueError("invalid QEMU PPM screenshot") from error
    if (magic != b"P6" or maximum != b"255" or width <= 0 or height <= 0 or
            len(pixels) != width * height * 3):
        raise ValueError("invalid QEMU PPM screenshot")
    return width, height, pixels


def contains_text(ppm, text, foreground=None, background=None, region=None):
    """Match complete Linux95 8x8 glyph cells, including their backgrounds."""
    width, height, pixels = _parse_ppm(ppm)
    if not text or any(ord(character) < 0x20 or ord(character) > 0x7E
                       for character in text):
        return False
    if len(text) * 8 > width:
        return False
    if region is None:
        left, top, right, bottom = 0, 0, width, height
    else:
        left, top, right, bottom = region
        if (left < 0 or top < 0 or right > width or bottom > height or
                right <= left or bottom <= top):
            raise ValueError("text-search region is outside screenshot bounds")
    if right - left < len(text) * 8 or bottom - top < 8:
        return False
    glyphs = _font_rows()
    if foreground is None and background is None:
        color_pairs = (
            ((0, 0, 0), (192, 192, 192)),  # status bar
            ((0, 0, 0), (224, 224, 224)),  # path row
            ((0, 0, 0), (255, 255, 255)),  # unselected entries
            ((255, 255, 255), (0, 0, 128)),  # dialog/details selection
            ((255, 255, 255), (0, 0, 192)),  # icon selection
        )
    else:
        color_pairs = ((foreground, background),)
    stride = width * 3
    for foreground_color, background_color in color_pairs:
        foreground_bytes = bytes(foreground_color)
        background_bytes = bytes(background_color)
        for y in range(top, bottom - 7):
            for x in range(left, right - len(text) * 8 + 1):
                matched = True
                for index, character in enumerate(text):
                    glyph = glyphs[ord(character) - 0x20]
                    glyph_x = x + index * 8
                    for row in range(8):
                        for column in range(8):
                            expected_color = (foreground_bytes
                                if glyph[row] & (0x80 >> column)
                                else background_bytes)
                            offset = ((y + row) * stride +
                                      (glyph_x + column) * 3)
                            if pixels[offset:offset + 3] != expected_color:
                                matched = False
                                break
                        if not matched:
                            break
                    if not matched:
                        break
                if matched:
                    return True
    return False


def wait_for_text(capture, text, present=True, timeout=3.0, stable_frames=2,
                  foreground=None, background=None, region=None):
    """Wait for a UI text state to be visible (or gone) across stable frames."""
    deadline = time.monotonic() + timeout
    stable = 0
    while True:
        visible = contains_text(capture(), text, foreground, background, region)
        if visible == present:
            stable += 1
            if stable >= stable_frames:
                return True
        else:
            stable = 0
        if time.monotonic() >= deadline:
            return False
        time.sleep(0.1)


def render_text_fixture(width, height, text):
    """Build a deterministic PPM test image using the same glyph table."""
    if width <= 0 or height <= 0 or len(text) * 8 > width:
        raise ValueError("text does not fit fixture dimensions")
    pixels = bytearray(bytes((192, 192, 192)) * width * height)
    stride = width * 3
    glyphs = _font_rows()
    for index, character in enumerate(text):
        glyph = glyphs[ord(character) - 0x20]
        for row in range(8):
            for column in range(8):
                if glyph[row] & (0x80 >> column):
                    offset = (8 + row) * stride + (index * 8 + column) * 3
                    pixels[offset:offset + 3] = b"\x00\x00\x00"
    header = f"P6\n{width} {height}\n255\n".encode()
    return header + pixels
