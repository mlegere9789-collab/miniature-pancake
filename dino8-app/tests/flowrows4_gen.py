#!/usr/bin/env python3
"""Second batch of Blender Geometry Nodes Register-row tests (src/flow/FlowNodesBlender2.cpp): for each row one .dflow graph,
one player script and one tests/sections/97_<name>.sh section (the section name, upper-cased, is the row's acceptance test id).

Machinery and conventions are those of flowrows3_gen.py (read its header): a graph ends in Construct Point -> Bake, a real
value is compared as round((value - closed form) * scale) = 0, counts are exact integers, every section carries a negative
control. Closed forms (polygon chord and area, frustum volume, Euler's V - E + F, Pappus-free lattice counts, the edge /
face recurrences of splitting every quad into four, the dodecahedron formula, point-to-box distances, ray-plane hits) are
worked out by hand and computed here by independent code, never read back from the nodes.
Run from dino8-app/tests:  python3 flowrows4_gen.py
"""
import math
import os

from flowrows_gen import SECTION
from flowrows3_gen import (T, PI, SQ5, ngon_area, frustum_volume, uvsphere_volume, dist, poly_len, resample, box)

HERE = os.path.dirname(os.path.abspath(__file__))


def vol(t, geo, c, msg, scale=1e5):
    n = t.n("Volume"); t.w(geo, 0, n, 0); t.exact(n, 0, c, msg, scale)


def area(t, geo, c, msg, scale=1e5):
    n = t.n("Area"); t.w(geo, 0, n, 0); t.exact(n, 0, c, msg, scale)


def cube(t, size=(2, 2, 2), loc=None):
    c = t.n("Cube", None, ("i", 2), ("i", 2), ("i", 2)); sv = t.vec(*size); t.w(sv, 0, c, 0)
    if loc is not None:
        c = move(t, c, loc)
    return c


def move(t, geo, loc, rot=(0, 0, 0), scl=(1, 1, 1)):
    x = t.n("Transform Geometry")
    t.w(geo, 0, x, 0)
    for port, v in ((1, loc), (2, rot), (3, scl)):
        t.w(t.vec(*v), 0, x, port)
    return x


def line(t, a, b):
    ln = t.n("Curve Line", ("i", 0)); t.w(t.pt(*a), 0, ln, 1); t.w(t.pt(*b), 0, ln, 2)
    return ln


def lst(t, *items):
    """Merge chain: a list of the items in order (a node input takes one wire, so a multi-input is built with Merge)."""
    cur = items[0]
    for it in items[1:]:
        m = t.n("Merge"); t.w(cur, 0, m, 0); t.w(it, 0, m, 1)
        cur = m
    return cur


def sample(t, curve, factor, pos, tan, msg, scale=1e12):
    s = t.n("Sample Curve", None, ("i", 0), float(factor)); t.w(curve, 0, s, 0)
    t.point_exact(s, 0, pos, msg + ": position", scale)
    if tan is not None:
        t.point_exact(s, 1, tan, msg + ": tangent", scale)


def bez(p0, h0, h1, p1, t):
    u = 1 - t
    return tuple(u ** 3 * p0[a] + 3 * u * u * t * h0[a] + 3 * u * t * t * h1[a] + t ** 3 * p1[a] for a in range(3))


