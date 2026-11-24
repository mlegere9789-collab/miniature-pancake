#!/usr/bin/env python3
"""Independent oracle for the round-3 CAD2D row sections (tests/sections/96_cd*.sh). Python standard library only.

Same method as cad2d_rows.py (which this module imports for the DXF reader and the check helpers): the app builds a small drawing from a
command script, exports DXF, and `check` parses the DXF itself (group codes per the DXF reference) and compares it with values worked out
by hand: closed forms are written next to each expectation, never read back from the app.

  gen CASE DIR            writes DIR/CASE.txt (the app script; @T@ is replaced by DIR)
  check CASE DIR APPLOG   prints "ok   ..." / "FAIL ..." lines, exit status 1 when any FAIL
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cad2d_rows as R  # noqa: E402
from cad2d_rows import PRE, check, close, entities, g, gf, has_line, spline_points  # noqa: E402

CASES = {}


def case(name):
    def deco(fn):
        CASES[name] = fn
        return fn
    return deco


def pts3(rec, c0=10):
    """The (x, y, z) triples of a record in order: group c0 (x), c0 + 10 (y), c0 + 20 (z)."""
    xs = [float(v) for c, v in rec[1] if c == c0]
    ys = [float(v) for c, v in rec[1] if c == c0 + 10]
    zs = [float(v) for c, v in rec[1] if c == c0 + 20]
    return list(zip(xs, ys, zs))


def dist(a, b):
    return math.sqrt(sum((p - q) ** 2 for p, q in zip(a, b)))


def spline_at(rec, t):
    """One point of a DXF SPLINE at parameter t (de Boor, rational through the weights of group 41), planar x, y only."""
    deg = int(g(rec, 71))
    knots = [float(v) for c, v in rec[1] if c == 40]
    xs = [float(v) for c, v in rec[1] if c == 10]
    ys = [float(v) for c, v in rec[1] if c == 20]
    ws = [float(v) for c, v in rec[1] if c == 41] or [1.0] * len(xs)
    cp = [(x * w, y * w, w) for x, y, w in zip(xs, ys, ws)]
    k = deg
    while k < len(cp) - 1 and knots[k + 1] <= t:
        k += 1
    d = [list(cp[j + k - deg]) for j in range(deg + 1)]
    for r in range(1, deg + 1):
        for j in range(deg, r - 1, -1):
            den = knots[j + 1 + k - r] - knots[j + k - deg]
            a = (t - knots[j + k - deg]) / den if den else 0.0
            d[j] = [(1 - a) * d[j - 1][m] + a * d[j][m] for m in range(3)]
    return (d[deg][0] / d[deg][2], d[deg][1] / d[deg][2])


def miss_distance(rec, p, n=4000):
    """Distance from the point p to the spline: coarse sampling, then a ternary search in the parameter around the nearest sample."""
    deg = int(g(rec, 71))
    knots = [float(v) for c, v in rec[1] if c == 40]
    ncp = len([1 for c, v in rec[1] if c == 10])
    t0, t1 = knots[deg], knots[ncp]
    ts = [t0 + (t1 - t0) * i / n for i in range(n + 1)]
    ds = [math.hypot(spline_at(rec, t)[0] - p[0], spline_at(rec, t)[1] - p[1]) for t in ts]
    i = min(range(len(ds)), key=lambda j: ds[j])
    lo, hi = ts[max(0, i - 1)], ts[min(n, i + 1)]
    for _ in range(80):
        a, b = lo + (hi - lo) / 3, hi - (hi - lo) / 3
        fa = math.hypot(spline_at(rec, a)[0] - p[0], spline_at(rec, a)[1] - p[1])
        fb = math.hypot(spline_at(rec, b)[0] - p[0], spline_at(rec, b)[1] - p[1])
        if fa < fb:
            hi = b
        else:
            lo = a
    t = (lo + hi) / 2
    return min(ds[i], math.hypot(spline_at(rec, t)[0] - p[0], spline_at(rec, t)[1] - p[1]))


def points(path):
    return [(gf(r, 10), gf(r, 20), gf(r, 30)) for r in entities(path, 'POINT')]


# DRAW-0004 SPLINE ----------------------------------------------------------------------------------------------------------
# A control-vertex spline of degree p keeps its control points exactly (DXF SPLINE: group 71 degree, 10/20/30 control points, 40 knots);
# a fit-point spline passes through every fit point (the exported spline is evaluated here with de Boor's algorithm).
@case('spline')
def spline():
    s = PRE + """Curve Degree=5 0,0,0 1,2,0 3,3,0 4,0,0 6,1,0 7,4,0 9,0,0
Enter
Curve Degree=10 0,10,0 1,12,0 2,10,0 3,12,0 4,10,0 5,12,0 6,10,0 7,12,0 8,10,0 9,12,0 10,10,0 11,12,0
Enter
InterpCrv 0,100,0 5,105,0 10,100,0 15,105,0 20,102,0 22,98,0
Enter
SelAll
Export @T@/spl.dxf
"""

    def chk(d, log):
        f = d + '/spl.dxf'
        sp = entities(f, 'SPLINE')
        check(len(sp) == 3, 'SPL_1: three SPLINE entities (found %d)' % len(sp))
        by_deg = {int(g(r, 71)): r for r in sp}
        r5 = by_deg.get(5)
        if r5 is not None:
            cv = [(x, y) for x, y, _ in pts3(r5)]
            want = [(0, 0), (1, 2), (3, 3), (4, 0), (6, 1), (7, 4), (9, 0)]
            check(cv == want, 'SPL_2: degree-5 control-vertex spline keeps its 7 control points exactly (found %s)' % cv)
            # a clamped degree-p curve starts and ends at its first and last control vertex
            ev = spline_points(r5, 50)
            check(close(ev[0][0], 0, 1e-9) and close(ev[0][1], 0, 1e-9) and close(ev[-1][0], 9, 1e-9) and close(ev[-1][1], 0, 1e-9),
                  'SPL_3: the degree-5 curve starts at the first (0,0) and ends at the last (9,0) control vertex')
        else:
            check(False, 'SPL_2: a degree-5 SPLINE exists')
            check(False, 'SPL_3: a degree-5 SPLINE exists')
        check(10 in by_deg and len(pts3(by_deg[10])) == 12, 'SPL_4: a degree-10 control-vertex spline (the documented maximum) is created with its 12 control points')
        fit = [(0, 100), (5, 105), (10, 100), (15, 105), (20, 102), (22, 98)]
        got = None
        for r in sp:
            c = pts3(r)
            if c and abs(c[0][1] - 100) < 1e-6 and len(c) > 5:
                got = r
        if got is not None:
            worst = max(miss_distance(got, p) for p in fit)
            check(worst < 1e-6, 'SPL_5: the fit-point spline passes through all 6 fit points (largest miss %.3g, bound 1e-6)' % worst)
        else:
            check(False, 'SPL_5: a fit-point spline exists')
    return s, chk


# DRAW-0007 ELLIPSE (center method) -----------------------------------------------------------------------------------------
# An ellipse with centre c, semi-axes a (along the unit vector u) and b (along the unit vector v perpendicular to u) is the set
# ((p - c).u / a)^2 + ((p - c).v / b)^2 = 1, its area is pi a b. The app exports it as a rational degree-2 SPLINE; the check samples that
# SPLINE with de Boor's algorithm and tests every sample against the implicit equation.
@case('ellipse')
def ellipse():
    s = PRE + """Ellipse 10,20,0 13,20,0 10,22,0
