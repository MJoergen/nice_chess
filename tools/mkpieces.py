#!/usr/bin/env python3
"""mkpieces.py - draws the chess pieces as glyphs of the font.

Usage: mkpieces.py OUTPUT.h [--preview PNG]

Each piece is 3x3 characters of 16x32 pixels, 48x96 pixels, drawn from
polygons and ellipses at 8 times the size and scaled down. A white
piece is its outline, and a black piece is filled, with the lines
inside it left out; both are drawn in one color on the square's color.
Writes pieces.h for src/ui/screen.c: the rows of the 108 glyphs, white
then black, P N B R Q K, and each piece's cells row by row. --preview
draws them on squares of the board's colors. Needs Pillow.
"""

from __future__ import annotations

import argparse

from PIL import Image, ImageChops, ImageDraw, ImageFilter

W, H = 48, 96       # A piece: 3x3 characters of 16x32 pixels
S = 8               # The scale at which it is drawn
LINE = 2            # The width of an outline, in pixels
GAP = 2             # The width of a line inside a black piece

# The parts of each piece, in pixels of 48x96, drawn in order, so a
# later part covers an earlier one. ("poly", points), ("ellipse", box),
# or ("rect", box). The bottom of each piece is at y = 90.
BASE = [("rect", (5, 82, 43, 90)), ("rect", (9, 76, 39, 82))]

PIECES = {
    "P": BASE + [
        ("poly", [(13, 76), (35, 76), (29, 52), (19, 52)]),
        ("ellipse", (12, 47, 36, 55)),
        ("ellipse", (14, 26, 34, 48)),
    ],
    "N": BASE + [
        ("poly", [(12, 76), (38, 76), (39, 58), (37, 42), (33, 31), (27, 24),
                  (25, 14), (21, 22), (17, 25), (7, 41), (6, 48), (10, 53),
                  (15, 51), (21, 46), (23, 52), (15, 63)]),
        ("ellipse", (17, 31, 22, 36)),
    ],
    "B": BASE + [
        ("poly", [(14, 76), (34, 76), (29, 58), (19, 58)]),
        ("ellipse", (11, 53, 37, 61)),
        ("ellipse", (13, 23, 35, 56)),
        ("poly", [(26, 31), (29, 34), (23, 42), (20, 39)]),
        ("ellipse", (20, 16, 28, 24)),
    ],
    "R": BASE + [
        ("poly", [(12, 76), (36, 76), (34, 36), (14, 36)]),
        ("rect", (9, 30, 39, 37)),
        ("poly", [(9, 12), (16, 12), (16, 19), (21, 19), (21, 12), (27, 12),
                  (27, 19), (32, 19), (32, 12), (39, 12), (39, 30), (9, 30)]),
    ],
    "Q": BASE + [
        ("poly", [(11, 76), (37, 76), (31, 56), (17, 56)]),
        ("ellipse", (10, 51, 38, 59)),
        ("poly", [(13, 53), (5, 20), (15, 36), (19, 15), (24, 34), (29, 15),
                  (33, 36), (43, 20), (35, 53)]),
        ("ellipse", (2, 15, 9, 22)), ("ellipse", (15, 10, 22, 17)),
        ("ellipse", (26, 10, 33, 17)), ("ellipse", (39, 15, 46, 22)),
    ],
    "K": BASE + [
        ("poly", [(11, 76), (37, 76), (31, 56), (17, 56)]),
        ("ellipse", (10, 51, 38, 59)),
        ("poly", [(13, 53), (7, 30), (41, 30), (35, 53)]),
        ("ellipse", (7, 22, 41, 38)),
        ("rect", (21, 3, 27, 24)),
        ("rect", (15, 8, 33, 14)),
    ],
}


def shape(kind: str, arg) -> Image.Image:
    """One part as a mask at S times the size."""
    im = Image.new("1", (W * S, H * S), 0)
    d = ImageDraw.Draw(im)
    if kind == "poly":
        d.polygon([(x * S, y * S) for x, y in arg], fill=1)
    elif kind == "ellipse":
        d.ellipse([v * S for v in arg], fill=1)
    else:
        d.rectangle([v * S for v in arg], fill=1)
    return im


