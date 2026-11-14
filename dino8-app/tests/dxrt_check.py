#!/usr/bin/env python3
"""Independent checker for the DXRT_ smoke section (tests/sections/70_dxrt.sh): DXF export -> import -> export round trip.
Python standard library only. rt_a.dxf is the drawing exported from the scene of tests/dxrt_script.txt; rt_b.dxf is that file imported
into an empty document and exported again. Two kinds of check, both from the AutoCAD DXF reference (group codes, units, OCS):

  1. rt_a.dxf itself holds what the scene is, by closed form (DXF reference, ENTITIES section):
       LINE       group 10/20/30 start, 11/21/31 end; layer = group 8
       CIRCLE     10/20/30 centre, 40 radius
       ARC        10/20/30 centre, 40 radius, 50 / 51 start / end angle in DEGREES, counter-clockwise from the OCS X axis
       LWPOLYLINE 90 vertex count, 70 bit 1 = closed, one 10/20 pair per vertex
       ELLIPSE    10 centre, 11 end of the major axis RELATIVE to the centre, 40 ratio minor/major, 41/42 start/end parameter (radians)
       SPLINE     71 degree, 73 control-point count
       HATCH      2 pattern name, 70 = 1 solid fill
       DIMENSION  70 bits 0..2 (value & 7): 0 linear, 1 aligned, 3 diameter, 4 radius (bits 32/64/128 are flags, not part of the type)
  2. rt_b.dxf equals rt_a.dxf: per entity type the same count, and the same multiset of entities by their key group values (every group
     except handles, owner / subclass markers and the anonymous *D block names, numbers compared to 5 decimals).
prints "ok   ..." / "FAIL ..." lines; exit status 1 when any FAIL
"""
import collections
import math
import sys

failed = 0


def check(cond, what):
    global failed
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        failed += 1


def parse(path):
    lines = open(path, encoding='latin-1').read().split('\n')
    pairs = []
    for i in range(0, len(lines) - 1, 2):
        try:
            pairs.append((int(lines[i].strip()), lines[i + 1].strip()))
        except ValueError:
            pass
    sections, cur, rec, i = {}, None, None, 0
    while i < len(pairs):
        code, val = pairs[i]
        if code == 0 and val == 'SECTION':
            cur = pairs[i + 1][1]
            sections[cur] = []
            rec = None
            i += 2
            continue
        if code == 0 and val in ('ENDSEC', 'EOF'):
            cur, rec = None, None
        elif code == 0 and cur is not None:
            rec = (val, [])
            sections[cur].append(rec)
        elif rec is not None:
            rec[1].append((code, val))
        i += 1
    return sections


def num(rec, code, default=None, nth=0):
    v = [x for c, x in rec[1] if c == code]
    return float(v[nth]) if len(v) > nth else default


def txt(rec, code, default=None):
    v = [x for c, x in rec[1] if c == code]
    return v[0] if v else default


def near(a, b, tol=1e-6):
    return a is not None and b is not None and abs(a - b) <= tol


def of(sec, kind):
    return [r for r in sec.get('ENTITIES', []) if r[0] == kind]


SKIP = {5, 105, 330, 360, 100, 102, 1005}


def signature(rec):
    out = []
    for c, v in rec[1]:
        if c in SKIP or (c == 2 and rec[0] in ('DIMENSION', 'INSERT')):
            continue
        try:
            out.append((c, round(float(v), 5) + 0.0))
        except ValueError:
            out.append((c, v))
    if rec[0] == 'ELLIPSE':
        # either end of the major axis describes the same ellipse (DXF reference: group 11 is only an axis end): normalise its sign
        major = [i for i, (c, v) in enumerate(out) if c in (11, 21, 31)]
        first = next((out[i][1] for i in major if out[i][1] != 0), 1.0)
        if first < 0:
            for i in major:
                out[i] = (out[i][0], -out[i][1] + 0.0)
    return (rec[0], tuple(out))


a = parse(sys.argv[1] + '/rt_a.dxf')
b = parse(sys.argv[1] + '/rt_b.dxf')

# ---- 1. the exported scene, by closed form -----------------------------------------------------------------------------
lines = {round(num(e, 20, 0.0), 6): e for e in of(a, 'LINE')}
check(len(of(a, 'LINE')) == 2 and 0.0 in lines and 5.0 in lines, 'DXRT lines: two LINEs at y = 0 and y = 5')
if 0.0 in lines and 5.0 in lines:
    l0, l5 = lines[0.0], lines[5.0]
    check(near(num(l0, 10), 0) and near(num(l0, 11), 10) and near(num(l5, 10), 0) and near(num(l5, 11), 10),
          'DXRT lines: both run x = 0 .. 10 (group 10 start, 11 end)')
    check(txt(l0, 8) == 'Default' and txt(l5, 8) == 'Walls', 'DXRT lines: layers Default and Walls (group 8)')