Ellipse 0,-50,0 3,-46,0 -1.5,-48,0
SelAll
Export @T@/ell.dxf
"""

    def chk(d, log):
        f = d + '/ell.dxf'
        el = entities(f, 'ELLIPSE')
        sp = entities(f, 'SPLINE')
        check(len(el) + len(sp) == 2, 'ELL_1: two ellipses give two entities, ELLIPSE or its exact rational SPLINE form (found %d ELLIPSE, %d SPLINE)' % (len(el), len(sp)))
        # first ellipse: the axis end (13,20) is 3 from the centre (10,20) along +x and the pick (10,22) is 2 from the first axis: a=3, b=2
        # second: (3,4) from the centre (0,-50) has length 5 and direction (0.6,0.8); the pick (-1.5,2) from the centre lies at distance
        # |(-1.5,2) x (0.6,0.8)| = |-1.5*0.8 - 2*0.6| = 2.4 from the first axis: a=5, b=2.4
        want = [((10, 20), 3.0, 2.0, 1.0, 0.0), ((0, -50), 5.0, 2.4, 0.6, 0.8)]
        if el:
            # DXF ELLIPSE: group 10 centre, 11 end of the major axis relative to the centre, 40 ratio minor/major, 41/42 start/end parameter
            for (cx, cy), a, b, ux, uy in want:
                hit = None
                for r in el:
                    if close(gf(r, 10), cx, 1e-8) and close(gf(r, 20), cy, 1e-8):
                        hit = r
                ok = False
                if hit is not None:
                    mx, my = gf(hit, 11), gf(hit, 21)
                    major = math.hypot(mx, my)
                    parallel = abs(mx * uy - my * ux) < 1e-8 * max(1.0, major)
                    ok = close(major, a, 1e-8) and parallel and close(gf(hit, 40) * major, b, 1e-8) and close(gf(hit, 41) or 0.0, 0.0, 1e-9) and close(gf(hit, 42), 2 * math.pi, 1e-8)
                check(ok, 'ELL_%s: the ellipse about (%g,%g) has a major semi-axis %g along (%g,%g), minor semi-axis %g (ratio %g) and runs the full 0..2 pi' % ('2' if a == 3.0 else '4', cx, cy, a, ux, uy, b, b / a))
                check(ok and abs(math.pi * major * gf(hit, 40) * major - math.pi * a * b) < 1e-8, 'ELL_%sa: its area pi a b = %.6f' % ('2' if a == 3.0 else '4', math.pi * a * b))
            return

        def fit(rec, cx, cy, a, b, ux, uy, tag):
            n = 2000
            pts = spline_points(rec, n)
            vx, vy = -uy, ux
            worst = 0.0
            for x, y in pts:
                u = ((x - cx) * ux + (y - cy) * uy) / a
                v = ((x - cx) * vx + (y - cy) * vy) / b
                worst = max(worst, abs(u * u + v * v - 1))
            check(worst < 1e-8, 'ELL_%s: every sample of the ellipse satisfies (u/a)^2 + (v/b)^2 = 1 for centre (%g,%g), a=%g, b=%g (worst residual %.3g, bound 1e-8)' % (tag, cx, cy, a, b, worst))
            area = 0.0
            for i in range(len(pts) - 1):
                area += pts[i][0] * pts[i + 1][1] - pts[i + 1][0] * pts[i][1]
            area = abs(area) / 2
            check(abs(area - math.pi * a * b) < 1e-4, 'ELL_%s: its area is pi a b = %.6f (polygon of 2000 samples gives %.6f, bound 1e-4 = the inscribed-polygon deficit)' % (tag + 'a', math.pi * a * b, area))

        for r in sp:
            c0 = spline_points(r, 4)[0]
            if abs(c0[1] - 20) < 1.5 and abs(c0[0] - 10) < 4:
                fit(r, 10, 20, 3, 2, 1, 0, '2')
            else:
                fit(r, 0, -50, 5, 2.4, 0.6, 0.8, '4')
    return s, chk


# DRAW-0006 ARC --------------------------------------------------------------------------------------------------------------
# DXF ARC: centre (10,20,30), radius 40, start angle 50, end angle 51 in degrees, counter-clockwise from start to end (DXF reference).
# Centre-start-end: the radius is |start - centre|; the end point only gives the END DIRECTION (AutoCAD CENTER/START/END, "the arc is drawn
# counterclockwise from the start point and ends on the line from the centre through the end point").
# Three points on the circle fix centre and radius uniquely: for (0,0),(10,0),(5,5) the centre is (5,0) and the radius 5.
@case('arc')
def arc():
    s = PRE + """Arc 0,0,0 5,0,0 0,9,0
