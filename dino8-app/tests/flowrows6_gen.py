#!/usr/bin/env python3
"""Sixth batch of Flow Register-row tests (Grasshopper Vector, Transform, data-tree and data-matching rows): for each row one
.dflow graph, one player script and one tests/sections/104_<name>.sh section (the section name, upper-cased, is the row's
acceptance test id). Machinery and conventions are those of flowrows_gen.py / flowrows2_gen.py (read their headers): every graph
ends in Construct Point (X = the value under test, Y = a case tag) -> Bake, and the section reads the points back by tag.

What this batch adds to the machinery
  * exact(): the same residual chain as flowrows2_gen.py (Remap Numbers(value; closed form c .. c + 1 -> 0 .. 1e12) -> Round To
    Multiple(1)) with an Absolute node at the end, so that a residual of -0.00001 (which rounds to a negative zero and would print
    as "-0") also reads "0". It prints "0" exactly when |value - c| < 5e-13, inside the 1e-12 absolute tolerance of the rows.
  * a builder (T) that numbers the tags itself: val() probes printed values, exact() a real value against a closed form, comps()
    the three coordinates of a point or vector, none() that a node gave nothing (a flagged node), stats() the shape of a data tree
    (branch count, items per branch, every path index) read through Tree Statistics and Deconstruct Path.
  * check lines for the player's own report of flagged nodes: "GrasshopperPlayer: <node> (#id) flagged: <why>".
  * every section ends with the control of flowrows2_gen.py: Square(2) against the wrong closed form 4.5 must read 500000000000,
    which shows that the chain reports a mismatch and that the 0 values are real matches.
Every expected value is a closed form worked out by hand (written beside it) and independent of the node code under test.
Run from dino8-app/tests:  python3 flowrows6_gen.py
"""
import math
import os

from flowrows_gen import G, SECTION, series

HERE = os.path.dirname(os.path.abspath(__file__))
PI = math.pi
S2 = math.sqrt(2.0)
S3 = math.sqrt(3.0)


def num(x):
    """A closed form written compactly for a message."""
    if abs(x - round(x)) < 1e-12:
        return "%d" % round(x)
    return "%.10g" % x


class T:
    """One test: a graph, the expected values by tag, and extra lines checked in the player output."""

    def __init__(self):
        self.g = G()
        self.expect = []   # (tag, "values", message)
        self.lines = []    # (fixed text, message) that must appear in the player output
        self.flagged = 0   # number of "flagged:" lines expected
        self.tag = 0

    def _next(self):
        self.tag += 1
        return self.tag

    # ---- nodes
    def vec(self, x, y, z):
        return self.g.node("Vector XYZ", float(x), float(y), float(z))

    def pt(self, x, y, z):
        return self.g.node("Construct Point", float(x), float(y), float(z))

    def mod(self, nid, port, flag):
        """Switches a parameter modifier (reverse, flatten, graft, simplify) on for an input of a node."""
        self.g.nodes[nid - 1]["inputs"][port][flag] = True

    # ---- observations
    def val(self, src, port, vals, msg):
        t = self._next()
        self.g.probe(src, port, t)
        self.expect.append((t, vals, msg))

    def present(self, src, port, msg, n="1"):
        """src.port holds exactly n value(s). A residual against a closed form of 0 also reads 0 when the value is missing (an
        empty input counts as 0), so every comparison with 0 first proves that there is a value."""
        ln = self.g.node("List Length")
        self.g.wire(src, port, ln, 0)
        self.val(ln, 0, n, "%s: a value is there" % msg)

    def exact(self, src, port, c, msg, check=True):
        if c == 0 and check:
            self.present(src, port, msg)
        t = self._next()
        g = self.g
        rm = g.node("Remap Numbers", None, float(c), float(c) + 1.0, 0.0, 1e12)
        g.wire(src, port, rm, 0)
        ro = g.node("Round To Multiple", None, 1.0)
        g.wire(rm, 0, ro, 0)
        ab = g.node("Absolute")
        g.wire(ro, 0, ab, 0)
        g.probe(ab, 0, t)
        self.expect.append((t, "0", msg))

    def comps(self, src, cs, msg, port=0):
        """X, Y, Z of a point, vector or plane origin (Deconstruct Point) against three closed forms."""
        self.present(src, port, msg)
        d = self.g.node("Deconstruct Point")
        self.g.wire(src, port, d, 0)
        for k, nm in enumerate("XYZ"):
            self.exact(d, k, cs[k], "%s: %s = %s" % (msg, nm, num(cs[k])), check=False)

    def none(self, src, port, msg):
        """The output gave nothing: its list has length 0 (what a flagged node leaves)."""
        ln = self.g.node("List Length")
        self.g.wire(src, port, ln, 0)
        self.val(ln, 0, "0", msg)

    def flag(self, nid, typ, why):
        self.lines.append(("GrasshopperPlayer: %s (#%d) flagged: %s" % (typ, nid, why), "%s #%d is flagged: %s" % (typ, nid, why)))
        self.flagged += 1

    def warns(self, nid, typ, why):
        self.lines.append(("GrasshopperPlayer: %s (#%d) warns: %s" % (typ, nid, why), "%s #%d warns: %s" % (typ, nid, why)))

    def stats(self, src, port, count, lengths, indices, msg, mods=()):
        """Tree Statistics of src.port (after the given modifiers on its input): branch count, items per branch, path indices."""
        g = self.g
        st = g.node("Tree Statistics", None)
        g.wire(src, port, st, 0)
        for m in mods:
            self.mod(st, 0, m)
        self.val(st, 2, count, "%s: %s branch(es)" % (msg, count))
        if lengths == "":
            self.none(st, 1, "%s: no lengths" % msg)
        else:
            self.val(st, 1, lengths, "%s: items per branch = '%s'" % (msg, lengths))
        if indices is not None:
            dp = g.node("Deconstruct Path")
            g.wire(st, 0, dp, 0)
            self.val(dp, 0, indices, "%s: path indices = '%s'" % (msg, indices))
        return st


def partition(g, n, size):
    """Series 0..n-1 cut into branches of `size` items: paths {0;0}, {0;1}, ..."""
    s = series(g, 0, 1, n)
    p = g.node("Partition List", None, ("i", size))
    g.wire(s, 0, p, 0)
    return p


