#!/usr/bin/env python3
"""Generates res/win32/theblackwall.ico: a dark void, a dark floor with a red
glow, and a wall of glowing crimson pixels rolling in a wave. Pure Python,
no image libraries - each size is written as a 32-bit BMP icon entry.

    python tools/make_icon.py
"""
import math
import os
import struct
import zlib

SIZES = [16, 24, 32, 48, 64, 128, 256]
OUT = os.path.join(os.path.dirname(__file__), "..", "res", "win32", "theblackwall.ico")

SPACE = (4, 0, 10)
FLOOR = (20, 5, 7)
WALL = (255, 59, 31)


def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else int(v)


def hash01(a, b):
    h = (a * 374761393 + b * 668265263) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def render(n):
    """Returns rows (top to bottom) of (r, g, b, a) tuples."""
    px = [[None] * n for _ in range(n)]
    horizon = int(n * 0.70)          # where the wall meets the floor
    cell = max(1, round(n / 15))     # wall pixel pitch
    gap = 1 if n >= 32 else 0
    wall_top = int(n * 0.04)

    for y in range(n):
        for x in range(n):
            fy = y / n
            if y >= horizon:
                # floor: dark, with the wall's glow at its foot fading downward
                t = (y - horizon) / max(1, n - horizon)
                glow = math.exp(-t * 4.0)
                r = FLOOR[0] + WALL[0] * 0.45 * glow
                g = FLOOR[1] + WALL[1] * 0.45 * glow
                b = FLOOR[2] + WALL[2] * 0.45 * glow
                px[y][x] = (clamp(r), clamp(g), clamp(b), 255)
            else:
                # deep space, a hint of purple toward the top
                t = 1.0 - fy
                px[y][x] = (clamp(SPACE[0] + 6 * t), clamp(SPACE[1]), clamp(SPACE[2] + 16 * t), 255)

    # the wall: columns of pixels displaced by a wave, bright at the foot,
    # fading into the void upward; the wave's near crest glows hottest
    cols = n // cell
    for c in range(cols + 1):
        cx = c * cell
        u = c / max(1, cols)
        wave = math.sin(u * math.pi * 1.9 - 1.4)     # crest in the middle
        shift = int(round(wave * cell * 1.3))          # vertical displacement hints the wave in z
        brightness = 0.55 + 0.55 * wave                # near crest bright, trough dim
        rows = (horizon - wall_top) // cell
        for rr in range(rows + 3):
            cy = horizon - (rr + 1) * cell + shift
            if cy + cell <= 0 or cy >= horizon:
                continue
            height = (horizon - cy) / max(1, horizon - wall_top)
            fade = math.exp(-height * 2.6)             # endless upward fade
            twinkle = 0.75 + 0.5 * hash01(c, rr)
            k = fade * brightness * twinkle
            if k < 0.02:
                continue
            for yy in range(max(cy, 0), min(cy + cell - gap, horizon)):
                for xx in range(cx, min(cx + cell - gap, n)):
                    base = px[yy][xx]
                    px[yy][xx] = (clamp(base[0] + WALL[0] * k), clamp(base[1] + WALL[1] * k),
                                  clamp(base[2] + WALL[2] * k), 255)
            # faint reflection of the lowest rows in the floor
            if rr < 3:
                ry0 = horizon + rr * cell
                for yy in range(ry0, min(ry0 + cell - gap, n)):
                    for xx in range(cx, min(cx + cell - gap, n)):
                        base = px[yy][xx]
                        kk = k * 0.22 * (1 - rr / 3)
                        px[yy][xx] = (clamp(base[0] + WALL[0] * kk), clamp(base[1] + WALL[1] * kk),
                                      clamp(base[2] + WALL[2] * kk), 255)
    # rounded corners for the large sizes (transparent), keeps small ones square
    if n >= 48:
        rad = n * 0.14
        for y in range(n):
            for x in range(n):
                dx = max(rad - x - 0.5, 0, x + 0.5 - (n - rad))
                dy = max(rad - y - 0.5, 0, y + 0.5 - (n - rad))
                if dx * dx + dy * dy > rad * rad:
                    r, g, b, _ = px[y][x]
                    px[y][x] = (r, g, b, 0)
    return px


def bmp_entry(px):
    n = len(px)
    header = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, 32, 0, n * n * 4, 0, 0, 0, 0)
    body = bytearray()
    for y in range(n - 1, -1, -1):            # bottom-up
        for x in range(n):
            r, g, b, a = px[y][x]
            body += bytes((b, g, r, a))
    mask_row = ((n + 31) // 32) * 4
    mask = bytearray()
    for y in range(n - 1, -1, -1):
        row = bytearray(mask_row)
        for x in range(n):
            if px[y][x][3] == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        mask += row
    return header + body + mask


def png_entry(px):
    n = len(px)
    raw = bytearray()
    for y in range(n):
        raw.append(0)
        for x in range(n):
            raw += bytes(px[y][x])

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def main():
    entries = []
    for n in SIZES:
        px = render(n)
        entries.append((n, png_entry(px) if n == 256 else bmp_entry(px)))
    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    blobs = bytearray()
    for n, blob in entries:
        out += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(blob), offset + len(blobs))
        blobs += blob
    out += blobs
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(out)
    print(f"wrote {OUT} ({len(out)} bytes, sizes {SIZES})")


if __name__ == "__main__":
    main()
