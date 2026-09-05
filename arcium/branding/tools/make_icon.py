#!/usr/bin/env python3
"""Generate Arcium's placeholder app icon as an .icns using only the standard library.

Usage: make_icon.py OUT_DIR   -> writes OUT_DIR/app.icns and OUT_DIR/product_logo_{16,32,48,128,256}.png
"""
import os, struct, subprocess, sys, tempfile, zlib

TOP = (0x5B, 0x6C, 0xFF)      # indigo
BOTTOM = (0xC0, 0x4F, 0xE0)   # violet


def png(width, height, rows):
    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + r for r in rows)
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9))
            + chunk(b'IEND', b''))


def icon(size):
    radius = size * 0.22
    inset = size * 0.08
    lo, hi = inset, size - inset
    rows = []
    for y in range(size):
        t = y / max(size - 1, 1)
        r = int(TOP[0] + (BOTTOM[0] - TOP[0]) * t)
        g = int(TOP[1] + (BOTTOM[1] - TOP[1]) * t)
        b = int(TOP[2] + (BOTTOM[2] - TOP[2]) * t)
        opaque = bytes((r, g, b, 255))
        clear = b'\x00\x00\x00\x00'
        py = y + 0.5
        cy = min(max(py, lo + radius), hi - radius)
        row = bytearray()
        for x in range(size):
            px = x + 0.5
            cx = min(max(px, lo + radius), hi - radius)
            inside = (lo <= px <= hi and lo <= py <= hi
                      and (px - cx) ** 2 + (py - cy) ** 2 <= radius ** 2)
            row += opaque if inside else clear
        rows.append(bytes(row))
    return png(size, size, rows)


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    for s in (16, 32, 48, 128, 256):
        with open(os.path.join(out_dir, f'product_logo_{s}.png'), 'wb') as f:
            f.write(icon(s))
    with tempfile.TemporaryDirectory() as tmp:
        iconset = os.path.join(tmp, 'app.iconset')
        os.mkdir(iconset)
        for s in (16, 32, 128, 256, 512):
            with open(os.path.join(iconset, f'icon_{s}x{s}.png'), 'wb') as f:
                f.write(icon(s))
            with open(os.path.join(iconset, f'icon_{s}x{s}@2x.png'), 'wb') as f:
                f.write(icon(s * 2))
        subprocess.run(['iconutil', '-c', 'icns', iconset, '-o',
                        os.path.join(out_dir, 'app.icns')], check=True)
    print('wrote icons to', out_dir)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