def make_rows():
    rows = []

    def row(name, rid, title, closed, build):
        rows.append((name, rid, title, closed, build))

    # ================================================================ Vector > Vector
    def b_unit(t):
        g = t.g
        for k, (v, c, nm) in enumerate((((3, 4, 0), (0.6, 0.8, 0.0), "(3,4,0)"), ((0, 0, 5), (0.0, 0.0, 1.0), "(0,0,5)"),
                                        ((1, 1, 1), (1 / S3, 1 / S3, 1 / S3), "(1,1,1)"), ((-2, 0, 0), (-1.0, 0.0, 0.0), "(-2,0,0)"),
                                        ((1, 2, 2), (1 / 3.0, 2 / 3.0, 2 / 3.0), "(1,2,2)"), ((0.003, 0.004, 0), (0.6, 0.8, 0.0), "(0.003,0.004,0)"))):
            u = g.node("Unit Vector"); g.wire(t.vec(*v), 0, u, 0)
            t.comps(u, c, "unit vector of %s" % nm)
            ln = g.node("Vector Length"); g.wire(u, 0, ln, 0)
            t.exact(ln, 0, 1.0, "length of the unit vector of %s = 1" % nm)
        z = g.node("Unit Vector"); g.wire(t.vec(0, 0, 0), 0, z, 0)
        t.none(z, 0, "the zero vector has no direction: the node gives nothing")
        t.flag(z, "Unit Vector", "A zero-length vector cannot be unitized")
    row("ghvunit", "D8-GH-VECTOR-0003", "Unit Vector",
        "v / |v|: (3,4,0)/5 = (0.6,0.8,0); (0,0,5)/5 = (0,0,1); (1,1,1)/sqrt 3; (-2,0,0)/2 = (-1,0,0); (1,2,2)/3 = (1/3,2/3,2/3); (0.003,0.004,0)/0.005 = (0.6,0.8,0); every unit vector has length 1; the zero vector has no direction, the node is flagged and gives nothing", b_unit)

    def b_unitxyz(t):
        g = t.g
        t.comps(g.node("Unit X"), (1.0, 0.0, 0.0), "Unit X")
        t.comps(g.node("Unit Y"), (0.0, 1.0, 0.0), "Unit Y")
        t.comps(g.node("Unit Z"), (0.0, 0.0, 1.0), "Unit Z")
        t.comps(g.node("Unit X", 2.5), (2.5, 0.0, 0.0), "Unit X times 2.5")
        t.comps(g.node("Unit Y", -3.0), (0.0, -3.0, 0.0), "Unit Y times -3")
        t.comps(g.node("Unit Z", 0.0), (0.0, 0.0, 0.0), "Unit Z times 0")
        ux, uy, uz = g.node("Unit X"), g.node("Unit Y"), g.node("Unit Z")
        c1 = g.node("Cross Product"); g.wire(ux, 0, c1, 0); g.wire(uy, 0, c1, 1)
        t.comps(c1, (0.0, 0.0, 1.0), "X x Y = Z (right-handed)")
        c2 = g.node("Cross Product"); g.wire(uy, 0, c2, 0); g.wire(uz, 0, c2, 1)
        t.comps(c2, (1.0, 0.0, 0.0), "Y x Z = X")
        d = g.node("Dot Product"); g.wire(ux, 0, d, 0); g.wire(uy, 0, d, 1)
        t.exact(d, 0, 0.0, "X . Y = 0 (orthogonal)")
        d2 = g.node("Dot Product"); g.wire(uz, 0, d2, 0); g.wire(uz, 0, d2, 1)
        t.exact(d2, 0, 1.0, "Z . Z = 1 (unit length)")
    row("ghvunitxyz", "D8-GH-VECTOR-0004", "Unit X, Unit Y, Unit Z",
        "the world axes scaled by the factor (default 1): X = (1,0,0), Y = (0,1,0), Z = (0,0,1); factor 2.5 on X = (2.5,0,0); factor -3 on Y = (0,-3,0); factor 0 on Z = (0,0,0); right-handed and orthonormal: X x Y = Z, Y x Z = X, X . Y = 0, Z . Z = 1", b_unitxyz)

    def b_decv(t):
        g = t.g
        v = t.vec(3, -4, 12)
        d = g.node("Deconstruct Vector"); g.wire(v, 0, d, 0)
        t.val(d, 0, "3", "X component of (3,-4,12) is 3")
        t.val(d, 1, "-4", "Y component is -4")
        t.val(d, 2, "12", "Z component is 12")
        v2 = t.vec(1.5, -2.25, 7)
        d2 = g.node("Deconstruct Vector"); g.wire(v2, 0, d2, 0)
        t.exact(d2, 0, 1.5, "X component of (1.5,-2.25,7) is 1.5")
        t.exact(d2, 1, -2.25, "Y component is -2.25")
        t.exact(d2, 2, 7.0, "Z component is 7")
        # round trip: components back into Vector XYZ give the vector again, with its length 13 = sqrt(9 + 16 + 144)
        r = g.node("Vector XYZ"); g.wire(d, 0, r, 0); g.wire(d, 1, r, 1); g.wire(d, 2, r, 2)
        t.comps(r, (3.0, -4.0, 12.0), "round trip through Vector XYZ")
        t.val(r, 1, "13", "length of the round-tripped vector is 13")
        # a plane has no vector components of its own: the node takes its normal (Z axis); XY plane -> (0,0,1)
        p = g.node("XY Plane", None); g.wire(t.pt(5, 6, 7), 0, p, 0)
        dp = g.node("Deconstruct Vector"); g.wire(p, 0, dp, 0)
        t.exact(dp, 2, 1.0, "the XY plane's normal has Z component 1")
    row("ghvdecv", "D8-GH-VECTOR-0007", "Deconstruct Vector",
        "the components are the coordinates: (3,-4,12) -> 3, -4, 12; (1.5,-2.25,7) -> 1.5, -2.25, 7; they round-trip through Vector XYZ (length sqrt(9 + 16 + 144) = 13); a plane contributes its normal, (0,0,1) for the XY plane", b_decv)

    def b_cross(t):
        g = t.g

        def cr(a, b, unit=None):
            n = g.node("Cross Product", None, None, ("b", True)) if unit else g.node("Cross Product")
            g.wire(t.vec(*a), 0, n, 0); g.wire(t.vec(*b), 0, n, 1)
            return n
        n1 = cr((1, 0, 0), (0, 1, 0)); t.comps(n1, (0.0, 0.0, 1.0), "i x j = k")
        n2 = cr((0, 1, 0), (1, 0, 0)); t.comps(n2, (0.0, 0.0, -1.0), "j x i = -k (anti-commutative)")
        # (1,2,3) x (4,5,6) = (2*6 - 3*5, 3*4 - 1*6, 1*5 - 2*4) = (-3, 6, -3)
        a, b = (1, 2, 3), (4, 5, 6)
        n3 = cr(a, b); t.comps(n3, (-3.0, 6.0, -3.0), "(1,2,3) x (4,5,6)")
        n3r = cr(b, a); t.comps(n3r, (3.0, -6.0, 3.0), "(4,5,6) x (1,2,3) is the negative")
        # Lagrange: |a x b|^2 = |a|^2 |b|^2 - (a . b)^2 = 14 * 77 - 32^2 = 54, so the length output is sqrt 54
        t.exact(n3, 1, math.sqrt(54.0), "length output of (1,2,3) x (4,5,6) = sqrt 54 (Lagrange identity)")
        d = g.node("Dot Product"); g.wire(n3, 0, d, 0); g.wire(t.vec(*a), 0, d, 1)
        t.exact(d, 0, 0.0, "(a x b) . a = -3 + 12 - 9 = 0 (perpendicular to a)")
        d2 = g.node("Dot Product"); g.wire(n3, 0, d2, 0); g.wire(t.vec(*b), 0, d2, 1)
        t.exact(d2, 0, 0.0, "(a x b) . b = -12 + 30 - 18 = 0 (perpendicular to b)")
        n4 = cr((2, 4, 6), (1, 2, 3)); t.comps(n4, (0.0, 0.0, 0.0), "parallel vectors give the zero vector")
        t.exact(n4, 1, 0.0, "and the length output is 0")
        # unitize: (3,0,0) x (0,4,0) = (0,0,12); unitized (0,0,1) while the length output stays 12
        n5 = cr((3, 0, 0), (0, 4, 0), True); t.comps(n5, (0.0, 0.0, 1.0), "unitized (3,0,0) x (0,4,0)")
        t.exact(n5, 1, 12.0, "length output stays 12 when unitizing")
        n6 = cr((3, 0, 0), (0, 4, 0)); t.comps(n6, (0.0, 0.0, 12.0), "(3,0,0) x (0,4,0) without unitize")
        n7 = cr((2, 4, 6), (1, 2, 3), True); t.comps(n7, (0.0, 0.0, 0.0), "unitizing a zero cross product leaves the zero vector")
        t.warns(n7, "Cross Product", "Parallel vectors: the cross product is zero and cannot be unitized")
    row("ghvcross", "D8-GH-VECTOR-0008", "Cross Product",
        "determinant expansion: i x j = k; j x i = -k; (1,2,3) x (4,5,6) = (2*6-3*5, 3*4-1*6, 1*5-2*4) = (-3,6,-3), its length sqrt 54 (|a|^2|b|^2 - (a.b)^2 = 14*77 - 32^2 = 54), perpendicular to both ((a x b).a = -3+12-9 = 0, (a x b).b = -12+30-18 = 0); parallel vectors give 0; (3,0,0) x (0,4,0) = (0,0,12), unitized (0,0,1) with the length output 12", b_cross)

    def b_angle(t):
        g = t.g
        cases = [((1, 0, 0), (0, 1, 0), PI / 2, "perpendicular"), ((1, 0, 0), (1, 1, 0), PI / 4, "45 degrees"),
                 ((1, 0, 0), (-1, 0, 0), PI, "opposite"), ((1, 0, 0), (2, 0, 0), 0.0, "parallel, same direction"),
                 ((1, 0, 0), (1, 1, 1), math.atan(S2), "(1,0,0) and (1,1,1): tan = sqrt(1 - 1/3) / (1/sqrt 3) = sqrt 2"),
                 ((0, 0, 2), (0, 3, 3), PI / 4, "(0,0,2) and (0,3,3): cos = 6 / (2 * 3 sqrt 2) = 1/sqrt 2"),
                 ((1, S3, 0), (1, 0, 0), PI / 3, "(1,sqrt 3,0) and (1,0,0): cos = 1/2")]
        for a, b, c, nm in cases:
            n = g.node("Angle"); g.wire(t.vec(*a), 0, n, 0); g.wire(t.vec(*b), 0, n, 1)
            t.exact(n, 0, c, "angle, %s" % nm)
            t.exact(n, 1, 2 * PI - c, "reflex angle, %s = 2 pi - angle" % nm)
        sw = g.node("Angle"); g.wire(t.vec(0, 1, 0), 0, sw, 0); g.wire(t.vec(1, 0, 0), 0, sw, 1)
        t.exact(sw, 0, PI / 2, "swapping the vectors keeps the angle (unsigned)")
        z = g.node("Angle"); g.wire(t.vec(0, 0, 0), 0, z, 0); g.wire(t.vec(1, 0, 0), 0, z, 1)
        t.none(z, 0, "a zero vector has no direction: no angle")
        t.none(z, 1, "and no reflex angle")
        t.flag(z, "Angle", "A zero-length vector has no direction")
    row("ghvangle", "D8-GH-VECTOR-0010", "Angle",
        "angle = arccos(a.b / |a||b|) in [0, pi] radians, reflex = 2 pi - angle: perpendicular pi/2 (reflex 3 pi/2); (1,0,0),(1,1,0) = pi/4; opposite = pi (reflex pi); same direction = 0 (reflex 2 pi); (1,0,0),(1,1,1): cos = 1/sqrt 3, tan = sqrt 2, angle = atan(sqrt 2); (0,0,2),(0,3,3) = pi/4; (1,sqrt 3,0),(1,0,0) = pi/3; swapping keeps the angle; a zero vector is flagged", b_angle)

    def b_rev(t):
        g = t.g
        rv = g.node("Vector Reverse"); g.wire(t.vec(3, -4, 12), 0, rv, 0)
        t.comps(rv, (-3.0, 4.0, -12.0), "reverse of (3,-4,12)")
        ln = g.node("Vector Length"); g.wire(rv, 0, ln, 0)
        t.val(ln, 0, "13", "the reversed vector keeps its length 13")
        rr = g.node("Vector Reverse"); g.wire(rv, 0, rr, 0)
        t.comps(rr, (3.0, -4.0, 12.0), "reversing twice gives the vector back")
        ad = g.node("Vector Addition"); g.wire(t.vec(3, -4, 12), 0, ad, 0); g.wire(rv, 0, ad, 1)
        t.comps(ad, (0.0, 0.0, 0.0), "v + reverse(v) = 0")
        z = g.node("Vector Reverse"); g.wire(t.vec(0, 0, 0), 0, z, 0)
        t.comps(z, (0.0, 0.0, 0.0), "reverse of the zero vector is the zero vector")
        h = g.node("Vector Reverse"); g.wire(t.vec(0.5, -0.25, 8), 0, h, 0)
        t.comps(h, (-0.5, 0.25, -8.0), "reverse of (0.5,-0.25,8)")
    row("ghvrev", "D8-GH-VECTOR-0013", "Vector Reverse",
        "every component changes sign: (3,-4,12) -> (-3,4,-12) with the same length 13; reversing twice returns the vector; v + reverse(v) = 0; the zero vector stays zero; (0.5,-0.25,8) -> (-0.5,0.25,-8)", b_rev)

    def b_rot(t):
        g = t.g

        def rot(v, ax, ang):
            n = g.node("Vector Rotate", None, None, float(ang)); g.wire(t.vec(*v), 0, n, 0); g.wire(t.vec(*ax), 0, n, 1)
            return n
        t.comps(rot((1, 0, 0), (0, 0, 1), PI / 2), (0.0, 1.0, 0.0), "X about Z by 90 degrees gives Y")
        t.comps(rot((0, 1, 0), (1, 0, 0), PI / 2), (0.0, 0.0, 1.0), "Y about X by 90 degrees gives Z")
        t.comps(rot((0, 0, 1), (0, 1, 0), PI / 2), (1.0, 0.0, 0.0), "Z about Y by 90 degrees gives X")
        t.comps(rot((1, 0, 0), (0, 0, 1), PI), (-1.0, 0.0, 0.0), "X about Z by 180 degrees gives -X")
        t.comps(rot((1, 0, 0), (0, 0, 1), -PI / 2), (0.0, -1.0, 0.0), "X about Z by -90 degrees gives -Y (clockwise)")
        t.comps(rot((1, 0, 0), (1, 0, 0), 1.234), (1.0, 0.0, 0.0), "a vector along the axis does not move")
        t.comps(rot((1, 0, 0), (1, 1, 1), 2 * PI / 3), (0.0, 1.0, 0.0), "120 degrees about (1,1,1) permutes the axes: X -> Y")
        t.comps(rot((0, 1, 0), (1, 1, 1), 2 * PI / 3), (0.0, 0.0, 1.0), "120 degrees about (1,1,1): Y -> Z")
        t.comps(rot((1, 0, 0), (0, 0, 5), PI / 2), (0.0, 1.0, 0.0), "the axis length does not matter: (0,0,5) acts as Z")
        t.comps(rot((1, 2, 3), (1, -1, 2), 2 * PI), (1.0, 2.0, 3.0), "a full turn gives the vector back")
        r = rot((1, 2, 3), (1, -1, 2), 0.7)
        ln = g.node("Vector Length"); g.wire(r, 0, ln, 0)
        t.exact(ln, 0, math.sqrt(14.0), "rotation keeps the length sqrt(1 + 4 + 9)")
        d = g.node("Dot Product"); g.wire(r, 0, d, 0); g.wire(t.vec(1, -1, 2), 0, d, 1)
        t.exact(d, 0, 1 * 1 + 2 * -1 + 3 * 2, "and the component along the axis: (1,2,3) . (1,-1,2) = 5")
        z = g.node("Vector Rotate", None, None, 1.0); g.wire(t.vec(1, 0, 0), 0, z, 0); g.wire(t.vec(0, 0, 0), 0, z, 1)
        t.none(z, 0, "a zero-length axis has no direction: nothing comes out")
        t.flag(z, "Vector Rotate", "A zero-length axis has no direction")
    row("ghvrot", "D8-GH-VECTOR-0014", "Vector Rotate",
        "right-hand rule, angles in radians: X about Z by pi/2 = Y; Y about X by pi/2 = Z; Z about Y by pi/2 = X; X about Z by pi = -X; by -pi/2 = -Y; a vector along the axis stays; 2 pi/3 about (1,1,1) permutes the axes (X -> Y, Y -> Z); the axis length is irrelevant; a full turn is the identity; the length and the component along the axis are kept (|(1,2,3)| = sqrt 14, (1,2,3).(1,-1,2) = 5); a zero axis is flagged", b_rot)

    # ================================================================ Vector > Plane
    def b_ploff(t):
        g = t.g
        xy = g.node("XY Plane", None); g.wire(t.pt(0, 0, 0), 0, xy, 0)

        def off(pl, dist):
            n = g.node("Plane Offset", None, float(dist)); g.wire(pl, 0, n, 0)
            return n
        n1 = off(xy, 2.5)
        t.comps(n1, (0.0, 0.0, 2.5), "XY plane offset by 2.5: origin")
        dv = g.node("Deconstruct Vector"); g.wire(n1, 0, dv, 0)
        t.exact(dv, 2, 1.0, "its normal is still Z")
        t.comps(off(xy, -4), (0.0, 0.0, -4.0), "offset by -4 goes against the normal")
        t.comps(off(xy, 0), (0.0, 0.0, 0.0), "offset 0 leaves the plane")
        t.comps(off(n1, 2), (0.0, 0.0, 4.5), "offsets add: 2.5 then 2 = 4.5")
        tl = g.node("Plane Normal"); g.wire(t.pt(1, 0, 0), 0, tl, 0); g.wire(t.vec(1, 1, 1), 0, tl, 1)
        n3 = off(tl, S3)
        t.comps(n3, (2.0, 1.0, 1.0), "plane through (1,0,0) with normal (1,1,1)/sqrt 3 offset by sqrt 3: (1,0,0) + (1,1,1)")
        dv3 = g.node("Deconstruct Vector"); g.wire(n3, 0, dv3, 0)
        t.exact(dv3, 0, 1 / S3, "the tilted plane keeps its normal (1,1,1)/sqrt 3")
        # axes of any length: the distance is measured along the UNIT normal. X axis (2,0,0), Y axis (0,3,0): normal (0,0,6), unit (0,0,1)
        pl = g.node("Plane Origin"); g.wire(t.pt(0, 0, 0), 0, pl, 0); g.wire(t.vec(2, 0, 0), 0, pl, 1); g.wire(t.vec(0, 3, 0), 0, pl, 2)
        t.comps(off(pl, 1), (0.0, 0.0, 1.0), "axes of length 2 and 3 do not scale the offset: origin (0,0,1)")
    row("ghvploff", "D8-GH-VECTOR-0054", "Plane Offset",
        "the origin moves along the unit normal: XY plane by 2.5 -> (0,0,2.5) with the normal still Z; by -4 -> (0,0,-4); by 0 unchanged; 2.5 then 2 -> z = 4.5; the plane through (1,0,0) with normal (1,1,1)/sqrt 3 offset by sqrt 3 -> (1,0,0) + (1,1,1) = (2,1,1), normal unchanged; axes (2,0,0),(0,3,0) have normal (0,0,6), unit (0,0,1), so offset 1 -> (0,0,1)", b_ploff)

    def b_plcp(t):
        g = t.g

        def cp(pl, p):
            n = g.node("Plane Closest Point"); g.wire(pl, 0, n, 0); g.wire(t.pt(*p), 0, n, 1)
            return n
        z2 = g.node("XY Plane", None); g.wire(t.pt(0, 0, 2), 0, z2, 0)
        a = cp(z2, (3, 4, 7)); t.comps(a, (3.0, 4.0, 2.0), "(3,4,7) onto the plane z = 2"); t.exact(a, 1, 5.0, "signed distance +5 above")
        b = cp(z2, (3, 4, -1)); t.comps(b, (3.0, 4.0, 2.0), "(3,4,-1) onto the plane z = 2"); t.exact(b, 1, -3.0, "signed distance -3 below")
        tl = g.node("Plane Normal"); g.wire(t.pt(0, 0, 0), 0, tl, 0); g.wire(t.vec(1, 1, 1), 0, tl, 1)
        c = cp(tl, (1, 2, 3))
        t.comps(c, (-1.0, 0.0, 1.0), "(1,2,3) onto x + y + z = 0: (1,2,3) - 2 (1,1,1)")
        t.exact(c, 1, 6 / S3, "signed distance (1 + 2 + 3) / sqrt 3 = 2 sqrt 3")
        d = g.node("Dot Product"); g.wire(c, 0, d, 0); g.wire(t.vec(1, 1, 1), 0, d, 1)
        t.exact(d, 0, 0.0, "the closest point lies on the plane: (-1,0,1) . (1,1,1) = 0")
        e = cp(tl, (1, -1, 0)); t.comps(e, (1.0, -1.0, 0.0), "a point on the plane stays where it is"); t.exact(e, 1, 0.0, "distance 0")
        f = g.node("Plane Normal"); g.wire(t.pt(1, 1, 1), 0, f, 0); g.wire(t.vec(0, 0, 2), 0, f, 1)
        h = cp(f, (5, -2, 4)); t.comps(h, (5.0, -2.0, 1.0), "(5,-2,4) onto the plane z = 1 (normal (0,0,2) acts as Z)"); t.exact(h, 1, 3.0, "signed distance +3")
        # the foot of the perpendicular is nearer than any other plane point: |p - foot| equals the distance
        dd = g.node("Distance"); g.wire(t.pt(1, 2, 3), 0, dd, 0); g.wire(c, 0, dd, 1)
        t.exact(dd, 0, 6 / S3, "|p - foot| = 2 sqrt 3 = the signed distance in size")
    row("ghvplcp", "D8-GH-VECTOR-0055", "Plane Closest Point",
        "foot of the perpendicular and signed distance along the unit normal: (3,4,7) onto z = 2 -> (3,4,2), +5; (3,4,-1) -> (3,4,2), -3; (1,2,3) onto x + y + z = 0 -> (1,2,3) - ((1+2+3)/sqrt 3) (1,1,1)/sqrt 3 = (-1,0,1), distance 6/sqrt 3 = 2 sqrt 3, (-1,0,1).(1,1,1) = 0; a point on the plane stays (distance 0); (5,-2,4) onto z = 1 -> (5,-2,1), +3", b_plcp)

    # ================================================================ Transform
    def b_move(t):
        g = t.g

        def mv(p, v):
            n = g.node("Move"); g.wire(t.pt(*p), 0, n, 0); g.wire(t.vec(*v), 0, n, 1)
            return n
        t.comps(mv((1, 2, 3), (4, 5, 6)), (5.0, 7.0, 9.0), "(1,2,3) moved by (4,5,6)")
        t.comps(mv((1, 2, 3), (-1, -2, -3)), (0.0, 0.0, 0.0), "moved by its own negative: the origin")
        t.comps(mv((1, 2, 3), (0, 0, 0)), (1.0, 2.0, 3.0), "a zero motion leaves the point")
        t.comps(mv((0.5, -0.25, 8), (0.25, 0.75, -8)), (0.75, 0.5, 0.0), "(0.5,-0.25,8) moved by (0.25,0.75,-8)")
        m1 = mv((1, 2, 3), (4, 5, 6))
        m2 = g.node("Move"); g.wire(m1, 0, m2, 0); g.wire(t.vec(-4, -5, -6), 0, m2, 1)
        t.comps(m2, (1.0, 2.0, 3.0), "moving there and back is the identity")
        m3 = g.node("Move"); g.wire(m1, 0, m3, 0); g.wire(t.vec(10, 20, 30), 0, m3, 1)
        t.comps(m3, (15.0, 27.0, 39.0), "two moves add: (1,2,3) + (4,5,6) + (10,20,30)")
        # distance between two points moved by the same vector is unchanged (3-4-5)
        a, b = mv((0, 0, 0), (7, 8, 9)), mv((3, 4, 0), (7, 8, 9))
        dd = g.node("Distance"); g.wire(a, 0, dd, 0); g.wire(b, 0, dd, 1)
        t.exact(dd, 0, 5.0, "a rigid move keeps the distance 5 between (0,0,0) and (3,4,0)")
    row("ghxmove", "D8-GH-XFORM-0010", "Move",
        "translation adds the vector: (1,2,3) + (4,5,6) = (5,7,9); + (-1,-2,-3) = (0,0,0); + 0 unchanged; (0.5,-0.25,8) + (0.25,0.75,-8) = (0.75,0.5,0); there and back is the identity; two moves add ((1,2,3) + (4,5,6) + (10,20,30) = (15,27,39)); the distance of two points moved alike stays 5 (3-4-5)", b_move)

    def b_xrot(t):
        g = t.g

        def rt(p, deg, plane=None):
            n = g.node("Rotate", None, float(deg)); g.wire(t.pt(*p), 0, n, 0)
            if plane is not None:
                g.wire(plane, 0, n, 2)
            return n
        t.comps(rt((1, 0, 0), 90), (0.0, 1.0, 0.0), "(1,0,0) by 90 degrees about Z")
        t.comps(rt((1, 0, 0), 180), (-1.0, 0.0, 0.0), "(1,0,0) by 180 degrees")
        t.comps(rt((2, 0, 0), 45), (S2, S2, 0.0), "(2,0,0) by 45 degrees: 2 (cos 45, sin 45) = (sqrt 2, sqrt 2)")
        t.comps(rt((1, 0, 0), -90), (0.0, -1.0, 0.0), "(1,0,0) by -90 degrees (clockwise)")
        t.comps(rt((1, 2, 3), 360), (1.0, 2.0, 3.0), "a full turn is the identity")
        t.comps(rt((1, 2, 3), 0), (1.0, 2.0, 3.0), "no turn")
        c = g.node("XY Plane", None); g.wire(t.pt(1, 1, 0), 0, c, 0)
        t.comps(rt((1, 0, 0), 90, c), (2.0, 1.0, 0.0), "about the plane origin (1,1,0): (1,0,0) is (0,-1) from it, turned 90 degrees ccw (x,y) -> (-y,x) = (1,0), so (2,1,0)")
        nx = g.node("Plane Normal"); g.wire(t.pt(0, 0, 0), 0, nx, 0); g.wire(t.vec(1, 0, 0), 0, nx, 1)
        t.comps(rt((0, 1, 0), 90, nx), (0.0, 0.0, 1.0), "about the axis X (plane normal (1,0,0)): Y -> Z by the right-hand rule")
        nz = g.node("XY Plane", None); g.wire(t.pt(0, 0, 0), 0, nz, 0)
        r = rt((3, 4, 5), 37.0, nz)
        dd = g.node("Distance"); g.wire(r, 0, dd, 0); g.wire(t.pt(0, 0, 5), 0, dd, 1)
        t.exact(dd, 0, 5.0, "any angle keeps the distance 5 from the axis (3,4 -> 5) and Z = 5")
    row("ghxrot", "D8-GH-XFORM-0008", "Rotate",
        "angle in degrees, counter-clockwise about the plane normal through the plane origin: (1,0,0) by 90 = (0,1,0); by 180 = (-1,0,0); (2,0,0) by 45 = (sqrt 2, sqrt 2, 0); by -90 = (0,-1,0); 360 and 0 are the identity; about origin (1,1,0): (1,0,0) -> (2,1,0); about the X axis (0,1,0) -> (0,0,1); (3,4,5) by 37 degrees stays 5 from the axis", b_xrot)

    def b_mirror(t):
        g = t.g

        def mr(p, plane):
            n = g.node("Mirror"); g.wire(t.pt(*p), 0, n, 0); g.wire(plane, 0, n, 1)
            return n
        xy = g.node("XY Plane", None); g.wire(t.pt(0, 0, 0), 0, xy, 0)
        t.comps(mr((1, 2, 3), xy), (1.0, 2.0, -3.0), "(1,2,3) across the XY plane: Z changes sign")
        z5 = g.node("XY Plane", None); g.wire(t.pt(0, 0, 5), 0, z5, 0)
        t.comps(mr((1, 2, 3), z5), (1.0, 2.0, 7.0), "across the plane z = 5: 3 -> 2 * 5 - 3 = 7")
        nx = g.node("Plane Normal"); g.wire(t.pt(0, 0, 0), 0, nx, 0); g.wire(t.vec(1, 0, 0), 0, nx, 1)
        t.comps(mr((1, 2, 3), nx), (-1.0, 2.0, 3.0), "across the plane with normal X: x changes sign")
        n11 = g.node("Plane Normal"); g.wire(t.pt(0, 0, 0), 0, n11, 0); g.wire(t.vec(1, 1, 0), 0, n11, 1)
        t.comps(mr((1, 0, 0), n11), (0.0, -1.0, 0.0), "across x + y = 0: p - 2 (p . n) n = (1,0,0) - (1,1,0) = (0,-1,0)")
        t.comps(mr((1, -1, 5), n11), (1.0, -1.0, 5.0), "a point on the mirror plane stays")
        m1 = mr((1, 2, 3), xy)
        m2 = g.node("Mirror"); g.wire(m1, 0, m2, 0); g.wire(xy, 0, m2, 1)
        t.comps(m2, (1.0, 2.0, 3.0), "mirroring twice is the identity")
        # the distance to the plane is kept: the point and its image are 2 * 3 = 6 apart across the XY plane
        dd = g.node("Distance"); g.wire(t.pt(1, 2, 3), 0, dd, 0); g.wire(m1, 0, dd, 1)
        t.exact(dd, 0, 6.0, "the point and its image are twice the distance to the plane apart: 2 * 3 = 6")
    row("ghxmirror", "D8-GH-XFORM-0005", "Mirror",
        "reflection p - 2 ((p - o) . n) n: across XY (1,2,3) -> (1,2,-3); across z = 5: z -> 2*5 - 3 = 7; across the plane with normal X: (-1,2,3); across x + y = 0: (1,0,0) -> (0,-1,0) and (1,-1,5) (on the plane) stays; twice is the identity; point and image are 2 * 3 = 6 apart", b_mirror)

    def b_linarr(t):
        g = t.g
        la = g.node("Linear Array", None, None, 3.0, ("i", 4)); g.wire(t.pt(0, 0, 0), 0, la, 0); g.wire(t.vec(0, 0, 2), 0, la, 1)
        ln = g.node("List Length"); g.wire(la, 0, ln, 0); t.val(ln, 0, "4", "count 4 gives 4 copies")
        d = g.node("Deconstruct Point"); g.wire(la, 0, d, 0)
        t.val(d, 2, "0 3 6 9", "direction (0,0,2) is unitized: copies at Z = 0, 3, 6, 9 (distance 3 apart)")
        t.val(d, 0, "0 0 0 0", "X stays 0")
        t.val(d, 1, "0 0 0 0", "Y stays 0")
        # direction (1,1,0), distance sqrt 2: step (1,1,0); start (1,-2,5): (1,-2,5), (2,-1,5), (3,0,5)
        lb = g.node("Linear Array", None, None, S2, ("i", 3)); g.wire(t.pt(1, -2, 5), 0, lb, 0); g.wire(t.vec(1, 1, 0), 0, lb, 1)
        for i, p in enumerate(((1.0, -2.0, 5.0), (2.0, -1.0, 5.0), (3.0, 0.0, 5.0))):
            li = g.node("List Item", None, ("i", i), ("b", False)); g.wire(lb, 0, li, 0)
            t.comps(li, p, "copy %d along (1,1,0) with step length sqrt 2" % i)
        lc = g.node("Linear Array", None, None, 7.0, ("i", 1)); g.wire(t.pt(4, 5, 6), 0, lc, 0); g.wire(t.vec(1, 0, 0), 0, lc, 1)
        lcl = g.node("List Length"); g.wire(lc, 0, lcl, 0); t.val(lcl, 0, "1", "count 1 gives just the original")
        dc = g.node("Deconstruct Point"); g.wire(lc, 0, dc, 0); t.val(dc, 0, "4", "the single copy is the original: X = 4")
        t.val(dc, 2, "6", "and Z = 6")
        # negative distance runs against the direction
        ld = g.node("Linear Array", None, None, -2.0, ("i", 3)); g.wire(t.pt(0, 0, 0), 0, ld, 0); g.wire(t.vec(1, 0, 0), 0, ld, 1)
        dd = g.node("Deconstruct Point"); g.wire(ld, 0, dd, 0); t.val(dd, 0, "0 -2 -4", "distance -2 along X: 0, -2, -4")
    row("ghxlinarr", "D8-GH-XFORM-0015", "Linear Array",
        "copy i sits at p + i * distance * unit(direction): (0,0,0) with direction (0,0,2), distance 3, count 4 -> Z = 0 3 6 9; (1,-2,5) with direction (1,1,0), distance sqrt 2, count 3 -> (1,-2,5), (2,-1,5), (3,0,5); count 1 is the original; distance -2 along X -> 0 -2 -4", b_linarr)

    def b_polarr(t):
        g = t.g

        def pa(p, plane, count, ang=360.0):
            n = g.node("Polar Array", None, None, ("i", count), float(ang)); g.wire(t.pt(*p), 0, n, 0); g.wire(plane, 0, n, 1)
            return n
        xy = g.node("XY Plane", None); g.wire(t.pt(0, 0, 0), 0, xy, 0)
        a = pa((2, 0, 0), xy, 4)
        ln = g.node("List Length"); g.wire(a, 0, ln, 0); t.val(ln, 0, "4", "count 4 gives 4 copies")
        for i, p in enumerate(((2.0, 0.0, 0.0), (0.0, 2.0, 0.0), (-2.0, 0.0, 0.0), (0.0, -2.0, 0.0))):
            li = g.node("List Item", None, ("i", i), ("b", False)); g.wire(a, 0, li, 0)
            t.comps(li, p, "copy %d of (2,0,0) in 4 steps of 90 degrees" % i)
        b = pa((1, 0, 0), xy, 3)
        for i, p in enumerate(((1.0, 0.0, 0.0), (-0.5, S3 / 2), (-0.5, -S3 / 2))):
            li = g.node("List Item", None, ("i", i), ("b", False)); g.wire(b, 0, li, 0)
            t.comps(li, (p[0], p[1], 0.0), "copy %d of (1,0,0) in 3 steps of 120 degrees: (cos, sin)" % i)
        cpl = g.node("XY Plane", None); g.wire(t.pt(1, 1, 0), 0, cpl, 0)
        c = pa((2, 1, 0), cpl, 2)
        li = g.node("List Item", None, ("i", 1), ("b", False)); g.wire(c, 0, li, 0)
        t.comps(li, (0.0, 1.0, 0.0), "about (1,1,0) with 2 copies the second is the point turned 180 degrees: (0,1,0)")
        # about the X axis (plane normal (1,0,0)): (0,1,0) -> (0,0,1) -> (0,-1,0) -> (0,0,-1)
        nx = g.node("Plane Normal"); g.wire(t.pt(0, 0, 0), 0, nx, 0); g.wire(t.vec(1, 0, 0), 0, nx, 1)
        e = pa((0, 1, 0), nx, 4)
        for i, p in enumerate(((0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0), (0.0, 0.0, -1.0))):
            li = g.node("List Item", None, ("i", i), ("b", False)); g.wire(e, 0, li, 0)
            t.comps(li, p, "copy %d about the X axis" % i)
        # equal spacing: all 6 copies are 1 from the centre, neighbours are 1 apart (a regular hexagon)
        h = pa((1, 0, 0), xy, 6)
        for i in range(6):
            l0 = g.node("List Item", None, ("i", i), ("b", False)); g.wire(h, 0, l0, 0)
            l1 = g.node("List Item", None, ("i", (i + 1) % 6), ("b", False)); g.wire(h, 0, l1, 0)
            dd = g.node("Distance"); g.wire(l0, 0, dd, 0); g.wire(l1, 0, dd, 1)
            t.exact(dd, 0, 1.0, "hexagon: copies %d and %d are 1 apart (side of the regular hexagon of circumradius 1)" % (i, (i + 1) % 6))
    row("ghxpolarr", "D8-GH-XFORM-0017", "Polar Array",
        "a full circle in N equal steps of 360/N degrees, copy i at angle 360 i / N (counter-clockwise about the plane normal): (2,0,0) in 4 steps -> (2,0,0), (0,2,0), (-2,0,0), (0,-2,0); (1,0,0) in 3 steps -> (cos 120, sin 120) = (-1/2, sqrt 3 / 2) and (-1/2, -sqrt 3 / 2); about (1,1,0) with 2 copies (2,1,0) -> (0,1,0); about the X axis (0,1,0) -> (0,0,1) -> (0,-1,0) -> (0,0,-1); 6 steps of (1,0,0): neighbours are 1 apart (regular hexagon, side = circumradius)", b_polarr)

    def b_orient(t):
        g = t.g

        def orient(p, src, dst):
            n = g.node("Orient"); g.wire(t.pt(*p), 0, n, 0); g.wire(src, 0, n, 1); g.wire(dst, 0, n, 2)
            return n
        xy = g.node("XY Plane", None); g.wire(t.pt(0, 0, 0), 0, xy, 0)
        # target frame: origin (5,6,7), X axis (0,1,0), Y axis (-1,0,0), so Z = X x Y = (0,0,1).  The point has frame coordinates (1,2,3):
        # origin + 1 X + 2 Y + 3 Z = (5,6,7) + (0,1,0) + (-2,0,0) + (0,0,3) = (3,7,10)
        tgt = g.node("Plane Origin"); g.wire(t.pt(5, 6, 7), 0, tgt, 0); g.wire(t.vec(0, 1, 0), 0, tgt, 1); g.wire(t.vec(-1, 0, 0), 0, tgt, 2)
        t.comps(orient((1, 2, 3), xy, tgt), (3.0, 7.0, 10.0), "frame coordinates (1,2,3) carried to the target frame")
        # translation only: source XY at (1,1,0) to target XY at (10,0,0): (2,3,0) has coordinates (1,2,0) -> (11,2,0)
        s2 = g.node("XY Plane", None); g.wire(t.pt(1, 1, 0), 0, s2, 0)
        t2 = g.node("XY Plane", None); g.wire(t.pt(10, 0, 0), 0, t2, 0)
        t.comps(orient((2, 3, 0), s2, t2), (11.0, 2.0, 0.0), "parallel frames: a pure translation")
        # flipped target: origin (0,0,0), X (1,0,0), Y (0,-1,0), Z = (0,0,-1): (1,2,3) -> (1,-2,-3), a half turn about X
        fl = g.node("Plane Origin"); g.wire(t.pt(0, 0, 0), 0, fl, 0); g.wire(t.vec(1, 0, 0), 0, fl, 1); g.wire(t.vec(0, -1, 0), 0, fl, 2)
        t.comps(orient((1, 2, 3), xy, fl), (1.0, -2.0, -3.0), "target turned half way about X: (1,-2,-3)")
        # the source frame itself lands on the target frame: its origin goes to the target origin
        t.comps(orient((0, 0, 0), xy, tgt), (5.0, 6.0, 7.0), "the source origin goes to the target origin")
        # rigid: the distance between two oriented points is kept (3-4-5)
        a, b = orient((0, 0, 0), xy, tgt), orient((3, 4, 0), xy, tgt)
        dd = g.node("Distance"); g.wire(a, 0, dd, 0); g.wire(b, 0, dd, 1)
        t.exact(dd, 0, 5.0, "orienting is rigid: the distance 5 is kept")
        # same plane: identity
        t.comps(orient((1, 2, 3), s2, s2), (1.0, 2.0, 3.0), "source = target is the identity")
    row("ghxorient", "D8-GH-XFORM-0012", "Orient",
        "a point keeps its coordinates in the frame: source XY at the origin, target origin (5,6,7), X (0,1,0), Y (-1,0,0), Z = X x Y = (0,0,1): (1,2,3) -> (5,6,7) + 1 (0,1,0) + 2 (-1,0,0) + 3 (0,0,1) = (3,7,10); the source origin goes to (5,6,7); parallel frames (1,1,0) -> (10,0,0) translate (2,3,0) to (11,2,0); target X (1,0,0), Y (0,-1,0), Z (0,0,-1): (1,2,3) -> (1,-2,-3); distances are kept; source = target is the identity", b_orient)

    # ================================================================ Data trees and data matching
    def b_longest(t):
        g = t.g
        a = series(g, 1, 1, 3)      # 1 2 3
        b = series(g, 10, 10, 5)    # 10 20 30 40 50
        n = g.node("Addition"); g.wire(a, 0, n, 0); g.wire(b, 0, n, 1)
        t.val(n, 0, "11 22 33 43 53", "3 + 5 items: five sums, the last item of the short list (3) repeated: 3 + 40, 3 + 50")
        ln = g.node("List Length"); g.wire(n, 0, ln, 0); t.val(ln, 0, "5", "the result has the length of the longest list")
        n2 = g.node("Addition"); g.wire(b, 0, n2, 0); g.wire(a, 0, n2, 1)
        t.val(n2, 0, "11 22 33 43 53", "the order of the inputs does not matter")
        one = series(g, 5, 0, 1)    # 5
        n3 = g.node("Addition"); g.wire(one, 0, n3, 0); g.wire(b, 0, n3, 1)
        t.val(n3, 0, "15 25 35 45 55", "a single item is repeated against every item of the long list")
        a2 = series(g, 1, 1, 3)
        n4 = g.node("Addition"); g.wire(a, 0, n4, 0); g.wire(a2, 0, n4, 1)
        t.val(n4, 0, "2 4 6", "equal lengths pair item by item")
        c = series(g, 100, 100, 2)  # 100 200 (shorter than 4): last repeated, not wrapped
        d = series(g, 1, 1, 4)      # 1 2 3 4
        n5 = g.node("Addition"); g.wire(c, 0, n5, 0); g.wire(d, 0, n5, 1)
        t.val(n5, 0, "101 202 203 204", "100 200 against 1 2 3 4: 200 is repeated (not 100 again): 101 202 203 204")
    row("ghtlongest", "D8-GH-TREES-0002", "Longest-list matching",
        "the shorter list repeats its LAST item: 1 2 3 + 10 20 30 40 50 = 11 22 33 (3+40) (3+50) = 11 22 33 43 53, five results; either order; 5 + (10 .. 50) = 15 25 35 45 55; equal lengths pair item by item (2 4 6); 100 200 + 1 2 3 4 = 101 202 203 204", b_longest)

    def b_tflat(t):
        g = t.g
        x = partition(g, 6, 2)   # {0;0} 0 1 | {0;1} 2 3 | {0;2} 4 5
        t.stats(x, 0, "3", "2 2 2", "0 0 0 1 0 2", "no modifier on 0..5 cut in pairs")
        t.stats(x, 0, "1", "6", "0", "flatten: one branch {0} with all six items", mods=("flatten",))
        gr = g.node("Graft Tree"); g.wire(x, 0, gr, 0)
        t.stats(gr, 0, "1", "6", "0", "flatten of a depth 3 tree is the same single branch {0}", mods=("flatten",))
        s = series(g, 0, 1, 4)
        t.stats(s, 0, "1", "4", "0", "flatten of a tree that is one branch {0} changes nothing", mods=("flatten",))
        # the same graph with the modifier on a DIFFERENT input does not flatten this one: Tree Statistics has one input, so the decoy
        # is the unmodified statistic above (3 branches), not 1.
    row("ghtflat", "D8-GH-TREES-0009", "Flatten parameter modifier",
        "flatten puts every item in one branch {0}: 0..5 cut in pairs is 3 branches {0;0} {0;1} {0;2} of 2 items (paths 0 0, 0 1, 0 2); flattened it is 1 branch of 6 with path {0}; a grafted (depth 3) copy flattens to the same; a tree that already is the single branch {0} stays", b_tflat)

    def b_tgraft(t):
        g = t.g
        s = series(g, 0, 1, 4)    # one branch {0} of 4 items
        t.stats(s, 0, "1", "4", "0", "no modifier on one list of 4")
        t.stats(s, 0, "4", "1 1 1 1", "0 0 0 1 0 2 0 3", "graft: every item its own branch, the item index appended: {0;0} {0;1} {0;2} {0;3}", mods=("graft",))
        x = partition(g, 6, 2)    # {0;0} {0;1} {0;2}, 2 items each
        t.stats(x, 0, "6", "1 1 1 1 1 1", "0 0 0 0 0 1 0 1 0 0 1 1 0 2 0 0 2 1", "graft of a 3 branch tree: path {a;b} and item i give {a;b;i}", mods=("graft",))
        e = series(g, 0, 1, 0)
        t.stats(e, 0, "0", "", None, "graft of an empty list: no branches", mods=("graft",))
    row("ghtgraft", "D8-GH-TREES-0010", "Graft parameter modifier",
        "graft appends the item's index to its branch path: one list of 4 at {0} -> {0;0} {0;1} {0;2} {0;3} (4 branches of 1 item); the tree {0;0} {0;1} {0;2} of 2 items each -> {0;0;0} {0;0;1} {0;1;0} {0;1;1} {0;2;0} {0;2;1} (6 branches of 1); an empty list has no branches; without the modifier the list stays one branch of 4", b_tgraft)

    def b_tsimp(t):
        g = t.g
        x = partition(g, 6, 2)                       # {0;0} {0;1} {0;2}
        gx = g.node("Graft Tree"); g.wire(x, 0, gx, 0)   # {0;k;i}: 0 0 0 | 0 0 1 | 0 1 0 | 0 1 1 | 0 2 0 | 0 2 1
        t.stats(gx, 0, "6", "1 1 1 1 1 1", "0 0 0 0 0 1 0 1 0 0 1 1 0 2 0 0 2 1", "no modifier on the grafted tree")
        t.stats(gx, 0, "6", "1 1 1 1 1 1", "0 0 0 1 1 0 1 1 2 0 2 1", "simplify: the first index 0 is shared by every branch and goes: {0;k;i} -> {k;i}", mods=("simplify",))
        y = partition(g, 3, 3)                       # one branch {0;0} of 3 items
        gy = g.node("Graft Tree"); g.wire(y, 0, gy, 0)   # {0;0;0} {0;0;1} {0;0;2}
        t.stats(gy, 0, "3", "1 1 1", "0 0 0 0 0 1 0 0 2", "no modifier on three branches sharing {0;0}")
        t.stats(gy, 0, "3", "1 1 1", "0 1 2", "simplify: the shared prefix {0;0} goes: {0;0;i} -> {i}", mods=("simplify",))
        s = series(g, 0, 1, 3)
        gs = g.node("Graft Tree"); g.wire(s, 0, gs, 0)
        ggs = g.node("Graft Tree"); g.wire(gs, 0, ggs, 0)    # {0;i;0}
        t.stats(ggs, 0, "3", "1 1 1", "0 0 0 0 1 0 0 2 0", "grafted twice: {0;0;0} {0;1;0} {0;2;0}")
        t.stats(ggs, 0, "3", "1 1 1", "0 0 1 0 2 0", "simplify drops only the shared first index: {i;0}", mods=("simplify",))
    row("ghtsimp", "D8-GH-TREES-0011", "Simplify parameter modifier",
        "simplify removes the leading path indices every branch shares and nothing else: {0;k;i} (k = 0..2, i = 0..1) -> {k;i} (only the first index 0 is shared); {0;0;i} for i = 0..2 -> {i} (the shared prefix {0;0} goes); {0;i;0} -> {i;0}; without the modifier the paths stay", b_tsimp)

    def b_trev(t):
        g = t.g
        s = series(g, 1, 1, 5)
        m0 = g.node("Merge", None, None); g.wire(s, 0, m0, 0)
        t.val(m0, 0, "1 2 3 4 5", "no modifier: the list as it is")
        m1 = g.node("Merge", None, None); g.wire(s, 0, m1, 0); t.mod(m1, 0, "reverse")
        t.val(m1, 0, "5 4 3 2 1", "reverse: 1..5 back to front")
        x = partition(g, 6, 3)    # {0;0} 0 1 2 | {0;1} 3 4 5
        m2 = g.node("Merge", None, None); g.wire(x, 0, m2, 0)
        t.val(m2, 0, "0 1 2 3 4 5", "no modifier on two branches of 3: 0 1 2 3 4 5")
        m3 = g.node("Merge", None, None); g.wire(x, 0, m3, 0); t.mod(m3, 0, "reverse")
        t.val(m3, 0, "2 1 0 5 4 3", "reverse works on each branch: 2 1 0 and 5 4 3 (not 5 4 3 2 1 0)")
        # the modifier is per input: reversing B only
        s2 = series(g, 10, 10, 3)
        m4 = g.node("Merge", None, None); g.wire(s, 0, m4, 0); g.wire(s2, 0, m4, 1); t.mod(m4, 1, "reverse")
        t.val(m4, 0, "1 2 3 4 5 30 20 10", "reverse on B only: A as is, then 30 20 10")
    row("ghtrev", "D8-GH-TREES-0012", "Reverse parameter modifier",
        "reverse turns each branch's list back to front: 1 2 3 4 5 -> 5 4 3 2 1; two branches 0 1 2 | 3 4 5 -> 2 1 0 | 5 4 3 (the branches keep their order); it is a property of one input: Merge(1..5, reversed 10 20 30) = 1 2 3 4 5 30 20 10", b_trev)

    # ================================================================ Sets > Tree
    def b_stats(t):
        g = t.g
        x7 = partition(g, 7, 3)    # {0;0} 3 | {0;1} 3 | {0;2} 1
        t.stats(x7, 0, "3", "3 3 1", "0 0 0 1 0 2", "0..6 cut in threes")
        s = series(g, 0, 1, 5)
        t.stats(s, 0, "1", "5", "0", "a plain list of 5")
        e = series(g, 0, 1, 0)
        st = g.node("Tree Statistics", None); g.wire(e, 0, st, 0)
        t.val(st, 2, "0", "an empty list has 0 branches")
        t.none(st, 1, "and no lengths")
        en = g.node("Entwine"); g.wire(series(g, 0, 1, 2), 0, en, 0); g.wire(series(g, 5, 1, 3), 0, en, 1)
        t.stats(en, 0, "2", "2 3", "0 0 0 1", "Entwine of 2 and 3 items: {0;0} {0;1}")
        g1 = g.node("Graft Tree"); g.wire(series(g, 0, 1, 3), 0, g1, 0)
        g2 = g.node("Graft Tree"); g.wire(g1, 0, g2, 0)
        t.stats(g2, 0, "3", "1 1 1", "0 0 0 0 1 0 0 2 0", "grafted twice: {0;0;0} {0;1;0} {0;2;0}")
        t.stats(g.node("Construct Point", 1.0, 2.0, 3.0), 0, "1", "1", "0", "a single item")
    row("ghstats", "D8-GH-SETS-0058", "Tree Statistics",
        "branch count, items per branch (in branch order) and the paths {a;b;c}: 0..6 cut in threes = 3 branches {0;0} {0;1} {0;2} of 3, 3, 1 items; one list of 5 = 1 branch {0}; an empty list = 0 branches; Entwine of 2 and 3 items = {0;0} {0;1} with 2, 3; a list grafted twice = {0;0;0} {0;1;0} {0;2;0}; one item = 1 branch of 1", b_stats)

    def b_cpath(t):
        g = t.g

        def cp(lists):
            """Construct Path of the indices of one or more Series merged in order."""
            src = None
            for s in lists:
                if src is None:
                    src = s
                else:
                    m = g.node("Merge", None, None); g.wire(src, 0, m, 0); g.wire(s, 0, m, 1); src = m
            n = g.node("Construct Path"); g.wire(src, 0, n, 0)
            return n

        def same(n, literal, msg, dist="0"):
            td = g.node("Text Distance", None, ("t", literal)); g.wire(n, 0, td, 0)
            t.val(td, 0, dist, msg)
        n1 = cp([series(g, 0, 1, 2), series(g, 3, 1, 1)])    # 0 1 3
        same(n1, "{0;1;3}", "indices 0 1 3 give the text {0;1;3}")
        same(n1, "{0;1;4}", "decoy: {0;1;4} is 1 edit away", "1")
        same(n1, "{3;1;0}", "decoy: {3;1;0} is 2 edits away", "2")
        same(n1, "{0,1,3}", "decoy: commas instead of semicolons are 2 edits away", "2")
        n2 = cp([series(g, 7, 1, 1)])
        same(n2, "{7}", "one index 7 gives {7}")
        n3 = cp([series(g, 12, 1, 1), series(g, 0, 1, 1)])
        same(n3, "{12;0}", "indices 12 and 0 give {12;0}")
        n4 = cp([series(g, 0, 1, 4)])
        same(n4, "{0;1;2;3}", "four indices 0 1 2 3 give {0;1;2;3}")
        e = cp([series(g, 0, 1, 0)])
        t.none(e, 0, "no indices: nothing (a path needs at least one)")
        t.flag(e, "Construct Path", "A path needs at least one index")
        ng = cp([series(g, -1, 1, 1)])
        t.none(ng, 0, "a negative index is not a path: nothing")
        t.flag(ng, "Construct Path", "A path index must be a non-negative integer")
        # round trip through Deconstruct Path
        dp = g.node("Deconstruct Path"); g.wire(n1, 0, dp, 0)
        t.val(dp, 0, "0 1 3", "round trip: the text {0;1;3} deconstructs to 0 1 3")
    row("ghcpath", "D8-GH-SETS-0062", "Construct Path",
        "the text form {i;j;k} of the indices, braces and semicolons: 0 1 3 -> {0;1;3}; 7 -> {7}; 12 0 -> {12;0}; 0 1 2 3 -> {0;1;2;3}, compared with literal text by edit distance (0 for the exact text, 1 for {0;1;4}, 2 for {3;1;0} and for the comma form {0,1,3}); no index or a negative one is flagged; it round-trips through Deconstruct Path", b_cpath)

    def b_dpath(t):
        g = t.g
        for k, (txt, idx, ln) in enumerate((("{0;1;3}", "0 1 3", "3"), ("{12}", "12", "1"), ("{ 2 ; 5 }", "2 5", "2"), ("{0;0;0;7}", "0 0 0 7", "4"))):
            n = g.node("Deconstruct Path", ("t", txt))
            t.val(n, 0, idx, "%s deconstructs to %s" % (txt, idx))
            t.val(n, 1, ln, "%s has %s indices" % (txt, ln))
        for txt, why in (("0;1", "no braces"), ("{}", "no index"), ("{a;b}", "letters"), ("{-1}", "negative index"), ("{1;;2}", "an empty index"), ("{1;2", "no closing brace")):
            n = g.node("Deconstruct Path", ("t", txt))
            t.none(n, 0, "%s (%s) is not a path: nothing comes out" % (txt, why))
            t.flag(n, "Deconstruct Path", "Not a path: expected indices between braces such as {0;1;3}")
    row("ghdpath", "D8-GH-SETS-0063", "Deconstruct Path",
        "the integers between the braces, in order: {0;1;3} -> 0 1 3 (3 indices); {12} -> 12; { 2 ; 5 } -> 2 5; {0;0;0;7} -> 0 0 0 7 (4 indices); text without braces, with no index, with letters, with a negative or an empty index or without the closing brace is not a path and the node is flagged", b_dpath)

    def b_graftn(t):
        g = t.g
        s = series(g, 0, 1, 4)
        gr = g.node("Graft Tree"); g.wire(s, 0, gr, 0)
        t.stats(gr, 0, "4", "1 1 1 1", "0 0 0 1 0 2 0 3", "Graft Tree of one list of 4: {0;0} {0;1} {0;2} {0;3}")
        x = partition(g, 6, 3)   # {0;0} 0 1 2 | {0;1} 3 4 5
        gx = g.node("Graft Tree"); g.wire(x, 0, gx, 0)
        t.stats(gx, 0, "6", "1 1 1 1 1 1", "0 0 0 0 0 1 0 0 2 0 1 0 0 1 1 0 1 2", "Graft Tree of two branches of 3: {a;b} + item i = {a;b;i}")
        fl = g.node("Flatten Tree"); g.wire(gx, 0, fl, 0)
        t.val(fl, 0, "0 1 2 3 4 5", "the items keep their order: flattening the grafted tree gives 0 1 2 3 4 5 back")
        e = series(g, 0, 1, 0)
        ge = g.node("Graft Tree"); g.wire(e, 0, ge, 0)
        st = g.node("Tree Statistics", None); g.wire(ge, 0, st, 0)
        t.val(st, 2, "0", "grafting an empty list leaves no branches")
    row("ghgraftn", "D8-GH-SETS-0051", "Graft Tree",
        "every item to its own branch with its index appended to the path: one list of 4 at {0} -> {0;0} {0;1} {0;2} {0;3}; the branches {0;0} {0;1} of 3 items -> {0;0;0} {0;0;1} {0;0;2} {0;1;0} {0;1;1} {0;1;2}; the items keep their order (0..5); an empty list stays empty", b_graftn)

    def b_flatn(t):
        g = t.g
        x = partition(g, 6, 2)   # 3 branches of 2
        fl = g.node("Flatten Tree"); g.wire(x, 0, fl, 0)
        t.stats(fl, 0, "1", "6", "0", "Flatten Tree of 3 branches: one branch {0} of 6")
        t.val(fl, 0, "0 1 2 3 4 5", "the items keep their order")
        fp = g.node("Flatten Tree", None, ("t", "{2;1}")); g.wire(x, 0, fp, 0)
        t.stats(fp, 0, "1", "6", "2 1", "with the path {2;1} the single branch is {2;1}")
        gx = g.node("Graft Tree"); g.wire(x, 0, gx, 0)
        f2 = g.node("Flatten Tree"); g.wire(gx, 0, f2, 0)
        t.stats(f2, 0, "1", "6", "0", "a depth 3 tree flattens to the same single branch {0}")
        f3 = g.node("Flatten Tree", None, ("t", "{7}")); g.wire(series(g, 0, 1, 3), 0, f3, 0)
        t.stats(f3, 0, "1", "3", "7", "one list sent to the path {7}")
        bad = g.node("Flatten Tree", None, ("t", "2;1")); g.wire(x, 0, bad, 0)
        t.none(bad, 0, "a path without braces is not a path: nothing comes out")
        t.flag(bad, "Flatten Tree", "Not a path: expected indices between braces such as {0;1;3}")
    row("ghflatn", "D8-GH-SETS-0052", "Flatten Tree",
        "one branch with every item in order: three branches {0;0} {0;1} {0;2} of 2 -> {0} of 6 holding 0 1 2 3 4 5; with the optional path {2;1} the branch is {2;1}; a grafted (depth 3) tree gives the same; one list sent to {7}; a path without braces is flagged", b_flatn)
    return rows