Arc 10,10,0 15,10,0 -50,10,0
Arc3Pt 20,0,0 30,0,0 25,5,0
Arc3Pt 0,100,0 10,100,0 5,95,0
SelAll
Export @T@/arc.dxf
"""

    def chk(d, log):
        f = d + '/arc.dxf'
        ar = R.arcs(f)
        check(len(ar) == 4, 'ARC_1: four ARC entities (found %d)' % len(ar))

        def find(cx, cy, r):
            return [a for a in ar if close(gf(a, 10), cx, 1e-8) and close(gf(a, 20), cy, 1e-8) and close(gf(a, 40), r, 1e-8)]

        def span(a):
            s0, e0 = gf(a, 50) % 360, gf(a, 51) % 360
            return s0, e0, (e0 - s0) % 360

        a1 = find(0, 0, 5)
        # start (5,0) is at angle 0, the end point (0,9) is straight up (90 degrees): the arc runs 0 -> 90 about (0,0), radius 5, a quarter circle
        check(len(a1) == 1 and close(span(a1[0])[0], 0, 1e-6) and close(span(a1[0])[1], 90, 1e-6), 'ARC_2: Arc centre (0,0) start (5,0) end direction (0,9) is the quarter arc 0 -> 90 degrees, radius 5')
        a2 = find(10, 10, 5)
        # start (15,10) is at angle 0 about (10,10); the end point (-50,10) lies in direction 180 degrees: a half circle 0 -> 180, radius 5
        check(len(a2) == 1 and close(span(a2[0])[0], 0, 1e-6) and close(span(a2[0])[2], 180, 1e-6), 'ARC_3: Arc centre (10,10) start (15,10) end direction towards (-50,10) is the half circle 0 -> 180, radius 5 (the end point only gives the direction)')
        # three points (20,0), (30,0) and (25,5): centre (25,0), radius 5; the arc through (25,5) between the two ends is the upper half: as a
        # counter-clockwise DXF arc it runs from the right end (30,0) at 0 degrees to the left end (20,0) at 180 degrees
        a3 = find(25, 0, 5)
        check(len(a3) == 1 and close(span(a3[0])[0], 0, 1e-6) and close(span(a3[0])[1], 180, 1e-6), 'ARC_4: Arc3Pt (20,0) (30,0) through (25,5) has centre (25,0), radius 5 and covers the upper half 0 -> 180 degrees')
        # (0,100), (10,100) through (5,95): centre (5,100), radius 5, the LOWER half: counter-clockwise from the left end (180) to the right end (360 = 0)
        a4 = find(5, 100, 5)
        check(len(a4) == 1 and close(span(a4[0])[0], 180, 1e-6) and close(span(a4[0])[1], 0, 1e-6), 'ARC_5: Arc3Pt (0,100) (10,100) through (5,95) has centre (5,100), radius 5 and covers the lower half 180 -> 360 degrees')
    return s, chk


# DRAW-0003 3DPOLY -------------------------------------------------------------------------------------------------------------
# A 3D polyline is a chain of straight segments whose vertices keep their own Z (DXF: POLYLINE with flag bit 8 = 3D polyline, VERTEX records).
@case('poly3d')
def poly3d():
    s = PRE + """Polyline 0,80,0 10,80,3 10,90,7 -4,90,-2.5
Enter
SelAll
Export @T@/p3d.dxf
"""

    def chk(d, log):
        f = d + '/p3d.dxf'
        pl = entities(f, 'POLYLINE')
        check(len(pl) == 1, 'P3D_1: one POLYLINE entity (found %d)' % len(pl))
        if pl:
            flag = int(g(pl[0], 70, '0'))
            check(flag & 8 == 8, 'P3D_2: its flag word has bit 8 (3D polyline), flag = %d' % flag)
        vs = entities(f, 'VERTEX')
        got = [(gf(v, 10), gf(v, 20), gf(v, 30)) for v in vs]
        want = [(0, 80, 0), (10, 80, 3), (10, 90, 7), (-4, 90, -2.5)]
        check(len(got) == 4 and all(all(close(a, b) for a, b in zip(p, q)) for p, q in zip(got, want)), 'P3D_3: the four vertices keep their own Z: %s' % got)
        # chain length: |(10,0,3)| + |(0,10,4)| + |(-14,0,-9.5)| = sqrt(109) + sqrt(116) + sqrt(286.25)
        total = sum(dist(got[i], got[i + 1]) for i in range(len(got) - 1)) if len(got) == 4 else 0
        want_len = math.sqrt(109) + math.sqrt(116) + math.sqrt(286.25)
        check(abs(total - want_len) < 1e-8, 'P3D_4: the chain length is sqrt(109) + sqrt(116) + sqrt(286.25) = %.9f (found %.9f)' % (want_len, total))
    return s, chk


# DRAW-0008 RECTANG -------------------------------------------------------------------------------------------------------------
# Two opposite corners give a closed rectangular polyline with vertices (x0,y0), (x1,y0), (x1,y1), (x0,y1); area |x1-x0| |y1-y0|.
@case('rectang')
def rectang():
    s = PRE + """Rectangle 20,0,0 30,5,0
Rectangle 100,50,0 90,40,0
SelAll
Export @T@/rec.dxf
"""

    def chk(d, log):
        f = d + '/rec.dxf'
        lp = R.lwpolys(f)
        check(len(lp) == 2 and all(c for c, _ in lp), 'REC_1: two closed LWPOLYLINEs (found %s)' % [(c, len(p)) for c, p in lp])
        check(R.has_poly(f, [(20, 0), (30, 0), (30, 5), (20, 5)]), 'REC_2: the corners (20,0) and (30,5) give the vertices (20,0) (30,0) (30,5) (20,5)')
        # the corners (100,50) and (90,40) in the opposite order still make an axis-parallel rectangle on the same four corners
        check(R.has_poly(f, [(90, 40), (100, 40), (100, 50), (90, 50)]), 'REC_3: the corners (100,50) and (90,40) (picked top-right first) give the four corners (90,40) (100,40) (100,50) (90,50)')
        areas = []
        for c, pts in lp:
            a = 0.0
            for i in range(len(pts)):
                x0, y0 = pts[i]
                x1, y1 = pts[(i + 1) % len(pts)]
                a += x0 * y1 - x1 * y0
            areas.append(round(abs(a) / 2, 9))
        check(sorted(areas) == [50.0, 100.0], 'REC_4: the shoelace areas are 10*5 = 50 and 10*10 = 100 (found %s)' % sorted(areas))
    return s, chk


# DRAW-0011 POINT ------------------------------------------------------------------------------------------------------------------
@case('point')
def point():
    s = PRE + """Point 1,2,3
