#!/usr/bin/env python3
"""Writes the Grasshopper geometry Register-row tests (Curve, Intersect): for each row one .dflow graph, one player script and one
tests/sections/97_<name>.sh section (the section name, upper-cased, is the row's acceptance test id).

The machinery is flowrows_gen.py's: every graph ends in Construct Point (X = the value under test, Y = a case tag) -> Bake, so the
player bakes one point per value and `List` prints it as "  X,TAG,0". The List printout keeps four significant digits for a
non-integer, so a real-valued result is never compared as printed text: it goes through
    Remap Numbers(value; closed form c .. c+1 -> 0 .. 1e9) -> Round To Multiple(1)
which is round((value - c) * 1e9) and prints "0" exactly when |value - c| < 5e-10, inside the 1e-9 absolute tolerance the Register
rows carry (the 1e-6 rows are not touched). A point is compared as the Distance to a Construct Point that holds the closed-form
coordinates, so the residual is the distance in 1e-9 units. Every closed form below is written as a number or a python math
expression, worked out by hand (the derivation is in each row's comment) and independent of the node code under test.

Each section also carries two controls, so a 0 above can only mean a real match:
  tag 99  Square(2) against the wrong closed form 4.5 must read -500000000 (the chain does not report 0 for everything);
  tag 98  the section's main measured value against its closed form PLUS 1e-6 must read -1000 (the chain resolves 1e-6).
Run from dino8-app/tests:  python3 flowgeo_gen.py
"""
import math
import os

from flowrows_gen import G

HERE = os.path.dirname(os.path.abspath(__file__))
SC = 1e9  # residual unit: one count = 1e-9
PI = math.pi
SQ2 = math.sqrt(2.0)
SQ3 = math.sqrt(3.0)


def W(nid, port=0):
    return ("w", nid, port)


def N(g, typ, *args):
    """A node whose inputs are literals, or W(node, port) wires."""
    lits = [None if (isinstance(a, tuple) and a and a[0] == "w") else a for a in args]
    nid = g.node(typ, *lits)
    for i, a in enumerate(args):
        if isinstance(a, tuple) and a and a[0] == "w":
            g.wire(a[1], a[2], nid, i)
    return nid


def P(g, x, y, z=0.0):
    return N(g, "Construct Point", float(x), float(y), float(z))


def V(g, x, y, z=0.0):
    return N(g, "Vector XYZ", float(x), float(y), float(z))


def PLN(g, origin, normal):
    """Plane from an origin and a normal (both given as numbers)."""
    return N(g, "Plane Normal", W(P(g, *origin)), W(V(g, *normal)))


def XYPL(g, origin):
    return N(g, "XY Plane", W(P(g, *origin)))


def PLXY(g, origin, xaxis, yaxis):
    return N(g, "Plane Origin", W(P(g, *origin)), W(V(g, *xaxis)), W(V(g, *yaxis)))


def LIST(g, nodes_ports):
    """A list from several single values (nested Merge)."""
    acc = nodes_ports[0]
    for np_ in nodes_ports[1:]:
        acc = (N(g, "Merge", W(*acc), W(*np_)), 0)
    return acc


def PLIST(g, pts):
    return LIST(g, [(P(g, *p), 0) for p in pts])


def POLY(g, pts, closed=False):
    lst = PLIST(g, pts)
    return N(g, "Polyline", W(*lst), ("b", closed))


class Row:
    """One section: builds the graph and collects the expected baked values."""

    def __init__(self, name, rid, title, closed):
        self.name, self.rid, self.title, self.closed = name, rid, title, closed
        self.g = G()
        self.exp = []   # (tag, values, message)
        self.tag = 0

    def _next(self):
        self.tag += 1
        assert self.tag < 98, self.name
        return self.tag

    def resid(self, src, port, c, tag, scale=SC):
        g = self.g
        rm = N(g, "Remap Numbers", W(src, port), float(c), float(c) + 1.0, 0.0, scale)
        ro = N(g, "Round To Multiple", W(rm), 1.0)
        g.probe(ro, 0, tag)

    def guard(self, src, port, msg):
        """src.port holds a value: a node that failed leaves its output empty, which every downstream default reads as 0, so a
        closed form of 0 (or of the origin) would match a failed node. List Length of the output is 1 only when it was produced."""
        self.raw(N(self.g, "List Length", W(src, port)), 0, "1", msg + " (the node under test produced the value)")

    def val(self, src, port, c, msg, scale=SC):
        """src.port is the number c (within 5e-10)."""
        t = self._next()
        self.resid(src, port, c, t, scale)
        self.exp.append((t, "0", msg))
        if abs(c) < 1e-12:
            self.guard(src, port, msg)

    def pt(self, src, port, xyz, msg):
        """src.port (a point, vector or plane origin) is the point xyz (within 5e-10)."""
        g = self.g
        t = self._next()
        e = P(g, *xyz)
        d = N(g, "Distance", W(src, port), W(e))
        self.resid(d, 0, 0.0, t)
        self.exp.append((t, "0", msg))
        if all(abs(v) < 1e-12 for v in xyz):
            self.guard(src, port, msg)

    def raw(self, src, port, expected, msg):
        """src.port printed as is (an exact integer or a short decimal)."""
        t = self._next()
        self.g.probe(src, port, t)
        self.exp.append((t, expected, msg))
        if expected == "0" and not msg.endswith("(the node under test produced the value)"):
            self.guard(src, port, msg)

    def count(self, src, port, n, msg):
        ln = N(self.g, "List Length", W(src, port))
        t = self._next()
        self.g.probe(ln, 0, t)
        self.exp.append((t, str(n), msg))

    def item(self, src, port, k):
        return N(self.g, "List Item", W(src, port), ("i", k), ("b", False))

    def finish(self, decoy=None):
        g = self.g
        if decoy is not None:
            src, port, c = decoy
            self.resid(src, port, c + 1e-6, 98)
            self.exp.append((98, "-1000", "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"))
        self.resid(N(g, "Square", 2.0), 0, 4.5, 99)
        self.exp.append((99, "-500000000", "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"))


SECTION = r'''# ---------------------------------------------------------------------------
# {up}_: Flow Register row {rid} ({title}), flowgeo_gen.py. The graph flowgeo_{name}.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: {closed}.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_{name}.dflow|g" "$HERE/flowgeo_{name}_script.txt" > "$TMPW/flowgeo_{name}_script.txt"
if [ -n "${{DISPLAY:-}}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_{name}_script.txt" 2>&1)" || {{ echo "$FR_OUT" | tail -20; echo "FAIL {up}_run: script exited non-zero"; fail=1; FR_OUT=""; }}
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_{name}_script.txt" 2>&1)" || {{ echo "$FR_OUT" | tail -20; echo "FAIL {up}_run: script exited non-zero"; fail=1; FR_OUT=""; }}
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {{
  local got
  got="$({{ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; }} | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   {up}_$1: $3 (measured '$got')"; else echo "FAIL {up}_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}}
if grep -qF "GrasshopperPlayer: solved {nn} node(s)" <<<"$FR_OUT"; then echo "ok   {up}_solve: the player solved all {nn} nodes (every node type is registered)"; else echo "FAIL {up}_solve: expected 'solved {nn} node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects {nobj} " <<<"$FR_OUT"; then echo "ok   {up}_objects: exactly {nobj} point(s) baked"; else echo "FAIL {up}_objects: expected {nobj} baked objects"; fail=1; fi
{checks}
'''


