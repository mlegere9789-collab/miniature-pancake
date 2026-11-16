#!/usr/bin/env python3
"""Writes the second batch of Flow Register-row tests (Maths, Sets, Vector): for each row one .dflow graph, one player
script and one tests/sections/95_<name>.sh section (the section name, upper-cased, is the row's acceptance test id).

The graph machinery is flowrows_gen.py's: every graph ends in Construct Point (X = the value under test, Y = a case tag) ->
Bake, so the player bakes one point per value and `List` prints it as "  X,TAG,0". The List printout keeps only four
significant digits for a non-integer, so a real-valued result is never compared as printed text. exact() instead
builds   Remap Numbers(value; closed form c .. c+1 -> 0 .. 1e12) -> Round To Multiple(1)   which is
round((value - c) * 1e12): it prints "0" exactly when |value - c| < 5e-13, inside the 1e-12 absolute tolerance the
Register rows carry. c is a closed form written below as an expression (math.pi / 6, (1 + sqrt 5) / 2, ...), worked
out by hand and independent of the node code under test. Run from dino8-app/tests:  python3 flowrows2_gen.py
"""
import math
import os

from flowrows_gen import G, SECTION, series

HERE = os.path.dirname(os.path.abspath(__file__))
SQ5 = math.sqrt(5.0)


USED = [0]


def exact(g, src, port, c, tag):
    """src.port must equal the closed form c within 5e-13; bakes the scaled residual (prints 0 on success)."""
    USED[0] += 1
    rm = g.node("Remap Numbers", None, float(c), float(c) + 1.0, 0.0, 1e12)
    g.wire(src, port, rm, 0)
    ro = g.node("Round To Multiple", None, 1.0)
    g.wire(rm, 0, ro, 0)
    g.probe(ro, 0, tag)


def vec(g, x, y, z):
    return g.node("Vector XYZ", float(x), float(y), float(z))


def pt(g, x, y, z):
    return g.node("Construct Point", float(x), float(y), float(z))


def comps(g, v, tags):
    """Bakes the X, Y, Z of a vector/point node's output 0 (printed, for exact integer or short values)."""
    d = g.node("Deconstruct Point")
    g.wire(v, 0, d, 0)
    for k, tag in enumerate(tags):
        if tag is not None:
            g.probe(d, k, tag)
    return d


def comps_exact(g, v, cs, tags, port=0):
    d = g.node("Deconstruct Point")
    g.wire(v, port, d, 0)
    for k in range(3):
        exact(g, d, k, cs[k], tags[k])