Point -4.5,0,0
Points 10,10,10 20,20,-20
Enter
SelAll
Export @T@/pnt.dxf
"""

    def chk(d, log):
        f = d + '/pnt.dxf'
        pts = points(f)
        want = [(1, 2, 3), (-4.5, 0, 0), (10, 10, 10), (20, 20, -20)]
        check(len(pts) == 4 and all(any(dist(p, w) < 1e-9 for p in pts) for w in want), 'PNT_1: Point and Points place POINT objects at (1,2,3), (-4.5,0,0), (10,10,10), (20,20,-20), 3D coordinates kept: %s' % pts)
    return s, chk


# MODIFY-0028 MATCHPROP ----------------------------------------------------------------------------------------------------------
# The source object's layer and colour are copied to the destination objects (DXF group 8 layer name, group 420 true colour = 0xRRGGBB).
@case('matchprop')
def matchprop():
    s = PRE + """NewLayer Walls
Line 0,0,0 10,0,0
SelLast
SetRenderColor
255,0,0
SetLayer
Default
Line 0,5,0 10,5,0
Line 0,9,0 10,9,0
Circle 50,50,0 3
SelID 3 2 4
MatchProperties
SelID 1
Enter
SelAll
Export @T@/mp.dxf
"""

    def chk(d, log):
        f = d + '/mp.dxf'
        want = 255 * 65536
        ls = entities(f, 'LINE')
        cs = entities(f, 'CIRCLE')
        check(len(ls) == 3 and len(cs) == 1, 'MP_1: three lines and one circle (found %d, %d)' % (len(ls), len(cs)))
        check(all(g(r, 8) == 'Walls' for r in ls + cs), 'MP_2: every line and the circle now lie on layer Walls: %s' % [g(r, 8) for r in ls + cs])
        check(all(g(r, 420) == str(want) for r in ls + cs), 'MP_3: every line and the circle carry the true colour 0xFF0000 = %d (group 420): %s' % (want, [g(r, 420) for r in ls + cs]))
        check(has_line(f, 0, 5, 10, 5, 'Walls') and has_line(f, 0, 9, 10, 9, 'Walls'), 'MP_4: the geometry of the matched lines is unchanged')
    return s, chk


# MODIFY-0030 CHPROP -----------------------------------------------------------------------------------------------------------
@case('chprop')
def chprop():
    s = PRE + """NewLayer Walls
SetLayer
Default
Line 0,0,0 10,0,0
Line 0,5,0 10,5,0
Circle 30,30,0 2
SelID 1 3
ChangeLayer
Walls
SelNone
SelID 2 3
SetRenderColor
0,128,255
SelAll
Export @T@/chp.dxf
"""

    def chk(d, log):
        f = d + '/chp.dxf'
        l1, l2 = [r for r in entities(f, 'LINE') if close(gf(r, 20), 0)][0], [r for r in entities(f, 'LINE') if close(gf(r, 20), 5)][0]
        c = entities(f, 'CIRCLE')[0]
        check(g(l1, 8) == 'Walls' and g(c, 8) == 'Walls' and g(l2, 8) == 'Default', 'CHP_1: ChangeLayer moves objects 1 and 3 to Walls and leaves object 2 on Default (layers %s %s %s)' % (g(l1, 8), g(c, 8), g(l2, 8)))
        want = str(0 * 65536 + 128 * 256 + 255)
        check(g(l2, 420) == want and g(c, 420) == want and g(l1, 420) is None, 'CHP_2: the colour 0,128,255 = 0x0080FF = %s (group 420) is set on objects 2 and 3 only (found %s %s %s)' % (want, g(l2, 420), g(c, 420), g(l1, 420)))
    return s, chk


# MODIFY-0025 ERASE ------------------------------------------------------------------------------------------------------------
# ERASE removes the selected objects; objects on a locked layer cannot be selected and survive an Erase All (ERASE help).
@case('erase')
def erase():
    s = PRE + """NewLayer Walls
Line 0,0,0 10,0,0
SetLayer
Default
Line 0,1,0 10,1,0
Line 0,2,0 10,2,0
Line 0,3,0 10,3,0
Circle 40,40,0 2
SelLast
Delete
SelAll
Export @T@/era_0.dxf
SelNone
SelWindow -1,0.5,0 11,1.5,0
Delete
SelNone
LayerLock Walls
SelAll
Delete
LayerUnlock
SelAll
Export @T@/era_1.dxf
"""

    def chk(d, log):
        z, a = d + '/era_0.dxf', d + '/era_1.dxf'
        check(len(entities(z, 'CIRCLE')) == 0 and len(entities(z, 'LINE')) == 4, 'ERA_1: Delete on the last object removes the circle and nothing else (found %d circles, %d lines)' % (len(entities(z, 'CIRCLE')), len(entities(z, 'LINE'))))
        check(any('Deleted 1 object(s)' in l for l in log.split('\n')) and not has_line(a, 0, 1, 10, 1), 'ERA_2: Delete on a window selection removes exactly the line (0,1)-(10,1)')
        # Walls is locked, so Select All takes only the two Default lines (0,2)-(10,2) and (0,3)-(10,3); the locked-layer line survives
        check(log.count('2 object(s) selected') >= 1 and not has_line(a, 0, 2, 10, 2) and not has_line(a, 0, 3, 10, 3), 'ERA_3: Select All with layer Walls locked selects the 2 remaining Default lines and Delete removes both')
        check(len(entities(a, 'LINE')) == 1 and has_line(a, 0, 0, 10, 0, 'Walls'), 'ERA_4: the locked-layer line (0,0)-(10,0) on Walls survives (found %d lines)' % len(entities(a, 'LINE')))
    return s, chk


# MODIFY-0015 ARRAYPATH ---------------------------------------------------------------------------------------------------------
# N items on an open path of length L are spaced L/(N-1) apart with the first on the path start and the last on the path end:
# L = 100, N = 6 gives the arc-length stations 0, 20, 40, 60, 80, 100 (ARRAYPATH, Divide method).
@case('arraypath')
def arraypath():
    s = PRE + """Line 0,0,0 100,0,0