def erode(im: Image.Image, px: int) -> Image.Image:
    """im less a band of px pixels (at S times the size) inside its edge."""
    im = im.convert("L")
    for _ in range(px):
        im = im.filter(ImageFilter.MinFilter(3))
    return im.convert("1")


def down(im: Image.Image) -> Image.Image:
    """Scales a mask down by S: a pixel is set if half of it is."""
    small = im.convert("L").resize((W, H), Image.Resampling.BOX)
    return small.point(lambda v: 255 if v >= 128 else 0).convert("1")


def draw(parts) -> tuple[Image.Image, Image.Image]:
    """The white piece, outlines, and the black piece, filled."""
    lines = Image.new("1", (W * S, H * S), 0)    # The outline of each part
    gaps = Image.new("1", (W * S, H * S), 0)     # Thinner, for the black piece
    whole = Image.new("1", (W * S, H * S), 0)
    for kind, arg in parts:
        m = shape(kind, arg)
        whole = ImageChops.logical_or(whole, m)
        # The part covers what is under it, then draws its edge.
        inner = erode(m, LINE * S)
        lines = ImageChops.logical_and(lines, ImageChops.invert(m))
        lines = ImageChops.logical_or(lines, ImageChops.logical_and(m, ImageChops.invert(inner)))
        thin = erode(m, GAP * S)
        gaps = ImageChops.logical_and(gaps, ImageChops.invert(m))
        gaps = ImageChops.logical_or(gaps, ImageChops.logical_and(m, ImageChops.invert(thin)))
    # A black piece leaves out its lines inside, not its edge.
    inside = erode(whole, (LINE + 1) * S)
    black = ImageChops.logical_and(whole, ImageChops.invert(ImageChops.logical_and(gaps, inside)))
    return down(lines), down(black)


def glyphs(im: Image.Image) -> list[list[int]]:
    """The 9 glyphs of a piece, row by row, each 32 rows of 16 bits."""
    px = im.load()
    out = []
    for cy in range(3):
        for cx in range(3):
            rows = []
            for y in range(32):
                v = 0
                for x in range(16):
                    if px[cx * 16 + x, cy * 32 + y]:
                        v |= 0x8000 >> x
                rows.append(v)
            out.append(rows)
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description="Draws the chess pieces as glyphs.")
    ap.add_argument("output")
    ap.add_argument("--preview")
    args = ap.parse_args()

    drawn = {p: draw(parts) for p, parts in PIECES.items()}
    with open(args.output, "w", encoding="ascii") as f:
        f.write("// The chess pieces as glyphs, from tools/mkpieces.py; do not edit.\n")
        f.write("// White, then black; P N B R Q K; the 9 cells of each row by row;\n")
        f.write("// 32 rows of 16 pixels each, the leftmost in bit 15.\n")
        f.write("static const uint16_t piece_glyphs[2 * 6 * 9][32] = {\n")
        for side in range(2):
            for p in "PNBRQK":
                for i, rows in enumerate(glyphs(drawn[p][side])):
                    f.write(f"    // {'wb'[side]}{p} {i}\n    {{")
                    f.write(", ".join(f"0x{v:04x}" for v in rows))
                    f.write("},\n")
        f.write("};\n")

    if args.preview:
        light, dark, ink = (0xCC, 0xAA, 0x77), (0x88, 0x66, 0x33), (0x11, 0x11, 0x11)
        img = Image.new("RGB", (6 * 80, 4 * 96))
        for side in range(2):
            for i, p in enumerate("PNBRQK"):
                for bg_row in range(2):
                    oy = (2 * side + bg_row) * 96
                    ox = i * 80
                    bg = light if (i + bg_row) % 2 == 0 else dark
                    img.paste(bg, (ox, oy, ox + 80, oy + 96))
                    m = drawn[p][side]
                    for y in range(H):
                        for x in range(W):
                            if m.getpixel((x, y)):
                                img.putpixel((ox + 16 + x, oy + y), ink)
        img.save(args.preview)


if __name__ == "__main__":
    main()
