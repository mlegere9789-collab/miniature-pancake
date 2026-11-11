#!/usr/bin/env python3
"""Independent image-file checker for the VCF_ smoke section (tests/sections/92_vcf.sh) and for PngWrite's standalone check. It parses
the file bytes itself (struct + zlib: chunk CRCs, IHDR, pHYs, IDAT inflate and scanline filter bytes; BMP header fields) and decodes
with Pillow, so nothing here shares code with the writer under test.

  image_check.py png FILE DPI [gradient W H]   PNG: signature, every chunk CRC, IHDR 8-bit truecolour no interlace, pHYs
                                               = round(DPI / 0.0254) pixels per metre in both directions with unit 1 (absent when DPI = 0),
                                               one IDAT run inflating to H * (1 + 3 W) bytes with every filter byte 0 or a valid filter,
                                               Pillow reports the same size and a DPI within 0.02 of DPI; with `gradient` the pixels
                                               equal r = 7x mod 256, g = 11y mod 256, b = 5(x + y) mod 256
  image_check.py bmp FILE DPI                  24-bit BMP: biXPelsPerMeter = biYPelsPerMeter = round(DPI / 0.0254) (0 when DPI = 0)
  image_check.py same A B                      Pillow decodes both to RGB: equal size and at least 99.9% equal pixels (two captures of one
                                               static frame)
  image_check.py notblank FILE                 more than one distinct colour
prints "ok   ..." / "FAIL ..." lines; exit status 1 when any FAIL
"""
import struct
import sys
import zlib

failed = 0


def check(cond, what):
    global failed
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        failed += 1


def ppm(dpi):
    return int(round(dpi / 0.0254)) if dpi > 0 else 0


def png(path, dpi, gradient=None):
    from PIL import Image
    data = open(path, 'rb').read()
    tag = path.rsplit('/', 1)[-1]
    check(data[:8] == b'\x89PNG\r\n\x1a\n', '%s: PNG signature' % tag)
    pos, chunks, crc_ok = 8, [], True
    while pos + 12 <= len(data):
        (n,) = struct.unpack('>I', data[pos:pos + 4])
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        (crc,) = struct.unpack('>I', data[pos + 8 + n:pos + 12 + n])
        crc_ok = crc_ok and zlib.crc32(typ + body) & 0xFFFFFFFF == crc
        chunks.append((typ, body))
        pos += 12 + n
    check(crc_ok and pos == len(data), '%s: every chunk CRC-32 is right and the file ends on a chunk boundary' % tag)
    names = [c[0] for c in chunks]
    check(names[0] == b'IHDR' and names[-1] == b'IEND', '%s: IHDR first, IEND last (%s)' % (tag, b' '.join(names).decode()))
    w, h, depth, ctype, comp, filt, inter = struct.unpack('>IIBBBBB', chunks[0][1])
    check((depth, ctype, comp, filt, inter) == (8, 2, 0, 0, 0), '%s: 8-bit truecolour RGB, deflate, no interlace' % tag)
    phys = [c[1] for c in chunks if c[0] == b'pHYs']
    if dpi > 0:
        want = ppm(dpi)
        got = struct.unpack('>IIB', phys[0]) if phys else None
        check(got == (want, want, 1), '%s: pHYs is %d x %d pixels/metre, unit metre (got %s)' % (tag, want, want, got))
    else:
        check(not phys, '%s: no pHYs chunk when no Dpi was given' % tag)
    raw = zlib.decompress(b''.join(c[1] for c in chunks if c[0] == b'IDAT'))
    check(len(raw) == h * (1 + 3 * w), '%s: the IDAT stream inflates to %d bytes = H x (1 + 3 W) for %d x %d' % (tag, len(raw), w, h))
    img = Image.open(path)
    check(img.size == (w, h) and img.mode == 'RGB', '%s: Pillow reads a %d x %d RGB image' % (tag, w, h))
    if dpi > 0:
        d = img.info.get('dpi')
        check(d is not None and abs(d[0] - dpi) < 0.02 and abs(d[1] - dpi) < 0.02, '%s: Pillow reports dpi %s (wanted %d, the pHYs integer quantises it by up to 0.013)' % (tag, d, dpi))
    if gradient:
        gw, gh = gradient
        check((w, h) == (gw, gh), '%s: size %d x %d' % (tag, w, h))
        px = img.load()
        bad = sum(1 for y in range(h) for x in range(w) if px[x, y] != ((7 * x) % 256, (11 * y) % 256, (5 * (x + y)) % 256))
        check(bad == 0, '%s: all %d pixels equal the closed-form gradient (%d differ)' % (tag, w * h, bad))


def bmp(path, dpi):
    from PIL import Image
    data = open(path, 'rb').read()
    tag = path.rsplit('/', 1)[-1]
    check(data[:2] == b'BM', '%s: BMP signature' % tag)
    bits = struct.unpack_from('<H', data, 28)[0]
    xppm, yppm = struct.unpack_from('<ii', data, 38)
    check(bits == 24, '%s: 24 bits per pixel' % tag)
    want = ppm(dpi)
    check((xppm, yppm) == (want, want), '%s: biX/YPelsPerMeter are %d (dpi %d); got %d, %d' % (tag, want, dpi, xppm, yppm))
    img = Image.open(path)
    check(img.size == struct.unpack_from('<ii', data, 18) , '%s: Pillow reads %s' % (tag, img.size))
    if dpi > 0:
        d = img.info.get('dpi')
        check(d is not None and abs(d[0] - dpi) < 1.0 and abs(d[1] - dpi) < 1.0, '%s: Pillow reports dpi %s (wanted %d)' % (tag, d, dpi))


def same(a, b):
    from PIL import Image
    ia, ib = Image.open(a).convert('RGB'), Image.open(b).convert('RGB')
    check(ia.size == ib.size, 'same: %s and %s have the same size %s / %s' % (a.rsplit('/', 1)[-1], b.rsplit('/', 1)[-1], ia.size, ib.size))
    if ia.size == ib.size:
        pa, pb = ia.tobytes(), ib.tobytes()
        n = ia.size[0] * ia.size[1]
        diff = sum(1 for i in range(0, len(pa), 3) if pa[i:i + 3] != pb[i:i + 3])
        check(diff <= 0.001 * n, 'same: %d of %d pixels differ (at most 0.1%% allowed)' % (diff, n))


def notblank(path):
    from PIL import Image
    colours = Image.open(path).convert('RGB').getcolors(1 << 24)
    check(colours is not None and len(colours) > 1, '%s: more than one distinct colour (%d)' % (path.rsplit('/', 1)[-1], len(colours or [])))


cmd = sys.argv[1]
if cmd == 'png':
    png(sys.argv[2], int(sys.argv[3]), (int(sys.argv[5]), int(sys.argv[6])) if len(sys.argv) > 4 and sys.argv[4] == 'gradient' else None)
elif cmd == 'bmp':
    bmp(sys.argv[2], int(sys.argv[3]))
elif cmd == 'same':
    same(sys.argv[2], sys.argv[3])
elif cmd == 'notblank':
    notblank(sys.argv[2])
else:
    print('usage: image_check.py png|bmp|same|notblank ...')
    sys.exit(2)
sys.exit(1 if failed else 0)