Point 0,10,0
Polyline 0,50,0 30,50,0 30,90,0
Enter
Point 0,60,0
Point 100,60,0
SelNone
SelID 2
ArrayCrv
SelID 1
Enter
6
SelNone
SelID 4
ArrayCrv
SelID 3
Enter
5
SelNone
SelID 5
ArrayCrv
SelID 3
Enter
Orientation=NoRotation
5
SelAll
Export @T@/apt.dxf
"""

    def chk(d, log):
        f = d + '/apt.dxf'
        pts = points(f)

        def has(x, y):
            return any(close(p[0], x, 1e-6) and close(p[1], y, 1e-6) for p in pts)

        check(all(has(x, 10) for x in (0, 20, 40, 60, 80, 100)), 'APT_1: 6 items along the line (0,0)-(100,0) sit at x = 0, 20, 40, 60, 80, 100 (stations of an even division of the length 100)')
        check(not any(close(p[1], 10, 1e-6) and (p[0] < -1e-6 or p[0] > 100 + 1e-6) for p in pts) and len([p for p in pts if close(p[1], 10, 1e-6)]) >= 6, 'APT_2: no item leaves the path (all y = 10, none beyond x = 100)')
        # L-shaped path (0,50) -> (30,50) -> (30,90): length 30 + 40 = 70, 5 items 17.5 apart: stations (0,50) (17.5,50) (30,55) (30,72.5) (30,90).
        # NoRotation copies are plain translations by station - start (0,50): the point (100,60) goes to (100,60) (117.5,60) (130,65) (130,82.5) (130,100)
        check(all(has(x, y) for x, y in [(100, 60), (117.5, 60), (130, 65), (130, 82.5), (130, 100)]), 'APT_3: Orientation=NoRotation: the point (100,60) is translated to the 5 stations: (100,60) (117.5,60) (130,65) (130,82.5) (130,100)')
        # Default (aligned) items turn with the tangent: on the vertical leg the tangent is +y, the item frame is turned by +90 degrees, so the
        # 10-unit offset above the start (0,60) - (0,50) = (0,10) becomes (-10,0): copies at station + (-10,0) on that leg
        check(all(has(x, y) for x, y in [(0, 60), (17.5, 60), (20, 55), (20, 72.5), (20, 90)]), 'APT_4: aligned items turn with the path: the point (0,60) goes to (0,60) (17.5,60) (20,55) (20,72.5) (20,90) (offset (0,10) rotated by 90 degrees on the vertical leg)')
    return s, chk


def header_vars(path):
    """DXF HEADER variables: {'$PDMODE': [(70, '35')], ...}. A header variable is group 9 (its name) followed by its value groups."""
    try:
        lines = open(path, encoding='latin-1').read().split('\n')
    except OSError:
        return {}
    pairs = []
    for i in range(0, len(lines) - 1, 2):
        try:
            pairs.append((int(lines[i].strip()), lines[i + 1].strip()))
        except ValueError:
            pass
    out, cur, inside = {}, None, False
    for code, val in pairs:
        if code == 0 and val == 'SECTION':
            inside = False
        if code == 2 and val == 'HEADER':
            inside = True
            continue
        if code == 0 and val == 'ENDSEC':
            inside = False
        if not inside:
            continue
        if code == 9:
            cur = val
            out[cur] = []
        elif cur is not None:
            out[cur].append((code, val))
    return out


def chains(path):
    """Every polyline of the file as a list of (x, y, z): LWPOLYLINE (10/20 pairs, z = group 38 elevation) and POLYLINE with its VERTEX records."""
    out = []
    for r in entities(path, 'LWPOLYLINE'):
        z = gf(r, 38) or 0.0
        xs = [float(v) for c, v in r[1] if c == 10]
        ys = [float(v) for c, v in r[1] if c == 20]
        out.append([(x, y, z) for x, y in zip(xs, ys)])
    cur = None
    for r in R.parse(path).get('ENTITIES', []):
        if r[0] == 'POLYLINE':
            cur = []
        elif r[0] == 'VERTEX' and cur is not None:
            cur.append((gf(r, 10), gf(r, 20), gf(r, 30) or 0.0))
        elif r[0] == 'SEQEND' and cur is not None:
            out.append(cur)
            cur = None
    return out


def same_chain(a, b, tol=1e-8):
    return len(a) == len(b) and all(dist(p, q) < tol for p, q in zip(a, b))


def has_segment(path, p, q, tol=1e-8):
    """A straight piece p-q exists as a LINE (either direction) or as a two-vertex polyline."""
    p, q = (p[0], p[1], p[2] if len(p) > 2 else 0.0), (q[0], q[1], q[2] if len(q) > 2 else 0.0)
    for r in entities(path, 'LINE'):
        a, b = (gf(r, 10), gf(r, 20), gf(r, 30) or 0.0), (gf(r, 11), gf(r, 21), gf(r, 31) or 0.0)
        if (dist(a, p) < tol and dist(b, q) < tol) or (dist(a, q) < tol and dist(b, p) < tol):
            return True
    for c in chains(path):
        if len(c) == 2 and ((dist(c[0], p) < tol and dist(c[1], q) < tol) or (dist(c[0], q) < tol and dist(c[1], p) < tol)):
            return True
    return False


# MODIFY-0019 LENGTHEN -------------------------------------------------------------------------------------------------------------
# LENGTHEN changes an open object at the end nearest the pick: Delta adds a length, Percent sets the new length to that percentage of the
# old one, Total sets the new length; closed objects are not changed. A straight line keeps its direction, a polyline lengthens along its
# last segment or is cut back along the chain, an arc keeps its centre and radius and changes its included angle.
@case('lengthen')
def lengthen():
    s = PRE + """Line 0,0,0 10,0,0
