#!/usr/bin/env python3
"""screenshot.py - draws niceemu's 80x25 screen as a PNG.

Usage: screenshot.py REPORT OUTPUT.png

REPORT is what niceemu printed at the halt with
    -d 0xFFFF0000:2000 -d 0xFFFFFF86:1 -d 0xFFFE0000:16384
the 2000 words of the screen, the font base, and the font RAM and the
character ROM (nice_cpu's SYSTEM.md §1.8), or what board.py --dump wrote.
(--color is not enough, as it leaves out the blanks at the end of each
row, whatever their color.) The characters are drawn in the font that
the base selects, so the picture is what --window shows. Needs Pillow.
"""

from __future__ import annotations

import argparse
import re
import sys

from PIL import Image

COLS, ROWS, W, H = 80, 25, 16, 32
SCREEN = 0xFFFF0000
FONT_RAM, FONT_ROM, FONT_BASE = 0xFFFE0000, 0xFFFE2000, 0xFFFFFF86


def read_dump(path: str) -> dict[int, int]:
    words: dict[int, int] = {}
    for line in open(path, encoding="latin-1"):
        m = re.match(r"^([0-9a-f]{8}):((?: [0-9a-f]{8})+)$", line.rstrip("\n"))
        if m:
            base = int(m.group(1), 16)
            for k, w in enumerate(m.group(2).split()):
                words[base + k] = int(w, 16)
    return words


def get(words: dict[int, int], addr: int, n: int, what: str, path: str) -> list[int]:
    if any(addr + i not in words for i in range(n)):
        sys.exit(f"screenshot.py: no dump of {what} in {path}")
    return [words[addr + i] for i in range(n)]


def rgb(c: int) -> tuple[int, int, int]:
    return ((c >> 8 & 15) * 17, (c >> 4 & 15) * 17, (c & 15) * 17)


def main() -> None:
    ap = argparse.ArgumentParser(description="Draws niceemu's screen as a PNG.")
    ap.add_argument("report")
    ap.add_argument("png")
    args = ap.parse_args()

    words = read_dump(args.report)
    screen = get(words, SCREEN, COLS * ROWS, "the screen", args.report)
    base = get(words, FONT_BASE, 1, "the font base", args.report)[0]
    # 0xFFFE0000 shows the font RAM; 0 and the reserved values the ROM.
    addr, what = (FONT_RAM, "the font RAM") if base == FONT_RAM else (FONT_ROM, "the character ROM")
    font = get(words, addr, 256 * 32, what, args.report)
    glyphs = [font[32 * c:32 * c + 32] for c in range(256)]
    img = Image.new("RGB", (COLS * W, ROWS * H))
    px = img.load()
    for i, word in enumerate(screen):
        r, c = divmod(i, COLS)
        bg, fg = rgb(word >> 20), rgb(word >> 8)
        g = glyphs[word & 0xFF]
        for y in range(H):
            for x in range(W):
                px[c * W + x, r * H + y] = fg if g[y] >> (15 - x) & 1 else bg
    img.save(args.png)


if __name__ == "__main__":
    main()
