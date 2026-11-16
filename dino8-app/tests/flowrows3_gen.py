#!/usr/bin/env python3
"""Writes the Blender Geometry Nodes Register-row tests (src/flow/FlowNodesBlender.cpp): for each row one .dflow
graph, one player script and one tests/sections/96_<name>.sh section (the section name, upper-cased, is the row's
acceptance test id).

Same machinery as flowrows_gen.py / flowrows2_gen.py: every graph ends in Construct Point (X = the value under test,
Y = a case tag) -> Bake and the section reads the baked points back with `List`. A real value is never compared as
printed text (List keeps four significant digits): exact() builds Remap Numbers(value; c .. c+1 -> 0 .. scale) ->
Round To Multiple(1), which prints 0 exactly when |value - c| < 0.5 / scale. Curve data are doubles (scale 1e12,
tolerance 5e-13); a mesh keeps single precision vertices, so a measured area / volume uses scale 1e5 (tolerance 5e-6)
or 1e4 and the row says so. Counts are exact integers.

Every closed form below is derived by hand from the shape (regular-polygon chord 2 r sin(pi/n), polygon area
(n/2) r^2 sin(2 pi/n), frustum volume h (A1 + A2 + sqrt(A1 A2)) / 3, Euler's V - E + F = 2 for a closed surface) and is
computed here by independent code, never read back from the node. The Blender conventions the nodes follow (point order,
point counts of Resolution) are from recollection of the Blender manual, which could not be fetched from this
environment (docs.blender.org does not resolve); the closed forms that do not depend on them are listed first.
Run from dino8-app/tests:  python3 flowrows3_gen.py
"""
import math
import os

from flowrows_gen import G, SECTION

HERE = os.path.dirname(os.path.abspath(__file__))
PI = math.pi
SQ5 = math.sqrt(5.0)


class T:
    """A graph plus a running case tag and the list of expected (tag, values, message)."""

    def __init__(self):
        self.g = G()
        self.tag = 0
        self.expect = []
        self.used = False

    def n(self, typ, *ins):
        return self.g.node(typ, *ins)

    def vec(self, x, y, z):
        return self.g.node("Vector XYZ", float(x), float(y), float(z))

    def pt(self, x, y, z):
        return self.g.node("Construct Point", float(x), float(y), float(z))

    def w(self, a, ap, b, bp):
        self.g.wire(a, ap, b, bp)

    def count(self, src, port, value, msg):
        self.tag += 1
        self.g.probe(src, port, self.tag)
        self.expect.append((self.tag, str(value), msg))

    def exact(self, src, port, c, msg, scale=1e12):
        """src.port must equal the closed form c within 0.5 / scale; the baked residual prints 0."""
        self.tag += 1
        self.used = True
        rm = self.g.node("Remap Numbers", None, float(c), float(c) + 1.0, 0.0, float(scale))
        self.g.wire(src, port, rm, 0)
        ro = self.g.node("Round To Multiple", None, 1.0)
        self.g.wire(rm, 0, ro, 0)
        ab = self.g.node("Absolute")          # drops the sign of a negative zero, so the printout is exactly 0
        self.g.wire(ro, 0, ab, 0)
        self.g.probe(ab, 0, self.tag)
        self.expect.append((self.tag, "0", msg))

    def point_exact(self, src, port, cs, msg, scale=1e12):
        d = self.g.node("Deconstruct Point")
        self.g.wire(src, port, d, 0)
        for k, axis in enumerate("XYZ"):
            self.exact(d, k, cs[k], "%s %s" % (msg, axis), scale)

    def bbox(self, geo, lo, hi, msg, scale=1e12):
        b = self.g.node("Bounding Box")
        self.g.wire(geo, 0, b, 0)
        self.point_exact(b, 0, lo, msg + ": min", scale)
        self.point_exact(b, 1, hi, msg + ": max", scale)

    def clen(self, geo, c, msg):
        n = self.g.node("Curve Length (Blender)")
        self.g.wire(geo, 0, n, 0)
        self.exact(n, 0, c, msg)

    def dsize(self, geo, pt=None, ed=None, fa=None, co=None, sp=None, msg=""):
        n = self.g.node("Domain Size")
        self.g.wire(geo, 0, n, 0)
        for port, val, name in ((0, pt, "points"), (1, ed, "edges"), (2, fa, "faces"), (3, co, "corners"), (4, sp, "splines")):
            if val is not None:
                self.count(n, port, val, "%s: %d %s" % (msg, val, name))

    def textis(self, src, expected, msg):
        """Text Distance(src, expected) = 0 exactly when the two strings are equal."""
        n = self.g.node("Text Distance", None, ("t", expected))
        self.g.wire(src, 0, n, 0)
        self.count(n, 0, 0, msg)

    def control(self):
        """Negative control for the residual chain: Square(2) = 4 against the wrong closed form 4.5 is exactly -0.5, so the chain must bake 500000000000."""
        if not self.used:
            return
        self.tag += 1
        sq = self.g.node("Square", 2.0)
        rm = self.g.node("Remap Numbers", None, 4.5, 5.5, 0.0, 1e12)
        self.g.wire(sq, 0, rm, 0)
        ro = self.g.node("Round To Multiple", None, 1.0)
        self.g.wire(rm, 0, ro, 0)
        ab = self.g.node("Absolute")
        self.g.wire(ro, 0, ab, 0)
        self.g.probe(ab, 0, self.tag)
        self.expect.append((self.tag, "500000000000", "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"))


# ------------------------------------------------------------------------------------------ independent closed forms
def ngon_area(n, r):
    return 0.5 * n * r * r * math.sin(2 * PI / n)


def frustum_volume(n, r1, r2, h):
    a1, a2 = ngon_area(n, r1), ngon_area(n, r2)
    return h * (a1 + a2 + math.sqrt(a1 * a2)) / 3.0


def uvsphere_volume(S, R, r):
    """Stack of polygonal frusta, one per ring pair (poles are apexes)."""
    rho = [0.0] + [r * math.sin(PI * k / R) for k in range(1, R)] + [0.0]
    z = [r * math.cos(PI * k / R) for k in range(0, R + 1)]
    return sum(frustum_volume(S, rho[k], rho[k + 1], z[k] - z[k + 1]) for k in range(R))


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def poly_len(pts, closed):
    s = sum(dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1))
    if closed:
        s += dist(pts[-1], pts[0])
    return s


def resample(pts, closed, n):
    """n points evenly spaced by length along the poly line (open: both ends kept, spacing L/(n-1); closed: L/n)."""
    ring = pts + [pts[0]] if closed else list(pts)
    cum = [0.0]
    for i in range(1, len(ring)):
        cum.append(cum[-1] + dist(ring[i - 1], ring[i]))
    total = cum[-1]
    out = []
    for k in range(n):
        s = total * k / n if closed else total * k / (n - 1)
        i = 0
        while i + 2 < len(ring) and cum[i + 1] < s:
            i += 1
        t = (s - cum[i]) / (cum[i + 1] - cum[i])
        t = min(1.0, max(0.0, t))
        out.append(tuple(ring[i][a] + (ring[i + 1][a] - ring[i][a]) * t for a in range(3)))
    return out


