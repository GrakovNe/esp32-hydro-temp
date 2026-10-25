#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
"""Generate main/glyphs.h: a bold orthogonal font and the three faces.

    tools/gen_glyphs.py                 # rewrite main/glyphs.h
    tools/gen_glyphs.py --preview out.png "M 22.1 happy" "P 33.1 sad"

Digits are seven-segment, letters are built from the same horizontal
and vertical bars; the few letters that need a diagonal get a
staircase of stroke-sized blocks, so nothing is ever slanted. Cell is
18x24 with a 4 px stroke. Faces are 20x20 smileys drawn from circles.
"""
import os
import struct
import sys
import zlib

W, H, S = 18, 24, 4
MID = (H - S) // 2            # top row of the middle bar
FACE = 20

# Bars: a top, g middle, d bottom; f/e left upper/lower, b/c right
# upper/lower, i/l centre upper/lower. Halves a1/a2 etc. split at the
# centre stroke. Diagonals h j k m run from a corner to the centre.
CX = (W - S) // 2
SEGMENTS = {
    "a":  ("rect", 0, 0, W, S),
    "a1": ("rect", 0, 0, CX + S, S),
    "a2": ("rect", CX, 0, W - CX, S),
    "g":  ("rect", 0, MID, W, S),
    "g1": ("rect", 0, MID, CX + S, S),
    "g2": ("rect", CX, MID, W - CX, S),
    "d":  ("rect", 0, H - S, W, S),
    "f":  ("rect", 0, 0, S, MID + S),
    "e":  ("rect", 0, MID, S, H - MID),
    "b":  ("rect", W - S, 0, S, MID + S),
    "c":  ("rect", W - S, MID, S, H - MID),
    "i":  ("rect", CX, 0, S, MID + S),
    "l":  ("rect", CX, MID, S, H - MID),
    "h":  ("stair", 0, 0, CX, MID),
    "j":  ("stair", W - S, 0, CX, MID),
    "k":  ("stair", 0, H - S, CX, MID),
    "m":  ("stair", W - S, H - S, CX, MID),
}

GLYPHS = {
    "0": "a b c d e f",
    "1": "b c hook",
    "2": "a b g e d",
    "3": "a b c d g",
    "4": "f g b c",
    "5": "a f g c d",
    "6": "a f g e c d",
    "7": "a b c",
    "8": "a b c d e f g",
    "9": "a b c d f g",
    "?": "a b g2 l dot",
    "A": "a b c e f g",
    "B": "a b c d g2 i l",
    "C": "a d e f",
    "D": "a b c d i l",
    "E": "a d e f g1",
    "F": "a e f g1",
    "G": "a c d e f g2",
    "H": "b c e f g",
    "I": "a d i l",
    "J": "b c d e",
    "K": "e f g1 j m",
    "L": "d e f",
    "M": "b c e f h j",
    "N": "b c e f h m",
    "O": "a b c d e f",
    "P": "a b e f g",
    "Q": "a b c d e f m",
    "R": "a b e f g m",
    "S": "a c d f g",
    "T": "a i l",
    "U": "b c d e f",
    "V": "e f j k",
    "W": "b c e f k m",
    "X": "h j k m",
    "Y": "h j l",
    "Z": "a d j k",
}

# Glyphs narrower than a cell: width and blocks (x, y, w, h).
NARROW = {
    ".": (S, [(0, H - S, S, S)]),
    "-": (10, [(0, MID, 10, S)]),
    " ": (6, []),
}

# Extra pieces used by name in GLYPHS.
EXTRA = {
    "hook": ("rect", W - 2 * S, 0, 2 * S, S),       # the flag of "1", two strokes wide
    "dot":  ("rect", CX, H - S, S, S),              # the point of "?"
}


def blank(w, h):
    return [[0] * w for _ in range(h)]


def fill(px, x, y, w, h):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if 0 <= yy < len(px) and 0 <= xx < len(px[0]):
                px[yy][xx] = 1


def stair(px, x0, y0, x1, y1):
    """Stroke-sized blocks stepping from (x0, y0) to (x1, y1)."""
    steps = max(abs(x1 - x0), abs(y1 - y0)) // S
    for t in range(steps + 1):
        x = x0 + round((x1 - x0) * t / steps / S) * S
        y = y0 + round((y1 - y0) * t / steps / S) * S
        fill(px, x, y, S, S)


def render_glyph(ch):
    if ch in NARROW:
        w, blocks = NARROW[ch]
        px = blank(w, H)
        for b in blocks:
            fill(px, *b)
        return px
    px = blank(W, H)
    for name in GLYPHS[ch].split():
        kind, *args = SEGMENTS.get(name) or EXTRA[name]
        if kind == "rect":
            fill(px, *args)
        else:
            stair(px, *args)
    return px