Line 0,10,0 10,10,0
Line 0,20,0 10,20,0
Arc 50,0,0 55,0,0 50,9,0
Polyline 0,40,0 10,40,0 10,50,0
Enter
Polyline 0,60,0 10,60,0 10,70,0 20,70,0
Enter
Circle 80,80,0 3
SelNone
Lengthen Mode=Delta 5
9,0,0
1,10,0
Enter
Lengthen Mode=Percent 150
9,20,0
Enter
Lengthen Mode=Total 15.707963267948966
50,5,0
Enter
Lengthen Mode=Total 25
10,49,0
Enter
Lengthen Mode=Delta -15
19,70,0
Enter
Lengthen Mode=Delta 5
83,80,0
Enter
SelAll
Export @T@/len.dxf
"""

    def chk(d, log):
        f = d + '/len.dxf'
        # Delta 5 at the end nearest the pick: the line (0,0)-(10,0) picked at (9,0) grows at its END to length 15; the line (0,10)-(10,10) picked at
        # (1,10) grows at its START: (-5,10)-(10,10)
        check(has_line(f, 0, 0, 15, 0), 'LEN_1: Delta 5 picked near the end (9,0) turns (0,0)-(10,0) into (0,0)-(15,0)')
        check(has_line(f, -5, 10, 10, 10), 'LEN_2: Delta 5 picked near the start (1,10) turns (0,10)-(10,10) into (-5,10)-(10,10) (the picked end moves, the far end stays)')
        # Percent 150: length 10 -> 15 at the picked end
        check(has_line(f, 0, 20, 15, 20), 'LEN_3: Percent 150 turns the length 10 into 15: (0,20)-(15,20)')
        # the quarter arc about (50,0), radius 5, runs 0 -> 90 degrees (length 5 pi / 2); Total 5 pi is the half circle, lengthened at the end
        # nearest (50,5): 0 -> 180 degrees about the same centre with the same radius
        ar = R.arcs(f)
        ok = False
        for a in ar:
            s0, e0 = gf(a, 50) % 360, gf(a, 51) % 360
            if close(gf(a, 10), 50, 1e-7) and close(gf(a, 20), 0, 1e-7) and close(gf(a, 40), 5, 1e-7) and close(s0, 0, 1e-6) and close(e0, 180, 1e-6):
                ok = True
        check(ok and len(ar) == 1, 'LEN_4: Total 5*pi on the quarter arc (centre (50,0), radius 5) gives the half circle 0 -> 180 degrees: centre and radius unchanged, included angle = length / radius = pi')
        ch = chains(f)
        # polyline (0,40)-(10,40)-(10,50), length 20, Total 25: the last segment (10,40)-(10,50) is lengthened by 5 along its own direction -> (10,55)
        check(any(same_chain(c, [(0, 40, 0), (10, 40, 0), (10, 55, 0)]) for c in ch), 'LEN_5: Total 25 on the polyline (0,40)-(10,40)-(10,50) (length 20) moves its end to (10,55): last segment lengthened along its direction')
        # polyline (0,60)-(10,60)-(10,70)-(20,70), length 30, Delta -15: the last segment (10) is removed and 5 more are cut from the middle one:
        # end (10,65)
        check(any(same_chain(c, [(0, 60, 0), (10, 60, 0), (10, 65, 0)]) for c in ch), 'LEN_6: Delta -15 on the polyline (0,60)-(10,60)-(10,70)-(20,70) (length 30) cuts it to (0,60)-(10,60)-(10,65), length 15')
        check(len(R.entities(f, 'CIRCLE')) == 1 and R.has_circle(f, 80, 80, 3), 'LEN_7: the circle (80,80) r=3 is closed and is not changed by Lengthen')
    return s, chk


# MODIFY-0069 REVERSE -------------------------------------------------------------------------------------------------------------
@case('reverse')
def reverse():
    s = PRE + """Line 0,0,0 10,5,0
Polyline 0,20,0 10,20,3 10,30,7
Enter
Curve Degree=3 0,50,0 5,60,0 10,40,0 15,55,0
Enter
Line 100,0,0 110,0,0
SelID 1 2 3
Reverse
SelAll
Export @T@/rev.dxf
"""

    def chk(d, log):
        f = d + '/rev.dxf'
        ls = {line_start(r): r for r in entities(f, 'LINE')}
        check((10, 5, 0) in ls and close(gf(ls[(10, 5, 0)], 11), 0) and close(gf(ls[(10, 5, 0)], 21), 0), 'REV_1: Reverse swaps the ends of the line (0,0)-(10,5): it now starts at (10,5) and ends at (0,0)')
        check((100, 0, 0) in ls and close(gf(ls[(100, 0, 0)], 11), 110), 'REV_2: the line (100,0)-(110,0) that was not selected keeps its direction')
        check(any(same_chain(c, [(10, 30, 7), (10, 20, 3), (0, 20, 0)]) for c in chains(f)), 'REV_3: Reverse lists the vertices of the 3D polyline (0,20,0) (10,20,3) (10,30,7) in the opposite order')
        sp = entities(f, 'SPLINE')
        got = [(x, y) for x, y, _ in pts3(sp[0])] if sp else []
        # control vertices of a cubic spline run the other way round; the clamped curve still passes through the same two end points
        check(got == [(15, 55), (10, 40), (5, 60), (0, 50)], 'REV_4: the control vertices of the cubic spline are reversed: (15,55) (10,40) (5,60) (0,50) (found %s)' % got)
        if sp:
            ev = spline_points(sp[0], 20)
            check(close(ev[0][0], 15, 1e-9) and close(ev[0][1], 55, 1e-9) and close(ev[-1][0], 0, 1e-9) and close(ev[-1][1], 50, 1e-9), 'REV_5: the reversed spline now starts at (15,55) and ends at (0,50)')
        else:
            check(False, 'REV_5: a SPLINE exists')
    return s, chk


def line_start(r):
    return (round(gf(r, 10), 9) + 0.0, round(gf(r, 20), 9) + 0.0, round(gf(r, 30) or 0.0, 9) + 0.0)


# DRAW-0012 MEASURE, DRAW-0044 DIVIDE, DRAW-0048 equal intervals ---------------------------------------------------------------------------
# MEASURE puts points at k*d of arc length from the chosen end without cutting the object: a line of length 100 measured at 30 gives
# s = 30, 60, 90. DIVIDE into n equal parts puts the n-1 interior points at k*L/n.
@case('measure')
def measure():
    s = PRE + """Line 0,0,0 100,0,0