def rows():
    out = []

    # ------------------------------------------------------------------ Curve > Primitive
    r = Row("gline", "D8-GH-CURVE-0038", "Line",
            "|(3,4,0)| = 5 (3-4-5); (1,2,3)->(3,4,4) has the difference (2,2,1) of length 3; (-1,-1,-1)->(1,1,1) has (2,2,2) of length 2 sqrt 3; "
            "the middle of (0,0,0)->(3,4,0) is (1.5,2,0) and its unit tangent (3,4,0)/5 = (0.6,0.8,0); the end points come back as given")
    g = r.g
    cases = [((0, 0, 0), (3, 4, 0), 5.0, "(0,0,0)->(3,4,0) has length 5"),
             ((1, 2, 3), (3, 4, 4), 3.0, "(1,2,3)->(3,4,4) has length 3"),
             ((-1, -1, -1), (1, 1, 1), 2 * SQ3, "(-1,-1,-1)->(1,1,1) has length 2 sqrt 3")]
    first = None
    for a, b, c, msg in cases:
        ln = N(g, "Line", W(P(g, *a)), W(P(g, *b)))
        cl = N(g, "Curve Length", W(ln))
        r.val(cl, 0, c, msg)
        ep = N(g, "End Points", W(ln))
        r.pt(ep, 0, a, "the start of the line is the start point given %s" % (a,))
        r.pt(ep, 1, b, "the end of the line is the end point given %s" % (b,))
        if first is None:
            first = (cl, 5.0)
            ev = N(g, "Evaluate Curve", W(ln), 0.5)
            r.pt(ev, 0, (1.5, 2.0, 0.0), "the middle of (0,0,0)->(3,4,0) is (1.5,2,0)")
            r.pt(ev, 1, (0.6, 0.8, 0.0), "its unit tangent is (0.6,0.8,0)")
    r.finish(decoy=(first[0], 0, first[1]))
    out.append(r)

    r = Row("glsdl", "D8-GH-CURVE-0039", "Line SDL",
            "end = start + unit(direction) * length: (1,2,3) along (0,3,4) (unit (0,0.6,0.8)) for 10 ends at (1,8,11); (0,0,0) along (2,0,0) for 5 ends at (5,0,0); "
            "(0,0,0) along (1,1,1) (unit (1,1,1)/sqrt 3) for 3 sqrt 3 ends at (3,3,3); the line is that long")
    g = r.g
    for k, (s, d_, ln_, e) in enumerate([((1, 2, 3), (0, 3, 4), 10.0, (1, 8, 11)), ((0, 0, 0), (2, 0, 0), 5.0, (5, 0, 0)), ((0, 0, 0), (1, 1, 1), 3 * SQ3, (3, 3, 3))]):
        sd = N(g, "Line SDL", W(P(g, *s)), W(V(g, *d_)), float(ln_))
        ep = N(g, "End Points", W(sd))
        r.pt(ep, 0, s, "case %d: the line starts at the start point %s" % (k + 1, s))
        r.pt(ep, 1, e, "case %d: the line ends at start + unit(direction) * length = %s" % (k + 1, e))
        cl = N(g, "Curve Length", W(sd))
        r.val(cl, 0, ln_, "case %d: the line is %.6g long" % (k + 1, ln_))
        if k == 0:
            dec = (cl, 0, ln_)
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gcirc", "D8-GH-CURVE-0041", "Circle",
            "circumference 2 pi r: r = 0.5, 1, 2.5, 10 give pi, 2 pi, 5 pi, 20 pi (the Rhino 8 reference records GH-CIRC-0001..4 read 3.14159265359, 6.28318530718, 15.7079632679, 62.8318530718); "
            "radius 0 gives length 0 and radius -1 the circle of radius 1, length 2 pi (records GH-CIRC-0005, -0006); a circle in the XY plane at (1,2,3) of radius 2 passes (3,2,3), (1,4,3), (-1,2,3), (1,0,3) a quarter, half and "
            "three quarters of the way round, counter-clockwise; the area pi r^2 and the centre (the plane origin) in the plane with normal (1,1,1) at (1,2,3), radius 2: 4 pi")
    g = r.g
    for rad, c, msg in [(0.5, PI * 1.0, "r = 0.5 has length pi"), (1.0, 2 * PI, "r = 1 has length 2 pi"), (2.5, 5 * PI, "r = 2.5 has length 5 pi"), (10.0, 20 * PI, "r = 10 has length 20 pi"),
                        (0.0, 0.0, "r = 0 has length 0 (record GH-CIRC-0005)"), (-1.0, 2 * PI, "r = -1 is the circle of radius 1, length 2 pi (record GH-CIRC-0006)")]:
        ci = N(g, "Circle", None, float(rad))
        cl = N(g, "Curve Length", W(ci))
        r.val(cl, 0, c, msg)
        if rad == 10.0:
            dec = (cl, 0, c)
    z0 = N(g, "End Points", W(N(g, "Circle", W(XYPL(g, (1, 2, 3))), 0.0)))
    r.pt(z0, 0, (1, 2, 3), "r = 0 is still a curve, a point-sized one at the plane origin (1,2,3): its start")
    r.pt(z0, 1, (1, 2, 3), "and its end")
    pl = XYPL(g, (1, 2, 3))
    ci = N(g, "Circle", W(pl), 2.0)
    for tt, xyz, msg in [(0.0, (3, 2, 3), "the start of the circle of radius 2 at (1,2,3) is (3,2,3)"), (0.25, (1, 4, 3), "a quarter of the way round is (1,4,3)"),
                         (0.5, (-1, 2, 3), "half way round is (-1,2,3)"), (0.75, (1, 0, 3), "three quarters of the way round is (1,0,3)")]:
        ev = N(g, "Evaluate Curve", W(ci), float(tt))
        r.pt(ev, 0, xyz, msg)
    til = N(g, "Circle", W(PLN(g, (1, 2, 3), (1, 1, 1))), 2.0)
    r.val(N(g, "Curve Length", W(til)), 0, 4 * PI, "the circle of radius 2 in the plane with normal (1,1,1) has length 4 pi")
    ar = N(g, "Curve Area", W(til))
    r.val(ar, 0, 4 * PI, "its area is pi r^2 = 4 pi")
    r.pt(ar, 1, (1, 2, 3), "its centre is the plane origin (1,2,3)")
    r.finish(decoy=dec)
    out.append(r)

    r = Row("glen", "D8-GH-CURVE-0020", "Curve Length",
            "circle r = 1: 2 pi; r = 2.5: 5 pi; r = 10: 20 pi (within 1e-9, where the 400-chord sum was short by 6e-5 on r = 1); arc of radius 2 over 270 degrees: r * 3 pi / 2 = 3 pi; "
            "line (0,0,0)-(3,4,0): 5; polyline (0,0,0),(6,0,0),(6,4,0),(0,4,0) closed: 6 + 4 + 6 + 4 = 20; a cubic curve whose four control points (0,0,0),(1,1,0),(5,5,0),(6,6,0) lie on one line is "
            "that segment: 6 sqrt 2 (a straight clamped curve between its end control points)")
    g = r.g
    for rad, c, msg in [(1.0, 2 * PI, "circle r = 1: 2 pi"), (2.5, 5 * PI, "circle r = 2.5: 5 pi"), (10.0, 20 * PI, "circle r = 10: 20 pi")]:
        cl = N(g, "Curve Length", W(N(g, "Circle", None, rad)))
        r.val(cl, 0, c, msg)
        if rad == 1.0:
            dec = (cl, 0, c)
    r.val(N(g, "Curve Length", W(N(g, "Arc", None, 2.0, 0.0, 270.0))), 0, 3 * PI, "arc of radius 2 over 270 degrees: 3 pi")
    r.val(N(g, "Curve Length", W(N(g, "Line", W(P(g, 0, 0, 0)), W(P(g, 3, 4, 0))))), 0, 5.0, "line (0,0,0)-(3,4,0): 5")
    r.val(N(g, "Curve Length", W(POLY(g, [(0, 0, 0), (6, 0, 0), (6, 4, 0), (0, 4, 0)], True))), 0, 20.0, "closed rectangle polyline 6 x 4: 20")
    nb = N(g, "NURBS Curve", W(*PLIST(g, [(0, 0, 0), (1, 1, 0), (5, 5, 0), (6, 6, 0)])), ("i", 3), ("b", False))
    r.val(N(g, "Curve Length", W(nb)), 0, 6 * SQ2, "cubic with four collinear control points (0,0,0)..(6,6,0): the segment, 6 sqrt 2")
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gcnr", "D8-GH-CURVE-0043", "Circle CNR",
            "the circle about centre c with normal n and radius r: every point is at distance r from c and (p - c).n = 0; it runs counter-clockwise about n, so (p - c) x tangent = r * unit(n) "
            "(centre (1,2,3), normal (0,0,2), r = 2: (0,0,2); centre (-1,0,2), normal (1,2,2) (length 3), r = 3: 3 * (1,2,2)/3 = (1,2,2)); the length is 2 pi r")
    g = r.g
    for k, (cen, nor, rad, cross) in enumerate([((1, 2, 3), (0, 0, 2), 2.0, (0, 0, 2)), ((-1, 0, 2), (1, 2, 2), 3.0, (1, 2, 2))]):
        pc = P(g, *cen)
        ci = N(g, "Circle CNR", W(pc), W(V(g, *nor)), rad)
        cl = N(g, "Curve Length", W(ci))
        r.val(cl, 0, 2 * PI * rad, "case %d: the length is 2 pi r" % (k + 1))
        if k == 0:
            dec = (cl, 0, 2 * PI * rad)
        for tt in (0.0, 0.3):
            ev = N(g, "Evaluate Curve", W(ci), float(tt))
            r.val(N(g, "Distance", W(ev, 0), W(pc)), 0, rad, "case %d, t = %g: the point is at distance r from the centre" % (k + 1, tt))
            rad_v = N(g, "Vector 2Pt", W(pc), W(ev, 0))
            r.val(N(g, "Dot Product", W(rad_v), W(V(g, *nor))), 0, 0.0, "case %d, t = %g: the point lies in the plane (radius vector . normal = 0)" % (k + 1, tt))
            cr = N(g, "Cross Product", W(rad_v), W(ev, 1))
            r.pt(cr, 0, cross, "case %d, t = %g: radius x tangent = r * unit(normal) %s, counter-clockwise about the normal" % (k + 1, tt, cross))
    r.finish(decoy=dec)
    out.append(r)

    r = Row("garc", "D8-GH-CURVE-0045", "Arc",
            "arc length r * (end - start) in radians: radius 2 from 30 to 120 degrees is 2 * pi/2 = pi, starting at 2 (cos 30, sin 30) = (sqrt 3, 1, 0) and ending at 2 (cos 120, sin 120) = (-1, sqrt 3, 0), "
            "its point half way along at angle 75 degrees is (2 cos 75, 2 sin 75, 0); from 0 to 270 degrees (3 pi) it starts at (2,0,0), ends at (0,-2,0), half way is at 135 degrees: (-sqrt 2, sqrt 2, 0); "
            "in the plane through (1,2,3) with axes (0,1,0) and (0,0,1) (normal (1,0,0)), radius 1.5 from 0 to 90 degrees starts at (1,3.5,3), ends at (1,2,4.5) and is 0.75 pi long")
    g = r.g
    d75 = math.radians(75.0)
    for k, (pl_, rad, a0, a1, length, st, en, mid) in enumerate([
            (None, 2.0, 30.0, 120.0, PI, (SQ3, 1, 0), (-1, SQ3, 0), (2 * math.cos(d75), 2 * math.sin(d75), 0)),
            (None, 2.0, 0.0, 270.0, 3 * PI, (2, 0, 0), (0, -2, 0), (-SQ2, SQ2, 0)),
            (PLXY(g, (1, 2, 3), (0, 1, 0), (0, 0, 1)), 1.5, 0.0, 90.0, 0.75 * PI, (1, 3.5, 3), (1, 2, 4.5), (1, 2 + 1.5 * math.cos(math.radians(45)), 3 + 1.5 * math.sin(math.radians(45))))]):
        ar = N(g, "Arc", W(pl_) if pl_ is not None else None, rad, a0, a1)
        cl = N(g, "Curve Length", W(ar))
        r.val(cl, 0, length, "arc %d: the length is r * angle = %.6g" % (k + 1, length))
        if k == 0:
            dec = (cl, 0, length)
        ep = N(g, "End Points", W(ar))
        r.pt(ep, 0, st, "arc %d: it starts at %s" % (k + 1, tuple(round(x, 6) for x in st)))
        r.pt(ep, 1, en, "arc %d: it ends at %s" % (k + 1, tuple(round(x, 6) for x in en)))
        el = N(g, "Evaluate Length", W(ar), 0.5, ("b", True))
        r.pt(el, 0, mid, "arc %d: the point half way along is %s" % (k + 1, tuple(round(x, 6) for x in mid)))
    r.finish(decoy=dec)
    out.append(r)

    r = Row("garc3", "D8-GH-CURVE-0046", "Arc 3Pt",
            "radius 2 about the origin, points at 0, 100 and 250 degrees: the arc starts at the first, passes the second (at arc length 2 * 100 degrees = 10 pi / 9 from the start) and ends at the third "
            "after 250 degrees, 25 pi / 9 long, angle 250 degrees = 25 pi / 18, centre (0,0,0), radius 2; the same points taken as 0, 250, 100 degrees run the other way round, 260 degrees = 13 pi / 9 "
            "(length 26 pi / 9); the unit-axis points (1,0,0), (0,1,0), (0,0,1) lie on a circle of centre (1/3,1/3,1/3) and radius sqrt(2/3) at 120 degrees from one another, so the arc through the "
            "middle one is 240 degrees = 4 pi / 3 long, sqrt(2/3) * 4 pi / 3")

    g = r.g

    def pp(deg, rad=2.0):
        a = math.radians(deg)
        return (rad * math.cos(a), rad * math.sin(a), 0.0)
    for k, (pa, pb, pc, rad, ang, cen, pb_len) in enumerate([
            (pp(0), pp(100), pp(250), 2.0, math.radians(250.0), (0, 0, 0), 2.0 * math.radians(100.0)),
            (pp(0), pp(250), pp(100), 2.0, math.radians(260.0), (0, 0, 0), 2.0 * math.radians(110.0)),
            ((1, 0, 0), (0, 1, 0), (0, 0, 1), math.sqrt(2.0 / 3.0), 4 * PI / 3, (1 / 3.0, 1 / 3.0, 1 / 3.0), math.sqrt(2.0 / 3.0) * 2 * PI / 3)]):
        a3 = N(g, "Arc 3Pt", W(P(g, *pa)), W(P(g, *pb)), W(P(g, *pc)))
        r.val(a3, 2, rad, "case %d: the radius is %.6g" % (k + 1, rad))
        r.val(a3, 3, ang, "case %d: the angle is %.6g radians" % (k + 1, ang))
        r.pt(a3, 1, cen, "case %d: the plane origin is the centre %s" % (k + 1, tuple(round(x, 6) for x in cen)))
        cl = N(g, "Curve Length", W(a3))
        r.val(cl, 0, rad * ang, "case %d: the arc is radius * angle = %.6g long" % (k + 1, rad * ang))
        if k == 0:
            dec = (cl, 0, rad * ang)
        ep = N(g, "End Points", W(a3))
        r.pt(ep, 0, pa, "case %d: it starts at the first point" % (k + 1))
        r.pt(ep, 1, pc, "case %d: it ends at the third point" % (k + 1))
        # the point at arc length pb_len from the start is the second point: the arc passes through it
        el = N(g, "Evaluate Length", W(a3), float(pb_len))
        r.pt(el, 0, pb, "case %d: at arc length %.6g from the start it is at the second point" % (k + 1, pb_len))
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gcir3", "D8-GH-CURVE-0042", "Circle 3Pt",
            "(0,0,0), (4,0,0), (0,3,0) form a right triangle whose hypotenuse (length 5) is a diameter: centre (2,1.5,0), radius 2.5, length 5 pi, area 6.25 pi; the unit-axis points (1,0,0), (0,1,0), (0,0,1) "
            "have the circumcentre (1/3,1/3,1/3) (equidistant: its distances to the three are all sqrt(2/3)), radius sqrt(2/3) and the circle is 2 pi sqrt(2/3) long; every one of the three points is at distance "
            "r from the centre and on the circle")
    g = r.g
    for k, (pts, cen, rad) in enumerate([([(0, 0, 0), (4, 0, 0), (0, 3, 0)], (2, 1.5, 0), 2.5), ([(1, 0, 0), (0, 1, 0), (0, 0, 1)], (1 / 3.0, 1 / 3.0, 1 / 3.0), math.sqrt(2.0 / 3.0))]):
        pn = [P(g, *p) for p in pts]
        c3 = N(g, "Circle 3Pt", W(pn[0]), W(pn[1]), W(pn[2]))
        r.val(c3, 2, rad, "case %d: the radius is %.6g" % (k + 1, rad))
        r.pt(c3, 1, cen, "case %d: the plane origin is the centre %s" % (k + 1, tuple(round(x, 6) for x in cen)))
        cl = N(g, "Curve Length", W(c3))
        r.val(cl, 0, 2 * PI * rad, "case %d: the circle is 2 pi r long" % (k + 1))
        if k == 0:
            dec = (cl, 0, 2 * PI * rad)
            ar = N(g, "Curve Area", W(c3))
            r.val(ar, 0, PI * rad * rad, "case 1: the area is pi r^2 = 6.25 pi")
        for j in range(3):
            r.val(N(g, "Distance", W(pn[j]), W(c3, 1)), 0, rad, "case %d: point %d is at distance r from the centre" % (k + 1, j + 1))
            cp = N(g, "Curve Closest Point", W(c3), W(pn[j]))
            r.val(cp, 2, 0.0, "case %d: point %d is on the circle (distance to the curve 0)" % (k + 1, j + 1))
    r.finish(decoy=dec)
    out.append(r)

    r = Row("garea", "D8-GH-CURVE-0027", "Curve Area",
            "circle of radius 2.5 in the plane z = 3 about (1,2,3): 6.25 pi, centroid (1,2,3); triangle (0,0,0),(4,0,0),(0,3,0): 6, centroid (4/3,1,0); L-shaped polygon (0,0),(4,0),(4,1),(1,1),(1,3),(0,3): "
            "4 * 1 + 1 * 2 = 6, centroid ((2 * 4 + 0.5 * 2)/6, (0.5 * 4 + 2 * 2)/6) = (1.5, 1); regular hexagon of circumradius 2: 3 sqrt 3 / 2 * 4 = 6 sqrt 3, centroid at the centre; "
            "the same triangle traced the other way has the same area")
    g = r.g
    ci = N(g, "Circle", W(XYPL(g, (1, 2, 3))), 2.5)
    ar = N(g, "Curve Area", W(ci))
    r.val(ar, 0, 6.25 * PI, "circle r = 2.5: area 6.25 pi")
    dec = (ar, 0, 6.25 * PI)
    r.pt(ar, 1, (1, 2, 3), "circle r = 2.5 at (1,2,3): centroid (1,2,3)")
    tri = N(g, "Curve Area", W(POLY(g, [(0, 0, 0), (4, 0, 0), (0, 3, 0)], True)))
    r.val(tri, 0, 6.0, "triangle 4 x 3: area 6")
    r.pt(tri, 1, (4 / 3.0, 1, 0), "triangle: centroid (4/3,1,0)")
    tri2 = N(g, "Curve Area", W(POLY(g, [(0, 0, 0), (0, 3, 0), (4, 0, 0)], True)))
    r.val(tri2, 0, 6.0, "the triangle traced the other way: area 6")
    lsh = N(g, "Curve Area", W(POLY(g, [(0, 0, 0), (4, 0, 0), (4, 1, 0), (1, 1, 0), (1, 3, 0), (0, 3, 0)], True)))
    r.val(lsh, 0, 6.0, "L-shape: area 6")
    r.pt(lsh, 1, (1.5, 1.0, 0), "L-shape: centroid (1.5,1,0)")
    hx = N(g, "Curve Area", W(N(g, "Polygon", None, 2.0, ("i", 6))))
    r.val(hx, 0, 6 * SQ3, "hexagon of circumradius 2: area 6 sqrt 3")
    r.pt(hx, 1, (0, 0, 0), "hexagon: centroid at the centre")
    r.finish(decoy=dec)
    out.append(r)

    r = Row("grec", "D8-GH-CURVE-0050", "Rectangle",
            "a 6 x 4 rectangle has area 6 * 4 = 24 and perimeter 20; 3 x 7 in the plane through (0,0,0) with normal (1,2,2): area 21, perimeter 20; it is a closed curve")
    g = r.g
    for k, (pl_, sx, sy) in enumerate([(None, 6.0, 4.0), (PLN(g, (0, 0, 0), (1, 2, 2)), 3.0, 7.0)]):
        rc = N(g, "Rectangle", W(pl_) if pl_ is not None else None, sx, sy)
        ar = N(g, "Curve Area", W(rc))
        r.val(ar, 0, sx * sy, "case %d: %g x %g has area %g" % (k + 1, sx, sy, sx * sy))
        if k == 0:
            dec = (ar, 0, sx * sy)
        r.val(N(g, "Curve Length", W(rc)), 0, 2 * (sx + sy), "case %d: perimeter %g" % (k + 1, 2 * (sx + sy)))
        r.raw(N(g, "Is Closed", W(rc)), 0, "1", "case %d: the rectangle is a closed curve" % (k + 1))
    r.finish(decoy=dec)
    out.append(r)

    r = Row("grec2", "D8-GH-CURVE-0052", "Rectangle 2Pt",
            "in the XY plane, corners (1,2) and (5,5): sides 4 and 3, area 12, perimeter 14, centroid (3,3.5,0); the corners given the other way round (5,5), (1,2) give the same; in the plane through the origin "
            "with axes (0,1,0) and (0,0,1) the points (0,1,2) and (7,4,6) are projected to (u,v) = (1,2) and (4,6): sides 3 and 4, area 12, perimeter 14, centroid at u = 2.5, v = 4, i.e. (0,2.5,4)")
    g = r.g
    for k, (pl_, a, b, cen) in enumerate([(None, (1, 2, 0), (5, 5, 0), (3, 3.5, 0)), (None, (5, 5, 0), (1, 2, 0), (3, 3.5, 0)),
                                        (PLXY(g, (0, 0, 0), (0, 1, 0), (0, 0, 1)), (0, 1, 2), (7, 4, 6), (0, 2.5, 4))]):
        rc = N(g, "Rectangle 2Pt", W(pl_) if pl_ is not None else None, W(P(g, *a)), W(P(g, *b)))
        ar = N(g, "Curve Area", W(rc))
        r.val(ar, 0, 12.0, "case %d: area 12" % (k + 1))
        if k == 0:
            dec = (ar, 0, 12.0)
        r.pt(ar, 1, cen, "case %d: centroid %s" % (k + 1, cen))
        r.val(N(g, "Curve Length", W(rc)), 0, 14.0, "case %d: perimeter 14" % (k + 1))
        r.raw(N(g, "Is Closed", W(rc)), 0, "1", "case %d: the rectangle is a closed curve" % (k + 1))
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gpgon", "D8-GH-CURVE-0054", "Polygon",
            "regular pentagon of circumradius 2 (Segments = 5): corners 2 (cos 72 k, sin 72 k), k = 0..4; side 2 R sin(36 degrees) = 2.35114100917, perimeter 5 * side, area (1/2) n R^2 sin(72 degrees) = 9.51056516295; "
            "the angle between two edges at a corner is (n - 2) 180 / n = 108 degrees (cosine -0.309016994375); hexagon of circumradius 1: perimeter 6, area 3 sqrt 3 / 2; it is closed")
    g = r.g
    pg = N(g, "Polygon", None, 2.0, ("i", 5))
    side = 2 * 2.0 * math.sin(PI / 5)
    cl = N(g, "Curve Length", W(pg))
    r.val(cl, 0, 5 * side, "pentagon R = 2: perimeter 5 * 2 R sin 36 degrees")
    dec = (cl, 0, 5 * side)
    r.val(N(g, "Curve Area", W(pg)), 0, 0.5 * 5 * 4.0 * math.sin(2 * PI / 5), "pentagon R = 2: area (1/2) n R^2 sin(72 degrees)")
    for k in range(5):
        ev = N(g, "Evaluate Curve", W(pg), k / 5.0)
        r.pt(ev, 0, (2 * math.cos(2 * PI * k / 5), 2 * math.sin(2 * PI * k / 5), 0), "pentagon: corner %d is 2 (cos %d, sin %d) degrees" % (k, 72 * k, 72 * k))
    # corner 1: edges to corner 0 and to corner 2
    e1 = N(g, "Evaluate Curve", W(pg), 0.2)
    e0 = N(g, "Evaluate Curve", W(pg), 0.0)
    e2 = N(g, "Evaluate Curve", W(pg), 0.4)
    u = N(g, "Vector 2Pt", W(e1, 0), W(e0, 0), ("b", True))
    v = N(g, "Vector 2Pt", W(e1, 0), W(e2, 0), ("b", True))
    r.val(N(g, "Dot Product", W(u), W(v)), 0, math.cos(math.radians(108.0)), "pentagon: the angle at a corner has cosine cos(108 degrees)")
    r.raw(N(g, "Is Closed", W(pg)), 0, "1", "pentagon is a closed curve")
    hx = N(g, "Polygon", None, 1.0, ("i", 6))
    r.val(N(g, "Curve Length", W(hx)), 0, 6.0, "hexagon R = 1: perimeter 6")
    r.val(N(g, "Curve Area", W(hx)), 0, 1.5 * SQ3, "hexagon R = 1: area 3 sqrt 3 / 2")
    r.finish(decoy=dec)
    out.append(r)

    # ------------------------------------------------------------------ Curve > Analysis
    r = Row("gendp", "D8-GH-CURVE-0011", "End Points",
            "a circle of radius 3 at (1,2,0) starts and ends at (4,2,0) (its X axis point); the line (1,2,3)->(3,4,4) at its two points; the polyline (0,0,0),(4,0,0),(4,3,0) at (0,0,0) and (4,3,0); "
            "the arc of radius 2 from 0 to 90 degrees at (2,0,0) and (0,2,0)")
    g = r.g
    ci = N(g, "Circle", W(XYPL(g, (1, 2, 0))), 3.0)
    ep = N(g, "End Points", W(ci))
    r.pt(ep, 0, (4, 2, 0), "circle: starts at (4,2,0)")
    r.pt(ep, 1, (4, 2, 0), "circle: ends at (4,2,0), where it started")
    r.val(N(g, "Distance", W(ep, 0), W(ep, 1)), 0, 0.0, "circle: start and end are one point")
    dec = (N(g, "Distance", W(ep, 0), W(P(g, 4, 2, 0))), 0, 0.0)
    ep = N(g, "End Points", W(N(g, "Line", W(P(g, 1, 2, 3)), W(P(g, 3, 4, 4)))))
    r.pt(ep, 0, (1, 2, 3), "line: starts at (1,2,3)")
    r.pt(ep, 1, (3, 4, 4), "line: ends at (3,4,4)")
    ep = N(g, "End Points", W(POLY(g, [(0, 0, 0), (4, 0, 0), (4, 3, 0)])))
    r.pt(ep, 0, (0, 0, 0), "polyline: starts at its first vertex (0,0,0)")
    r.pt(ep, 1, (4, 3, 0), "polyline: ends at its last vertex (4,3,0)")
    ep = N(g, "End Points", W(N(g, "Arc", None, 2.0, 0.0, 90.0)))
    r.pt(ep, 0, (2, 0, 0), "arc: starts at (2,0,0)")
    r.pt(ep, 1, (0, 2, 0), "arc: ends at (0,2,0)")
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gclosed", "D8-GH-CURVE-0016", "Is Closed",
            "closed: a circle, a rectangle, a polygon, a circle through three points, a polyline given Closed = true; open: a line, a 90 degree arc, a polyline of three vertices, a cubic NURBS curve through 4 "
            "control points; a NURBS curve given Closed = true repeats its first control point and closes")
    g = r.g
    pts3 = [(0, 0, 0), (4, 0, 0), (4, 3, 0)]
    cases = [("circle", N(g, "Circle", None, 2.0), "1"), ("rectangle", N(g, "Rectangle", None, 6.0, 4.0), "1"), ("polygon", N(g, "Polygon", None, 2.0, ("i", 5)), "1"),
             ("circle through three points", N(g, "Circle 3Pt", W(P(g, 0, 0, 0)), W(P(g, 4, 0, 0)), W(P(g, 0, 3, 0))), "1"),
             ("closed polyline", POLY(g, pts3, True), "1"), ("NURBS curve with Closed = true", N(g, "NURBS Curve", W(*PLIST(g, [(0, 0, 0), (2, 1, 0), (3, 3, 0), (0, 4, 0)])), ("i", 3), ("b", True)), "1"),
             ("line", N(g, "Line", W(P(g, 0, 0, 0)), W(P(g, 3, 4, 0))), "0"), ("90 degree arc", N(g, "Arc", None, 2.0, 0.0, 90.0), "0"), ("open polyline", POLY(g, pts3, False), "0"),
             ("cubic NURBS curve", N(g, "NURBS Curve", W(*PLIST(g, [(0, 0, 0), (2, 1, 0), (3, 3, 0), (0, 4, 0)])), ("i", 3), ("b", False)), "0")]
    for name, nid, want in cases:
        r.raw(N(g, "Is Closed", W(nid)), 0, want, "%s: Is Closed = %s" % (name, want))
    r.finish()
    out.append(r)

    r = Row("gpline", "D8-GH-CURVE-0055", "Polyline / Control Points",
            "a polyline keeps its vertices as the control points of its degree-1 curve: (0,0,0),(4,0,0),(4,3,0),(0,3,0) gives 4 control points at those places, weights 1, and the length 4 + 3 + 4 = 11 "
            "(14 closed); the cubic NURBS curve through 6 given control points returns the same 6 points, 6 weights of 1 and 6 + 3 - 1 = 8 knots")
    g = r.g
    vs = [(0, 0, 0), (4, 0, 0), (4, 3, 0), (0, 3, 0)]
    pl_ = POLY(g, vs, False)
    cp = N(g, "Control Points", W(pl_))
    r.count(cp, 0, 4, "polyline: 4 control points")
    for k, v in enumerate(vs):
        r.pt(r.item(cp, 0, k), 0, v, "polyline: control point %d is the vertex %s" % (k, v))
    r.raw(N(g, "Mass Addition", W(cp, 1)), 0, "4", "polyline: the 4 weights are 1 (sum 4)")
    cl = N(g, "Curve Length", W(pl_))
    r.val(cl, 0, 11.0, "polyline: length 4 + 3 + 4 = 11")
    dec = (cl, 0, 11.0)
    r.val(N(g, "Curve Length", W(POLY(g, vs, True))), 0, 14.0, "closed polyline: length 14")
    cps = [(0, 0, 0), (1, 2, 0), (2, 3, 1), (4, 3, 1), (5, 1, 0), (6, 0, 0)]
    nb = N(g, "NURBS Curve", W(*PLIST(g, cps)), ("i", 3), ("b", False))
    cp2 = N(g, "Control Points", W(nb))
    r.count(cp2, 0, 6, "cubic curve: 6 control points")
    for k, v in enumerate(cps):
        r.pt(r.item(cp2, 0, k), 0, v, "cubic curve: control point %d is %s" % (k, v))
    r.raw(N(g, "Mass Addition", W(cp2, 1)), 0, "6", "cubic curve: the 6 weights are 1 (sum 6)")
    r.count(cp2, 2, 8, "cubic curve: 6 + 3 - 1 = 8 knots")
    r.finish(decoy=dec)
    out.append(r)

    # ------------------------------------------------------------------ Curve > Division
    r = Row("gdivc", "D8-GH-CURVE-0031", "Divide Curve",
            "circle of radius 2 about (1,2,0) into 8: a closed curve gives 8 points (the ninth would be the first again), at angles 45 k degrees: (1 + 2 cos, 2 + 2 sin, 0), the tangent at k = 2 is (-1,0,0); "
            "the line (0,0,0)-(10,0,0) into 4: 5 points at x = 0, 2.5, 5, 7.5, 10; the arc of radius 3 from 0 to 90 degrees into 3: 4 points at 0, 30, 60, 90 degrees, 3 (cos, sin); "
            "the closed polyline (0,0),(6,0),(6,4),(0,4) (perimeter 20) into 10: 10 points 2 apart along the outline: (0,0),(2,0),(4,0),(6,0),(6,2),(6,4),(4,4),(2,4),(0,4),(0,2)")
    g = r.g
    ci = N(g, "Circle", W(XYPL(g, (1, 2, 0))), 2.0)
    dv = N(g, "Divide Curve", W(ci), ("i", 8))
    r.count(dv, 0, 8, "circle into 8: 8 points (closed)")
    for k in range(8):
        a = PI * k / 4
        r.pt(r.item(dv, 0, k), 0, (1 + 2 * math.cos(a), 2 + 2 * math.sin(a), 0), "circle into 8: point %d at %d degrees" % (k, 45 * k))
    r.pt(r.item(dv, 1, 2), 0, (-1, 0, 0), "circle into 8: the tangent at point 2 (90 degrees) is (-1,0,0)")
    dec = (N(g, "Distance", W(r.item(dv, 0, 3)), W(P(g, 1 + 2 * math.cos(3 * PI / 4), 2 + 2 * math.sin(3 * PI / 4), 0))), 0, 0.0)
    dl = N(g, "Divide Curve", W(N(g, "Line", W(P(g, 0, 0, 0)), W(P(g, 10, 0, 0)))), ("i", 4))
    r.count(dl, 0, 5, "line into 4: 5 points (open)")
    for k in range(5):
        r.pt(r.item(dl, 0, k), 0, (2.5 * k, 0, 0), "line into 4: point %d at x = %g" % (k, 2.5 * k))
    da = N(g, "Divide Curve", W(N(g, "Arc", None, 3.0, 0.0, 90.0)), ("i", 3))
    r.count(da, 0, 4, "arc into 3: 4 points (open)")
    for k in range(4):
        a = math.radians(30.0 * k)
        r.pt(r.item(da, 0, k), 0, (3 * math.cos(a), 3 * math.sin(a), 0), "arc into 3: point %d at %d degrees" % (k, 30 * k))
    dp = N(g, "Divide Curve", W(POLY(g, [(0, 0, 0), (6, 0, 0), (6, 4, 0), (0, 4, 0)], True)), ("i", 10))
    r.count(dp, 0, 10, "closed polyline into 10: 10 points")
    for k, xy in enumerate([(0, 0), (2, 0), (4, 0), (6, 0), (6, 2), (6, 4), (4, 4), (2, 4), (0, 4), (0, 2)]):
        r.pt(r.item(dp, 0, k), 0, (xy[0], xy[1], 0), "closed polyline into 10: point %d at %s" % (k, xy))
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gdivl", "D8-GH-CURVE-0033", "Divide Length",
            "points at arc lengths 0, L, 2L, ... from the start, the remainder at the end getting no point of its own: the line (0,0,0)-(10,0,0) with L = 3 gives x = 0, 3, 6, 9 (4 points); with L = 2.5 gives "
            "x = 0, 2.5, 5, 7.5, 10 (5 points, the last multiple lands on the end); the circle of radius 2 with L = 1 gives 13 points (circumference 12.566 holds 12 steps) at angles k / 2 radians, (2 cos, 2 sin); "
            "the polyline (0,0,0),(4,0,0),(4,3,0) (length 7) with L = 2.5 gives (0,0), (2.5,0), (4,1)")
    g = r.g
    ln = N(g, "Line", W(P(g, 0, 0, 0)), W(P(g, 10, 0, 0)))
    d3 = N(g, "Divide Length", W(ln), 3.0)
    r.count(d3, 0, 4, "line, L = 3: 4 points")
    for k in range(4):
        r.pt(r.item(d3, 0, k), 0, (3.0 * k, 0, 0), "line, L = 3: point %d at x = %g" % (k, 3.0 * k))
    d25 = N(g, "Divide Length", W(ln), 2.5)
    r.count(d25, 0, 5, "line, L = 2.5: 5 points")
    for k in range(5):
        r.pt(r.item(d25, 0, k), 0, (2.5 * k, 0, 0), "line, L = 2.5: point %d at x = %g" % (k, 2.5 * k))
    dc = N(g, "Divide Length", W(N(g, "Circle", None, 2.0)), 1.0)
    r.count(dc, 0, 13, "circle r = 2, L = 1: 13 points")
    for k in range(13):
        a = 0.5 * k
        r.pt(r.item(dc, 0, k), 0, (2 * math.cos(a), 2 * math.sin(a), 0), "circle r = 2, L = 1: point %d at %g radians" % (k, a))
    dec = (N(g, "Distance", W(r.item(dc, 0, 5)), W(P(g, 2 * math.cos(2.5), 2 * math.sin(2.5), 0))), 0, 0.0)
    dpl = N(g, "Divide Length", W(POLY(g, [(0, 0, 0), (4, 0, 0), (4, 3, 0)])), 2.5)
    r.count(dpl, 0, 3, "polyline of length 7, L = 2.5: 3 points")
    for k, xy in enumerate([(0, 0), (2.5, 0), (4, 1)]):
        r.pt(r.item(dpl, 0, k), 0, (xy[0], xy[1], 0), "polyline, L = 2.5: point %d at %s" % (k, xy))
    r.finish(decoy=dec)
    out.append(r)

    # ------------------------------------------------------------------ Curve > Util / Analysis
    r = Row("grev", "D8-GH-CURVE-0085", "Reverse Curve",
            "the line (1,2,3)->(4,6,3) reversed starts at (4,6,3) and ends at (1,2,3), has the same length 5 and the middle (2.5,4,3) with unit tangent (-0.6,-0.8,0) (it was (0.6,0.8,0)); the circle of radius 2 at the "
            "origin starts at (2,0,0) both ways, its tangent there (0,1,0) turns into (0,-1,0) and a quarter of the way round is (0,-2,0) instead of (0,2,0); the polyline (0,0,0),(4,0,0),(4,3,0) reversed starts at (4,3,0), "
            "ends at (0,0,0) and one unit along from its start is (4,2,0)")
    g = r.g
    ln = N(g, "Line", W(P(g, 1, 2, 3)), W(P(g, 4, 6, 3)))
    rv = N(g, "Reverse Curve", W(ln))
    ep = N(g, "End Points", W(rv))
    r.pt(ep, 0, (4, 6, 3), "line: starts at (4,6,3)")
    r.pt(ep, 1, (1, 2, 3), "line: ends at (1,2,3)")
    cl = N(g, "Curve Length", W(rv))
    r.val(cl, 0, 5.0, "line: still 5 long")
    dec = (cl, 0, 5.0)
    ev = N(g, "Evaluate Curve", W(rv), 0.5)
    r.pt(ev, 0, (2.5, 4, 3), "line: the middle is (2.5,4,3)")
    r.pt(ev, 1, (-0.6, -0.8, 0), "line: the tangent is (-0.6,-0.8,0)")
    ci = N(g, "Circle", None, 2.0)
    rc = N(g, "Reverse Curve", W(ci))
    e0 = N(g, "Evaluate Curve", W(rc), 0.0)
    r.pt(e0, 0, (2, 0, 0), "circle: starts at (2,0,0)")
    r.pt(e0, 1, (0, -1, 0), "circle: the tangent at the start is (0,-1,0)")
    r.pt(N(g, "Evaluate Curve", W(rc), 0.25), 0, (0, -2, 0), "circle: a quarter of the way round is (0,-2,0)")
    pl_ = POLY(g, [(0, 0, 0), (4, 0, 0), (4, 3, 0)])
    rp = N(g, "Reverse Curve", W(pl_))
    ep = N(g, "End Points", W(rp))
    r.pt(ep, 0, (4, 3, 0), "polyline: starts at (4,3,0)")
    r.pt(ep, 1, (0, 0, 0), "polyline: ends at (0,0,0)")
    r.pt(N(g, "Evaluate Length", W(rp), 1.0), 0, (4, 2, 0), "polyline: one unit from the new start is (4,2,0)")
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gevlen", "D8-GH-CURVE-0013", "Evaluate Length",
            "the circle of radius 2 at the origin: arc length 1 from its start is angle 1/2 radian, (2 cos 0.5, 2 sin 0.5, 0); a fraction 0.25 of its length (Normalized) is a quarter turn (0,2,0), tangent (-1,0,0); "
            "length 0 is the start (2,0,0); a length past the end is the end; the line (1,2,3)->(4,6,3) at length 2.5 (half of 5) is (2.5,4,3) with tangent (0.6,0.8,0); "
            "the polyline (0,0,0),(4,0,0),(4,3,0) at length 5 is (4,1,0): 4 along the first edge and 1 along the second")
    g = r.g
    ci = N(g, "Circle", None, 2.0)
    e1 = N(g, "Evaluate Length", W(ci), 1.0)
    r.pt(e1, 0, (2 * math.cos(0.5), 2 * math.sin(0.5), 0), "circle: length 1 from the start is at 0.5 radian")
    dec = (N(g, "Distance", W(e1, 0), W(P(g, 2 * math.cos(0.5), 2 * math.sin(0.5), 0))), 0, 0.0)
    e2 = N(g, "Evaluate Length", W(ci), 0.25, ("b", True))
    r.pt(e2, 0, (0, 2, 0), "circle: a quarter of the length is (0,2,0)")
    r.pt(e2, 1, (-1, 0, 0), "circle: the tangent there is (-1,0,0)")
    r.pt(N(g, "Evaluate Length", W(ci), 0.0), 0, (2, 0, 0), "circle: length 0 is the start (2,0,0)")
    r.pt(N(g, "Evaluate Length", W(ci), 50.0), 0, (2, 0, 0), "circle: a length past the end is the end, (2,0,0) again")
    ln = N(g, "Line", W(P(g, 1, 2, 3)), W(P(g, 4, 6, 3)))
    e3 = N(g, "Evaluate Length", W(ln), 2.5)
    r.pt(e3, 0, (2.5, 4, 3), "line: length 2.5 is (2.5,4,3)")
    r.pt(e3, 1, (0.6, 0.8, 0), "line: the tangent is (0.6,0.8,0)")
    pl_ = POLY(g, [(0, 0, 0), (4, 0, 0), (4, 3, 0)])
    r.pt(N(g, "Evaluate Length", W(pl_), 5.0), 0, (4, 1, 0), "polyline: length 5 is (4,1,0)")
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gcurv", "D8-GH-CURVE-0003", "Curve Curvature",
            "a circle of radius R has curvature 1/R everywhere, the vector pointing at the centre: R = 2.5 at the origin gives length 0.4 at t = 0.1, 0.5, 0.9 and centre = point + vector * R^2 = (0,0,0); "
            "the arc of radius 4 (0 to 90 degrees) has curvature 0.25; the circle of radius 2 about (1,2,3) in the plane with normal (1,1,1) has curvature 0.5 and the same centre property; "
            "a straight line has curvature 0")
    g = r.g
    ci = N(g, "Circle", None, 2.5)
    for tt in (0.1, 0.5, 0.9):
        cv = N(g, "Curve Curvature", W(ci), tt)
        vl = N(g, "Vector Length", W(cv, 1))
        r.val(vl, 0, 0.4, "circle R = 2.5, t = %g: curvature 1/R = 0.4" % tt)
        if tt == 0.1:
            dec = (vl, 0, 0.4)
        mv = N(g, "Move", W(cv, 0), W(N(g, "Vector Scale", W(cv, 1), 6.25)))
        r.pt(mv, 0, (0, 0, 0), "circle R = 2.5, t = %g: point + vector * R^2 is the centre" % tt)
    arc = N(g, "Arc", None, 4.0, 0.0, 90.0)
    cv = N(g, "Curve Curvature", W(arc), 0.37)
    r.val(N(g, "Vector Length", W(cv, 1)), 0, 0.25, "arc R = 4: curvature 0.25")
    r.pt(N(g, "Move", W(cv, 0), W(N(g, "Vector Scale", W(cv, 1), 16.0))), 0, (0, 0, 0), "arc R = 4: point + vector * R^2 is the centre (0,0,0)")
    til = N(g, "Circle", W(PLN(g, (1, 2, 3), (1, 1, 1))), 2.0)
    cv = N(g, "Curve Curvature", W(til), 0.6)
    r.val(N(g, "Vector Length", W(cv, 1)), 0, 0.5, "circle R = 2 in a tilted plane: curvature 0.5")
    r.pt(N(g, "Move", W(cv, 0), W(N(g, "Vector Scale", W(cv, 1), 4.0))), 0, (1, 2, 3), "tilted circle: point + vector * R^2 is the centre (1,2,3)")
    cv = N(g, "Curve Curvature", W(N(g, "Line", W(P(g, 0, 0, 0)), W(P(g, 3, 4, 0)))), 0.5)
    r.val(N(g, "Vector Length", W(cv, 1)), 0, 0.0, "line: curvature 0")
    r.guard(cv, 1, "line: the curvature vector exists")
    r.finish(decoy=dec)
    out.append(r)

    r = Row("geval", "D8-GH-CURVE-0012", "Evaluate Curve",
            "the circle of radius 2 about (1,2,0) at t = 0, 0.25, 0.5, 0.75 (fractions of the domain) is (3,2,0), (1,4,0), (-1,2,0), (1,0,0) with unit tangents (0,1,0), (-1,0,0), (0,-1,0), (1,0,0); "
            "the line (0,0,0)->(3,4,0) at t = 0, 0.5, 1 is (0,0,0), (1.5,2,0), (3,4,0), tangent (0.6,0.8,0); the arc of radius 2 from 0 to 90 degrees at t = 0.5 is at 45 degrees, (sqrt 2, sqrt 2, 0), tangent (-sqrt(1/2), sqrt(1/2), 0)")
    g = r.g
    ci = N(g, "Circle", W(XYPL(g, (1, 2, 0))), 2.0)
    for tt, p_, tg in [(0.0, (3, 2, 0), (0, 1, 0)), (0.25, (1, 4, 0), (-1, 0, 0)), (0.5, (-1, 2, 0), (0, -1, 0)), (0.75, (1, 0, 0), (1, 0, 0))]:
        ev = N(g, "Evaluate Curve", W(ci), tt)
        r.pt(ev, 0, p_, "circle, t = %g: the point %s" % (tt, p_))
        r.pt(ev, 1, tg, "circle, t = %g: the unit tangent %s" % (tt, tg))
        if tt == 0.25:
            dec = (N(g, "Distance", W(ev, 0), W(P(g, 1, 4, 0))), 0, 0.0)
    ln = N(g, "Line", W(P(g, 0, 0, 0)), W(P(g, 3, 4, 0)))
    for tt, p_ in [(0.0, (0, 0, 0)), (0.5, (1.5, 2, 0)), (1.0, (3, 4, 0))]:
        ev = N(g, "Evaluate Curve", W(ln), tt)
        r.pt(ev, 0, p_, "line, t = %g: the point %s" % (tt, p_))
        r.pt(ev, 1, (0.6, 0.8, 0), "line, t = %g: the unit tangent (0.6,0.8,0)" % tt)
    ev = N(g, "Evaluate Curve", W(N(g, "Arc", None, 2.0, 0.0, 90.0)), 0.5)
    r.pt(ev, 0, (SQ2, SQ2, 0), "arc, t = 0.5: the point at 45 degrees (sqrt 2, sqrt 2)")
    r.pt(ev, 1, (-math.sqrt(0.5), math.sqrt(0.5), 0), "arc, t = 0.5: the unit tangent (-sqrt(1/2), sqrt(1/2))")
    r.finish(decoy=dec)
    out.append(r)

    # ------------------------------------------------------------------ Intersect
    r = Row("glxl", "D8-GH-XSECT-0004", "Line x Line",
            "skew lines (0,0,0)-(10,0,0) and (3,5,2)-(3,-5,2): closest points (3,0,0) at 0.3 along the first and (3,0,2) at 0.5 along the second, distance 2; crossing diagonals (0,0,0)-(4,4,0) and (0,4,0)-(4,0,0): "
            "both points (2,2,0), distance 0, parameters 0.5 and 0.5; (0,0,0)-(2,2,0) against (1,-3,5)-(1,3,5): (1,1,0) at 0.5 and (1,1,5) at 2/3, distance 5; the lines are infinite: (0,0,0)-(1,0,0) against "
            "(3,-1,1)-(3,1,1): (3,0,0) at parameter 3 and (3,0,1) at 0.5, distance 1")
    g = r.g
    cases = [((0, 0, 0), (10, 0, 0), (3, 5, 2), (3, -5, 2), (3, 0, 0), (3, 0, 2), 2.0, 0.3, 0.5),
             ((0, 0, 0), (4, 4, 0), (0, 4, 0), (4, 0, 0), (2, 2, 0), (2, 2, 0), 0.0, 0.5, 0.5),
             ((0, 0, 0), (2, 2, 0), (1, -3, 5), (1, 3, 5), (1, 1, 0), (1, 1, 5), 5.0, 0.5, 2.0 / 3.0),
             ((0, 0, 0), (1, 0, 0), (3, -1, 1), (3, 1, 1), (3, 0, 0), (3, 0, 1), 1.0, 3.0, 0.5)]
    for k, (a0, a1, b0, b1, pa, pb, dist, sa, sb) in enumerate(cases):
        la = N(g, "Line", W(P(g, *a0)), W(P(g, *a1)))
        lb = N(g, "Line", W(P(g, *b0)), W(P(g, *b1)))
        x = N(g, "Line x Line", W(la), W(lb))
        r.pt(x, 0, pa, "case %d: the nearest point on the first line is %s" % (k + 1, tuple(round(v, 6) for v in pa)))
        r.pt(x, 1, pb, "case %d: the nearest point on the second line is %s" % (k + 1, tuple(round(v, 6) for v in pb)))
        r.val(x, 2, dist, "case %d: the distance between them is %g" % (k + 1, dist))
        r.val(x, 3, sa, "case %d: the first parameter is %.6g" % (k + 1, sa))
        r.val(x, 4, sb, "case %d: the second parameter is %.6g" % (k + 1, sb))
        if k == 0:
            dec = (x, 2, dist)
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gpxp", "D8-GH-XSECT-0005", "Plane x Plane",
            "z = 1 (origin (0,0,1), normal (0,0,1)) and x = 2 (origin (2,0,0), normal (1,0,0)) meet in the line x = 2, z = 1 along n1 x n2 = (0,1,0), nearest to (0,0,1) at (2,0,1); "
            "x + y = 3 (origin (1,2,3), normal (1,1,0)) and y + z = 0 (origin (0,0,0), normal (0,1,1)) meet along (1,-1,1)/sqrt 3 through (8/3,1/3,-1/3) (it satisfies both equations, and "
            "(p - (1,2,3)).(1,-1,1) = 5/3 + 5/3 - 10/3 = 0 makes it the nearest to the first origin); the segment returned is 2 long about that point")
    g = r.g
    for k, (oa, na, ob, nb_, pt_, dr) in enumerate([((0, 0, 1), (0, 0, 1), (2, 0, 0), (1, 0, 0), (2, 0, 1), (0, 1, 0)),
                                                     ((1, 2, 3), (1, 1, 0), (0, 0, 0), (0, 1, 1), (8 / 3.0, 1 / 3.0, -1 / 3.0), (1 / SQ3, -1 / SQ3, 1 / SQ3))]):
        x = N(g, "Plane x Plane", W(PLN(g, oa, na)), W(PLN(g, ob, nb_)))
        r.pt(x, 1, pt_, "case %d: the point nearest the first origin is %s" % (k + 1, tuple(round(v, 6) for v in pt_)))
        r.pt(x, 2, dr, "case %d: the direction is n1 x n2 normalized %s" % (k + 1, tuple(round(v, 6) for v in dr)))
        cl = N(g, "Curve Length", W(x))
        r.val(cl, 0, 2.0, "case %d: the segment is 2 long" % (k + 1))
        if k == 0:
            dec = (cl, 0, 2.0)
        ev = N(g, "Evaluate Curve", W(x), 0.5)
        r.pt(ev, 0, pt_, "case %d: the segment is centred on that point" % (k + 1))
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gppp", "D8-GH-XSECT-0006", "Plane x Plane x Plane",
            "the planes x = 1, y = 2, z = 3 meet at (1,2,3); the planes n.p = d with n1 = (1,1,1), d1 = 6; n2 = (1,-1,0), d2 = -1; n3 = (0,2,-1), d3 = 1 all hold at (1,2,3) (1 + 2 + 3 = 6, 1 - 2 = -1, 4 - 3 = 1) "
            "and the determinant of the normals is 1 (0 + 2) - 1 (0 - 0) + 1 (2 - 0) = 4, nonzero, so it is the only common point; the planes 2x + y = -0.25, y + 3z = 4.75, x - z = -1.5 meet at (0.5,-1.25,2) "
            "(1 - 1.25 = -0.25, -1.25 + 6 = 4.75, 0.5 - 2 = -1.5)")
    g = r.g
    cases = [([((1, 0, 0), (1, 0, 0)), ((0, 2, 0), (0, 1, 0)), ((0, 0, 3), (0, 0, 1))], (1, 2, 3)),
             ([((2, 2, 2), (1, 1, 1)), ((0, 1, 0), (1, -1, 0)), ((0, 0, -1), (0, 2, -1))], (1, 2, 3)),
             ([((0, -0.25, 0), (2, 1, 0)), ((7, 4.75, 0), (0, 1, 3)), ((-1.5, 9, 0), (1, 0, -1))], (0.5, -1.25, 2))]
    for k, (pls, want) in enumerate(cases):
        x = N(g, "Plane x Plane x Plane", *[W(PLN(g, o, n)) for o, n in pls])
        r.pt(x, 0, want, "case %d: the three planes meet at %s" % (k + 1, want))
        if k == 2:
            dec = (N(g, "Distance", W(x, 0), W(P(g, *want))), 0, 0.0)
    r.finish(decoy=dec)
    out.append(r)

    r = Row("gcxp", "D8-GH-XSECT-0003", "Curve x Plane",
            "the circle of radius 3 about the origin and the plane x = 0: 2 points, (0,3,0) then (0,-3,0) in curve order; the plane x = 1: (1, sqrt 8, 0) then (1,-sqrt 8, 0) (1 + 8 = 9); the plane x = 5: none; "
            "the line (0,0,0)-(0,0,10) and the plane z = 4: (0,0,4); the closed polyline (0,0),(6,0),(6,4),(0,4) and the plane x = 3: (3,0,0) and (3,4,0)")
    g = r.g
    ci = N(g, "Circle", None, 3.0)
    x0 = N(g, "Curve x Plane", W(ci), W(PLN(g, (0, 0, 0), (1, 0, 0))))
    r.count(x0, 0, 2, "circle x (x = 0): 2 points")
    r.pt(r.item(x0, 0, 0), 0, (0, 3, 0), "circle x (x = 0): the first point (0,3,0)")
    r.pt(r.item(x0, 0, 1), 0, (0, -3, 0), "circle x (x = 0): the second point (0,-3,0)")
    x1 = N(g, "Curve x Plane", W(ci), W(PLN(g, (1, 0, 0), (1, 0, 0))))
    r.count(x1, 0, 2, "circle x (x = 1): 2 points")
    r.pt(r.item(x1, 0, 0), 0, (1, math.sqrt(8.0), 0), "circle x (x = 1): the first point (1,sqrt 8,0)")
    r.pt(r.item(x1, 0, 1), 0, (1, -math.sqrt(8.0), 0), "circle x (x = 1): the second point (1,-sqrt 8,0)")
    dec = (N(g, "Distance", W(r.item(x1, 0, 0)), W(P(g, 1, math.sqrt(8.0), 0))), 0, 0.0)
    x5 = N(g, "Curve x Plane", W(ci), W(PLN(g, (5, 0, 0), (1, 0, 0))))
    r.count(x5, 0, 0, "circle x (x = 5): no point")
    xl = N(g, "Curve x Plane", W(N(g, "Line", W(P(g, 0, 0, 0)), W(P(g, 0, 0, 10)))), W(PLN(g, (0, 0, 4), (0, 0, 1))))
    r.count(xl, 0, 1, "line x (z = 4): 1 point")
    r.pt(r.item(xl, 0, 0), 0, (0, 0, 4), "line x (z = 4): the point (0,0,4)")
    xp = N(g, "Curve x Plane", W(POLY(g, [(0, 0, 0), (6, 0, 0), (6, 4, 0), (0, 4, 0)], True)), W(PLN(g, (3, 0, 0), (1, 0, 0))))
    r.count(xp, 0, 2, "rectangle x (x = 3): 2 points")
    r.pt(r.item(xp, 0, 0), 0, (3, 0, 0), "rectangle x (x = 3): the first point (3,0,0)")
    r.pt(r.item(xp, 0, 1), 0, (3, 4, 0), "rectangle x (x = 3): the second point (3,4,0)")
    r.finish(decoy=dec)
    out.append(r)

    return out


