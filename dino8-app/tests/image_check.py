#!/usr/bin/env python3
"""Independent image-file checker for the VCF_ smoke section (tests/sections/92_vcf.sh) and for PngWrite's standalone check. Python
standard library only (struct + zlib; no Pillow, which the CI runner does not install): it parses the PNG chunks and the BMP header
itself and decodes the pixels with its own unfilter / row-order code, so nothing here shares code with the writer under test.

  image_check.py png FILE DPI [gradient W H]   PNG: signature, every chunk CRC, IHDR 8-bit truecolour no interlace, pHYs
                                               = round(DPI / 0.0254) pixels per metre in both directions with unit 1 (absent when DPI = 0),
                                               one IDAT run inflating to H * (1 + 3 W) bytes whose filter bytes are valid (0..4) and
                                               which unfilters to W x H pixels; with `gradient` the pixels equal
                                               r = 7x mod 256, g = 11y mod 256, b = 5(x + y) mod 256
  image_check.py bmp FILE DPI                  24-bit BMP: biXPelsPerMeter = biYPelsPerMeter = round(DPI / 0.0254) (0 when DPI = 0),
                                               data size = padded rows x height
  image_check.py same A B                      both decode (PNG or BMP, by extension) to equal size and at least 99.9% equal pixels (two
                                               captures of one static frame)
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


def chunks_of(data):
    pos, out, crc_ok = 8, [], True
    while pos + 12 <= len(data):
        (n,) = struct.unpack('>I', data[pos:pos + 4])
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        (crc,) = struct.unpack('>I', data[pos + 8 + n:pos + 12 + n])
        crc_ok = crc_ok and zlib.crc32(typ + body) & 0xFFFFFFFF == crc
        out.append((typ, body))
        pos += 12 + n
    return out, crc_ok, pos


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def decode_png(path):
    """-> (width, height, rows) with rows a list of bytes objects (RGB, top-down), or None when it is not an 8-bit RGB PNG."""
    data = open(path, 'rb').read()
    chunks, _, _ = chunks_of(data)
    w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', chunks[0][1])
    if (depth, ctype, inter) != (8, 2, 0):
        return None
    raw = zlib.decompress(b''.join(c[1] for c in chunks if c[0] == b'IDAT'))
    stride = 3 * w
    rows, prev = [], bytes(stride)
    for y in range(h):
        base = y * (stride + 1)
        f = raw[base]
        line = bytearray(raw[base + 1:base + 1 + stride])
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                line[i] = (line[i] + paeth(a, b, c)) & 255
        rows.append(bytes(line))
        prev = bytes(line)
    return w, h, rows


def decode_bmp(path):
    data = open(path, 'rb').read()
    off = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    bits = struct.unpack_from('<H', data, 28)[0]
    if bits != 24:
        return None
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(abs(h)):
        line = data[off + y * stride:off + y * stride + w * 3]
        rows.append(b''.join(bytes((line[3 * x + 2], line[3 * x + 1], line[3 * x])) for x in range(w)))  # BGR -> RGB
    if h > 0:
        rows.reverse()  # bottom-up file rows -> top-down
    return w, abs(h), rows


def decode(path):
    return decode_png(path) if path.lower().endswith('.png') else decode_bmp(path)


def png(path, dpi, gradient=None):
    data = open(path, 'rb').read()
    tag = path.rsplit('/', 1)[-1]
    check(data[:8] == b'\x89PNG\r\n\x1a\n', '%s: PNG signature' % tag)
    chunks, crc_ok, pos = chunks_of(data)
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
    check(all(raw[y * (1 + 3 * w)] <= 4 for y in range(h)), '%s: every scanline filter byte is a valid PNG filter type (0..4)' % tag)
    dec = decode_png(path)
    check(dec is not None and dec[0] == w and dec[1] == h and len(dec[2]) == h, '%s: decodes to a %d x %d RGB image' % (tag, w, h))
    if gradient and dec:
        gw, gh = gradient
        check((w, h) == (gw, gh), '%s: size %d x %d' % (tag, w, h))
        bad = sum(1 for y in range(h) for x in range(w) if tuple(dec[2][y][3 * x:3 * x + 3]) != ((7 * x) % 256, (11 * y) % 256, (5 * (x + y)) % 256))
        check(bad == 0, '%s: all %d pixels equal the closed-form gradient (%d differ)' % (tag, w * h, bad))


def bmp(path, dpi):
    data = open(path, 'rb').read()
    tag = path.rsplit('/', 1)[-1]
    check(data[:2] == b'BM', '%s: BMP signature' % tag)
    w, h = struct.unpack_from('<ii', data, 18)
    bits = struct.unpack_from('<H', data, 28)[0]
    xppm, yppm = struct.unpack_from('<ii', data, 38)
    check(bits == 24, '%s: 24 bits per pixel' % tag)
    want = ppm(dpi)
    check((xppm, yppm) == (want, want), '%s: biX/YPelsPerMeter are %d (dpi %d); got %d, %d' % (tag, want, dpi, xppm, yppm))
    off = struct.unpack_from('<I', data, 10)[0]
    check(len(data) == off + ((w * 3 + 3) & ~3) * abs(h), '%s: file size = header + padded rows x %d (%d x %d)' % (tag, abs(h), w, abs(h)))


def same(a, b):
    da, db = decode(a), decode(b)
    ta, tb = a.rsplit('/', 1)[-1], b.rsplit('/', 1)[-1]
    check(da is not None and db is not None and da[0] == db[0] and da[1] == db[1], 'same: %s and %s decode to the same size (%s / %s)' % (ta, tb, da and da[:2], db and db[:2]))
    if da and db and da[0] == db[0] and da[1] == db[1]:
        w, h = da[0], da[1]
        diff = sum(1 for y in range(h) for x in range(w) if da[2][y][3 * x:3 * x + 3] != db[2][y][3 * x:3 * x + 3])
        check(diff <= 0.001 * w * h, 'same: %d of %d pixels differ (at most 0.1%% allowed)' % (diff, w * h))


def notblank(path):
    d = decode(path)
    colours = set()
    if d:
        for row in d[2]:
            colours.update(row[i:i + 3] for i in range(0, len(row), 3))
            if len(colours) > 1:
                break
    check(d is not None and len(colours) > 1, '%s: more than one distinct colour' % path.rsplit('/', 1)[-1])


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