# ------------------------------------------------------------------------------------------------------------------------------
HELPERS = r'''# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {{
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   {up}_$2"; else echo "FAIL {up}_$2 (no line containing '$1')"; fail=1; fi
}}
fr_cnt() {{
  local n
  n="$({{ grep -cF -- "$1" <<<"$FR_OUT" || true; }})"
  if [ "$n" = "$2" ]; then echo "ok   {up}_$3 (measured $n)"; else echo "FAIL {up}_$3 (measured $n, expected $2)"; fail=1; fi
}}
'''


def main():
    anchor = 'if grep -qF "GrasshopperPlayer: solved {nn} node(s)"'
    assert anchor in SECTION
    template = SECTION.replace(anchor, HELPERS + anchor, 1)
    template = template.replace("# {up}_: Flow Register row {rid} ({title}), flowrows_gen.py.", "# {up}_: Flow Register row {rid} ({title}), flowrows6_gen.py.")
    for name, rid, title, closed, build in make_rows():
        t = T()
        build(t)
        # control: Square(2) = 4 against the wrong closed form 4.5 is exactly -0.5, so the chain must bake round(-0.5 * 1e12) = -500000000000,
        # which the Absolute node turns into 500000000000: the 0 values above are real matches and not a chain that reports 0 whatever it is given
        t.exact(t.g.node("Square", 2.0), 0, 4.5, "x")
        t.expect[-1] = (t.expect[-1][0], "500000000000", "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches")
        nobj = sum(len(vals.split()) if vals else 0 for _tag, vals, _msg in t.expect)
        with open(os.path.join(HERE, "flowrows_%s.dflow" % name), "w") as f:
            f.write(t.g.text())
        with open(os.path.join(HERE, "flowrows_%s_script.txt" % name), "w") as f:
            f.write("# Flow Register row %s (%s): player + bake, read back by sections/104_%s.sh (see flowrows6_gen.py)\n"
                    "Grasshopper\nGrasshopperPlayer @GRAPH@ Bake=Yes\n@expect_objects %d\nSelAll\nList\n" % (rid, title, name, nobj))
        checks = [('fr_chk %d "%s" "%s"' % (tag, vals, msg.replace('"', "'"))) for tag, vals, msg in t.expect]
        for text, msg in t.lines:
            checks.append('fr_has "%s" "%s"' % (text.replace('"', '\\"'), msg.replace('"', "'")))
        checks.append('fr_cnt "flagged:" %d "flagged: exactly %d node(s) are flagged" ' % (t.flagged, t.flagged))
        text = template.format(up=name.upper(), rid=rid, title=title, name=name, closed=closed, nn=len(t.g.nodes), nobj=nobj, checks="\n".join(checks))
        with open(os.path.join(HERE, "sections", "104_%s.sh" % name), "w") as f:
            f.write(text)
        print(name, rid, "nodes", len(t.g.nodes), "objects", nobj, "checks", len(checks))


if __name__ == "__main__":
    main()