circles = of(a, 'CIRCLE')
c20 = [c for c in circles if near(num(c, 10), 20)]
check(len(circles) == 3 and len(c20) == 1 and near(num(c20[0], 20), 0) and near(num(c20[0], 40), 5),
      'DXRT circle: centre (20,0), radius 5 (group 10/20, 40); three CIRCLEs in all (the other two carry the radius / diameter dimension)')
arcs = of(a, 'ARC')
check(len(arcs) == 1 and near(num(arcs[0], 10), 40) and near(num(arcs[0], 20), 0) and near(num(arcs[0], 40), 5),
      'DXRT arc: centre (40,0), radius 5')
if arcs:
    check(near(num(arcs[0], 50), 0.0, 1e-5) and near(num(arcs[0], 51), 90.0, 1e-5),
          'DXRT arc: start angle 0, end angle 90 degrees (groups 50, 51 are degrees, counter-clockwise)')
polys = of(a, 'LWPOLYLINE')
openp = [p for p in polys if int(num(p, 70, 0)) % 2 == 0]
closedp = [p for p in polys if int(num(p, 70, 0)) % 2 == 1]
check(len(polys) == 2 and len(openp) == 1 and len(closedp) == 1, 'DXRT polylines: one open and one closed LWPOLYLINE (group 70 bit 1)')
if openp and closedp:
    check(near(num(openp[0], 90), 4) and [num(openp[0], 10, nth=k) for k in range(4)] == [0, 10, 10, 0]
          and [num(openp[0], 20, nth=k) for k in range(4)] == [20, 20, 30, 30], 'DXRT polyline: 4 vertices (0,20) (10,20) (10,30) (0,30)')
    check(near(num(closedp[0], 90), 4) and sorted(num(closedp[0], 10, nth=k) for k in range(4)) == [100, 100, 120, 120]
          and sorted(num(closedp[0], 20, nth=k) for k in range(4)) == [100, 100, 110, 110], 'DXRT rectangle: 4 vertices of (100,100)-(120,110)')
ell = of(a, 'ELLIPSE')
check(len(ell) == 1, 'DXRT ellipse: exported as one ELLIPSE entity')
if ell:
    e = ell[0]
    check(near(num(e, 10), 60) and near(num(e, 20), 0), 'DXRT ellipse: centre (60,0)')
    check(near(abs(num(e, 11)), 8) and near(num(e, 21, 0.0), 0.0), 'DXRT ellipse: major axis end (+-8,0) relative to the centre (group 11)')
    check(near(num(e, 40), 3.0 / 8.0), 'DXRT ellipse: minor/major ratio 3/8 (group 40)')
    check(near(num(e, 41), 0.0) and near(num(e, 42), 2 * math.pi, 1e-5), 'DXRT ellipse: full ellipse, parameters 0 .. 2 pi (groups 41, 42)')
spl = of(a, 'SPLINE')
check(len(spl) == 1 and near(num(spl[0], 71), 3) and num(spl[0], 73, 0) >= 4,
      'DXRT spline: one SPLINE, degree 3 (group 71), at least 4 control points (group 73)')
hat = of(a, 'HATCH')
check(len(hat) == 1 and txt(hat[0], 2) == 'SOLID' and near(num(hat[0], 70), 1), 'DXRT hatch: one SOLID HATCH with group 70 = 1')
dims = of(a, 'DIMENSION')
types = sorted(int(num(d, 70, 0)) & 7 for d in dims)
check(types == [0, 1, 3, 4], 'DXRT dimensions: types linear, aligned, diameter, radius (group 70 & 7 = 0, 1, 3, 4); got %s' % types)

# ---- 2. the same drawing after import and export again --------------------------------------------------------------------
ca = collections.Counter(r[0] for r in a.get('ENTITIES', []))
cb = collections.Counter(r[0] for r in b.get('ENTITIES', []))
check(ca == cb, 'DXRT round trip: entity counts per type equal; before %s after %s' % (dict(sorted(ca.items())), dict(sorted(cb.items()))))
la = sorted(txt(r, 2) for r in a.get('TABLES', []) if r[0] == 'LAYER')
lb = sorted(txt(r, 2) for r in b.get('TABLES', []) if r[0] == 'LAYER')
check(la == lb and 'Walls' in la, 'DXRT round trip: the same layers %s -> %s' % (la, lb))
for kind in sorted(ca):
    sa = collections.Counter(signature(r) for r in of(a, kind))
    sb = collections.Counter(signature(r) for r in of(b, kind))
    check(sa == sb, 'DXRT round trip: every %s keeps its key group values (%d entities)' % (kind, sum(sa.values())))
    if sa != sb:
        for s in list((sa - sb).elements())[:3]:
            print('     only before: %s' % (s,))
        for s in list((sb - sa).elements())[:3]:
            print('     only after:  %s' % (s,))
sys.exit(1 if failed else 0)