def box(pts):
    return (tuple(min(p[a] for p in pts) for a in range(3)), tuple(max(p[a] for p in pts) for a in range(3)))


def make_rows():
    rows = []

    def row(name, label, title, closed, build):
        t = T()
        build(t)
        t.control()
        rows.append((name, label, title, closed, t))

    # ---------------------------------------------------------------- curve primitives
    def b_line(t):
        a = t.pt(1, 2, 3); b = t.pt(4, 6, 15)
        ln = t.n("Curve Line", ("i", 0)); t.w(a, 0, ln, 1); t.w(b, 0, ln, 2)
        t.clen(ln, 13.0, "Points mode (1,2,3)->(4,6,15) has length sqrt(9+16+144) = 13")
        t.dsize(ln, pt=2, sp=1, msg="a line has")
        t.bbox(ln, (1, 2, 3), (4, 6, 15), "box of the line")
        s = t.pt(0, 0, 0); dv = t.vec(3, 4, 0)
        d2 = t.n("Curve Line", ("i", 1), None, None, None, 10.0); t.w(s, 0, d2, 1); t.w(dv, 0, d2, 3)
        t.clen(d2, 10.0, "Direction mode (3,4,0) is made unit length first: Length 10")
        t.bbox(d2, (0, 0, 0), (6, 8, 0), "Direction mode ends at 10 * (0.6, 0.8, 0) = (6,8,0)")
        dz = t.vec(0, 0, 5)
        d3 = t.n("Curve Line", ("i", 1), None, None, None, 2.0); t.w(s, 0, d3, 1); t.w(dz, 0, d3, 3)
        t.bbox(d3, (0, 0, 0), (0, 0, 2), "Direction (0,0,5) with Length 2 ends at (0,0,2)")
        dflt = t.n("Curve Line")
        t.clen(dflt, 1.0, "the default line runs (0,0,0)->(0,0,1)")
    row("bfline", "Curve Line", "Curve Line",
        "|(4,6,15) - (1,2,3)| = sqrt(3^2 + 4^2 + 12^2) = 13; Direction mode ends at Start + unit(Direction) * Length: (3,4,0)/5 * 10 = (6,8,0), (0,0,5) at 2 = (0,0,2); the default line is (0,0,0)->(0,0,1) of length 1",
        b_line)

    def b_circle(t):
        c32 = t.n("Curve Circle", ("i", 32), 3.0)
        t.dsize(c32, pt=32, sp=1, msg="a 32-point circle has (the closing point is not counted)")
        t.clen(c32, 2 * 32 * 3.0 * math.sin(PI / 32), "32-gon of radius 3: perimeter 2 n r sin(pi/n)")
        t.bbox(c32, (-3, -3, 0), (3, 3, 0), "32 points include the angles 0, 90, 180, 270 degrees: box +-3 in XY")
        c4 = t.n("Curve Circle", ("i", 4), 2.0)
        t.clen(c4, 4 * 2.0 * math.sqrt(2.0), "4 points of radius 2 form a square of side 2 sqrt 2")
        t.bbox(c4, (-2, -2, 0), (2, 2, 0), "the square's corners are on the axes")
        c3 = t.n("Curve Circle", ("i", 3), 2.0)
        t.clen(c3, 6 * math.sqrt(3.0), "3 points of radius 2: equilateral triangle, side 2 sqrt 3, perimeter 6 sqrt 3")
        t.bbox(c3, (-1, -math.sqrt(3.0), 0), (2, math.sqrt(3.0), 0), "triangle points at 0, 120, 240 degrees: x from -1 to 2, y +-sqrt 3 (counter-clockwise from (r,0))")
        cd = t.n("Curve Circle")
        t.dsize(cd, pt=32, msg="the default circle has")
        t.clen(cd, 64 * math.sin(PI / 32), "the default radius is 1")
        c1 = t.n("Curve Circle", ("i", 2), 1.0)
        t.dsize(c1, pt=3, msg="Resolution below 3 is raised to a triangle, giving")
    row("bfcirc", "Curve Circle", "Curve Circle",
        "n points at angle 2 pi i / n on radius r: perimeter 2 n r sin(pi/n) (n=32, r=3 -> 18.8...; n=4, r=2 -> 8 sqrt 2; n=3, r=2 -> 6 sqrt 3); point count n with the closing point not counted; the n=3 box is x in [-r/2, r], y = +-r sqrt 3 / 2",
        b_circle)

    def b_arc(t):
        a5 = t.n("Arc (Blender)", ("i", 5), 2.0, 0.0, PI)
        t.dsize(a5, pt=5, sp=1, msg="5 points on a half circle:")
        t.clen(a5, 4 * 2 * 2.0 * math.sin(PI / 8), "4 chords of 2 r sin(sweep / (2 * 4)) with r = 2, sweep = pi")
        t.bbox(a5, (-2, 0, 0), (2, 2, 0), "half circle of radius 2 from angle 0 to pi: x +-2, y 0..2")
        ad = t.n("Arc (Blender)")
        t.dsize(ad, pt=16, msg="the default arc has")
        t.clen(ad, 15 * 2 * math.sin((5 * PI / 4) / 30), "default: 16 points, radius 1, sweep 5 pi / 4 -> 15 chords of 2 sin(sweep / 30)")
        aq = t.n("Arc (Blender)", ("i", 3), 1.0, PI / 2, PI)
        t.bbox(aq, (-1, -1, 0), (0, 1, 0), "3 points from 90 to 270 degrees: (0,1), (-1,0), (0,-1) -> x -1..0, y -1..1")
    row("bfarc", "Arc", "Arc",
        "points at angle a0 + sweep i / (n-1), both ends included: 5 points over pi at r=2 = 4 chords of 2 r sin(pi/8) = 16 sin(pi/8), box [-2,2] x [0,2]; default 16 points r=1 sweep 5 pi/4 -> 15 chords of 2 sin(5 pi/120)",
        b_arc)

    def b_quad(t):
        q = t.n("Quadrilateral", 6.0, 4.0)
        t.dsize(q, pt=4, sp=1, msg="a quadrilateral has")
        t.clen(q, 20.0, "6 x 4 rectangle: perimeter 2 (6 + 4) = 20")
        t.bbox(q, (-3, -2, 0), (3, 2, 0), "centred on the origin: x +-3, y +-2")
        qd = t.n("Quadrilateral")
        t.clen(qd, 8.0, "the default 2 x 2 square has perimeter 8")
        t.bbox(qd, (-1, -1, 0), (1, 1, 0), "the default square spans +-1")
    row("bfquad", "Quadrilateral", "Quadrilateral",
        "a w x h rectangle centred on the origin: perimeter 2 (w + h) = 20 for 6 x 4, corners at (+-w/2, +-h/2); four points",
        b_quad)

    def b_qbez(t):
        # Start (0,0,0), Middle (4,8,0), End (8,0,0): B(t) = (8 t, 16 t (1 - t), 0)
        s = t.pt(0, 0, 0); m = t.pt(4, 8, 0); e = t.pt(8, 0, 0)
        q = t.n("Quadratic Bezier", ("i", 8)); t.w(s, 0, q, 1); t.w(m, 0, q, 2); t.w(e, 0, q, 3)
        t.dsize(q, pt=9, sp=1, msg="8 edges give")
        pts = [(8 * i / 8, 16 * (i / 8) * (1 - i / 8), 0.0) for i in range(9)]
        t.clen(q, poly_len(pts, False), "the 8-edge polyline of B(t) = (8 t, 16 t (1-t), 0)")
        t.bbox(q, (0, 0, 0), (8, 4, 0), "ends at Start and End, peak B(1/2) = (Start + 2 Middle + End) / 4 = (4, 4, 0)")
        q2 = t.n("Quadratic Bezier", ("i", 2)); t.w(s, 0, q2, 1); t.w(m, 0, q2, 2); t.w(e, 0, q2, 3)
        t.clen(q2, 2 * dist((0, 0, 0), (4, 4, 0)), "2 edges: (0,0,0) -> (4,4,0) -> (8,0,0), length 2 * 4 sqrt 2")
        qd = t.n("Quadratic Bezier")
        t.dsize(qd, pt=17, msg="the default Resolution 16 gives")
        t.bbox(qd, (-1, 0, 0), (1, 1, 0), "default Start (-1,0,0), Middle (0,2,0), End (1,0,0): peak (0,1,0)")
    row("bfqbez", "Quadratic Bézier", "Quadratic Bezier",
        "B(t) = (1-t)^2 P0 + 2 t (1-t) P1 + t^2 P2 sampled at t = i / Resolution (Resolution + 1 points): P0 = (0,0,0), P1 = (4,8,0), P2 = (8,0,0) gives (8 t, 16 t (1-t), 0), the peak B(1/2) = (4,4,0); 2 edges is (0,0,0)-(4,4,0)-(8,0,0). The Resolution = edges convention is from recollection of the Blender manual",
        b_qbez)

    def b_star(t):
        s5 = t.n("Star", ("i", 5), 1.0, 2.0, 0.0)
        t.dsize(s5, pt=10, sp=1, msg="a 5-point star has")
        edge = math.sqrt(1 + 4 - 2 * 2 * math.cos(PI / 5))
        t.clen(s5, 10 * edge, "10 edges, each sqrt(ro^2 + ri^2 - 2 ro ri cos(pi/5)) by the law of cosines")
        t.bbox(s5, (2 * math.cos(4 * PI / 5), -2 * math.sin(2 * PI / 5), 0), (2, 2 * math.sin(2 * PI / 5), 0), "outer points at 0, 72, ..., 288 degrees: x from 2 cos 144 to 2, y +-2 sin 72")
        tw = PI / 10
        s4 = t.n("Star", ("i", 5), 1.0, 2.0, tw)
        e1 = math.sqrt(5 - 4 * math.cos(PI / 5 + tw)); e2 = math.sqrt(5 - 4 * math.cos(PI / 5 - tw))
        t.clen(s4, 5 * (e1 + e2), "twist pi/10 turns each inner point towards the next outer one: edges at angles pi/5 + tw and pi/5 - tw")
        s3 = t.n("Star", ("i", 3), 1.0, 1.0, 0.0)
        t.clen(s3, 6 * math.sqrt(2 - 2 * math.cos(PI / 3)), "equal radii make a regular hexagon of side 1: perimeter 6")
        sd = t.n("Star")
        t.dsize(sd, pt=16, msg="the default star (8 points) has")
    row("bfstar", "Star", "Star",
        "2P vertices alternating outer (angle 2 pi i / P) and inner (angle 2 pi (i + 1/2) / P + twist): without twist every edge is sqrt(ro^2 + ri^2 - 2 ro ri cos(pi/P)); with twist the two edges of each tooth are sqrt(.. cos(pi/P + tw)) and sqrt(.. cos(pi/P - tw)); ro = ri = 1, P = 3 is a hexagon of perimeter 6",
        b_star)

    def b_spiral(t):
        sp = t.n("Spiral", ("i", 4), 2.0, 1.0, 3.0, 4.0, ("b", False))
        t.dsize(sp, pt=9, sp=1, msg="4 points per rotation over 2 rotations gives 8 edges, so")
        pts = [((1 + 2 * i / 8) * math.cos(2 * PI * 2 * i / 8), (1 + 2 * i / 8) * math.sin(2 * PI * 2 * i / 8), 4.0 * i / 8) for i in range(9)]
        lo, hi = box(pts)
        t.bbox(sp, lo, hi, "box of the 9 points (radius 1 + i/4, angle i pi/2, height i/2)")
        t.clen(sp, poly_len(pts, False), "length of the 9-point helix polyline")
        t.bbox(sp, (-2.5, -2.75, 0), (3, 2.25, 4), "hand check: the last point is (3,0,4), y runs -2.75 (i=7) to 2.25 (i=5)")
        rv = t.n("Spiral", ("i", 4), 2.0, 1.0, 3.0, 4.0, ("b", True))
        t.bbox(rv, (-2.5, -2.25, 0), (3, 2.75, 4), "Reverse winds the other way: y mirrored")
    row("bfspir", "Spiral", "Spiral",
        "n = floor(Resolution * Rotations) = 8 edges, point i at angle 2 pi Rotations i / n, radius r0 + (r1 - r0) i / n = 1 + i/4, height 4 i / 8: last point (3,0,4); x in [-2.5 (i=6), 3], y in [-2.75 (i=7), 2.25 (i=5)]; Reverse mirrors y. The point-count convention is from recollection of the Blender manual",
        b_spiral)

    def b_clen(t):
        q = t.n("Quadrilateral", 6.0, 4.0)
        t.clen(q, 20.0, "closed 6 x 4 rectangle includes the closing edge: 20")
        a = t.pt(0, 0, 0); b = t.pt(2, 3, 6)
        ln = t.n("Curve Line", ("i", 0)); t.w(a, 0, ln, 1); t.w(b, 0, ln, 2)
        t.clen(ln, 7.0, "open line (0,0,0)->(2,3,6): sqrt(4 + 9 + 36) = 7")
        c = t.n("Curve Circle", ("i", 6), 5.0)
        t.clen(c, 30.0, "regular hexagon of radius 5 has side 5: perimeter 30")
        s = t.pt(0, 0, 0); m = t.pt(3, 4, 0); e = t.pt(6, 0, 0)
        q2 = t.n("Quadratic Bezier", ("i", 2)); t.w(s, 0, q2, 1); t.w(m, 0, q2, 2); t.w(e, 0, q2, 3)
        t.clen(q2, 2 * math.sqrt(9 + 4), "two edges (0,0,0)-(3,2,0)-(6,0,0): 2 sqrt 13")
    row("bfclen", "Curve Length", "Curve Length",
        "sum of the edge lengths, the closing edge of a cyclic curve included: 6 x 4 rectangle 20; line (2,3,6) 7; hexagon of radius 5 = 6 * 5 = 30; (0,0,0)-(3,2,0)-(6,0,0) = 2 sqrt 13",
        b_clen)

    def b_subd(t):
        q = t.n("Quadrilateral", 6.0, 4.0)
        sq = t.n("Subdivide Curve", None, ("i", 2)); t.w(q, 0, sq, 0)
        t.dsize(sq, pt=12, sp=1, msg="a closed 4-point curve with 2 cuts per edge has 4 * 3 =")
        t.clen(sq, 20.0, "subdividing keeps the shape: perimeter still 20")
        t.bbox(sq, (-3, -2, 0), (3, 2, 0), "box unchanged")
        a = t.pt(0, 0, 0); b = t.pt(0, 0, 9)
        ln = t.n("Curve Line", ("i", 0)); t.w(a, 0, ln, 1); t.w(b, 0, ln, 2)
        sl = t.n("Subdivide Curve", None, ("i", 2)); t.w(ln, 0, sl, 0)
        t.dsize(sl, pt=4, msg="an open 2-point line with 2 cuts has (2 - 1) * 3 + 1 =")
        t.clen(sl, 9.0, "length stays 9")
        s0 = t.n("Subdivide Curve", None, ("i", 0)); t.w(ln, 0, s0, 0)
        t.dsize(s0, pt=2, msg="0 cuts changes nothing:")
        s = t.pt(0, 0, 0); m = t.pt(4, 8, 0); e = t.pt(8, 0, 0)
        qb = t.n("Quadratic Bezier", ("i", 4)); t.w(s, 0, qb, 1); t.w(m, 0, qb, 2); t.w(e, 0, qb, 3)
        sb = t.n("Subdivide Curve", None, ("i", 3)); t.w(qb, 0, sb, 0)
        t.dsize(sb, pt=4 * 4 + 1, msg="a 5-point open curve with 3 cuts has 4 * 4 + 1 =")
        pts = [(8 * i / 4, 16 * (i / 4) * (1 - i / 4), 0.0) for i in range(5)]
        t.clen(sb, poly_len(pts, False), "the new points lie on the old edges, so the polyline length is unchanged")
    row("bfsubd", "Subdivide Curve", "Subdivide Curve",
        "k cuts in each of the m edges: open n points -> (n-1)(k+1) + 1, cyclic n points -> n (k+1): the 4-point rectangle with 2 cuts has 12 points, a 2-point line with 2 cuts 4, with 0 cuts 2, a 5-point curve with 3 cuts 17; the new points lie on the old edges so length and box do not change. The even spacing of the new points is not read back separately (untested)",
        b_subd)

    def b_resa(t):
        a = t.pt(0, 0, 0); b = t.pt(0, 0, 10)
        ln = t.n("Curve Line", ("i", 0)); t.w(a, 0, ln, 1); t.w(b, 0, ln, 2)
        rl = t.n("Resample Curve", None, ("i", 5)); t.w(ln, 0, rl, 0)
        t.dsize(rl, pt=5, sp=1, msg="resampling a line to 5:")
        t.clen(rl, 10.0, "both ends kept: length 10")
        t.bbox(rl, (0, 0, 0), (0, 0, 10), "ends stay at z = 0 and 10")
        q = t.n("Quadrilateral", 6.0, 4.0)
        r8 = t.n("Resample Curve", None, ("i", 8)); t.w(q, 0, r8, 0)
        rect = [(3.0, 2.0, 0.0), (-3.0, 2.0, 0.0), (-3.0, -2.0, 0.0), (3.0, -2.0, 0.0)]
        p8 = resample(rect, True, 8)
        t.dsize(r8, pt=8, sp=1, msg="a cyclic curve resampled to 8:")
        t.clen(r8, poly_len(p8, True), "8 points 20 / 8 = 2.5 apart round the rectangle, joined: octagon-like polygon")
        lo, hi = box(p8)
        t.bbox(r8, lo, hi, "box of the 8 resampled points")
        r4 = t.n("Resample Curve", None, ("i", 4)); t.w(q, 0, r4, 0)
        t.clen(r4, poly_len(resample(rect, True, 4), True), "4 points 5 apart")
        r20 = t.n("Resample Curve", None, ("i", 20)); t.w(q, 0, r20, 0)
        t.clen(r20, 20.0, "20 points 1 apart land on every corner (6 and 4 are whole): the rectangle itself, perimeter 20")
        t.bbox(r20, (-3, -2, 0), (3, 2, 0), "and its box")
        s = t.pt(0, 0, 0); m = t.pt(4, 8, 0); e = t.pt(8, 0, 0)
        qb = t.n("Quadratic Bezier", ("i", 2)); t.w(s, 0, qb, 1); t.w(m, 0, qb, 2); t.w(e, 0, qb, 3)
        rq = t.n("Resample Curve", None, ("i", 3)); t.w(qb, 0, rq, 0)
        mid = resample([(0.0, 0.0, 0.0), (4.0, 4.0, 0.0), (8.0, 0.0, 0.0)], False, 3)[1]
        t.bbox(rq, (0, 0, 0), (8, mid[1], 0), "3 points on (0,0,0)-(4,4,0)-(8,0,0): the middle one is at half the length, the corner (4,4,0)")
    row("bfresa", "Resample Curve", "Resample Curve",
        "Count mode: open curve keeps both ends, spacing L / (n-1) (line of length 10 to 5 points: z = 0, 2.5, 5, 7.5, 10); cyclic starts at the first point, spacing L / n (6 x 4 rectangle, L = 20: 20 points 1 apart pass every corner and give the rectangle back, 8 points 2.5 apart and 4 points 5 apart give the polygons the independent walk in flowrows3_gen.py computes); a 3-point resample of (0,0,0)-(4,4,0)-(8,0,0) puts its middle point at the corner (4,4,0). The Length mode is untested",
        b_resa)

    # ---------------------------------------------------------------- mesh primitives
    def b_cube(t):
        sz = t.vec(2, 3, 4)
        c2 = t.n("Cube", None, ("i", 2), ("i", 2), ("i", 2)); t.w(sz, 0, c2, 0)
        t.dsize(c2, pt=8, ed=12, fa=6, co=24, msg="the 2 x 2 x 2 lattice cube has")
        t.bbox(c2, (-1, -1.5, -2), (1, 1.5, 2), "Size (2,3,4) centred on the origin", 1e5)
        va = t.n("Volume"); t.w(c2, 0, va, 0); t.exact(va, 0, 24.0, "volume 2 * 3 * 4 = 24 (outward faces, closed)", 1e5)
        ar = t.n("Area"); t.w(c2, 0, ar, 0); t.exact(ar, 0, 52.0, "area 2 (2*3 + 3*4 + 2*4) = 52", 1e5)
        c3 = t.n("Cube", None, ("i", 3), ("i", 4), ("i", 5)); t.w(sz, 0, c3, 0)
        nx, ny, nz = 3, 4, 5
        v = nx * ny * nz - (nx - 2) * (ny - 2) * (nz - 2)
        f = 2 * ((nx - 1) * (ny - 1) + (ny - 1) * (nz - 1) + (nx - 1) * (nz - 1))
        t.dsize(c3, pt=v, ed=v + f - 2, fa=f, co=4 * f, msg="3 x 4 x 5 lattice points (surface only):")
        v3 = t.n("Volume"); t.w(c3, 0, v3, 0); t.exact(v3, 0, 24.0, "the subdivided cube has the same volume 24", 1e5)
        cd = t.n("Cube")
        t.dsize(cd, pt=8, fa=6, msg="the default cube has")
        vd = t.n("Volume"); t.w(cd, 0, vd, 0); t.exact(vd, 0, 1.0, "the default cube has volume 1", 1e5)
    row("bfcube", "Cube", "Cube",
        "surface lattice of nx x ny x nz points: V = nx ny nz - (nx-2)(ny-2)(nz-2) (8 for 2,2,2; 54 for 3,4,5), F = 2((nx-1)(ny-1) + (ny-1)(nz-1) + (nx-1)(nz-1)) (6; 52), E = V + F - 2 by Euler's V - E + F = 2 for a closed surface, corners 4F; volume sx sy sz = 24, area 2 (sx sy + sy sz + sx sz) = 52; the default cube has volume 1. Single precision vertices: volume and area compared within 5e-6",
        b_cube)

    def b_grid(t):
        sx = t.n("Grid", 4.0, 6.0, ("i", 3), ("i", 5))
        t.dsize(sx, pt=15, ed=2 * 5 + 4 * 3, fa=8, co=32, msg="a 3 x 5 vertex grid has V = 15, E = (nx-1) ny + (ny-1) nx = 22, F = 8, 4F =")
        t.bbox(sx, (-2, -3, 0), (2, 3, 0), "4 x 6 centred on the origin, flat in Z", 1e5)
        ar = t.n("Area"); t.w(sx, 0, ar, 0); t.exact(ar, 0, 24.0, "area 4 * 6 = 24", 1e5)
        gd = t.n("Grid")
        t.dsize(gd, pt=9, ed=12, fa=4, msg="the default grid (3 x 3 vertices) has")
        ar2 = t.n("Area"); t.w(gd, 0, ar2, 0); t.exact(ar2, 0, 1.0, "the default size is 1 x 1: area 1", 1e5)
        g2 = t.n("Grid", 1.0, 1.0, ("i", 2), ("i", 2))
        t.dsize(g2, pt=4, ed=4, fa=1, co=4, msg="a 2 x 2 vertex grid is one quad:")
    row("bfgrid", "Grid", "Grid",
        "nx x ny vertices: V = nx ny, F = (nx-1)(ny-1), E = (nx-1) ny + (ny-1) nx, corners 4F, area sx sy: 3 x 5 over 4 x 6 -> 15, 22, 8, 32, area 24, box +-2 x +-3 at z = 0; default 3 x 3 over 1 x 1 -> 9, 12, 4, area 1",
        b_grid)

    def b_uvs(t):
        S, R, r = 8, 4, 2.0
        s = t.n("UV Sphere", ("i", S), ("i", R), r)
        V = S * (R - 1) + 2; F = S * R; E = S * (R - 1) + S * R
        t.dsize(s, pt=V, ed=E, fa=F, co=3 * 2 * S + 4 * S * (R - 2), msg="8 segments, 4 rings:")
        t.bbox(s, (-r, -r, -r), (r, r, r), "poles on +-Z, rings at 90 degree multiples: box +-2", 1e5)
        va = t.n("Volume"); t.w(s, 0, va, 0); t.exact(va, 0, uvsphere_volume(S, R, r), "volume of the 8 x 4 frustum stack, radius 2", 1e5)
        d = t.n("UV Sphere")
        t.dsize(d, pt=32 * 15 + 2, ed=32 * 15 + 32 * 16, fa=512, msg="the default 32 x 16 sphere has")
        vd = t.n("Volume"); t.w(d, 0, vd, 0); t.exact(vd, 0, uvsphere_volume(32, 16, 1.0), "default volume (frustum stack, within 5e-6 of the 4.1219 polyhedron)", 1e5)
    row("bfuvsp", "UV Sphere", "UV Sphere",
        "S segments, R rings: V = S (R - 1) + 2, F = S R (triangles at the poles, quads between), E = S (R - 1) ring edges + S R meridian edges, corners 6 S + 4 S (R - 2); volume = (S/6) sin(2 pi/S) sum over ring pairs h (rho1^2 + rho1 rho2 + rho2^2) with rho_k = r sin(pi k / R), z_k = r cos(pi k / R) (S/2 rho^2 sin(2 pi/S) is the polygon area); poles on the Z axis so the box is +-r",
        b_uvs)

    def b_cyl(t):
        n, r, h = 8, 2.0, 3.0
        c = t.n("Cylinder", ("i", n), ("i", 1), r, h, ("i", 1))
        t.dsize(c, pt=2 * n, ed=3 * n, fa=n + 2, co=4 * n + 2 * n, msg="8-gon prism with n-gon caps:")
        va = t.n("Volume"); t.w(c, 0, va, 0); t.exact(va, 0, ngon_area(n, r) * h, "volume = polygon area (n/2) r^2 sin(2 pi/n) times depth", 1e5)
        ar = t.n("Area"); t.w(c, 0, ar, 0)
        t.exact(ar, 0, n * 2 * r * math.sin(PI / n) * h + 2 * ngon_area(n, r), "area = n * chord * depth + 2 * polygon area", 1e5)
        t.bbox(c, (-r, -r, -h / 2), (r, r, h / 2), "z from -depth/2 to +depth/2, x and y +-r (8 is a multiple of 4)", 1e5)
        cf = t.n("Cylinder", ("i", n), ("i", 3), r, h, ("i", 2))
        t.dsize(cf, pt=n * 4 + 2, ed=n * 4 + n * 3 + 2 * n, fa=n * 3 + 2 * n, msg="side segments 3, triangle-fan caps:")
        vf = t.n("Volume"); t.w(cf, 0, vf, 0); t.exact(vf, 0, ngon_area(n, r) * h, "same volume with fan caps", 1e5)
        cn = t.n("Cylinder", ("i", n), ("i", 1), r, h, ("i", 0))
        t.dsize(cn, pt=2 * n, ed=3 * n, fa=n, msg="no caps: open tube")
        cd = t.n("Cylinder")
        t.dsize(cd, pt=64, ed=96, fa=34, co=192, msg="the default cylinder has")
        vd = t.n("Volume"); t.w(cd, 0, vd, 0); t.exact(vd, 0, ngon_area(32, 1.0) * 2.0, "default volume 16 sin(pi/16) * 2 (polygon prism)", 1e5)
    row("bfcyl", "Cylinder", "Cylinder",
        "n-gon prism: V = n (side + 1) (+2 centres with fan caps), F = n side + 2 (n-gon caps) or n side + 2n (fan), E by rings n (side + 1), verticals n side, spokes 2n for fans; volume (n/2) r^2 sin(2 pi/n) depth, area n 2 r sin(pi/n) depth + n r^2 sin(2 pi/n); an open tube has no cap faces (fill type none). Single precision: within 5e-6",
        b_cyl)

    def b_cone(t):
        n, r, h = 8, 2.0, 3.0
        c = t.n("Cone", ("i", n), ("i", 1), 0.0, r, h, ("i", 1))
        t.dsize(c, pt=n + 1, ed=2 * n, fa=n + 1, co=3 * n + n, msg="8-gon cone: apex plus ring,")
        va = t.n("Volume"); t.w(c, 0, va, 0); t.exact(va, 0, ngon_area(n, r) * h / 3.0, "cone volume = base area * height / 3", 1e5)
        t.bbox(c, (-r, -r, -h / 2), (r, r, h / 2), "base at z = -1.5, apex at +1.5", 1e5)
        f = t.n("Cone", ("i", n), ("i", 2), 1.0, r, h, ("i", 1))
        t.dsize(f, pt=n * 3, ed=n * 3 + n * 2, fa=n * 2 + 2, msg="frustum with 2 side segments:")
        vf = t.n("Volume"); t.w(f, 0, vf, 0); t.exact(vf, 0, frustum_volume(n, 1.0, r, h), "volume = h (A1 + A2 + sqrt(A1 A2)) / 3 (radius is linear in height, so the two segments add up to one frustum)", 1e5)
        ct = t.n("Cone", ("i", n), ("i", 1), r, 0.0, h, ("i", 1))
        t.dsize(ct, pt=n + 1, ed=2 * n, fa=n + 1, msg="apex at the bottom:")
        vt = t.n("Volume"); t.w(ct, 0, vt, 0); t.exact(vt, 0, ngon_area(n, r) * h / 3.0, "inverted cone has the same volume", 1e5)
        cd = t.n("Cone")
        t.dsize(cd, pt=33, ed=64, fa=33, co=128, msg="the default cone has")
        vd = t.n("Volume"); t.w(cd, 0, vd, 0); t.exact(vd, 0, ngon_area(32, 1.0) * 2.0 / 3.0, "default volume", 1e5)
    row("bfcone", "Cone", "Cone",
        "radius linear in height: a zero end radius collapses to one apex vertex with no cap; volume of a polygonal frustum h (A1 + A2 + sqrt(A1 A2)) / 3 with A = (n/2) r^2 sin(2 pi/n), which is A h / 3 for the cone; apex cone: V = n + 1, F = n + 1 (n triangles + the n-gon base), E = 2 n; frustum with 2 side segments: V = 3 n, F = 2 n + 2, E = 3 n + 2 n",
        b_cone)

    def b_mcir(t):
        n = 8
        c0 = t.n("Mesh Circle", ("i", n), 2.0, ("i", 0))
        t.dsize(c0, pt=n, ed=n, fa=0, co=0, msg="unfilled 8-gon:")
        c1 = t.n("Mesh Circle", ("i", n), 2.0, ("i", 1))
        t.dsize(c1, pt=n, ed=n, fa=1, co=n, msg="n-gon fill (one face):")
        a1 = t.n("Area"); t.w(c1, 0, a1, 0); t.exact(a1, 0, ngon_area(n, 2.0), "n-gon area (n/2) r^2 sin(2 pi/n) = 4 sqrt 2 * ... for r = 2", 1e5)
        c2 = t.n("Mesh Circle", ("i", n), 2.0, ("i", 2))
        t.dsize(c2, pt=n + 1, ed=2 * n, fa=n, co=3 * n, msg="triangle-fan fill:")
        a2 = t.n("Area"); t.w(c2, 0, a2, 0); t.exact(a2, 0, ngon_area(n, 2.0), "fan area is the same polygon area", 1e5)
        t.bbox(c2, (-2, -2, 0), (2, 2, 0), "radius 2 in the XY plane", 1e5)
        cd = t.n("Mesh Circle")
        t.dsize(cd, pt=32, ed=32, fa=0, msg="the default (32 vertices, no fill) has")
    row("bfmcir", "Mesh Circle", "Mesh Circle",
        "n vertices on radius r: fill none = n edges, no face; n-gon = n edges, 1 face with n corners; triangle fan = n + 1 vertices, 2 n edges (n rim + n spokes), n triangles, 3 n corners; polygon area (n/2) r^2 sin(2 pi/n) = 4 sqrt 2 for n = 8, r = 2",
        b_mcir)

    def b_mlin(t):
        l = t.n("Mesh Line")
        t.dsize(l, pt=10, ed=9, fa=0, co=0, msg="the default line (10 vertices):")
        t.bbox(l, (0, 0, 0), (0, 0, 9), "offset (0,0,1): z = 0 .. 9", 1e5)
        s = t.pt(1, 2, 3); o = t.vec(0.5, 0, -1)
        l2 = t.n("Mesh Line", ("i", 5)); t.w(s, 0, l2, 1); t.w(o, 0, l2, 2)
        t.dsize(l2, pt=5, ed=4, msg="5 vertices:")
        t.bbox(l2, (1, 2, -1), (3, 2, 3), "Start + i * (0.5, 0, -1), i = 0..4 -> x 1..3, z 3..-1", 1e5)
        e = t.vec(4, 0, 8); z = t.pt(0, 0, 0)
        l3 = t.n("Mesh Line", ("i", 5), None, None, ("i", 1)); t.w(z, 0, l3, 1); t.w(e, 0, l3, 2)
        t.bbox(l3, (0, 0, 0), (4, 0, 8), "End Points mode: Start + (End - Start) i / 4 reaches (4,0,8) at the last vertex", 1e5)
        l1 = t.n("Mesh Line", ("i", 1))
        t.dsize(l1, pt=1, ed=0, msg="one vertex has no edge:")
    row("bfmlin", "Mesh Line", "Mesh Line",
        "Count vertices, Count - 1 edges, no face: 10 -> 9; vertex i = Start + i Offset (box z 0..9 for the default; (1,2,3) + i (0.5,0,-1), i = 0..4 -> x 1..3, z -1..3); End Points mode: Start + (End - Start) i / (Count - 1)",
        b_mlin)

    def b_ico(t):
        for s, tag in ((1, "icosahedron"), (2, "2 subdivisions"), (3, "3 subdivisions"), (4, "4 subdivisions")):
            i = t.n("Ico Sphere", 1.0, ("i", s))
            F = 20 * 4 ** (s - 1); V = 10 * 4 ** (s - 1) + 2
            t.dsize(i, pt=V, ed=30 * 4 ** (s - 1), fa=F, co=3 * F, msg="%s: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1)," % tag)
        i1 = t.n("Ico Sphere", 2.0, ("i", 1))
        a = 2.0 / math.sin(2 * PI / 5)
        t.exact(_vol(t, i1), 0, (5.0 / 12.0) * (3 + SQ5) * a ** 3, "icosahedron of circumradius 2: edge a = R / sin(2 pi/5), volume 5 (3 + sqrt 5) a^3 / 12", 1e5)
        t.bbox(i1, (-2 * 2 / SQ5, -2 * (2 / SQ5) * math.sin(2 * PI / 5), -2), (2 * 2 / SQ5, 2 * (2 / SQ5) * math.sin(2 * PI / 5), 2), "poles at +-R; two rings of 5 at height +-R/sqrt 5 and radius 2R/sqrt 5", 1e5)
        i3 = t.n("Ico Sphere", 1.0, ("i", 3))
        t.bbox_z(i3, -1.0, 1.0, "the poles stay at z = +-1 after subdividing", 1e5)
        idf = t.n("Ico Sphere")
        t.dsize(idf, pt=12, fa=20, msg="the default has one subdivision level:")
    row("bfico", "Ico Sphere", "Ico Sphere",
        "Subdivisions s: F = 20 * 4^(s-1), E = 30 * 4^(s-1), V = 10 * 4^(s-1) + 2 (Euler: V - E + F = 2): 12/30/20, 42/120/80, 162/480/320, 642/1920/1280; the s = 1 solid is the icosahedron of circumradius R: edge a = R / sin(2 pi/5), volume 5 (3 + sqrt 5) a^3 / 12, vertices at the poles and two rings of five at height +-R/sqrt 5, radius 2 R / sqrt 5",
        b_ico)

    # ---------------------------------------------------------------- measuring
    def b_bbox(t):
        sz = t.vec(2, 6, 10)
        c2 = t.n("Cube", None, ("i", 2), ("i", 2), ("i", 2)); t.w(sz, 0, c2, 0)
        t.bbox(c2, (-1, -3, -5), (1, 3, 5), "mesh: Size (2,6,10) centred on the origin", 1e5)
        s = t.pt(1, 2, 3); m = t.pt(4, -2, 9)
        ln = t.n("Curve Line", ("i", 0)); t.w(s, 0, ln, 1); t.w(m, 0, ln, 2)
        t.bbox(ln, (1, -2, 3), (4, 2, 9), "curve: componentwise min and max of (1,2,3) and (4,-2,9)")
        p = t.pt(7, -8, 9)
        t.bbox(p, (7, -8, 9), (7, -8, 9), "a single point is its own box")
        c3 = t.n("Curve Circle", ("i", 4), 3.0)
        t.bbox(c3, (-3, -3, 0), (3, 3, 0), "circle of radius 3 with 4 points")
        q = t.n("Quadrilateral", 10.0, 2.0)
        t.bbox(q, (-5, -1, 0), (5, 1, 0), "rectangle 10 x 2")
    row("bfbbox", "Bounding Box", "Bounding Box",
        "componentwise minimum and maximum of the vertices: cube of Size (2,6,10) -> +-(1,3,5); line (1,2,3)-(4,-2,9) -> (1,-2,3), (4,2,9); a point is its own box; circle r = 3 with 4 points -> +-3 in XY; rectangle 10 x 2 -> +-(5,1). The Blender node also outputs a box mesh (untested)",
        b_bbox)

    def b_dsiz(t):
        c = t.n("Cube")
        t.dsize(c, pt=8, ed=12, fa=6, co=24, sp=0, msg="cube:")
        g = t.n("Grid", 1.0, 1.0, ("i", 4), ("i", 2))
        t.dsize(g, pt=8, ed=3 * 2 + 1 * 4, fa=3, co=12, msg="4 x 2 grid: V = 8, E = 3 * 2 + 1 * 4 = 10, F = 3, corners 12;")
        n = t.n("Mesh Circle", ("i", 6), 1.0, ("i", 1))
        t.dsize(n, pt=6, ed=6, fa=1, co=6, msg="hexagon n-gon: 6 points, 6 edges, one face with 6 corners;")
        f = t.n("Mesh Circle", ("i", 6), 1.0, ("i", 2))
        t.dsize(f, pt=7, ed=12, fa=6, co=18, msg="hexagon fan:")
        w = t.n("Mesh Line", ("i", 7))
        t.dsize(w, pt=7, ed=6, fa=0, co=0, msg="7-vertex line: wire edges count, no face:")
        i = t.n("Ico Sphere", 1.0, ("i", 1))
        t.dsize(i, pt=12, ed=30, fa=20, co=60, sp=0, msg="icosahedron:")
        cc = t.n("Curve Circle", ("i", 12), 1.0)
        t.dsize(cc, pt=12, ed=0, fa=0, co=0, sp=1, msg="12-point cyclic curve: 12 points, 1 spline, no mesh elements;")
        ln = t.n("Curve Line")
        t.dsize(ln, pt=2, sp=1, msg="open line:")
        p = t.pt(1, 2, 3)
        t.dsize(p, pt=1, msg="a point:")
    row("bfdsiz", "Domain Size", "Domain Size",
        "Point/Edge/Face/Face Corner counts of closed forms: cube 8/12/6/24, 4 x 2 grid 8/10/3/12, n-gon hexagon 6/6/1/6 (a fan of triangles inside an n-gon counts as one face with its boundary corners), hexagon fan 7/12/6/18, 7-vertex wire 7/6/0/0, icosahedron 12/30/20/60; a cyclic 12-point curve has 12 points (the stored closing point is not counted) and 1 spline",
        b_dsiz)

    # ---------------------------------------------------------------- utilities
    def b_jstr(t):
        m1 = t.n("Merge", ("t", "alpha"), ("t", "beta"))
        m2 = t.n("Merge", None, ("t", "gamma")); t.w(m1, 0, m2, 0)
        j = t.n("Join Strings", ("t", ", ")); t.w(m2, 0, j, 1)
        t.textis(j, "alpha, beta, gamma", "three strings with delimiter ', ' -> 'alpha, beta, gamma'")
        wrong = t.n("Text Distance", None, ("t", "alpha,beta,gamma")); t.w(j, 0, wrong, 0)
        t.count(wrong, 0, 2, "control: against 'alpha,beta,gamma' (both spaces missing) the distance is 2, so the 0 above is a real match")
        j0 = t.n("Join Strings"); t.w(m2, 0, j0, 1)
        t.textis(j0, "alphabetagamma", "the default delimiter is empty: 'alphabetagamma'")
        sg = t.n("Join Strings", ("t", "-"));
        one = t.n("Merge", ("t", "solo"), None); t.w(one, 0, sg, 1)
        t.textis(sg, "solo", "one string has no delimiter")
        jn = t.n("Join Strings", ("t", "-"), None)
        t.textis(jn, "", "no strings give the empty string")
        mm = t.n("Merge", ("t", "a"), ("t", "b"))
        jm = t.n("Join Strings", ("t", "--")); t.w(mm, 0, jm, 1)
        t.textis(jm, "a--b", "delimiter '--' between 'a' and 'b'")
    row("bfjstr", "Join Strings", "Join Strings",
        "concatenation in order with the delimiter between neighbours only: alpha, beta, gamma with ', ' -> 'alpha, beta, gamma' (Text Distance to the expected string is 0 exactly when the strings are equal; the control string without the space is 1 edit away), empty delimiter -> 'alphabetagamma', one string -> itself, none -> ''",
        b_jstr)

    def b_swit(t):
        s0 = t.n("Switch", ("b", False), 10.0, 20.0)
        t.exact(s0, 0, 10.0, "false selects the False input 10")
        s1 = t.n("Switch", ("b", True), 10.0, 20.0)
        t.exact(s1, 0, 20.0, "true selects the True input 20")
        st = t.n("Switch", ("b", True), ("t", "no"), ("t", "yes"))
        t.textis(st, "yes", "text: true selects 'yes'")
        sf = t.n("Switch", ("b", False), ("t", "no"), ("t", "yes"))
        t.textis(sf, "no", "text: false selects 'no'")
        a = t.pt(1, 2, 3); b = t.pt(4, 5, 6)
        sp = t.n("Switch", ("b", True)); t.w(a, 0, sp, 1); t.w(b, 0, sp, 2)
        t.point_exact(sp, 0, (4, 5, 6), "point: true selects (4,5,6)")
        sq = t.n("Switch", ("b", False)); t.w(a, 0, sq, 1); t.w(b, 0, sq, 2)
        t.point_exact(sq, 0, (1, 2, 3), "point: false selects (1,2,3)")
        sd = t.n("Switch", None, 7.0, 9.0)
        t.exact(sd, 0, 7.0, "the default Switch is false")
    row("bfswit", "Switch", "Switch",
        "Switch false passes the False input, true the True input, for numbers (10 / 20), text ('no' / 'yes') and points ((1,2,3) / (4,5,6)); default false",
        b_swit)

    def b_idxs(t):
        for idx, want in ((0, 10.0), (1, 20.0), (2, 30.0), (3, 40.0)):
            n = t.n("Index Switch", ("i", idx), 10.0, 20.0, 30.0, 40.0)
            t.exact(n, 0, want, "index %d selects Item %d = %g" % (idx, idx, want))
        for idx in (4, -1, 100):
            n = t.n("Index Switch", ("i", idx), 10.0, 20.0, 30.0, 40.0)
            t.exact(n, 0, 0.0, "index %d is out of range: the default 0" % idx)
        nt = t.n("Index Switch", ("i", 9), ("t", "a"), ("t", "b"))
        t.textis(nt, "", "out-of-range index on text gives the empty string")
        n2 = t.n("Index Switch", ("i", 1), ("t", "a"), ("t", "b"))
        t.textis(n2, "b", "index 1 selects 'b'")
    row("bfidxs", "Index Switch", "Index Switch",
        "Index i in 0..3 passes Item i: 10 20 30 40 for 0..3; an index outside 0..3 gives the default of the type (0, empty text). The node has four fixed items; adding, moving and removing items (node.index_switch_item_*) is untested",
        b_idxs)

    def b_llen(t):
        for n_, c in ((7, 7), (1, 1)):
            s = t.n("Series", 0.0, 1.0, ("i", n_))
            ln = t.n("List Length (Blender)"); t.w(s, 0, ln, 0)
            t.count(ln, 0, c, "Series of %d items has length %d" % (n_, c))
        s = t.n("Series", 0.0, 1.0, ("i", 3))
        cull = t.n("Cull Index", None, None, ("b", False)); t.w(s, 0, cull, 0)
        idx = t.n("Series", 0.0, 1.0, ("i", 3)); t.w(idx, 0, cull, 1)
        ln = t.n("List Length (Blender)"); t.w(cull, 0, ln, 0)
        t.count(ln, 0, 0, "culling all three items leaves an empty list of length 0")
        s2 = t.n("Series", 5.0, 2.0, ("i", 12))
        cull2 = t.n("Cull Index", None, None, ("b", False)); t.w(s2, 0, cull2, 0)
        idx2 = t.n("Series", 0.0, 1.0, ("i", 4)); t.w(idx2, 0, cull2, 1)
        ln2 = t.n("List Length (Blender)"); t.w(cull2, 0, ln2, 0)
        t.count(ln2, 0, 8, "12 items minus 4 culled = 8")
    row("bfllen", "List Length", "List Length",
        "the item count: Series of 7 -> 7, of 1 -> 1; 3 items with all 3 indices culled -> 0; 12 items with 4 culled -> 8",
        b_llen)

    def b_glit(t):
        s = t.n("Series", 10.0, 10.0, ("i", 4))
        for idx, want in ((0, 10.0), (1, 20.0), (3, 40.0)):
            n = t.n("Get List Item", None, ("i", idx)); t.w(s, 0, n, 0)
            t.exact(n, 0, want, "item %d of 10 20 30 40 is %g" % (idx, want))
        for idx in (4, -1, 50):
            n = t.n("Get List Item", None, ("i", idx)); t.w(s, 0, n, 0)
            t.exact(n, 0, 0.0, "index %d is outside the list: the default 0" % idx)
        a = t.pt(1, 2, 3); b = t.pt(4, 5, 6)
        m = t.n("Merge"); t.w(a, 0, m, 0); t.w(b, 0, m, 1)
        n = t.n("Get List Item", None, ("i", 1)); t.w(m, 0, n, 0)
        t.point_exact(n, 0, (4, 5, 6), "item 1 of the point list (1,2,3), (4,5,6)")
        n2 = t.n("Get List Item", None, ("i", 2)); t.w(m, 0, n2, 0)
        t.point_exact(n2, 0, (0, 0, 0), "index 2 of a 2-point list is the default point")
    row("bfglit", "Get List Item", "Get List Item",
        "zero-based indexing: item 0, 1, 3 of 10 20 30 40 = 10, 20, 40; an index outside the list (4, -1, 50) gives the type's default 0; the second of two points (1,2,3), (4,5,6) is (4,5,6) and index 2 gives (0,0,0)",
        b_glit)
    return rows