SelLast
Measure
30
Line 0,10,0 100,10,0
SelLast
Measure From=End
30
Polyline 0,30,0 50,30,0 50,80,0
Enter
SelLast
Measure
30
Circle 200,0,0 10
SelLast
Measure
20
Line 0,100,0 100,100,0
SelLast
Divide Ends=No 5
Circle 300,0,0 10
SelLast
Divide Ends=No 4
Line 0,120,0 100,120,0
SelLast
Divide Mode=SegmentLength Ends=No 30
SelAll
Export @T@/mea.dxf
"""

    def chk(d, log):
        f = d + '/mea.dxf'
        pts = points(f)

        def has(x, y):
            return any(abs(p[0] - x) < 1e-6 and abs(p[1] - y) < 1e-6 for p in pts)

        check(all(has(x, 0) for x in (30, 60, 90)) and not has(0, 0) and not has(100, 0), 'MEA_1: Measure 30 on the line (0,0)-(100,0) puts points at x = 30, 60, 90 and none on the ends')
        # From=End: distances 30, 60, 90 counted from the end (100,10): x = 70, 40, 10
        check(all(has(x, 10) for x in (70, 40, 10)) and not has(100, 10) and not has(0, 10), 'MEA_2: Measure From=End 30 on (0,10)-(100,10) counts from its end: x = 70, 40, 10')
        # polyline (0,30)-(50,30)-(50,80), length 50 + 50 = 100: s = 30 -> (30,30); s = 60 -> 10 up the second leg (50,40); s = 90 -> (50,70)
        check(has(30, 30) and has(50, 40) and has(50, 70), 'MEA_3: Measure 30 along the bent polyline (length 100) puts points at (30,30), (50,40), (50,70) (arc length, across the corner)')
        # circle radius 10 about (200,0): the length is 20 pi = 62.83; from its seam (angle 0, counter-clockwise) s = 20, 40, 60 is the angle s / 10 rad
        check(all(has(200 + 10 * math.cos(s_ / 10.0), 10 * math.sin(s_ / 10.0)) for s_ in (20, 40, 60)), 'MEA_4: Measure 20 on the circle r=10 puts points at the angles 2, 4 and 6 radians (s / r) from the seam')
        check(all(has(x, 100) for x in (20, 40, 60, 80)) and not has(0, 100) and not has(100, 100) and len([p for p in pts if abs(p[1] - 100) < 1e-6]) == 4, 'DIV_1: Divide into 5 segments (Ends=No) puts the 4 interior points at x = 20, 40, 60, 80')
        # a closed curve divided into 4 parts keeps its start point: the points are at angles 0, 90, 180, 270 degrees
        check(has(310, 0) and has(300, 10) and has(290, 0) and has(300, -10) and len([p for p in pts if abs(p[0] - 300) < 11 and abs(p[1]) < 11]) == 4, 'DIV_2: Divide the circle r=10 about (300,0) into 4 parts (Ends=No) gives 4 points at 0, 90, 180, 270 degrees')
        check(all(has(x, 120) for x in (30, 60, 90)) and len([p for p in pts if abs(p[1] - 120) < 1e-6]) == 3, 'DIV_3: Divide by segment length 30 on a 100 line (Ends=No) puts points at x = 30, 60, 90')
        check(len(entities(f, 'LINE')) == 4 and len(entities(f, 'CIRCLE')) == 2, 'MEA_5: the objects are not cut: still 4 lines and 2 circles (found %d, %d); the polyline stays one' % (len(entities(f, 'LINE')), len(entities(f, 'CIRCLE'))))
    return s, chk


# MODIFY-0054 BREAKATPOINT -----------------------------------------------------------------------------------------------------------------
@case('breakatpoint')
def breakatpoint():
    s = PRE + """Line 0,0,0 10,0,0
SelLast
BreakAtPoint
4,0,0
Line 0,10,0 10,10,0
SelLast
BreakAtPoint
4,13,0
Circle 50,50,0 3
SelLast
BreakAtPoint
Polyline 0,30,0 10,30,0 10,40,0
Enter
SelLast
BreakAtPoint
10,35,0
Arc 100,0,0 105,0,0 100,9,0
SelLast
BreakAtPoint
103.5355339059327,3.5355339059327,0
SelAll
Export @T@/bap.dxf
"""

    def chk(d, log):
        f = d + '/bap.dxf'
        check(has_segment(f, (0, 0), (4, 0)) and has_segment(f, (4, 0), (10, 0)) and len(entities(f, 'LINE')) >= 4, 'BAP_1: BreakAtPoint at (4,0) splits the line (0,0)-(10,0) into (0,0)-(4,0) and (4,0)-(10,0)')
        check(has_segment(f, (0, 10), (4, 10)) and has_segment(f, (4, 10), (10, 10)) and not has_segment(f, (0, 10), (10, 10)), 'BAP_2: a point off the object, (4,13), is projected onto it: (0,10)-(10,10) is broken at (4,10)')
        check(len(R.entities(f, 'CIRCLE')) == 1 and R.has_circle(f, 50, 50, 3) and any('closed' in l and 'BreakAtPoint' in l for l in log.split('\n')), 'BAP_3: the circle is refused (one circle, unchanged, with a BreakAtPoint warning about closed objects)')
        check(any(same_chain(c, [(0, 30, 0), (10, 30, 0), (10, 35, 0)]) for c in chains(f)) and has_segment(f, (10, 35), (10, 40)), 'BAP_4: the polyline (0,30)-(10,30)-(10,40) broken at (10,35) gives (0,30)-(10,30)-(10,35) and (10,35)-(10,40)')
        ar = R.arcs(f)

        def span(a):
            return gf(a, 50) % 360, gf(a, 51) % 360

        ok = len(ar) == 2 and all(close(gf(a, 10), 100, 1e-7) and close(gf(a, 20), 0, 1e-7) and close(gf(a, 40), 5, 1e-7) for a in ar)
        spans = sorted(span(a) for a in ar) if ar else []
        check(ok and len(spans) == 2 and close(spans[0][0], 0, 1e-5) and close(spans[0][1], 45, 1e-5) and close(spans[1][0], 45, 1e-5) and close(spans[1][1], 90, 1e-5),
              'BAP_5: BreakAtPoint at the 45 degree point of the quarter arc about (100,0), r=5 gives the arcs 0 -> 45 and 45 -> 90 degrees')
    return s, chk


# DRAW-0026 PTYPE ------------------------------------------------------------------------------------------------------------------------
# PDMODE and PDSIZE are stored in the drawing; the DXF reference lists $PDMODE (group 70) and $PDSIZE (group 40) in the HEADER section.
# A cross (3) inside a circle (+32) is 35; a negative size is a percentage of the viewport height.
@case('ptype')
def ptype():
    s = PRE + """Point 1,2,3