def make_rows():
    rows = []

    def row(name, rid, title, closed, build, expect):
        rows.append((name, rid, title, closed, build, expect))

    def zeros(tags):
        return [(t, "0", m) for t, m in tags]

    # ---------------------------------------------------------------- Maths > Operators
    def b_abs(g):
        for k, x in enumerate((-3.2, 5.0, 0.0, -0.0001)):
            exact(g, g.node("Absolute", x), 0, abs(x), k + 1)
    row("r2abs", "D8-GH-MATHS-0014", "Absolute", "|-3.2| = 3.2; |5| = 5; |0| = 0; |-0.0001| = 0.0001", b_abs,
        zeros([(1, "|-3.2| = 3.2"), (2, "|5| = 5"), (3, "|0| = 0"), (4, "|-0.0001| = 0.0001")]))

    def b_roots(g):
        cases = [("Square Root", 144.0, 12.0), ("Square Root", 2.0, math.sqrt(2.0)), ("Square Root", 0.0, 0.0),
                 ("Cube Root", 27.0, 3.0), ("Cube Root", -8.0, -2.0), ("Cube Root", 2.0, 2.0 ** (1.0 / 3.0))]
        for k, (n, x, c) in enumerate(cases):
            exact(g, g.node(n, x), 0, c, k + 1)
    row("r2roots", "D8-GH-MATHS-0019", "Square Root, Cube Root",
        "sqrt 144 = 12; sqrt 2 = 1.41421356237...; sqrt 0 = 0; cbrt 27 = 3; cbrt(-8) = -2 because (-2)^3 = -8; cbrt 2 = 2^(1/3) = 1.25992104989...", b_roots,
        zeros([(1, "sqrt 144 = 12"), (2, "sqrt 2 = 1.4142135623731"), (3, "sqrt 0 = 0"), (4, "cbrt 27 = 3"), (5, "cbrt(-8) = -2"), (6, "cbrt 2 = 2^(1/3)")]))

    def b_logs(g):
        cases = [("Logarithm", 1000.0, 3.0), ("Logarithm", 0.01, -2.0), ("Logarithm", 1.0, 0.0),
                 ("Natural Logarithm", math.e, 1.0), ("Natural Logarithm", 1.0, 0.0),
                 ("Exponent", 1.0, math.e), ("Exponent", 0.0, 1.0), ("Exponent", -1.0, 1.0 / math.e)]
        for k, (n, x, c) in enumerate(cases):
            exact(g, g.node(n, x), 0, c, k + 1)
    row("r2logs", "D8-GH-MATHS-0022", "Log, Natural Log, Exponent",
        "log10 1000 = 3 (10^3); log10 0.01 = -2; log10 1 = 0; ln e = 1; ln 1 = 0; e^1 = e = 2.71828182846; e^0 = 1; e^-1 = 1/e", b_logs,
        zeros([(1, "log10 1000 = 3"), (2, "log10 0.01 = -2"), (3, "log10 1 = 0"), (4, "ln e = 1"), (5, "ln 1 = 0"),
               (6, "e^1 = e"), (7, "e^0 = 1"), (8, "e^-1 = 1/e")]))

    def b_logn(g):
        cases = [(1024.0, 2.0, 10.0), (81.0, 3.0, 4.0), (0.001, 10.0, -3.0), (1.0, 7.0, 0.0), (8.0, 0.5, -3.0)]
        for k, (x, b, c) in enumerate(cases):
            exact(g, g.node("Log N", x, b), 0, c, k + 1)
    row("r2logn", "D8-GH-MATHS-0023", "Log N",
        "log_b(x) is the exponent y with b^y = x: 2^10 = 1024 -> 10; 3^4 = 81 -> 4; 10^-3 = 0.001 -> -3; 7^0 = 1 -> 0; 0.5^-3 = 8 -> -3", b_logn,
        zeros([(1, "log2 1024 = 10"), (2, "log3 81 = 4"), (3, "log10 0.001 = -3"), (4, "log7 1 = 0"), (5, "log0.5 8 = -3")]))

    def b_intdiv(g):
        for k, (a, b) in enumerate(((7, 2), (-7, 2), (7, -2), (100, 7), (6, 3))):
            g.probe(g.node("Integer Division", ("i", a), ("i", b)), 0, k + 1)
        for k, (a, b) in enumerate(((7, 2), (-7, 3), (7, -3), (10, 5), (100, 7))):
            g.probe(g.node("Modulus", ("i", a), ("i", b)), 0, k + 11)
    row("r2intdiv", "D8-GH-MATHS-0015", "Integer Division, Modulus",
        "truncating integer division with a = q*b + r, |r| < |b|, r taking the sign of a: 7/2 = 3 r 1; -7/2 = -3 r -1; 7/-2 = -3 r 1; 100/7 = 14 r 2; 6/3 = 2 r 0; -7 mod 3 = -1 (-7 = -2*3 - 1 would floor, so truncation gives -7 = -2*3 + -1); 7 mod -3 = 1 (7 = -2*-3 + 1); 10 mod 5 = 0",
        b_intdiv,
        [(1, "3", "7 \\ 2 = 3"), (2, "-3", "-7 \\ 2 = -3 (truncates toward zero)"), (3, "-3", "7 \\ -2 = -3"), (4, "14", "100 \\ 7 = 14"), (5, "2", "6 \\ 3 = 2"),
         (11, "1", "7 mod 2 = 1"), (12, "-1", "-7 mod 3 = -1 (sign of the dividend)"), (13, "1", "7 mod -3 = 1"), (14, "0", "10 mod 5 = 0"), (15, "2", "100 mod 7 = 2")])

    def b_round(g):
        n = 0
        for node, cases in (("Round", ((2.5, 3.0), (-2.5, -3.0), (2.4, 2.0), (2.6, 3.0), (-2.4, -2.0))),
                            ("Floor", ((2.7, 2.0), (-2.3, -3.0), (4.0, 4.0))),
                            ("Ceiling", ((2.1, 3.0), (-2.7, -2.0), (4.0, 4.0))),
                            ("Truncate", ((2.9, 2.0), (-2.9, -2.0), (0.4, 0.0)))):
            for x, c in cases:
                n += 1
                exact(g, g.node(node, x), 0, c, n)
    row("r2round", "D8-GH-MATHS-0021", "Round, Floor, Ceiling, Truncate",
        "Round: 2.5 -> 3, -2.5 -> -3 (halves away from zero), 2.4 -> 2, 2.6 -> 3, -2.4 -> -2; Floor (largest integer <= x): 2.7 -> 2, -2.3 -> -3, 4 -> 4; Ceiling (smallest integer >= x): 2.1 -> 3, -2.7 -> -2, 4 -> 4; Truncate (drop the fraction): 2.9 -> 2, -2.9 -> -2, 0.4 -> 0",
        b_round,
        zeros([(1, "round 2.5 = 3"), (2, "round -2.5 = -3"), (3, "round 2.4 = 2"), (4, "round 2.6 = 3"), (5, "round -2.4 = -2"),
               (6, "floor 2.7 = 2"), (7, "floor -2.3 = -3"), (8, "floor 4 = 4"),
               (9, "ceiling 2.1 = 3"), (10, "ceiling -2.7 = -2"), (11, "ceiling 4 = 4"),
               (12, "truncate 2.9 = 2"), (13, "truncate -2.9 = -2"), (14, "truncate 0.4 = 0")]))

    def b_gates(g):
        tag = 0
        table = {"Gate And": (0, 0, 0, 1), "Gate Or": (0, 1, 1, 1), "Gate Xor": (0, 1, 1, 0), "Gate Nand": (1, 1, 1, 0),
                 "Gate Nor": (1, 0, 0, 0), "Gate Xnor": (1, 0, 0, 1)}
        for name in table:
            for a in (False, True):
                for b in (False, True):
                    tag += 1
                    g.probe(g.node(name, ("b", a), ("b", b)), 0, tag)
        for a in (False, True):
            tag += 1
            g.probe(g.node("Gate Not", ("b", a)), 0, tag)
    gate_expect = []
    t = 0
    for name, outs in (("And", (0, 0, 0, 1)), ("Or", (0, 1, 1, 1)), ("Xor", (0, 1, 1, 0)), ("Nand", (1, 1, 1, 0)),
                       ("Nor", (1, 0, 0, 0)), ("Xnor", (1, 0, 0, 1))):
        for (a, b), o in zip(((0, 0), (0, 1), (1, 0), (1, 1)), outs):
            t += 1
            gate_expect.append((t, str(o), "%s(%s,%s) = %d" % (name, "T" if a else "F", "T" if b else "F", o)))
    for a, o in ((0, 1), (1, 0)):
        t += 1
        gate_expect.append((t, str(o), "Not(%s) = %d" % ("T" if a else "F", o)))
    row("r2gates", "D8-GH-MATHS-0028", "Gate And, Or, Not, Xor, Nand, Nor, Xnor",
        "truth tables over (A,B) = FF, FT, TF, TT: And 0001, Or 0111, Xor 0110, Nand 1110, Nor 1000, Xnor 1001; Not F = T, Not T = F", b_gates, gate_expect)

    # ---------------------------------------------------------------- Maths > Trig
    P = math.pi

    def b_trig(g):
        cases = [("Sine", P / 6, 0.5), ("Sine", 0.0, 0.0), ("Sine", -P / 2, -1.0),
                 ("Cosine", P / 3, 0.5), ("Cosine", 0.0, 1.0), ("Cosine", P, -1.0),
                 ("Tangent", P / 4, 1.0), ("Tangent", 0.0, 0.0), ("Tangent", -P / 4, -1.0)]
        for k, (n, x, c) in enumerate(cases):
            exact(g, g.node(n, x), 0, c, k + 1)
    row("r2trig", "D8-GH-MATHS-0030", "Sine, Cosine, Tangent",
        "angles in radians: sin(pi/6) = 1/2; sin 0 = 0; sin(-pi/2) = -1; cos(pi/3) = 1/2; cos 0 = 1; cos pi = -1; tan(pi/4) = 1; tan 0 = 0; tan(-pi/4) = -1",
        b_trig,
        zeros([(1, "sin(pi/6) = 0.5"), (2, "sin 0 = 0"), (3, "sin(-pi/2) = -1"), (4, "cos(pi/3) = 0.5"), (5, "cos 0 = 1"), (6, "cos pi = -1"),
               (7, "tan(pi/4) = 1"), (8, "tan 0 = 0"), (9, "tan(-pi/4) = -1")]))

    def b_inv(g):
        cases = [("Arcsin", 0.5, P / 6), ("Arcsin", 1.0, P / 2), ("Arcsin", -1.0, -P / 2),
                 ("Arccos", 0.5, P / 3), ("Arccos", 1.0, 0.0), ("Arccos", -1.0, P),
                 ("Arctan", 1.0, P / 4), ("Arctan", -1.0, -P / 4), ("Arctan", 0.0, 0.0)]
        for k, (n, x, c) in enumerate(cases):
            exact(g, g.node(n, x), 0, c, k + 1)
    row("r2invtrig", "D8-GH-MATHS-0031", "Arcsin, Arccos, Arctan",
        "inverse of the forward cases, principal values in radians: asin 1/2 = pi/6; asin 1 = pi/2; asin -1 = -pi/2; acos 1/2 = pi/3; acos 1 = 0; acos -1 = pi; atan 1 = pi/4; atan -1 = -pi/4; atan 0 = 0",
        b_inv,
        zeros([(1, "asin 0.5 = pi/6"), (2, "asin 1 = pi/2"), (3, "asin -1 = -pi/2"), (4, "acos 0.5 = pi/3"), (5, "acos 1 = 0"), (6, "acos -1 = pi"),
               (7, "atan 1 = pi/4"), (8, "atan -1 = -pi/4"), (9, "atan 0 = 0")]))

    def b_deg(g):
        cases = [("Degrees", P, 180.0), ("Degrees", P / 2, 90.0), ("Degrees", -P / 4, -45.0), ("Degrees", 0.0, 0.0),
                 ("Radians", 180.0, P), ("Radians", 90.0, P / 2), ("Radians", 45.0, P / 4), ("Radians", -360.0, -2 * P)]
        for k, (n, x, c) in enumerate(cases):
            exact(g, g.node(n, x), 0, c, k + 1)
    row("r2angle", "D8-GH-MATHS-0035", "Degrees, Radians",
        "180 degrees = pi radians: Degrees(pi) = 180, Degrees(pi/2) = 90, Degrees(-pi/4) = -45, Degrees(0) = 0; Radians(180) = pi, Radians(90) = pi/2, Radians(45) = pi/4, Radians(-360) = -2 pi",
        b_deg,
        zeros([(1, "pi rad = 180 deg"), (2, "pi/2 rad = 90 deg"), (3, "-pi/4 rad = -45 deg"), (4, "0 rad = 0 deg"),
               (5, "180 deg = pi rad"), (6, "90 deg = pi/2 rad"), (7, "45 deg = pi/4 rad"), (8, "-360 deg = -2 pi rad")]))

    def b_pi(g):
        exact(g, g.node("Pi"), 0, P, 1)
        exact(g, g.node("Pi", 2.0), 0, 2 * P, 2)
        exact(g, g.node("Pi", 0.5), 0, P / 2, 3)
        exact(g, g.node("Pi", 1.0 / 180.0), 0, P / 180, 4)
        exact(g, g.node("Pi", 0.0), 0, 0.0, 5)
    row("r2pi", "D8-GH-MATHS-0036", "Pi",
        "Pi(factor) = factor * pi with pi = 3.14159265358979: default factor 1 -> pi; 2 -> tau = 6.28318530717959; 0.5 -> pi/2 = 1.5707963267949; 1/180 -> pi/180 = 0.0174532925199433; 0 -> 0",
        b_pi,
        zeros([(1, "pi"), (2, "2 pi"), (3, "pi/2"), (4, "pi/180"), (5, "0 * pi = 0")]))

    def b_phi(g):
        exact(g, g.node("Golden Ratio"), 0, (1 + SQ5) / 2, 1)
        exact(g, g.node("Golden Ratio", 2.0), 0, 1 + SQ5, 2)
        exact(g, g.node("Golden Ratio", -1.0), 0, -(1 + SQ5) / 2, 3)
        # the defining property phi^2 = phi + 1: Remap(phi^2; phi .. phi + 1 -> 0 .. 1) must be exactly 1
        p = g.node("Golden Ratio")
        sq = g.node("Square")
        g.wire(p, 0, sq, 0)
        rm2 = g.node("Remap Numbers", None, (1 + SQ5) / 2, (1 + SQ5) / 2 + 1.0, 0.0, 1.0)
        g.wire(sq, 0, rm2, 0)
        exact(g, rm2, 0, 1.0, 4)
    row("r2golden", "D8-GH-MATHS-0039", "Golden Ratio",
        "phi = (1 + sqrt 5) / 2 = 1.61803398874989; factor 2 -> 1 + sqrt 5; factor -1 -> -phi; the defining property phi^2 = phi + 1 (so (phi^2 - phi) / 1 = 1)",
        b_phi,
        zeros([(1, "phi = (1 + sqrt 5) / 2"), (2, "2 phi = 1 + sqrt 5"), (3, "-phi"), (4, "phi^2 - phi = 1")]))

    # ---------------------------------------------------------------- Maths > Util
    def b_max(g):
        for k, (a, b, c) in enumerate(((3.0, 7.0, 7.0), (-1.0, -5.0, -1.0), (2.0, 2.0, 2.0), (-0.5, 0.25, 0.25), (1e9, -1e9, 1e9))):
            exact(g, g.node("Maximum", a, b), 0, c, k + 1)
    row("r2max", "D8-GH-MATHS-0082", "Maximum", "max(3,7) = 7; max(-1,-5) = -1; max(2,2) = 2; max(-0.5,0.25) = 0.25; max(1e9,-1e9) = 1e9", b_max,
        zeros([(1, "max(3,7) = 7"), (2, "max(-1,-5) = -1"), (3, "max(2,2) = 2"), (4, "max(-0.5,0.25) = 0.25"), (5, "max(1e9,-1e9) = 1e9")]))

    def b_min(g):
        for k, (a, b, c) in enumerate(((3.0, 7.0, 3.0), (-1.0, -5.0, -5.0), (2.0, 2.0, 2.0), (-0.5, 0.25, -0.5), (1e9, -1e9, -1e9))):
            exact(g, g.node("Minimum", a, b), 0, c, k + 1)
    row("r2min", "D8-GH-MATHS-0083", "Minimum", "min(3,7) = 3; min(-1,-5) = -5; min(2,2) = 2; min(-0.5,0.25) = -0.5; min(1e9,-1e9) = -1e9", b_min,
        zeros([(1, "min(3,7) = 3"), (2, "min(-1,-5) = -5"), (3, "min(2,2) = 2"), (4, "min(-0.5,0.25) = -0.5"), (5, "min(1e9,-1e9) = -1e9")]))

    # ---------------------------------------------------------------- Sets > List / Sequence
    def b_cullidx(g):
        s = series(g, 0, 1, 8)                       # 0..7
        i1 = series(g, 0, 2, 3)                      # 0 2 4
        c1 = g.node("Cull Index", None, None); g.wire(s, 0, c1, 0); g.wire(i1, 0, c1, 1); g.probe(c1, 0, 1)
        i2 = series(g, 8, -9, 2)                     # 8 -1: outside 0..7
        c2 = g.node("Cull Index", None, None); g.wire(s, 0, c2, 0); g.wire(i2, 0, c2, 1); g.probe(c2, 0, 2)
        c3 = g.node("Cull Index", None, None, ("b", True)); g.wire(s, 0, c3, 0); g.wire(i2, 0, c3, 1); g.probe(c3, 0, 3)
        i4 = series(g, 3, 0, 3)                      # 3 3 3: the same index repeated removes one item
        c4 = g.node("Cull Index", None, None); g.wire(s, 0, c4, 0); g.wire(i4, 0, c4, 1); g.probe(c4, 0, 4)
    row("r2cullidx", "D8-GH-SETS-0008", "Cull Index",
        "0..7 without indices 0,2,4 = 1 3 5 6 7; indices 8 and -1 without wrap are outside the list and ignored = 0..7; with wrap 8 mod 8 = 0 and -1 = 7 so items 0 and 7 go = 1 2 3 4 5 6; index 3 listed three times removes only item 3 = 0 1 2 4 5 6 7",
        b_cullidx,
        [(1, "1 3 5 6 7", "culling indices 0,2,4 of 0..7 leaves 1 3 5 6 7"),
         (2, "0 1 2 3 4 5 6 7", "indices 8 and -1 without wrap are ignored"),
         (3, "1 2 3 4 5 6", "with wrap 8 -> 0 and -1 -> 7 are removed"),
         (4, "0 1 2 4 5 6 7", "a repeated index removes the item once")])

    def b_dispatch(g):
        s = series(g, 0, 1, 6)                       # 0..5
        pat = series(g, 0, 1, 2)                     # 0 1 -> false true, repeating
        d = g.node("Dispatch"); g.wire(s, 0, d, 0); g.wire(pat, 0, d, 1)
        g.probe(d, 0, 1); g.probe(d, 1, 2)
        one = series(g, 1, 1, 1)                     # a single true
        d2 = g.node("Dispatch"); g.wire(s, 0, d2, 0); g.wire(one, 0, d2, 1)
        g.probe(d2, 0, 3)
        ln = g.node("List Length"); g.wire(d2, 1, ln, 0); g.probe(ln, 0, 4)
        z = series(g, 0, 0, 1)                       # a single false
        d3 = g.node("Dispatch"); g.wire(s, 0, d3, 0); g.wire(z, 0, d3, 1)
        ln3 = g.node("List Length"); g.wire(d3, 0, ln3, 0); g.probe(ln3, 0, 5)
        g.probe(d3, 1, 6)
    row("r2dispatch", "D8-GH-SETS-0011", "Dispatch",
        "0..5 with the repeating pattern F,T: the items at pattern-true positions 1,3,5 go to A and those at false positions 0,2,4 go to B; the one-entry pattern T sends all six items to A and none to B; the one-entry pattern F sends none to A and all six to B",
        b_dispatch,
        [(1, "1 3 5", "A holds the items at true positions 1 3 5"), (2, "0 2 4", "B holds the items at false positions 0 2 4"),
         (3, "0 1 2 3 4 5", "pattern T sends everything to A"), (4, "0", "pattern T leaves B empty"),
         (5, "0", "pattern F leaves A empty"), (6, "0 1 2 3 4 5", "pattern F sends everything to B")])

    def b_replace(g):
        s = series(g, 0, 1, 5)                       # 0..4
        items = series(g, 100, 100, 2)               # 100 200
        idx = series(g, 1, 2, 2)                     # 1 3
        r1 = g.node("Replace Items", None, None, None); g.wire(s, 0, r1, 0); g.wire(items, 0, r1, 1); g.wire(idx, 0, r1, 2); g.probe(r1, 0, 1)
        one = series(g, 7, 1, 1)                     # a single replacement 7 used for both indices
        r2 = g.node("Replace Items", None, None, None); g.wire(s, 0, r2, 0); g.wire(one, 0, r2, 1); g.wire(idx, 0, r2, 2); g.probe(r2, 0, 2)
        out = series(g, 9, 1, 1)
        far = series(g, 5, 1, 1)                     # index 5 is outside 0..4
        r3 = g.node("Replace Items", None, None, None); g.wire(s, 0, r3, 0); g.wire(out, 0, r3, 1); g.wire(far, 0, r3, 2); g.probe(r3, 0, 3)
        r4 = g.node("Replace Items", None, None, None, ("b", True)); g.wire(s, 0, r4, 0); g.wire(out, 0, r4, 1); g.wire(far, 0, r4, 2); g.probe(r4, 0, 4)
    row("r2replace", "D8-GH-SETS-0013", "Replace Items",
        "0..4 with items 100,200 put at indices 1,3 = 0 100 2 200 4; a single item 7 for both indices = 0 7 2 7 4; index 5 is outside the list and ignored without wrap = 0 1 2 3 4; with wrap 5 mod 5 = 0 so item 0 becomes 9 = 9 1 2 3 4",
        b_replace,
        [(1, "0 100 2 200 4", "items 100,200 replace indices 1,3"), (2, "0 7 2 7 4", "one item serves both indices"),
         (3, "0 1 2 3 4", "an out-of-range index is ignored without wrap"), (4, "9 1 2 3 4", "with wrap index 5 is index 0")])

    def b_insert(g):
        s = series(g, 0, 1, 3)                       # 0 1 2
        x9 = series(g, 9, 1, 1)
        i0 = series(g, 0, 1, 1); i1 = series(g, 1, 1, 1); i3 = series(g, 3, 1, 1)
        for tag, ix in ((1, i0), (2, i1), (3, i3)):
            n = g.node("Insert Items", None, None, None); g.wire(s, 0, n, 0); g.wire(x9, 0, n, 1); g.wire(ix, 0, n, 2); g.probe(n, 0, tag)
        two = series(g, 8, 1, 2)                     # 8 9
        ends = series(g, 0, 3, 2)                    # 0 3
        n4 = g.node("Insert Items", None, None, None); g.wire(s, 0, n4, 0); g.wire(two, 0, n4, 1); g.wire(ends, 0, n4, 2); g.probe(n4, 0, 4)
        i7 = series(g, 7, 1, 1)                      # far past the end: clamped to the end without wrap
        n5 = g.node("Insert Items", None, None, None); g.wire(s, 0, n5, 0); g.wire(x9, 0, n5, 1); g.wire(i7, 0, n5, 2); g.probe(n5, 0, 5)
        ln = g.node("List Length"); g.wire(n5, 0, ln, 0); g.probe(ln, 0, 6)
    row("r2insert", "D8-GH-SETS-0014", "Insert Items",
        "list 0 1 2: item 9 at index 0 goes in front of everything = 9 0 1 2; at index 1 = 0 9 1 2; at index 3 (the length) appends = 0 1 2 9; items 8,9 at indices 0,3 = 8 0 1 2 9; index 7 past the end is clamped to an append = 0 1 2 9 (4 items)",
        b_insert,
        [(1, "9 0 1 2", "insert at 0 prepends"), (2, "0 9 1 2", "insert at 1 goes between 0 and 1"), (3, "0 1 2 9", "insert at the length appends"),
         (4, "8 0 1 2 9", "items 8,9 at indices 0 and 3"), (5, "0 1 2 9", "an index past the end appends"), (6, "4", "the result has 4 items")])

    def b_itemidx(g):
        s = series(g, 10, 10, 4)                     # 10 20 30 40
        for tag, x in ((1, 30.0), (2, 10.0), (3, 40.0), (4, 25.0)):
            n = g.node("Item Index", None, x); g.wire(s, 0, n, 0); g.probe(n, 0, tag)
        dup = series(g, 7, 0, 4)                     # 7 7 7 7: first match
        n = g.node("Item Index", None, 7.0); g.wire(dup, 0, n, 0); g.probe(n, 0, 5)
        neg = series(g, -2, 1, 5)                    # -2 -1 0 1 2
        n2 = g.node("Item Index", None, 0.0); g.wire(neg, 0, n2, 0); g.probe(n2, 0, 6)
    row("r2itemidx", "D8-GH-SETS-0020", "Item Index",
        "in 10 20 30 40 (indices 0..3): 30 is at index 2, 10 at 0, 40 at 3, 25 is absent (-1); in 7 7 7 7 the first match 0 is returned; in -2 -1 0 1 2 the value 0 is at index 2",
        b_itemidx,
        [(1, "2", "30 is at index 2"), (2, "0", "10 is at index 0"), (3, "3", "40 is at index 3"), (4, "-1", "25 is not in the list"),
         (5, "0", "the first of equal items is found"), (6, "2", "0 in -2..2 is at index 2")])

    def b_range(g):
        r = g.node("Range", 0.0, 1.0, ("i", 10))     # 11 values 0, 0.1, ... 1
        ln = g.node("List Length"); g.wire(r, 0, ln, 0); g.probe(ln, 0, 1)
        li0 = g.node("List Item", None, ("i", 0), ("b", False)); g.wire(r, 0, li0, 0); exact(g, li0, 0, 0.0, 2)
        li10 = g.node("List Item", None, ("i", 10), ("b", False)); g.wire(r, 0, li10, 0); exact(g, li10, 0, 1.0, 3)
        li3 = g.node("List Item", None, ("i", 3), ("b", False)); g.wire(r, 0, li3, 0); exact(g, li3, 0, 0.3, 4)
        ma = g.node("Mass Addition"); g.wire(r, 0, ma, 0); exact(g, ma, 0, 5.5, 5)
        r5 = g.node("Range", 0.0, 10.0, ("i", 5)); g.probe(r5, 0, 6)
        r4 = g.node("Range", -1.0, 1.0, ("i", 4)); g.probe(r4, 0, 7)
        r1 = g.node("Range", 3.0, 8.0, ("i", 1)); g.probe(r1, 0, 8)
        rr = g.node("Range", 5.0, 0.0, ("i", 5)); g.probe(rr, 0, 9)
    row("r2range", "D8-GH-SETS-0031", "Range",
        "Range(a, b, n) is a + (b - a) i / n for i = 0..n, n + 1 values (the Grasshopper Primer: 10 steps return 11 values): (0,1,10) has 11 values, the first 0, the last 1, the fourth 3/10 = 0.3 and sum 11 * (0 + 1) / 2 = 5.5; (0,10,5) = 0 2 4 6 8 10; (-1,1,4) = -1 -0.5 0 0.5 1; (3,8,1) = 3 8; (5,0,5) descends 5 4 3 2 1 0",
        b_range,
        [(1, "11", "ten steps give eleven values"), (2, "0", "the first value is the domain start"), (3, "0", "the last value is the domain end"),
         (4, "0", "the value at index 3 is 0.3"), (5, "0", "the eleven values sum to 5.5"),
         (6, "0 2 4 6 8 10", "(0,10,5) = 0 2 4 6 8 10"), (7, "-1 -0.5 0 0.5 1", "(-1,1,4) = -1 -0.5 0 0.5 1"),
         (8, "3 8", "one step gives the two end points"), (9, "5 4 3 2 1 0", "a descending domain runs from 5 down to 0")])

    # ---------------------------------------------------------------- Vector
    def b_vxyz(g):
        v = vec(g, 3, 4, 0)
        comps(g, v, (1, 2, 3))
        g.probe(v, 1, 4)
        v2 = vec(g, 1, 2, 2); g.probe(v2, 1, 5)
        v3 = vec(g, -2, 3, 6); g.probe(v3, 1, 6)
        v4 = vec(g, 0, 0, 0); g.probe(v4, 1, 7)
        v5 = vec(g, 1, 1, 1); exact(g, v5, 1, math.sqrt(3.0), 8)
    row("r2vecxyz", "D8-GH-VECTOR-0001", "Vector XYZ",
        "components pass through: (3,4,0) -> 3 4 0; its length sqrt(3^2 + 4^2) = 5; (1,2,2) -> sqrt(1 + 4 + 4) = 3; (-2,3,6) -> sqrt(4 + 9 + 36) = 7; (0,0,0) -> 0; (1,1,1) -> sqrt 3",
        b_vxyz,
        [(1, "3", "X of (3,4,0) is 3"), (2, "4", "Y of (3,4,0) is 4"), (3, "0", "Z of (3,4,0) is 0"), (4, "5", "length of (3,4,0) is 5"),
         (5, "3", "length of (1,2,2) is 3"), (6, "7", "length of (-2,3,6) is 7"), (7, "0", "length of the zero vector is 0"), (8, "0", "length of (1,1,1) is sqrt 3")])

    def b_v2pt(g):
        a = pt(g, 1, 2, 3); b = pt(g, 4, 6, 3)
        v = g.node("Vector 2Pt"); g.wire(a, 0, v, 0); g.wire(b, 0, v, 1)
        comps(g, v, (1, 2, 3)); g.probe(v, 1, 4)
        u = g.node("Vector 2Pt", None, None, ("b", True)); g.wire(a, 0, u, 0); g.wire(b, 0, u, 1)
        comps_exact(g, u, (0.6, 0.8, 0.0), (5, 6, 7)); g.probe(u, 1, 8)
        z = g.node("Vector 2Pt", None, None, ("b", True)); g.wire(a, 0, z, 0); g.wire(a, 0, z, 1)
        comps(g, z, (9, 10, 11)); g.probe(z, 1, 12)
        rev = g.node("Vector 2Pt"); g.wire(b, 0, rev, 0); g.wire(a, 0, rev, 1)
        comps(g, rev, (13, 14, 15))
    row("r2vec2pt", "D8-GH-VECTOR-0002", "Vector 2Pt",
        "tip - base: (1,2,3) -> (4,6,3) is (3,4,0) of length 5; unitized it is (3,4,0)/5 = (0.6,0.8,0) and the reported length stays 5; equal points give the zero vector of length 0; swapping the points negates it: (-3,-4,0)",
        b_v2pt,
        [(1, "3", "X of (4,6,3)-(1,2,3) is 3"), (2, "4", "Y is 4"), (3, "0", "Z is 0"), (4, "5", "length is 5"),
         (5, "0", "unitized X is 0.6"), (6, "0", "unitized Y is 0.8"), (7, "0", "unitized Z is 0"), (8, "5", "length stays 5 when unitizing"),
         (9, "0", "equal points give X 0"), (10, "0", "Y 0"), (11, "0", "Z 0"), (12, "0", "length 0"),
         (13, "-3", "reversed X is -3"), (14, "-4", "reversed Y is -4"), (15, "0", "reversed Z is 0")])

    def b_amp(g):
        v = vec(g, 1, 1, 0)
        a = g.node("Amplitude", None, 5.0); g.wire(v, 0, a, 0)
        ln = g.node("Vector Length"); g.wire(a, 0, ln, 0); exact(g, ln, 0, 5.0, 1)
        comps_exact(g, a, (5 / math.sqrt(2.0), 5 / math.sqrt(2.0), 0.0), (2, 3, 4))
        v2 = vec(g, 3, 4, 0)
        a2 = g.node("Amplitude", None, 10.0); g.wire(v2, 0, a2, 0)
        comps_exact(g, a2, (6.0, 8.0, 0.0), (5, 6, 7))
        v3 = vec(g, 0, 0, 3)
        a3 = g.node("Amplitude", None, -2.0); g.wire(v3, 0, a3, 0)
        comps_exact(g, a3, (0.0, 0.0, -2.0), (8, 9, 10))
    row("r2amp", "D8-GH-VECTOR-0005", "Amplitude",
        "direction kept, length set: (1,1,0) at 5 has length 5 and components 5/sqrt 2 = 3.5355339059 each; (3,4,0) at 10 = (6,8,0); (0,0,3) at -2 = (0,0,-2) (a negative amplitude reverses)",
        b_amp,
        zeros([(1, "length of (1,1,0) at amplitude 5 is 5"), (2, "X is 5/sqrt 2"), (3, "Y is 5/sqrt 2"), (4, "Z is 0"),
               (5, "(3,4,0) at 10 has X 6"), (6, "Y 8"), (7, "Z 0"), (8, "(0,0,3) at -2 has X 0"), (9, "Y 0"), (10, "Z -2")]))

    def b_vlen(g):
        for k, (x, y, z, c) in enumerate(((3, 4, 0, 5.0), (3, 4, 12, 13.0), (-1, -2, -2, 3.0), (0, 0, 0, 0.0), (0, 0, -7, 7.0), (1, 1, 0, math.sqrt(2.0)))):
            v = vec(g, x, y, z); ln = g.node("Vector Length"); g.wire(v, 0, ln, 0); exact(g, ln, 0, c, k + 1)
    row("r2veclen", "D8-GH-VECTOR-0006", "Vector Length",
        "sqrt(x^2 + y^2 + z^2): (3,4,0) = 5; (3,4,12) = sqrt(169) = 13; (-1,-2,-2) = sqrt 9 = 3; (0,0,0) = 0; (0,0,-7) = 7; (1,1,0) = sqrt 2",
        b_vlen,
        zeros([(1, "|(3,4,0)| = 5"), (2, "|(3,4,12)| = 13"), (3, "|(-1,-2,-2)| = 3"), (4, "|0| = 0"), (5, "|(0,0,-7)| = 7"), (6, "|(1,1,0)| = sqrt 2")]))

    def b_dot(g):
        for k, (a, b, c) in enumerate((((1, 2, 3), (4, -5, 6), 12.0), ((1, 0, 0), (0, 1, 0), 0.0), ((2, 0, 0), (3, 0, 0), 6.0),
                                       ((1, 2, 3), (-1, -2, -3), -14.0), ((0.5, 0.25, 2), (4, 8, 0.125), 4.25))):
            va = vec(g, *a); vb = vec(g, *b)
            d = g.node("Dot Product"); g.wire(va, 0, d, 0); g.wire(vb, 0, d, 1); exact(g, d, 0, c, k + 1)
    row("r2dot", "D8-GH-VECTOR-0009", "Dot Product",
        "ax*bx + ay*by + az*bz: (1,2,3).(4,-5,6) = 4 - 10 + 18 = 12; (1,0,0).(0,1,0) = 0 (orthogonal); (2,0,0).(3,0,0) = 6; (1,2,3).(-1,-2,-3) = -14; (0.5,0.25,2).(4,8,0.125) = 2 + 2 + 0.25 = 4.25",
        b_dot,
        zeros([(1, "(1,2,3).(4,-5,6) = 12"), (2, "orthogonal pair = 0"), (3, "(2,0,0).(3,0,0) = 6"), (4, "(1,2,3).(-1,-2,-3) = -14"), (5, "(0.5,0.25,2).(4,8,0.125) = 4.25")]))
    return rows