def make_rows():
    rows = []

    def row(name, label, title, closed, build):
        t = T()
        build(t)
        t.control()
        rows.append((name, label, title, closed, t))

    # ---------------------------------------------------------------- curves
    def b_bez(t):
        P0, H0, H1, P1 = (0, 0, 0), (0, 6, 0), (6, 6, 0), (6, 0, 0)
        b = t.n("Bezier Segment", ("i", 8))
        for port, p in ((1, P0), (2, H0), (3, H1), (4, P1)):
            t.w(t.pt(*p), 0, b, port)
        t.dsize(b, pt=9, sp=1, msg="8 edges give")
        pts = [bez(P0, H0, H1, P1, i / 8) for i in range(9)]
        t.clen(b, poly_len(pts, False), "the 8-edge polyline of the cubic Bezier through (0,0,0) (0,6,0) (6,6,0) (6,0,0)")
        t.bbox(b, (0, 0, 0), (6, 4.5, 0), "B(1/2) = (3, 4.5, 0) is the highest point (y(t) = 18 t (1-t)); x runs 0..6")
        d = t.n("Bezier Segment")
        t.dsize(d, pt=17, msg="the default Resolution 16 gives")
        t.bbox(d, (-1, 0, 0), (1, 0.375, 0), "default (-1,0,0) (-0.5,0.5,0) (0.5,0.5,0) (1,0,0): B(1/2).y = 3 * 0.125 * 0.5 * 2 = 0.375")
        s = t.n("Bezier Segment", ("i", 1))
        t.clen(s, 2.0, "1 edge joins the end points (-1,0,0) and (1,0,0): length 2")
    row("bfbez", "Bézier Segment", "Bezier Segment",
        "B(t) = (1-t)^3 P0 + 3 (1-t)^2 t H0 + 3 (1-t) t^2 H1 + t^3 P1 at t = i / Resolution (Resolution + 1 points): P0 = (0,0,0), H0 = (0,6,0), H1 = (6,6,0), P1 = (6,0,0) gives x(t) = 6 (3t^2 - 2t^3) ... y(t) = 18 t (1-t), peak (3, 4.5, 0) at t = 1/2; defaults peak at y = 0.375; 1 edge is the chord. The Resolution = edges convention is from recollection of the Blender manual",
        b_bez)

    def b_scur(t):
        ln = line(t, (0, 0, 0), (0, 0, 10))
        sample(t, ln, 0.25, (0, 0, 2.5), (0, 0, 1), "line, factor 0.25 of length 10")
        s = t.n("Sample Curve", None, ("i", 1), None, 7.0); t.w(ln, 0, s, 0)
        t.point_exact(s, 0, (0, 0, 7), "Length mode 7 along the line: position")
        over = t.n("Sample Curve", None, None, 3.0); t.w(ln, 0, over, 0)
        t.point_exact(over, 0, (0, 0, 10), "a factor above 1 is clamped to the end: position")
        q = t.n("Quadrilateral", 6.0, 4.0)
        sample(t, q, 0.15, (0, 2, 0), (-1, 0, 0), "rectangle (start (3,2), counter-clockwise), s = 3 on the top edge")
        sample(t, q, 0.6, (-1, -2, 0), (1, 0, 0), "s = 12 on the bottom edge")
        sample(t, q, 0.9, (3, 0, 0), (0, 1, 0), "s = 18 on the closing right edge")
    row("bfscur", "Sample Curve", "Sample Curve",
        "position at a fraction of the length (perimeter included for a cyclic curve): line length 10 at 0.25 = (0,0,2.5), at length 7 = (0,0,7), factor 3 is clamped to (0,0,10); rectangle 6 x 4 from (3,2) through (-3,2), (-3,-2), (3,-2) and back: s = 3 -> (0,2,0) heading -X, s = 12 -> (-1,-2,0) heading +X, s = 18 -> (3,0,0) heading +Y",
        b_scur)

    def b_rev(t):
        ln = line(t, (0, 0, 0), (0, 0, 10))
        r = t.n("Reverse Curve"); t.w(ln, 0, r, 0)
        sample(t, r, 0.0, (0, 0, 10), (0, 0, -1), "reversed line starts at (0,0,10) heading -Z")
        sample(t, r, 1.0, (0, 0, 0), None, "and ends at the old start")
        t.dsize(r, pt=2, sp=1, msg="still a 2-point line:")
        q = t.n("Quadrilateral", 6.0, 4.0)
        rq = t.n("Reverse Curve"); t.w(q, 0, rq, 0)
        sample(t, rq, 0.0, (3, -2, 0), (-1, 0, 0), "reversed rectangle starts at the old last corner (3,-2) heading -X")
        t.dsize(rq, pt=4, msg="still cyclic with 4 points:")
        t.clen(rq, 20.0, "and still length 20")
        rb = t.n("Reverse Curve"); b = t.n("Bezier Segment", ("i", 4)); t.w(b, 0, rb, 0)
        t.clen(rb, poly_len([bez((-1, 0, 0), (-0.5, 0.5, 0), (0.5, 0.5, 0), (1, 0, 0), i / 4) for i in range(5)], False), "a reversed curve has the same length")
        sample(t, rb, 0.0, (1, 0, 0), None, "its first point is the old end (1,0,0)")
    row("bfrev", "Reverse Curve", "Reverse Curve",
        "the point order is reversed: line (0,0,0)->(0,0,10) starts at (0,0,10) heading -Z and ends at (0,0,0); the rectangle (3,2) (-3,2) (-3,-2) (3,-2) becomes (3,-2) (-3,-2) (-3,2) (3,2): starts at (3,-2) heading -X, length 20, 4 cyclic points; the length of a reversed open curve does not change",
        b_rev)

    def b_trim(t):
        ln = line(t, (0, 0, 0), (0, 0, 10))
        a = t.n("Trim Curve", None, ("i", 0), 0.2, 0.7); t.w(ln, 0, a, 0)
        t.clen(a, 5.0, "factors 0.2..0.7 of length 10 keep length 5")
        t.bbox(a, (0, 0, 2), (0, 0, 7), "from z = 2 to z = 7")
        b = t.n("Trim Curve", None, ("i", 1), 8.0, 3.0); t.w(ln, 0, b, 0)
        t.bbox(b, (0, 0, 3), (0, 0, 8), "Length mode 8..3 is swapped to 3..8")
        t.dsize(b, pt=2, sp=1, msg="a cut inside one edge has")
        q = t.n("Quadrilateral", 6.0, 4.0)
        c = t.n("Trim Curve", None, ("i", 0), 0.25, 0.75); t.w(q, 0, c, 0)
        t.dsize(c, pt=4, sp=1, msg="s = 5..15 of the rectangle keeps the two corners (-3,2), (-3,-2) between the cuts (-2,2) and (2,-2):")
        t.clen(c, 10.0, "half of the perimeter 20")
        t.bbox(c, (-3, -2, 0), (2, 2, 0), "box of (-2,2) (-3,2) (-3,-2) (2,-2)")
        w = t.n("Trim Curve", None, ("i", 0), 0.0, 1.0); t.w(ln, 0, w, 0)
        t.clen(w, 10.0, "0..1 keeps everything")
    row("bftrmc", "Trim Curve", "Trim Curve",
        "line length 10: factors 0.2..0.7 -> z 2..7 (length 5); Length mode 8..3 is swapped -> z 3..8; rectangle 6 x 4 from (3,2): s = 5 is (-2,2), s = 15 is (2,-2), the corners (-3,2) (s = 6) and (-3,-2) (s = 10) lie between: 4 points, length 10, box [-3,2] x [-2,2]; 0..1 keeps the whole line",
        b_trim)

    def b_cyc(t):
        ln = line(t, (0, 0, 0), (0, 0, 4))
        c = t.n("Set Spline Cyclic", None, ("b", True)); t.w(ln, 0, c, 0)
        t.dsize(c, pt=2, sp=1, msg="a cyclic 2-point line still has")
        t.clen(c, 8.0, "and its closing edge doubles the length: 4 + 4")
        q = t.n("Quadrilateral", 6.0, 4.0)
        o = t.n("Set Spline Cyclic", None, ("b", False)); t.w(q, 0, o, 0)
        t.dsize(o, pt=4, msg="opening the rectangle keeps")
        t.clen(o, 16.0, "but drops the closing edge: 20 - 4 = 16")
        ci = t.n("Curve Circle", ("i", 8), 1.0)
        oc = t.n("Set Spline Cyclic", None, ("b", False)); t.w(ci, 0, oc, 0)
        t.clen(oc, 14 * math.sin(PI / 8), "open 8-gon of radius 1: 7 chords of 2 sin(pi/8)")
        b = t.n("Bezier Segment", ("i", 4)); cb = t.n("Set Spline Cyclic", None, ("b", True)); t.w(b, 0, cb, 0)
        pts = [bez((-1, 0, 0), (-0.5, 0.5, 0), (0.5, 0.5, 0), (1, 0, 0), i / 4) for i in range(5)]
        t.clen(cb, poly_len(pts, True), "closing the 5-point Bezier adds the chord (1,0,0) -> (-1,0,0) of length 2")
        t.dsize(cb, pt=5, sp=1, msg="and keeps its 5 points:")
        d = t.n("Set Spline Cyclic"); t.w(q, 0, d, 0)
        t.clen(d, 16.0, "the default Cyclic is false")
    row("bfcyc", "Set Spline Cyclic", "Set Spline Cyclic",
        "cyclic adds the closing edge from the last point to the first: 2-point line of length 4 -> 8, 5-point Bezier + chord 2; not cyclic removes it: rectangle 20 - 4 = 16, 8-gon of radius 1 = 7 * 2 sin(pi/8); the points never change; the default is not cyclic",
        b_cyc)

    def b_fill(t):
        q = t.n("Quadrilateral", 6.0, 4.0)
        f = t.n("Fill Curve"); t.w(q, 0, f, 0)
        t.dsize(f, pt=4, ed=4, fa=1, co=4, msg="rectangle as one n-gon:")
        area(t, f, 24.0, "area 6 * 4 = 24")
        ft = t.n("Fill Curve", None, ("i", 1)); t.w(q, 0, ft, 0)
        t.dsize(ft, pt=4, ed=5, fa=2, co=6, msg="rectangle as triangles (V - 2 = 2):")
        s = t.n("Star", ("i", 5), 1.0, 2.0, 0.0)
        fs = t.n("Fill Curve"); t.w(s, 0, fs, 0)
        t.dsize(fs, pt=10, ed=10, fa=1, co=10, msg="concave 5-point star as one n-gon:")
        area(t, fs, 5 * 2.0 * 1.0 * math.sin(PI / 5), "star area = P ro ri sin(pi/P) (2P triangles from the centre of 1/2 ro ri sin(pi/P))")
        fst = t.n("Fill Curve", None, ("i", 1)); t.w(s, 0, fst, 0)
        t.dsize(fst, pt=10, ed=17, fa=8, co=24, msg="star as triangles: F = V - 2 = 8, E = V + F - 1 = 17 (a disk),")
        area(t, fst, 5 * 2.0 * 1.0 * math.sin(PI / 5), "same area from triangles")
        ci = t.n("Curve Circle", ("i", 6), 2.0)
        fc = t.n("Fill Curve"); t.w(ci, 0, fc, 0)
        area(t, fc, ngon_area(6, 2.0), "hexagon of radius 2: (6/2) r^2 sin(60 degrees)")
        rc = t.n("Raycast", None, None, None, 100.0); t.w(f, 0, rc, 0)
        t.w(t.pt(0, 0, 5), 0, rc, 1); t.w(t.vec(0, 0, -1), 0, rc, 2)
        t.point_exact(rc, 2, (0, 0, 1), "a counter-clockwise curve fills with a face normal +Z")
        o = t.n("Reverse Curve"); t.w(q, 0, o, 0)
        fo = t.n("Fill Curve"); t.w(o, 0, fo, 0)
        r2 = t.n("Raycast", None, None, None, 100.0); t.w(fo, 0, r2, 0)
        t.w(t.pt(0, 0, 5), 0, r2, 1); t.w(t.vec(0, 0, -1), 0, r2, 2)
        t.point_exact(r2, 2, (0, 0, -1), "the reversed (clockwise) curve fills with normal -Z")
    row("bffill", "Fill Curve", "Fill Curve",
        "polygon area: rectangle 6 x 4 = 24 (one n-gon: V 4, E 4, F 1, corners 4; as triangles F = V - 2 = 2, E = 5, corners 6); concave 5-point star ro 2, ri 1 = P ro ri sin(pi/P) = 10 sin 36 degrees (n-gon: 10/10/1/10; triangles F = 8, E = V + F - 1 = 17); regular hexagon r = 2: 3 r^2 sin 60; normal +Z for a counter-clockwise curve and -Z when reversed",
        b_fill)

    def b_c2m(t):
        zl = line(t, (0, 0, 0), (0, 0, 5))
        pr = t.n("Curve Circle", ("i", 8), 1.0)
        m = t.n("Curve to Mesh", None, None, ("b", True)); t.w(zl, 0, m, 0); t.w(pr, 0, m, 1)
        t.dsize(m, pt=16, ed=24, fa=10, co=48, msg="8-gon profile along a 2-point line with caps: V = 2 * 8, F = 8 quads + 2 caps, E = 24,")
        vol(t, m, ngon_area(8, 1.0) * 5.0, "volume = profile area (8/2) sin(45 degrees) times length 5")
        t.bbox(m, (-1, -1, 0), (1, 1, 5), "the profile keeps its X and Y, the curve gives Z", 1e5)
        nc = t.n("Curve to Mesh", None, None, ("b", False)); t.w(zl, 0, nc, 0); t.w(pr, 0, nc, 1)
        t.dsize(nc, pt=16, ed=24, fa=8, co=32, msg="without caps:")
        sd = t.n("Subdivide Curve", None, ("i", 2)); t.w(zl, 0, sd, 0)
        m4 = t.n("Curve to Mesh", None, None, ("b", True)); t.w(sd, 0, m4, 0); t.w(pr, 0, m4, 1)
        t.dsize(m4, pt=32, ed=8 * 4 + 8 * 3, fa=8 * 3 + 2, msg="4 curve points x 8 profile points:")
        vol(t, m4, ngon_area(8, 1.0) * 5.0, "the same volume with 4 rings")
        xl = line(t, (0, 0, 0), (5, 0, 0))
        mx = t.n("Curve to Mesh", None, None, ("b", True)); t.w(xl, 0, mx, 0); t.w(pr, 0, mx, 1)
        vol(t, mx, ngon_area(8, 1.0) * 5.0, "along X the swept volume is the same")
        ring = t.n("Curve Circle", ("i", 8), 3.0); sq = t.n("Quadrilateral", 1.0, 1.0)
        tor = t.n("Curve to Mesh"); t.w(ring, 0, tor, 0); t.w(sq, 0, tor, 1)
        t.dsize(tor, pt=32, ed=64, fa=32, co=128, msg="cyclic path x cyclic profile (a torus): V - E + F = 0 with V = 8 * 4,")
        t.bbox_z(tor, -0.5, 0.5, "the square profile's Y becomes world Z for a path in the XY plane", 1e5)
        sh = t.n("Curve to Mesh"); t.w(sd, 0, sh, 0); t.w(line(t, (0, 0, 0), (2, 0, 0)), 0, sh, 1)
        t.dsize(sh, pt=8, ed=4 + 2 * 3, fa=3, co=12, msg="open profile along 4 points: a sheet of 3 quads,")
        area(t, sh, 10.0, "sheet area width 2 x length 5")
        wr = t.n("Curve to Mesh"); t.w(zl, 0, wr, 0)
        t.dsize(wr, pt=2, ed=1, fa=0, msg="no profile: the curve's points joined by edges:")
    row("bfc2m", "Curve to Mesh", "Curve to Mesh",
        "n points along the curve x m profile points: V = n m, F = (n-1) m quads (+2 n-gon caps), E = V + F - 2 for a capped tube (Euler): 8-gon profile along a 2-point line V 16, E 24, F 10 (8 + 2), corners 48; volume = profile area (m/2) r^2 sin(2 pi/m) x length 5 = 14.14...; the same with 4 rings (V 32, F 26, E 56); a cyclic path with a cyclic profile is a torus: V - E + F = 0 (32 / 64 / 32); an open 2-point profile along 4 points is a sheet of 3 quads of area 2 x 5 = 10. The profile frame (X along cross(Z, T)) is from recollection; only frame-independent quantities are checked off the Z axis",
        b_c2m)

    def b_c2p(t):
        ln = line(t, (0, 0, 0), (0, 0, 10))
        p = t.n("Curve to Points", None, ("i", 5)); t.w(ln, 0, p, 0)
        t.dsize(p, pt=5, ed=0, fa=0, msg="5 points on the line:")
        t.bbox(p, (0, 0, 0), (0, 0, 10), "both ends kept")
        tg = t.n("Get List Item", None, ("i", 2)); t.w(p, 1, tg, 0)
        t.point_exact(tg, 0, (0, 0, 1), "tangent of the line")
        q = t.n("Quadrilateral", 6.0, 4.0)
        rect = [(3.0, 2.0, 0.0), (-3.0, 2.0, 0.0), (-3.0, -2.0, 0.0), (3.0, -2.0, 0.0)]
        c8 = t.n("Curve to Points", None, ("i", 8)); t.w(q, 0, c8, 0)
        lo, hi = box(resample(rect, True, 8))
        t.dsize(c8, pt=8, msg="8 points 2.5 apart round the rectangle:")
        t.bbox(c8, lo, hi, "their box (independent perimeter walk)")
        tg0 = t.n("Get List Item", None, ("i", 0)); t.w(c8, 1, tg0, 0)
        t.point_exact(tg0, 0, (-1, 0, 0), "the first point (3,2) heads -X")
        c20 = t.n("Curve to Points", None, ("i", 20)); t.w(q, 0, c20, 0)
        t.bbox(c20, (-3, -2, 0), (3, 2, 0), "20 points 1 apart land on all four corners")
        c1 = t.n("Curve to Points", None, ("i", 1)); t.w(ln, 0, c1, 0)
        t.dsize(c1, pt=1, msg="Count 1:")
        t.bbox(c1, (0, 0, 0), (0, 0, 0), "a single point sits at the start")
    row("bfc2p", "Curve to Points", "Curve to Points",
        "Count points evenly spaced by length: line length 10, 5 points z = 0, 2.5, ... 10 with tangent (0,0,1); rectangle 6 x 4 (L = 20) with 8 points 2.5 apart (box from an independent perimeter walk), with 20 points 1 apart on every corner (box +-3, +-2); the first rectangle point (3,2) heads -X; Count 1 is the start point. The Evaluated and Length modes are untested",
        b_c2p)

    def b_m2c(t):
        ml = t.n("Mesh Line", ("i", 5))
        mc = t.n("Mesh to Curve"); t.w(ml, 0, mc, 0)
        ln = t.n("List Length (Blender)"); t.w(mc, 0, ln, 0)
        t.count(ln, 0, 1, "a chain of 5 vertices is one spline")
        c0 = t.n("Get List Item", None, ("i", 0)); t.w(mc, 0, c0, 0)
        t.dsize(c0, pt=5, sp=1, msg="of 5 points:")
        t.clen(c0, 4.0, "length 4 (4 edges of 1)")
        ci = t.n("Mesh Circle", ("i", 8), 2.0, ("i", 0))
        mci = t.n("Mesh to Curve"); t.w(ci, 0, mci, 0)
        l2 = t.n("List Length (Blender)"); t.w(mci, 0, l2, 0)
        t.count(l2, 0, 1, "an edge loop is one spline")
        c1 = t.n("Get List Item", None, ("i", 0)); t.w(mci, 0, c1, 0)
        t.dsize(c1, pt=8, sp=1, msg="of 8 points:")
        t.clen(c1, 8 * 2 * 2.0 * math.sin(PI / 8), "cyclic: 8 chords of 2 r sin(pi/8) with r = 2, the closing edge included", 1e5)
        cu = t.n("Cube", None, ("i", 2), ("i", 2), ("i", 2))
        mcu = t.n("Mesh to Curve"); t.w(cu, 0, mcu, 0)
        l3 = t.n("List Length (Blender)"); t.w(mcu, 0, l3, 0)
        t.count(l3, 0, 12, "the 12 edges of a cube meet three at every corner: one spline per edge")
        c3 = t.n("Get List Item", None, ("i", 3)); t.w(mcu, 0, c3, 0)
        t.clen(c3, 1.0, "of length 1 (default cube size)")
        gr = t.n("Grid", 2.0, 2.0, ("i", 2), ("i", 2))
        mg = t.n("Mesh to Curve"); t.w(gr, 0, mg, 0)
        l4 = t.n("List Length (Blender)"); t.w(mg, 0, l4, 0)
        t.count(l4, 0, 1, "the 4 edges around one quad are one cyclic spline")
    row("bfm2c", "Mesh to Curve", "Mesh to Curve",
        "Mesh Line of 5 vertices = 1 spline of 5 points, length 4; an unfilled 8-gon = 1 cyclic spline of 8 points, length 8 * 2 r sin(pi/8) with r = 2; the 12 edges of a cube (degree 3 at every corner) = 12 splines of length 1; the 4 edges of one quad = 1 cyclic spline. Splitting at corners with three or more edges is from recollection of the Blender behaviour",
        b_m2c)

    # ---------------------------------------------------------------- meshes and geometry
    def b_join(t):
        a = cube(t, (1, 1, 1)); b = cube(t, (1, 1, 1), (3, 0, 0))
        j = t.n("Join Geometry"); t.w(lst(t, a, b), 0, j, 0)
        t.dsize(j, pt=16, ed=24, fa=12, co=48, msg="two cubes:")
        vol(t, j, 2.0, "volumes add: 1 + 1")
        t.bbox(j, (-0.5, -0.5, -0.5), (3.5, 0.5, 0.5), "box spans both cubes", 1e5)
        cy = t.n("Cylinder"); cu = t.n("Cube")
        j2 = t.n("Join Geometry"); t.w(lst(t, cy, cu), 0, j2, 0)
        t.dsize(j2, pt=64 + 8, ed=96 + 12, fa=34 + 6, co=192 + 24, msg="a default cylinder (2 n-gon caps) and a cube: the n-gons stay one face each:")
        g1 = t.n("Grid", 1.0, 1.0, ("i", 3), ("i", 3)); g2 = t.n("Grid", 1.0, 1.0, ("i", 3), ("i", 3)); g3 = t.n("Grid", 1.0, 1.0, ("i", 2), ("i", 2))
        j3 = t.n("Join Geometry"); t.w(lst(t, g1, g2, g3), 0, j3, 0)
        t.dsize(j3, pt=9 + 9 + 4, ed=12 + 12 + 4, fa=4 + 4 + 1, msg="three grids:")
        area(t, j3, 3.0, "areas add: 1 + 1 + 1")
    row("bfjoin", "Join Geometry", "Join Geometry",
        "counts add and nothing is welded: two cubes 16/24/12/48, volume 1 + 1, box x -0.5..3.5; a default cylinder (64/96/34/192) plus a cube (8/12/6/24) = 72/108/40/216 (n-gons kept, so the cap faces are not split); three grids 22/28/9, area 3",
        b_join)

    def b_flip(t):
        c = cube(t, (2, 2, 2))
        vol(t, c, 8.0, "an unflipped cube has volume +8")
        f = t.n("Flip Faces"); t.w(c, 0, f, 0)
        vol(t, f, -8.0, "flipped: signed volume -8")
        t.dsize(f, pt=8, ed=12, fa=6, co=24, msg="same elements:")
        n = t.n("Flip Faces", None, ("b", False)); t.w(c, 0, n, 0)
        vol(t, n, 8.0, "Selection false changes nothing")
        ff = t.n("Flip Faces"); t.w(f, 0, ff, 0)
        vol(t, ff, 8.0, "flipping twice restores +8")
        gr = t.n("Grid", 1.0, 1.0, ("i", 3), ("i", 3))
        rc = t.n("Raycast", None, None, None, 100.0); t.w(gr, 0, rc, 0); t.w(t.pt(0.1, 0.1, 2), 0, rc, 1); t.w(t.vec(0, 0, -1), 0, rc, 2)
        t.point_exact(rc, 2, (0, 0, 1), "a grid faces +Z")
        fg = t.n("Flip Faces"); t.w(gr, 0, fg, 0)
        r2 = t.n("Raycast", None, None, None, 100.0); t.w(fg, 0, r2, 0); t.w(t.pt(0.1, 0.1, 2), 0, r2, 1); t.w(t.vec(0, 0, -1), 0, r2, 2)
        t.point_exact(r2, 2, (0, 0, -1), "the flipped grid faces -Z")
        t.dsize(fg, pt=9, ed=12, fa=4, msg="grid elements unchanged:")
    row("bfflip", "Flip Faces", "Flip Faces",
        "reversing every face negates the signed volume of a closed mesh (+8 -> -8 for a cube of size 2, back to +8 when flipped twice) and the normal of a grid (+Z -> -Z) and leaves every count unchanged; Selection false does nothing",
        b_flip)

    def b_tri(t):
        c = cube(t, (2, 2, 2))
        tr = t.n("Triangulate"); t.w(c, 0, tr, 0)
        t.dsize(tr, pt=8, ed=18, fa=12, co=36, msg="cube: 6 quads -> 12 triangles, 6 new diagonals:")
        vol(t, tr, 8.0, "volume unchanged")
        area(t, tr, 24.0, "area unchanged")
        k = t.n("Triangulate", None, ("i", 5)); t.w(c, 0, k, 0)
        t.dsize(k, pt=8, ed=12, fa=6, co=24, msg="Minimum Vertices 5 leaves quads alone:")
        cy = t.n("Cylinder", ("i", 8), ("i", 1), 2.0, 3.0, ("i", 1))
        tc = t.n("Triangulate"); t.w(cy, 0, tc, 0)
        t.dsize(tc, pt=16, fa=16 + 12, co=3 * 28, msg="8-gon prism: 8 quads -> 16 triangles, 2 caps -> 6 triangles each:")
        vol(t, tc, ngon_area(8, 2.0) * 3.0, "prism volume unchanged")
        tk = t.n("Triangulate", None, ("i", 9)); t.w(cy, 0, tk, 0)
        t.dsize(tk, pt=16, fa=10, co=48, msg="Minimum Vertices 9 keeps the 8-gon caps and the quads:")
        mc = t.n("Mesh Circle", ("i", 8), 2.0, ("i", 1))
        tm = t.n("Triangulate"); t.w(mc, 0, tm, 0)
        t.dsize(tm, pt=8, ed=8 + 5, fa=6, co=18, msg="one 8-gon -> 6 triangles, 5 diagonals:")
        area(t, tm, ngon_area(8, 2.0), "area unchanged")
        gr = t.n("Grid", 1.0, 1.0, ("i", 3), ("i", 3))
        tg = t.n("Triangulate"); t.w(gr, 0, tg, 0)
        t.dsize(tg, pt=9, ed=12 + 4, fa=8, msg="3 x 3 grid: 4 quads -> 8 triangles, 4 diagonals:")
    row("bftri", "Triangulate", "Triangulate",
        "each quad becomes 2 triangles and adds one edge; an n-gon of k corners becomes k - 2 triangles and k - 3 edges; volume and area do not change: cube (8/12/6/24) -> 8/18/12/36; 8-gon prism (16 vertices, 8 quads + 2 caps) -> 16 + 2 * 6 = 28 triangles; Minimum Vertices above the corner count keeps the face (5 keeps quads; 9 keeps the 8-gon caps and the quads); 8-gon -> 6 triangles; 3 x 3 grid -> 8 triangles, 16 edges",
        b_tri)

    def b_subm(t):
        c = cube(t, (2, 2, 2))
        s1 = t.n("Subdivide Mesh"); t.w(c, 0, s1, 0)
        t.dsize(s1, pt=8 + 12 + 6, ed=2 * 12 + 4 * 6, fa=24, co=96, msg="cube level 1: V' = V + E + F, E' = 2E + 4F, F' = 4F:")
        area(t, s1, 24.0, "surface unchanged: area 24")
        vol(t, s1, 8.0, "volume 8")
        s2 = t.n("Subdivide Mesh", None, ("i", 2)); t.w(c, 0, s2, 0)
        t.dsize(s2, pt=26 + 48 + 24, ed=2 * 48 + 4 * 24, fa=96, msg="level 2 repeats the recurrence (26/48/24 -> 98/192/96):")
        i = t.n("Ico Sphere", 2.0, ("i", 1))
        si = t.n("Subdivide Mesh"); t.w(i, 0, si, 0)
        t.dsize(si, pt=12 + 30, ed=2 * 30 + 3 * 20, fa=80, co=240, msg="icosahedron level 1 (triangles: V' = V + E, E' = 2E + 3F, F' = 4F):")
        a = 2.0 / math.sin(2 * PI / 5)
        vol(t, si, (5.0 / 12.0) * (3 + SQ5) * a ** 3, "volume of the icosahedron of circumradius 2 is unchanged")
        g = t.n("Grid", 1.0, 1.0, ("i", 3), ("i", 3))
        sg = t.n("Subdivide Mesh"); t.w(g, 0, sg, 0)
        t.dsize(sg, pt=9 + 12 + 4, ed=2 * 12 + 4 * 4, fa=16, msg="3 x 3 grid:")
        area(t, sg, 1.0, "area 1")
        z = t.n("Subdivide Mesh", None, ("i", 0)); t.w(c, 0, z, 0)
        t.dsize(z, pt=8, ed=12, fa=6, msg="Level 0 changes nothing:")
    row("bfsubm", "Subdivide Mesh", "Subdivide Mesh",
        "splitting every quad into four (edge midpoints and a centre) and every triangle into four: V' = V + E (+ F for quads), E' = 2E + 4F (quads) or 2E + 3F (triangles), F' = 4F: cube 8/12/6 -> 26/48/24 -> 98/192/96; icosahedron 12/30/20 -> 42/120/80; 3 x 3 grid 9/12/4 -> 25/40/16; area and volume do not move (24 and 8 for the size-2 cube). The Blender triangle and n-gon splitting rules are from recollection (n-gons are flagged here)",
        b_subm)

    def b_trn(t):
        c = cube(t, (2, 2, 2))
        tl = move(t, c, (1, 2, 3))
        t.bbox(tl, (0, 1, 2), (2, 3, 4), "translation (1,2,3) of the size-2 cube", 1e5)
        u = t.n("Cube", None, ("i", 2), ("i", 2), ("i", 2))
        sc = move(t, u, (0, 0, 0), (0, 0, 0), (2, 3, 4))
        vol(t, sc, 24.0, "scale (2,3,4) of the unit cube: volume 24")
        t.bbox(sc, (-1, -1.5, -2), (1, 1.5, 2), "box +-(1, 1.5, 2)", 1e5)
        box246 = cube(t, (2, 4, 6))
        rz = move(t, box246, (0, 0, 0), (0, 0, PI / 2))
        t.bbox(rz, (-2, -1, -3), (2, 1, 3), "rotation 90 degrees about Z swaps X and Y extents", 1e5)
        rx = move(t, box246, (0, 0, 0), (PI / 2, 0, 0))
        t.bbox(rx, (-1, -3, -2), (1, 3, 2), "rotation 90 degrees about X swaps Y and Z extents", 1e5)
        sr = move(t, u, (0, 0, 5), (0, 0, PI / 2), (2, 1, 1))
        t.bbox(sr, (-0.5, -1, 4.5), (0.5, 1, 5.5), "scale (2,1,1) comes first, then the 90 degree turn about Z, then the move: x +-0.5, y +-1, z 4.5..5.5", 1e5)
        mi = move(t, u, (0, 0, 0), (0, 0, 0), (-1, 1, 1))
        vol(t, mi, 1.0, "a mirror (scale -1) keeps the faces outward: volume +1")
        ln = line(t, (0, 0, 0), (0, 0, 1))
        r = move(t, ln, (0, 0, 0), (PI / 2, 0, PI / 2))
        t.bbox(r, (0, 0, 0), (1, 0, 0), "XYZ order: (0,0,1) turned 90 degrees about X goes to (0,-1,0), then 90 degrees about Z to (1,0,0)")
        ci = t.n("Curve Circle", ("i", 4), 1.0)
        sci = move(t, ci, (0, 0, 0), (0, 0, 0), (3, 1, 1))
        t.bbox(sci, (-3, -1, 0), (3, 1, 0), "circle (4 points) scaled (3,1,1)")
        pt = t.pt(1, 2, 3)
        pp = move(t, pt, (1, 1, 1), (0, 0, 0), (2, 2, 2))
        t.bbox(pp, (3, 5, 7), (3, 5, 7), "point (1,2,3) scaled by 2 then moved by (1,1,1)")
    row("bftrn", "Transform Geometry", "Transform Geometry",
        "p' = T + R S p: scale (2,3,4) of the unit cube has volume 24 and box +-(1,1.5,2); rotating a 2 x 4 x 6 box 90 degrees about Z gives extents 4 x 2 x 6, about X 2 x 6 x 4; scale (2,1,1), then rotation about Z by 90 degrees, then move (0,0,5) of the unit cube spans x +-0.5, y +-1, z 4.5..5.5; a mirror keeps the volume +1; Euler XYZ (90, 0, 90 degrees) takes (0,0,1) -> (0,-1,0) -> (1,0,0); point (1,2,3) -> (3,5,7)",
        b_trn)

    def b_merg(t):
        g = t.n("Grid", 1.0, 1.0, ("i", 3), ("i", 3))
        g2 = move(t, g, (1, 0, 0))
        j = t.n("Join Geometry"); t.w(lst(t, g, g2), 0, j, 0)
        t.dsize(j, pt=18, ed=24, fa=8, msg="two grids side by side share a column but are not welded:")
        w = t.n("Merge by Distance"); t.w(j, 0, w, 0)
        t.dsize(w, pt=15, ed=22, fa=8, co=32, msg="welded: the 3 shared vertices and 2 shared edges merge:")
        area(t, w, 2.0, "area 2")
        c = cube(t, (1, 1, 1))
        jc = t.n("Join Geometry"); t.w(lst(t, c, c), 0, jc, 0)
        wc = t.n("Merge by Distance"); t.w(jc, 0, wc, 0)
        t.dsize(wc, pt=8, ed=12, fa=6, msg="two identical cubes weld into one (duplicate faces go):")
        vol(t, wc, 1.0, "volume 1")
        far = t.n("Merge by Distance", None, 0.1); t.w(j, 0, far, 0)
        t.dsize(far, pt=15, msg="a distance of 0.1 is below the grid spacing 0.5 and welds only the shared column:")
        big = t.n("Merge by Distance", None, 0.6); t.w(g, 0, big, 0)
        t.dsize(big, pt=1, msg="above the spacing 0.5 every vertex of a grid chains into one:")
        ml = t.n("Mesh Line", ("i", 5))
        wl = t.n("Merge by Distance", None, 0.5); t.w(ml, 0, wl, 0)
        t.dsize(wl, pt=5, ed=4, msg="spacing 1 is above the distance 0.5: nothing merges:")
        wl2 = t.n("Merge by Distance", None, 1.5); t.w(ml, 0, wl2, 0)
        t.dsize(wl2, pt=1, ed=0, msg="distance 1.5 chains the five vertices of a line into one:")
    row("bfmerg", "Merge by Distance", "Merge by Distance",
        "two 3 x 3 grids side by side: 18 vertices, 24 edges; welded: 15 vertices (the shared column of 3 merges), 22 edges (2 shared edges), 8 faces, area 2; two identical unit cubes weld to 8 / 12 / 6 with volume 1 (duplicate faces are removed); distance 0.1 welds only coincident vertices; above the grid spacing everything chains together; a line of spacing 1 stays whole at distance 0.5 and collapses to one vertex at 1.5. The position of a welded group (the mean here) is untested",
        b_merg)

    def b_m2p(t):
        c = cube(t, (2, 2, 2))
        p = t.n("Mesh to Points"); t.w(c, 0, p, 0)
        t.dsize(p, pt=8, ed=0, fa=0, co=0, msg="the 8 corners of a cube as points:")
        t.bbox(p, (-1, -1, -1), (1, 1, 1), "same box", 1e5)
        s = t.n("UV Sphere", ("i", 8), ("i", 4), 2.0)
        ps = t.n("Mesh to Points"); t.w(s, 0, ps, 0)
        t.dsize(ps, pt=8 * 3 + 2, ed=0, fa=0, msg="UV sphere 8 x 4 has 26 vertices:")
        ml = t.n("Mesh Line", ("i", 4))
        pl = t.n("Mesh to Points"); t.w(ml, 0, pl, 0)
        t.dsize(pl, pt=4, ed=0, msg="the edges of a line are dropped:")
        t.bbox(pl, (0, 0, 0), (0, 0, 3), "z = 0..3")
    row("bfm2p", "Mesh to Points", "Mesh to Points",
        "Vertices mode keeps one point per vertex and no edges or faces: cube 8, UV sphere 8 x 4 = 8 * 3 + 2 = 26, line of 4 = 4 with the same box. The Edges, Faces and Corners modes and the Radius / Selection fields are untested",
        b_m2p)

    def b_p2v(t):
        p = t.n("Points", ("i", 5), None, 0.1); t.w(t.pt(1, 2, 3), 0, p, 1)
        v = t.n("Points to Vertices"); t.w(p, 0, v, 0)
        t.dsize(v, pt=5, ed=0, fa=0, msg="5 points become 5 vertices:")
        t.bbox(v, (1, 2, 3), (1, 2, 3), "at (1,2,3)")
        c = cube(t, (2, 2, 2))
        vc = t.n("Points to Vertices"); t.w(c, 0, vc, 0)
        t.dsize(vc, pt=8, ed=0, fa=0, msg="faces of the input are dropped:")
        a = cube(t, (2, 2, 2)); j = t.n("Join Geometry"); t.w(lst(t, a, p), 0, j, 0)
        t.dsize(j, pt=13, fa=6, ed=12, msg="points joined to a cube keep the mesh intact:")
    row("bfp2v", "Points to Vertices", "Points to Vertices",
        "the vertex count and positions are the points' own and there are no edges or faces: 5 points at (1,2,3) -> 5 vertices; a cube's 8 vertices with its 12 edges and 6 faces dropped; joined to a cube they add 5 to its 8 vertices",
        b_p2v)

    def b_dual(t):
        c = cube(t, (1, 1, 1))
        d = t.n("Dual Mesh"); t.w(c, 0, d, 0)
        t.dsize(d, pt=6, ed=12, fa=8, co=24, msg="dual of a cube is an octahedron:")
        vol(t, d, 4.0 / 3.0 * 0.5 ** 3, "volume 4/3 d^3 for vertices at distance d = 0.5 from the centre")
        t.bbox(d, (-0.5, -0.5, -0.5), (0.5, 0.5, 0.5), "its vertices are the face centres", 1e5)
        dd = t.n("Dual Mesh"); t.w(d, 0, dd, 0)
        t.dsize(dd, pt=8, ed=12, fa=6, co=24, msg="dual of the octahedron is a cube again:")
        vol(t, dd, (1.0 / 3.0) ** 3, "of side 1/3 (the octahedron's face centres are at +-1/6): volume 1/27")
        i = t.n("Ico Sphere", 1.0, ("i", 1))
        di = t.n("Dual Mesh"); t.w(i, 0, di, 0)
        t.dsize(di, pt=20, ed=30, fa=12, co=60, msg="dual of the icosahedron: a dodecahedron, 12 pentagons:")
        a = 1.0 / math.sin(2 * PI / 5)
        rho = a * (3 * math.sqrt(3.0) + math.sqrt(15.0)) / 12.0       # icosahedron inradius = dodecahedron circumradius
        b = rho * 4.0 / (math.sqrt(3.0) * (1 + SQ5))                  # dodecahedron edge from its circumradius
        vol(t, di, b ** 3 * (15 + 7 * SQ5) / 4.0, "dodecahedron volume b^3 (15 + 7 sqrt 5) / 4 with edge b = 4 rho / (sqrt 3 (1 + sqrt 5)), rho = inradius of the unit-circumradius icosahedron")
        cy = t.n("Cylinder", ("i", 8), ("i", 1), 1.0, 2.0, ("i", 1))
        dc = t.n("Dual Mesh"); t.w(cy, 0, dc, 0)
        t.dsize(dc, pt=10, ed=24, fa=16, msg="dual of an 8-gon prism (8 quads + 2 caps = 10 vertices, 16 vertices -> 16 faces):")
    row("bfdual", "Dual Mesh", "Dual Mesh",
        "vertices at face centres, one face per original vertex: cube (side 1) -> octahedron 6/12/8 with volume 4/3 * 0.5^3; its dual -> cube of side 1/3 (volume 1/27); icosahedron (R = 1) -> dodecahedron 20/30/12 (12 pentagons, 60 corners) of volume b^3 (15 + 7 sqrt 5)/4 with b = 4 rho / (sqrt 3 (1 + sqrt 5)), rho = a (3 sqrt 3 + sqrt 15)/12, a = 1/sin(2 pi/5); an 8-gon prism (16 vertices, 10 faces) has 10 dual vertices, 16 dual faces and 24 edges (Euler)",
        b_dual)

    def b_bool(t):
        a = cube(t, (2, 2, 2)); b = cube(t, (2, 2, 2), (1, 0, 0))
        u = t.n("Mesh Boolean", ("i", 1)); t.w(a, 0, u, 1); t.w(lst(t, b), 0, u, 2)
        vol(t, u, 12.0, "union of two 2-cubes overlapping by half: 8 + 8 - 4", 1e4)
        t.bbox(u, (-1, -1, -1), (2, 1, 1), "union box x -1..2", 1e4)
        i = t.n("Mesh Boolean", ("i", 0)); t.w(a, 0, i, 1); t.w(lst(t, b), 0, i, 2)
        vol(t, i, 4.0, "intersection x 0..1: 1 * 2 * 2", 1e4)
        t.bbox(i, (0, -1, -1), (1, 1, 1), "intersection box", 1e4)
        d = t.n("Mesh Boolean", ("i", 2)); t.w(a, 0, d, 1); t.w(lst(t, b), 0, d, 2)
        vol(t, d, 4.0, "difference A - B: x -1..0, 1 * 2 * 2", 1e4)
        t.bbox(d, (-1, -1, -1), (0, 1, 1), "difference box", 1e4)
        c = cube(t, (1, 1, 1), (-1, 0, 0))
        d2 = t.n("Mesh Boolean", ("i", 2)); t.w(a, 0, d2, 1); t.w(lst(t, b, c), 0, d2, 2)
        vol(t, d2, 3.5, "A minus (B and a unit cube over x -1.5..-0.5): 4 - 0.5 = 3.5", 1e4)
    row("bfbool", "Mesh Boolean", "Mesh Boolean",
        "two size-2 cubes shifted by 1 along X overlap in 1 x 2 x 2 = 4: union 8 + 8 - 4 = 12 (box x -1..2), intersection 4 (box x 0..1), difference A - B = 4 (box x -1..0); A - (B and a unit cube covering x -1.5..-0.5 about the axis) = 4 - 0.5 = 3.5. Single precision mesh: volumes compared within 5e-5",
        b_bool)

    def b_prox(t):
        c = cube(t, (2, 2, 2))
        for p, pos, dd, msg in (((5, 0, 0), (1, 0, 0), 4.0, "a point 4 outside a face"), ((3, 3, 0), (1, 1, 0), 2 * math.sqrt(2.0), "beside an edge: the edge point (1,1,0), distance 2 sqrt 2"),
                                ((3, 3, 3), (1, 1, 1), 2 * math.sqrt(3.0), "beyond a corner"), ((0, 0, 0.5), (0, 0, 1), 0.5, "inside: the distance to the surface, 0.5 to the top face"),
                                ((0.25, 0.5, 0.75), (0.25, 0.5, 1), 0.25, "inside near the top: 0.25")):
            n = t.n("Geometry Proximity"); t.w(c, 0, n, 0); t.w(t.pt(*p), 0, n, 1)
            t.point_exact(n, 0, pos, msg + ": position", 1e5)
            t.exact(n, 1, dd, msg + ": distance", 1e5)
        g = t.n("Grid", 1.0, 1.0, ("i", 3), ("i", 3))
        n2 = t.n("Geometry Proximity"); t.w(g, 0, n2, 0); t.w(t.pt(0.2, 0.3, 2), 0, n2, 1)
        t.point_exact(n2, 0, (0.2, 0.3, 0), "grid, point above: foot (0.2,0.3,0)", 1e5)
        t.exact(n2, 1, 2.0, "grid, point above: distance 2", 1e5)
        n3 = t.n("Geometry Proximity"); t.w(g, 0, n3, 0); t.w(t.pt(3, 0, 1), 0, n3, 1)
        t.point_exact(n3, 0, (0.5, 0, 0), "grid, point beside: nearest is the edge point (0.5,0,0)", 1e5)
        t.exact(n3, 1, math.sqrt(2.5 ** 2 + 1.0), "grid, point beside: distance sqrt(2.5^2 + 1)", 1e5)
    row("bfprox", "Geometry Proximity", "Geometry Proximity",
        "closest point of a size-2 cube: (5,0,0) -> (1,0,0) at 4; (3,3,0) -> (1,1,0) at 2 sqrt 2; (3,3,3) -> (1,1,1) at 2 sqrt 3; inside points give the nearest face: (0,0,0.5) -> (0,0,1) at 0.5, (0.25,0.5,0.75) -> (0.25,0.5,1) at 0.25; unit grid: (0.2,0.3,2) -> (0.2,0.3,0) at 2, (3,0,1) -> (0.5,0,0) at sqrt(2.5^2 + 1). Single precision: within 5e-6",
        b_prox)

    def b_ray(t):
        c = cube(t, (2, 2, 2))

        def ray(o, d, length, hit, pos=None, nrm=None, dd=None, msg=""):
            n = t.n("Raycast", None, None, None, float(length)); t.w(c, 0, n, 0); t.w(t.pt(*o), 0, n, 1); t.w(t.vec(*d), 0, n, 2)
            t.count(n, 0, 1 if hit else 0, msg + ": Is Hit %d" % (1 if hit else 0))
            if hit:
                t.point_exact(n, 1, pos, msg + ": hit position", 1e5)
                t.point_exact(n, 2, nrm, msg + ": hit normal", 1e5)
            t.exact(n, 3, dd, msg + ": distance", 1e5)
        ray((5, 0, 0), (-1, 0, 0), 100, True, (1, 0, 0), (1, 0, 0), 4.0, "from +X towards the cube")
        ray((5, 0, 0), (-3, 0, 0), 100, True, (1, 0, 0), (1, 0, 0), 4.0, "the direction is normalised")
        ray((0.5, 0.5, 5), (0, 0, -1), 100, True, (0.5, 0.5, 1), (0, 0, 1), 4.0, "from above")
        ray((0, 0, 0), (0, 1, 0), 100, True, (0, 1, 0), (0, 1, 0), 1.0, "from inside the box the first hit is the wall ahead")
        ray((4, 2, 0.5), (-2, -1, 0), 100, True, (1, 0.5, 0.5), (1, 0, 0), 1.5 * math.sqrt(5.0), "an oblique ray hits the +X face at t = 1.5 (distance 1.5 sqrt 5)")
        ray((5, 0, 0), (-1, 0, 0), 3, False, None, None, 3.0, "out of reach (4 > Ray Length 3): the distance is the Ray Length")
        ray((5, 0, 0), (1, 0, 0), 100, False, None, None, 100.0, "pointing away")
        ray((5, 5, 0), (-1, 0, 0), 100, False, None, None, 100.0, "passing beside the cube")
        g = t.n("Grid", 1.0, 1.0, ("i", 3), ("i", 3))
        up = t.n("Raycast", None, None, None, 100.0); t.w(g, 0, up, 0); t.w(t.pt(0.1, 0.1, -2), 0, up, 1); t.w(t.vec(0, 0, 1), 0, up, 2)
        t.exact(up, 3, 2.0, "a ray from below hits the grid's back: distance 2", 1e5)
        t.point_exact(up, 2, (0, 0, 1), "and reports the face normal +Z", 1e5)
    row("bfray", "Raycast", "Raycast",
        "size-2 cube: from (5,0,0) along -X hits (1,0,0), normal +X, distance 4 (also when the direction is (-3,0,0)); from (0.5,0.5,5) down hits (0.5,0.5,1) normal +Z; from the centre along +Y hits (0,1,0) at 1; (4,2,0.5) along (-2,-1,0) hits the x = 1 face at t = 1.5 so distance 1.5 sqrt 5; Ray Length 3 < 4 misses and reports Hit Distance = Ray Length; pointing away and passing beside miss; a unit grid hit from below reports normal +Z at 2. Position and normal of a miss are zero. The Blender miss values are from recollection",
        b_ray)

    def b_hull(t):
        s = t.n("UV Sphere", ("i", 8), ("i", 4), 2.0)
        h = t.n("Convex Hull"); t.w(s, 0, h, 0)
        vol(t, h, uvsphere_volume(8, 4, 2.0), "the 8 x 4 sphere polyhedron is convex: the hull has its volume", 1e4)
        t.bbox(h, (-2, -2, -2), (2, 2, 2), "box +-2", 1e4)
        j = t.n("Join Geometry"); c = cube(t, (2, 2, 2)); p = t.n("Points", ("i", 1)); t.w(t.pt(0, 0, 3), 0, p, 1)
        t.w(lst(t, c, p), 0, j, 0)
        hp = t.n("Convex Hull"); t.w(j, 0, hp, 0)
        vol(t, hp, 8.0 + 4.0 * 2.0 / 3.0, "cube + apex above the top face: a square pyramid of base 4, height 2 on top: 8 + 8/3", 1e4)
        t.bbox(hp, (-1, -1, -1), (1, 1, 3), "box z -1..3", 1e4)
        cc = t.n("Cube", None, ("i", 3), ("i", 3), ("i", 3)); sv = t.vec(2, 2, 2); t.w(sv, 0, cc, 0)
        hc = t.n("Convex Hull"); t.w(cc, 0, hc, 0)
        vol(t, hc, 8.0, "a 3 x 3 x 3 lattice cube has the hull of its 8 corners: volume 8", 1e4)
    row("bfhull", "Convex Hull", "Convex Hull",
        "the hull of a convex polyhedron is itself: UV sphere 8 x 4 radius 2 has the frustum-stack volume (the same closed form as BFUVSP) and box +-2; a size-2 cube plus one point 2 above its top face centre adds a square pyramid of base 4 and height 2: 8 + 8/3 = 10.667, box z -1..3; a 3 x 3 x 3 lattice cube has the hull of its 8 corners, volume 8. Single precision mesh: within 5e-5",
        b_hull)

    def b_pts(t):
        p = t.n("Points", ("i", 7), None, 0.5); t.w(t.pt(1, -2, 3), 0, p, 1)
        t.dsize(p, pt=7, ed=0, fa=0, co=0, sp=0, msg="7 points:")
        t.bbox(p, (1, -2, 3), (1, -2, 3), "all at (1,-2,3)")
        d = t.n("Points")
        t.dsize(d, pt=1, msg="the default is one point:")
        t.bbox(d, (0, 0, 0), (0, 0, 0), "at the origin")
        big = t.n("Points", ("i", 1000))
        t.dsize(big, pt=1000, msg="1000 points:")
    row("bfpts", "Points", "Points",
        "Count points at Position: 7 at (1,-2,3), box a single point; the default is 1 point at the origin; 1000 points. The Position and Radius fields are constants here (untested as fields)",
        b_pts)

    def b_near(t):
        ml = t.n("Mesh Line", ("i", 10))
        for p, idx, msg in (((0, 0, 3.4), 3, "z = 3.4 is nearest to vertex 3"), ((0, 0, 3.6), 4, "z = 3.6 is nearest to vertex 4"), ((5, 5, -5), 0, "far off to the side, below: vertex 0"),
                            ((0, 0, 99), 9, "far above: the last vertex 9"), ((0, 0, 0), 0, "exactly at vertex 0"), ((0.3, -0.2, 6.1), 6, "off axis: z = 6.1 -> vertex 6")):
            n = t.n("Sample Nearest"); t.w(ml, 0, n, 0); t.w(t.pt(*p), 0, n, 1)
            t.count(n, 0, idx, msg)
        c = cube(t, (2, 2, 2))
        n = t.n("Sample Nearest"); t.w(c, 0, n, 0); t.w(t.pt(0, 0, 0), 0, n, 1)
        t.count(n, 0, 0, "a point at the centre of a cube is equally near all corners: the lowest index wins")
    row("bfnear", "Sample Nearest", "Sample Nearest",
        "vertex i of the 10-vertex line sits at z = i, so the nearest to z = 3.4 is 3, to 3.6 is 4, to (5,5,-5) is 0, to z = 99 is 9, to the vertex itself is its own index. The index order of other meshes and the tie rule are untested against Blender",
        b_near)

    def b_comb(t):
        a = t.n("Series", 1.0, 1.0, ("i", 3)); b = t.n("Series", 10.0, 1.0, ("i", 2))
        m = t.n("Merge"); t.w(a, 0, m, 0); t.w(b, 0, m, 1)
        c = t.n("Combine List"); t.w(m, 0, c, 0)
        t.tag += 1
        t.g.probe(c, 0, t.tag)
        t.expect.append((t.tag, "1 2 3 10 11", "the five items come out in the order they went in"))
        ln = t.n("List Length (Blender)"); t.w(c, 0, ln, 0)
        t.count(ln, 0, 5, "list length 5")
        gi = t.n("Get List Item", None, ("i", 3)); t.w(c, 0, gi, 0)
        t.exact(gi, 0, 10.0, "item 3 is 10")
        one = t.n("Combine List", ("t", "solo"))
        l1 = t.n("List Length (Blender)"); t.w(one, 0, l1, 0)
        t.count(l1, 0, 1, "one item is a list of length 1")
        tx = t.n("Merge", ("t", "x"), ("t", "y")); ct = t.n("Combine List"); t.w(tx, 0, ct, 0)
        g1 = t.n("Get List Item", None, ("i", 1)); t.w(ct, 0, g1, 0)
        t.textis(g1, "y", "text items keep their order: item 1 is 'y'")
    row("bfcomb", "Combine List", "Combine List",
        "the items come back as one list in the order they were given: 1 2 3 then 10 11 -> length 5, item 3 = 10; a single text item is a list of 1; text items 'x', 'y' -> item 1 = 'y'",
        b_comb)
    return rows


def main():
    for name, label, title, closed, t in make_rows():
        nobj = sum(len(vals.split()) if vals else 0 for _tag, vals, _msg in t.expect)
        with open(os.path.join(HERE, "flowrows_%s.dflow" % name), "w") as f:
            f.write(t.g.text())
        with open(os.path.join(HERE, "flowrows_%s_script.txt" % name), "w") as f:
            f.write("# Flow Register row %s (Blender Geometry Nodes): player + bake, read back by sections/97_%s.sh (see flowrows4_gen.py)\n"
                    "Grasshopper\nGrasshopperPlayer @GRAPH@ Bake=Yes\n@expect_objects %d\nSelAll\nList\n" % (title, name, nobj))
        checks = "\n".join('fr_chk %d "%s" "%s"' % (tag, vals, msg.replace('"', "'")) for tag, vals, msg in t.expect)
        with open(os.path.join(HERE, "sections", "97_%s.sh" % name), "w") as f:
            f.write(SECTION.format(up=name.upper(), rid="geometry_nodes/" + label, title=title, name=name, closed=closed,
                                   nn=len(t.g.nodes), nobj=nobj, checks=checks))
        print(name, label, "nodes", len(t.g.nodes), "objects", nobj)


if __name__ == "__main__":
    main()