def main():
    for r in rows():
        g = r.g
        nobj = sum(len(vals.split()) if vals else 0 for _tag, vals, _msg in r.exp)
        with open(os.path.join(HERE, "flowgeo_%s.dflow" % r.name), "w") as f:
            f.write(g.text())
        with open(os.path.join(HERE, "flowgeo_%s_script.txt" % r.name), "w") as f:
            f.write("# Flow Register row %s (%s): player + bake, read back by sections/97_%s.sh (see flowgeo_gen.py)\n"
                    "Grasshopper\nGrasshopperPlayer @GRAPH@ Bake=Yes\n@expect_objects %d\nSelAll\nList\n" % (r.rid, r.title, r.name, nobj))
        checks = "\n".join('fr_chk %d "%s" "%s"' % (tag, vals, msg.replace('"', "'")) for tag, vals, msg in r.exp)
        with open(os.path.join(HERE, "sections", "97_%s.sh" % r.name), "w") as f:
            f.write(SECTION.format(up=r.name.upper(), rid=r.rid, title=r.title, name=r.name, closed=r.closed.replace("\n", " "), nn=len(g.nodes), nobj=nobj, checks=checks))
        print(r.name, r.rid, "nodes", len(g.nodes), "objects", nobj)


if __name__ == "__main__":
    main()