def _vol(t, src):
    v = t.n("Volume")
    t.w(src, 0, v, 0)
    return v


def _bbox_z(self, geo, zlo, zhi, msg, scale):
    b = self.g.node("Bounding Box")
    self.g.wire(geo, 0, b, 0)
    for port, c, tag in ((0, zlo, "min"), (1, zhi, "max")):
        d = self.g.node("Deconstruct Point")
        self.g.wire(b, port, d, 0)
        self.exact(d, 2, c, "%s: %s Z" % (msg, tag), scale)


T.bbox_z = _bbox_z


def main():
    for name, label, title, closed, t in make_rows():
        nobj = sum(len(vals.split()) if vals else 0 for _tag, vals, _msg in t.expect)
        with open(os.path.join(HERE, "flowrows_%s.dflow" % name), "w") as f:
            f.write(t.g.text())
        with open(os.path.join(HERE, "flowrows_%s_script.txt" % name), "w") as f:
            f.write("# Flow Register row %s (Blender Geometry Nodes): player + bake, read back by sections/96_%s.sh (see flowrows3_gen.py)\n"
                    "Grasshopper\nGrasshopperPlayer @GRAPH@ Bake=Yes\n@expect_objects %d\nSelAll\nList\n" % (title, name, nobj))
        checks = "\n".join('fr_chk %d "%s" "%s"' % (tag, vals, msg.replace('"', "'")) for tag, vals, msg in t.expect)
        with open(os.path.join(HERE, "sections", "96_%s.sh" % name), "w") as f:
            f.write(SECTION.format(up=name.upper(), rid="geometry_nodes/" + label, title=title, name=name, closed=closed,
                                   nn=len(t.g.nodes), nobj=nobj, checks=checks))
        print(name, label, "nodes", len(t.g.nodes), "objects", nobj)


if __name__ == "__main__":
    main()
