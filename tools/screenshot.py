#!/usr/bin/env python3
"""screenshot.py - draws niceemu's 80x25 screen as a PNG.

Usage: screenshot.py [--font FONT_ZIG] REPORT OUTPUT.png

REPORT is what niceemu printed at the halt with --dump 0xFFFF0000:2000,
the 2000 words of the screen. (--color is not enough, as it leaves out
the blanks at the end of each row, whatever their color.) The
characters are drawn in the font of niceemu's window, from
emulator/src/font.zig of nice_cpu (by default of the newest
deps/nice_cpu-*), so the picture is what --window
shows. Needs Pillow.
"""

from __future__ import annotations

import argparse
import glob
import re
import sys

from PIL import Image

COLS, ROWS, W, H = 80, 25, 16, 32
SCREEN = 0xFFFF0000


def read_font(path: str) -> list[list[int]]:
    glyphs = []
    for line in open(path):
        if line.lstrip().startswith(".{"):
            glyphs.append([int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4})", line)])
    if len(glyphs) != 256:
        sys.exit(f"screenshot.py: {path} has {len(glyphs)} glyphs, not 256")
    return glyphs


def read_screen(path: str) -> list[int]:
    words: dict[int, int] = {}
    for line in open(path, encoding="latin-1"):
        m = re.match(r"^([0-9a-f]{8}):((?: [0-9a-f]{8})+)$", line.rstrip("\n"))
        if m:
            base = int(m.group(1), 16)
            for k, w in enumerate(m.group(2).split()):
                words[base + k] = int(w, 16)
    if SCREEN + COLS * ROWS - 1 not in words:
        sys.exit(f"screenshot.py: no dump of the screen in {path}")
    return [words[SCREEN + i] for i in range(COLS * ROWS)]


def rgb(c: int) -> tuple[int, int, int]:
    return ((c >> 8 & 15) * 17, (c >> 4 & 15) * 17, (c & 15) * 17)


def main() -> None:
    ap = argparse.ArgumentParser(description="Draws niceemu's screen as a PNG.")
    ap.add_argument("report")
    ap.add_argument("png")
    fonts = sorted(glob.glob("deps/nice_cpu-*/emulator/src/font.zig"))
    ap.add_argument("--font", default=fonts[-1] if fonts else None, required=not fonts)
    args = ap.parse_args()

    glyphs = read_font(args.font)
    img = Image.new("RGB", (COLS * W, ROWS * H))
    px = img.load()
    for i, word in enumerate(read_screen(args.report)):
        r, c = divmod(i, COLS)
        bg, fg = rgb(word >> 20), rgb(word >> 8)
        g = glyphs[word & 0xFF]
        for y in range(H):
            for x in range(W):
                px[c * W + x, r * H + y] = fg if g[y] >> (15 - x) & 1 else bg
    img.save(args.png)


if __name__ == "__main__":
    main()
