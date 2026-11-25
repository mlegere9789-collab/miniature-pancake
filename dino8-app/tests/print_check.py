#!/usr/bin/env python3
"""Independent checker for the PRT_ smoke section (tests/sections/91_prt.sh): reads a Print-to-SVG and a Print-to-PDF of the same
drawing with Python's xml.etree and a minimal PDF content-stream scan (plus pdfinfo for the page box when installed) and compares
them with closed forms.

The drawing is a 10 x 10 square with its lower-left corner at the origin and a circle of radius 10 centred at (30, 0), millimetre
document, viewed from Top. Its extents are x in [0, 40], y in [-10, 10], so for a page scale s (page millimetres per model unit):
  * the page is A4 landscape 297 x 210 mm (margin 10) and grows to max(297, 40 s + 20) x max(210, 20 s + 20) when s forces more;
    "fit" uses s = min((297 - 20) / 40, (210 - 20) / 20) = 6.925;
  * the drawing is centred, so its lower-left is at ((W - 40 s) / 2, (H - 20 s) / 2) and the line y = 0 is at SVG y = H / 2;
  * the square is closed with four edges of 10 s, spanning SVG x from ox to ox + 10 s and y from H/2 - 10 s to H/2;
  * the circle has its centre at (ox + 30 s, H / 2), a perimeter close to 2 pi 10 s and a bounding box close to 20 s square;
  * the PDF page box is the same size in points (1 mm = 72 / 25.4 pt) and every PDF path is the SVG path, y flipped.

usage: print_check.py SVG PDF (scale S | fit)      prints "ok   ..." / "FAIL ..." lines, exit status 1 when any FAIL
"""
import math
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

failed = 0


def check(cond, what):
    global failed
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        failed += 1


def num(s):
    return float(s)


def parse_svg_path(d):
    """'M x y L x y ... Z' -> (points, closed)"""
    toks = re.findall(r'[MLZ]|-?[0-9.]+(?:e[-+]?[0-9]+)?', d)
    pts, closed, i = [], False, 0
    while i < len(toks):
        t = toks[i]
        if t in 'ML':
            pts.append((num(toks[i + 1]), num(toks[i + 2])))
            i += 3
        elif t == 'Z':
            closed = True
            i += 1
        else:
            raise ValueError('unexpected token ' + t)
    return pts, closed


def unique_points(pts):
    out = list(pts)
    if len(out) > 1 and math.dist(out[0], out[-1]) < 1e-6:
        out.pop()
    return out