def ring(px, cx, cy, r_out, r_in):
    for y in range(len(px)):
        for x in range(len(px[0])):
            d2 = (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2
            if r_in * r_in <= d2 <= r_out * r_out:
                px[y][x] = 1


def arc(px, cx, cy, r, thick, lower):
    """Mouth: part of a ring below (smile) or above (frown) its centre."""
    for y in range(len(px)):
        for x in range(len(px[0])):
            d2 = (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2
            inside = (r - thick) ** 2 <= d2 <= r * r
            dy = y + .5 - cy
            if inside and ((lower and dy >= r * .35) or (not lower and dy <= -r * .35)):
                px[y][x] = 1


def render_face(name):
    px = blank(FACE, FACE)
    c = FACE / 2
    ring(px, c, c, 10, 8)
    if name == "happy":
        fill(px, 5, 6, 3, 3)
        fill(px, 12, 6, 3, 3)
        arc(px, c, c - 1, 6.5, 2, lower=True)
    elif name == "sad":
        fill(px, 5, 6, 3, 3)
        fill(px, 12, 6, 3, 3)
        arc(px, c, c + 6.5, 6.5, 2, lower=False)
    else:
        fill(px, 5, 7, 3, 2)
        fill(px, 12, 7, 3, 2)
        fill(px, 6, 13, 8, 2)
    return px


# Screen layout. Emitted into glyphs.h, so display.c and the preview agree.
SCREEN_W, SCREEN_H = 128, 64
ROW_H = 32
BLOCK_L, BLOCK_R = 21, 107    # the temperature block is the middle two thirds
GAP = 3
VALUE_MAX_W = BLOCK_R - BLOCK_L - 4
LAYOUT = {"SCREEN_W": SCREEN_W, "SCREEN_H": SCREEN_H, "ROW_H": ROW_H,
          "BLOCK_L": BLOCK_L, "BLOCK_R": BLOCK_R, "GAP": GAP,
          "VALUE_MAX_W": VALUE_MAX_W}


def pack(px):
    w = len(px[0])
    stride = (w + 7) // 8
    out = []
    for row in px:
        for bx in range(stride):
            byte = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < w and row[x]:
                    byte |= 0x80 >> bit
            out.append(byte)
    return out


def c_array(name, data, per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + per_line]) + ",")
    return "static const uint8_t %s[] = {\n%s\n};\n" % (name, "\n".join(lines))


def ident(ch):
    if ch.isalnum():
        return "g_" + ch.lower()
    return "g_" + {".": "dot", "-": "minus", "?": "qm", " ": "space"}[ch]


def write_header(path):
    chars = sorted(set(GLYPHS) | set(NARROW))
    out = ["/* generated by tools/gen_glyphs.py - do not edit */",
           "#pragma once", "", "#include <stdint.h>", "",
           "#define GLYPH_H %d" % H, "#define FACE_W %d" % FACE,
           "#define FACE_H %d" % FACE, ""]
    out += ["#define LAYOUT_%s %d" % kv for kv in LAYOUT.items()]
    out += ["",
           "typedef struct {", "    char    ch;", "    uint8_t w;",
           "    const uint8_t *bits; /* row-major, MSB first, rows padded to bytes */",
           "} glyph_t;", ""]
    for ch in chars:
        out.append(c_array(ident(ch), pack(render_glyph(ch))))
    out.append("static const glyph_t GLYPHS[] = {")
    for ch in chars:
        out.append("    { '%s', %d, %s }," % (ch, len(render_glyph(ch)[0]), ident(ch)))
    out.append("};")
    out.append("")
    for name in ("happy", "sad", "none"):
        out.append(c_array("face_" + name, pack(render_face(name))))
    with open(path, "w") as f:
        f.write("\n".join(out))


def blit(screen, px, x0, y0):
    for y, row in enumerate(px):
        for x, v in enumerate(row):
            if v and 0 <= x0 + x < SCREEN_W and 0 <= y0 + y < SCREEN_H:
                screen[y0 + y][x0 + x] = 1


def text_width(text):
    return sum(len(render_glyph(c)[0]) + GAP for c in text) - GAP


def draw_text(screen, text, x, y):
    for ch in text:
        px = render_glyph(ch)
        blit(screen, px, x, y)
        x += len(px[0]) + GAP


def draw_row(screen, letter, value, face, row):
    y = row * ROW_H + (ROW_H - H) // 2
    blit(screen, render_glyph(letter), (BLOCK_L - W) // 2, y)
    if text_width(value) > VALUE_MAX_W:          # same fallback as display.c
        value = value.split(".")[0]
    draw_text(screen, value, SCREEN_W // 2 - text_width(value) // 2, y)
    blit(screen, render_face(face), BLOCK_R + (SCREEN_W - BLOCK_R - FACE) // 2,
         row * ROW_H + (ROW_H - FACE) // 2)


def png(path, screen, scale=4):
    w, h = SCREEN_W * scale, SCREEN_H * scale
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            on = screen[y // scale][x // scale]
            raw += bytes((110, 200, 255) if on else (10, 12, 20))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def preview(path, rows):
    screen = blank(SCREEN_W, SCREEN_H)
    for row, spec in enumerate(rows[:SCREEN_H // ROW_H]):
        letter, value, face = spec.split()
        draw_row(screen, letter, value, face, row)
    png(path, screen)


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if len(argv) >= 2 and argv[1] == "--preview":
        preview(argv[2], argv[3:])
        return
    write_header(os.path.join(root, "main", "glyphs.h"))


if __name__ == "__main__":
    main(sys.argv)