PType Style=35 Size=-5
Enter
PType Style=7
Enter
SelAll
Export @T@/pt.dxf
"""

    def chk(d, log):
        f = d + '/pt.dxf'
        h = header_vars(f)
        check(h.get('$PDMODE') == [(70, '35')], 'PTY_1: the DXF header holds $PDMODE = 35 (cross in a circle) (found %s)' % h.get('$PDMODE'))
        check(h.get('$PDSIZE') and close(float(h['$PDSIZE'][0][1]), -5, 1e-9), 'PTY_2: the DXF header holds $PDSIZE = -5 (5 percent of the viewport height) (found %s)' % h.get('$PDSIZE'))
        check(len(points(f)) == 1, 'PTY_3: the point object is still there (found %d)' % len(points(f)))
        check(any('PType' in l and 'ignored' in l for l in log.split('\n')), 'PTY_4: Style=7 is not a PDMODE code and is refused, leaving 35 in force')
    return s, chk


# DRAW-0041 3DFACE --------------------------------------------------------------------------------------------------------------------------------
# DXF 3DFACE: four corners (groups 10, 11, 12, 13); a triangle repeats the third corner as the fourth. The corners need not be coplanar.
@case('face3d')
def face3d():
    s = PRE + """3DFace 0,0,0 10,0,0 10,10,5 0,10,5
3DFace 20,0,0 30,0,0 30,10,9 20,10,2
3DFace 40,0,0 50,0,0 50,10,3
Enter
SelAll
Export @T@/f3d.dxf
"""

    def chk(d, log):
        f = d + '/f3d.dxf'
        fs = entities(f, '3DFACE')
        check(len(fs) == 3, 'F3D_1: three 3DFACE entities (found %d)' % len(fs))
        corners = [[(gf(r, c), gf(r, c + 10), gf(r, c + 20)) for c in (10, 11, 12, 13)] for r in fs]

        def has_face(want):
            return any(all(dist(a, b) < 1e-6 for a, b in zip(c, want)) for c in corners)

        check(has_face([(0, 0, 0), (10, 0, 0), (10, 10, 5), (0, 10, 5)]), 'F3D_2: the planar quad keeps its four corners (0,0,0) (10,0,0) (10,10,5) (0,10,5)')
        check(has_face([(20, 0, 0), (30, 0, 0), (30, 10, 9), (20, 10, 2)]), 'F3D_3: the non-planar quad keeps its four corners (z = 0, 0, 9, 2)')
        check(has_face([(40, 0, 0), (50, 0, 0), (50, 10, 3), (50, 10, 3)]), 'F3D_4: three corners then Enter make a triangle: the fourth corner repeats the third (DXF 3DFACE)')
    return s, chk


# DRAW-0056 About Circles: centre-radius and three-point circles ----------------------------------------------------------------------------
# Three points fix the circumscribed circle: (0,0),(4,0),(0,3) is a right triangle, so the hypotenuse (4,0)-(0,3) of length 5 is a diameter:
# centre = its midpoint (2,1.5), radius 2.5. (100,0),(110,0),(105,5): the chord (100,0)-(110,0) has its perpendicular bisector x = 105 and
# |(105,y) - (100,0)| = |(105,y) - (105,5)| gives y = 0, radius 5.
@case('circles')
def circles():
    s = PRE + """Circle 5,5,0 2.5
Circle 3Point 0,0,0 4,0,0 0,3,0
Circle 3P 100,0,0 110,0,0 105,5,0
SelAll
Export @T@/ci3.dxf
"""

    def chk(d, log):
        f = d + '/ci3.dxf'
        check(R.has_circle(f, 5, 5, 2.5), 'CI3_1: Circle centre (5,5) radius 2.5')
        check(R.has_circle(f, 2, 1.5, 2.5), 'CI3_2: Circle 3Point through (0,0), (4,0), (0,3) has centre (2,1.5) and radius 2.5 (Thales: the hypotenuse is a diameter)')
        check(R.has_circle(f, 105, 0, 5), 'CI3_3: Circle 3P through (100,0), (110,0), (105,5) has centre (105,0) and radius 5')
        check(len(entities(f, 'CIRCLE')) == 3, 'CI3_4: exactly three circles (found %d)' % len(entities(f, 'CIRCLE')))
    return s, chk


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    mode, name, d = sys.argv[1], sys.argv[2], sys.argv[3]
    if name not in CASES:
        print('FAIL cad2d_rows3: unknown case %s' % name)
        return 1
    script, chk = CASES[name]()
    if mode == 'gen':
        open('%s/%s.txt' % (d, name), 'w').write(script.replace('@T@', d))
        return 0
    chk(d, R.app_log(sys.argv[4]) if len(sys.argv) > 4 else '')
    return 1 if R.failed else 0


if __name__ == '__main__':
    sys.exit(main())