def bbox(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def perimeter(pts, closed):
    n = len(pts)
    total = sum(math.dist(pts[i], pts[i + 1]) for i in range(n - 1))
    if closed and n > 1 and math.dist(pts[0], pts[-1]) > 1e-6:
        total += math.dist(pts[-1], pts[0])
    return total


def pdf_paths(pdf_bytes):
    """Every m/l/h-then-S path of the (uncompressed) content stream: list of (points, closed), PDF points."""
    m = re.search(rb'stream\r?\n(.*?)endstream', pdf_bytes, re.S)
    text = m.group(1).decode('latin-1')
    paths, cur, closed = [], [], False
    for line in text.split('\n'):
        line = line.strip()
        mm = re.fullmatch(r'(-?[0-9.]+) (-?[0-9.]+) ([ml])', line)
        if mm:
            if mm.group(3) == 'm' and cur:
                paths.append((cur, closed))
                cur, closed = [], False
            cur.append((num(mm.group(1)), num(mm.group(2))))
        elif line == 'h':
            closed = True
        elif line == 'S':
            if cur:
                paths.append((cur, closed))
            cur, closed = [], False
    return paths


def main():
    svg_path, pdf_path, mode = sys.argv[1], sys.argv[2], sys.argv[3]
    tag = svg_path.rsplit('/', 1)[-1].rsplit('.', 1)[0]
    if mode == 'fit':
        s = min((297.0 - 20) / 40.0, (210.0 - 20) / 20.0)
        W, H = 297.0, 210.0
    else:
        s = float(sys.argv[4])
        W, H = max(297.0, 40 * s + 20), max(210.0, 20 * s + 20)

    root = ET.parse(svg_path).getroot()
    check(root.get('width') == ('%gmm' % W) and root.get('height') == ('%gmm' % H),
          '%s: SVG page is %g x %g mm (width=%s height=%s)' % (tag, W, H, root.get('width'), root.get('height')))
    vb = [float(x) for x in (root.get('viewBox') or '0 0 0 0').split()]
    check(abs(vb[2] - W) < 1e-3 and abs(vb[3] - H) < 1e-3, '%s: SVG viewBox matches the page (%s)' % (tag, root.get('viewBox')))

    ns = '{http://www.w3.org/2000/svg}'
    svg_paths = [parse_svg_path(p.get('d')) for p in root.iter(ns + 'path')]
    check(len(svg_paths) == 2, '%s: the SVG holds exactly 2 paths (square and circle), got %d' % (tag, len(svg_paths)))
    if len(svg_paths) != 2:
        return
    square = next((p for p in svg_paths if len(unique_points(p[0])) == 4), None)
    circle = next((p for p in svg_paths if len(unique_points(p[0])) > 12), None)
    check(square is not None and circle is not None, '%s: one 4-corner path and one many-point path' % tag)
    if square is None or circle is None:
        return

    ox = (W - 40 * s) / 2.0
    sq = unique_points(square[0])
    check(square[1], '%s: the square path is closed (Z)' % tag)
    edges = [math.dist(sq[i], sq[(i + 1) % 4]) for i in range(4)]
    check(all(abs(e - 10 * s) < 2e-3 for e in edges), '%s: the square has four edges of 10 x %g = %g mm (got %s)' % (tag, s, 10 * s, ', '.join('%.3f' % e for e in edges)))
    x0, y0, x1, y1 = bbox(sq)
    check(abs(x0 - ox) < 2e-3 and abs(x1 - (ox + 10 * s)) < 2e-3, '%s: the square spans SVG x %.3f .. %.3f (centred drawing: %.3f .. %.3f)' % (tag, x0, x1, ox, ox + 10 * s))
    check(abs(y1 - H / 2) < 2e-3 and abs(y0 - (H / 2 - 10 * s)) < 2e-3, '%s: the square spans SVG y %.3f .. %.3f (y = 0 is the page middle %.3f)' % (tag, y0, y1, H / 2))

    cp = unique_points(circle[0])
    cx0, cy0, cx1, cy1 = bbox(cp)
    check(abs((cx0 + cx1) / 2 - (ox + 30 * s)) < 0.01 * 10 * s + 2e-3 and abs((cy0 + cy1) / 2 - H / 2) < 0.01 * 10 * s + 2e-3,
          '%s: the circle is centred at (%.3f, %.3f) (closed form %.3f, %.3f)' % (tag, (cx0 + cx1) / 2, (cy0 + cy1) / 2, ox + 30 * s, H / 2))
    check(abs((cx1 - cx0) - 20 * s) < 0.01 * 20 * s + 2e-3 and abs((cy1 - cy0) - 20 * s) < 0.01 * 20 * s + 2e-3,
          '%s: the circle is %.3f x %.3f mm (closed form %g)' % (tag, cx1 - cx0, cy1 - cy0, 20 * s))
    per = perimeter(cp, True)
    check(abs(per - 2 * math.pi * 10 * s) < 0.01 * 2 * math.pi * 10 * s, '%s: the circle perimeter is %.3f mm (closed form 2 pi 10 s = %.3f, 1%% allowed for the polyline)' % (tag, per, 2 * math.pi * 10 * s))

    # --- PDF
    pdf = open(pdf_path, 'rb').read()
    m = re.search(rb'/MediaBox \[0 0 ([0-9.]+) ([0-9.]+)\]', pdf)
    pt = 72.0 / 25.4
    check(m is not None and abs(float(m.group(1)) - W * pt) < 0.01 and abs(float(m.group(2)) - H * pt) < 0.01,
          '%s: the PDF MediaBox is %.3f x %.3f pt = %g x %g mm (got %s)' % (tag, W * pt, H * pt, W, H, m.groups() if m else None))
    try:
        info = subprocess.run(['pdfinfo', pdf_path], capture_output=True, text=True).stdout
        pm = re.search(r'Page size:\s+([0-9.]+) x ([0-9.]+) pts', info)
        if pm:
            check(abs(float(pm.group(1)) - W * pt) < 0.01 and abs(float(pm.group(2)) - H * pt) < 0.01, '%s: pdfinfo reports page size %s x %s pts' % (tag, pm.group(1), pm.group(2)))
        else:
            print('note %s: pdfinfo printed no page size (%r)' % (tag, info[:80]))
    except FileNotFoundError:
        print('note %s: pdfinfo is not installed; the MediaBox check above stands alone' % tag)
    ppaths = pdf_paths(pdf)
    check(len(ppaths) == 2, '%s: the PDF content stream holds 2 stroked paths, got %d' % (tag, len(ppaths)))
    if len(ppaths) == 2:
        worst = 0.0
        for (spts, scl), (ppts, pcl) in zip(svg_paths, ppaths):
            conv = [(x / pt, H - y / pt) for x, y in ppts]  # PDF points (y up) -> SVG millimetres (y down)
            if len(conv) != len(spts) or scl != pcl:
                worst = 1e9
                continue
            worst = max(worst, max(math.dist(a, b) for a, b in zip(conv, spts)))
        check(worst < 5e-3, '%s: every PDF path is the SVG path (worst difference %.4f mm, y flipped, pt -> mm)' % (tag, worst))


main()
sys.exit(1 if failed else 0)