def main():
    for name, rid, title, closed, build, expect in make_rows():
        g = G()
        USED[0] = 0
        build(g)
        if USED[0]:
            # Negative control for the residual chain: Square(2) = 4 against the wrong closed form 4.5 is exactly -0.5, so
            # the chain must bake round(-0.5 * 1e12) = -500000000000 - proving that the 0 baked above is a real match
            # and not a chain that reports 0 whatever it is given.
            exact(g, g.node("Square", 2.0), 0, 4.5, 99)
            expect = list(expect) + [(99, "-500000000000", "control: the residual chain reports Square(2) - 4.5 as -500000000000, so the 0 values above are real matches")]
        nobj = sum(len(vals.split()) if vals else 0 for _tag, vals, _msg in expect)
        with open(os.path.join(HERE, "flowrows_%s.dflow" % name), "w") as f:
            f.write(g.text())
        with open(os.path.join(HERE, "flowrows_%s_script.txt" % name), "w") as f:
            f.write("# Flow Register row %s (%s): player + bake, read back by sections/95_%s.sh (see flowrows2_gen.py)\n"
                    "Grasshopper\nGrasshopperPlayer @GRAPH@ Bake=Yes\n@expect_objects %d\nSelAll\nList\n" % (rid, title, name, nobj))
        checks = "\n".join('fr_chk %d "%s" "%s"' % (tag, vals, msg) for tag, vals, msg in expect)
        with open(os.path.join(HERE, "sections", "95_%s.sh" % name), "w") as f:
            f.write(SECTION.format(up=name.upper(), rid=rid, title=title, name=name, closed=closed, nn=len(g.nodes), nobj=nobj, checks=checks))
        print(name, rid, "nodes", len(g.nodes), "objects", nobj)


if __name__ == "__main__":
    main()
