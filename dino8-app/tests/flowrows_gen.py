#!/usr/bin/env python3
"""Writes the Flow Register-row tests: for each row one .dflow graph, one player script and one
tests/sections/94_<name>.sh section (the section name, upper-cased, is the row's acceptance test id).

Every graph ends in Construct Point (X = the value under test, Y = a case tag) -> Bake, so the player
bakes one point per value and `List` prints it as "  X,TAG,0". Each section reads the points of a tag back
in baking order and compares them with a value written below as a closed form, worked out by hand and
independent of the node code under test. Run from dino8-app/tests:  python3 flowrows_gen.py
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def lit(v):
    if v is None:
        return {"has": False, "value": {"kind": "Null"}}
    if isinstance(v, tuple):
        kind, x = v
        if kind == "t":
            return {"has": True, "value": {"kind": "Text", "text": x}}
        if kind == "b":
            return {"has": True, "value": {"kind": "Boolean", "num": 1 if x else 0}}
        if kind == "i":
            return {"has": True, "value": {"kind": "Integer", "num": x}}
    return {"has": True, "value": {"kind": "Number", "num": v}}


class G:
    def __init__(self):
        self.nodes, self.wires = [], []

    def node(self, typ, *ins):
        nid = len(self.nodes) + 1
        self.nodes.append({"id": nid, "type": typ, "x": 150 * nid, "y": 0, "enabled": True, "preview": True,
                           "slider_min": 0, "slider_max": 10, "slider_value": 0, "slider_integer": False,
                           "toggle": False, "text": "", "colour": [1, 1, 1], "refs": [],
                           "inputs": [lit(v) for v in ins]})
        return nid

    def wire(self, a, ap, b, bp):
        self.wires.append({"from": a, "fp": ap, "to": b, "tp": bp})

    def probe(self, src, port, tag):
        """src.port -> Construct Point(X) with Y = tag -> Bake."""
        p = self.node("Construct Point", None, float(tag), 0.0)
        b = self.node("Bake")
        self.wire(src, port, p, 0)
        self.wire(p, 0, b, 0)

    def text(self):
        out = ['{"format":"dflow1","nodes":[']
        out.append(",\n".join(json.dumps(n, separators=(",", ":")) for n in self.nodes))
        out.append('],"wires":[')
        out.append(",\n".join(json.dumps(w, separators=(",", ":")) for w in self.wires))
        out.append("]}\n")
        return "\n".join(out)


def series(g, start, step, count):
    return g.node("Series", float(start), float(step), ("i", count))


def make_rows():
    rows = []

    def row(name, rid, title, closed, build, expect):
        rows.append((name, rid, title, closed, build, expect))

    # --- D8-GH-MATHS-0016 Factorial
    def b_fact(g):
        for n, tag in ((10, 1), (0, 2), (5, 3)):
            g.probe(g.node("Factorial", ("i", n)), 0, tag)
    row("frfact", "D8-GH-MATHS-0016", "Factorial",
        "10! = 3628800; 0! = 1 (empty product); 5! = 120", b_fact,
        [(1, "3628800", "10! = 3628800"), (2, "1", "0! = 1"), (3, "120", "5! = 120")])

    # --- D8-GH-MATHS-0017 Mass Addition
    def b_madd(g):
        s = series(g, 1, 1, 100)
        m = g.node("Mass Addition"); g.wire(s, 0, m, 0); g.probe(m, 0, 1)
        s4 = series(g, 1, 1, 4)
        m4 = g.node("Mass Addition"); g.wire(s4, 0, m4, 0); g.probe(m4, 1, 2)
    row("frmassadd", "D8-GH-MATHS-0017", "Mass Addition",
        "sum 1..100 = 100*101/2 = 5050; partial sums of 1,2,3,4 = 1,3,6,10", b_madd,
        [(1, "5050", "sum of 1..100 = 5050"), (2, "1 3 6 10", "partial sums of 1,2,3,4 = 1,3,6,10")])

    # --- D8-GH-MATHS-0018 Mass Multiplication
    def b_mmul(g):
        s = series(g, 1, 1, 6)
        m = g.node("Mass Multiplication"); g.wire(s, 0, m, 0); g.probe(m, 0, 1)
        s5 = series(g, 1, 1, 5)
        m5 = g.node("Mass Multiplication"); g.wire(s5, 0, m5, 0); g.probe(m5, 1, 2)
    row("frmassmul", "D8-GH-MATHS-0018", "Mass Multiplication",
        "product 1..6 = 6! = 720; partial products of 1..5 = 1,2,6,24,120", b_mmul,
        [(1, "720", "product of 1..6 = 720"), (2, "1 2 6 24 120", "partial products of 1..5 = 1,2,6,24,120")])

    # --- D8-GH-MATHS-0020 Square
    def b_sq(g):
        for x, tag in ((12.0, 1), (-3.0, 2), (0.5, 3)):
            g.probe(g.node("Square", x), 0, tag)
    row("frsquare", "D8-GH-MATHS-0020", "Square",
        "12^2 = 144; (-3)^2 = 9; 0.5^2 = 0.25", b_sq,
        [(1, "144", "12^2 = 144"), (2, "9", "(-3)^2 = 9"), (3, "0.25", "0.5^2 = 0.25")])

    # --- D8-GH-MATHS-0038 Pythagorean
    def b_pyth(g):
        for a, b, tag in ((3.0, 4.0, 1), (5.0, 12.0, 2), (1.0, 1.0, 3)):
            g.probe(g.node("Pythagorean", a, b), 0, tag)
    row("frpyth", "D8-GH-MATHS-0038", "Pythagorean",
        "(3,4) -> 5; (5,12) -> 13; (1,1) -> sqrt 2 = 1.4142 (prints 1.414)", b_pyth,
        [(1, "5", "hypotenuse of 3,4 = 5"), (2, "13", "hypotenuse of 5,12 = 13"), (3, "1.414", "hypotenuse of 1,1 = sqrt 2 = 1.414")])

    # --- D8-GH-MATHS-0049 Power Of Two
    def b_p2(g):
        for x, tag in ((10.0, 1), (0.0, 2), (-2.0, 3)):
            g.probe(g.node("Power Of Two", x), 0, tag)
    row("frpow2", "D8-GH-MATHS-0049", "Power Of Two",
        "2^10 = 1024; 2^0 = 1; 2^-2 = 1/4 = 0.25", b_p2,
        [(1, "1024", "2^10 = 1024"), (2, "1", "2^0 = 1"), (3, "0.25", "2^-2 = 0.25")])

    # --- D8-GH-MATHS-0081 Average
    def b_avg(g):
        s = series(g, 1, 1, 4); a = g.node("Average"); g.wire(s, 0, a, 0); g.probe(a, 0, 1)
        s2 = series(g, 0, 2, 5); a2 = g.node("Average"); g.wire(s2, 0, a2, 0); g.probe(a2, 0, 2)
    row("fravg", "D8-GH-MATHS-0081", "Average",
        "mean(1,2,3,4) = 10/4 = 2.5; mean(0,2,4,6,8) = 20/5 = 4", b_avg,
        [(1, "2.5", "mean of 1,2,3,4 = 2.5"), (2, "4", "mean of 0,2,4,6,8 = 4")])

    # --- D8-GH-MATHS-0091 Round To Multiple
    def b_rm(g):
        for v, m, tag in ((13.7, 5.0, 1), (-13.7, 5.0, 2), (7.0, 2.0, 3)):
            g.probe(g.node("Round To Multiple", v, m), 0, tag)
    row("frroundm", "D8-GH-MATHS-0091", "Round To Multiple",
        "13.7 to a multiple of 5: 13.7/5 = 2.74 -> 3 -> 15; -13.7 -> -15; 7 to a multiple of 2: 3.5 -> 4 (half away from zero) -> 8", b_rm,
        [(1, "15", "13.7 to a multiple of 5 = 15"), (2, "-15", "-13.7 to a multiple of 5 = -15"), (3, "8", "7 to a multiple of 2 = 8 (half rounds away from zero)")])

    # --- D8-GH-MATHS-0024 Relative Differences
    def b_rd(g):
        s = series(g, 1, 1, 3)
        sq = g.node("Square"); g.wire(s, 0, sq, 0)       # 1,4,9
        rd = g.node("Relative Differences"); g.wire(sq, 0, rd, 0); g.probe(rd, 0, 1)
        s2 = series(g, 10, -3, 4)                         # 10,7,4,1
        rd2 = g.node("Relative Differences"); g.wire(s2, 0, rd2, 0); g.probe(rd2, 0, 2)
    row("frreldiff", "D8-GH-MATHS-0024", "Relative Differences",
        "{1,4,9} -> {3,5}; {10,7,4,1} -> {-3,-3,-3}", b_rd,
        [(1, "3 5", "differences of 1,4,9 = 3,5"), (2, "-3 -3 -3", "differences of 10,7,4,1 = -3,-3,-3")])

    # --- D8-GH-MATHS-0099 Fibonacci, F(30) = 832040
    def b_f30(g):
        f = g.node("Fibonacci", ("i", 31))
        li = g.node("List Item", None, ("i", 30), ("b", False)); g.wire(f, 0, li, 0); g.probe(li, 0, 1)
        f11 = g.node("Fibonacci", ("i", 11))
        li2 = g.node("List Item", None, ("i", 10), ("b", False)); g.wire(f11, 0, li2, 0); g.probe(li2, 0, 2)
    row("frfib30", "D8-GH-MATHS-0099", "Fibonacci (nth term)",
        "F(30) = 832040; F(10) = 55 (F(0)=0, F(1)=1)", b_f30,
        [(1, "832040", "F(30) = 832040"), (2, "55", "F(10) = 55")])

    # --- D8-GH-SETS-0075 Fibonacci, first 10 terms
    def b_fseq(g):
        f = g.node("Fibonacci", ("i", 10)); g.probe(f, 0, 1)
    row("frfibseq", "D8-GH-SETS-0075", "Fibonacci (sequence)",
        "first 10 terms 0 1 1 2 3 5 8 13 21 34", b_fseq,
        [(1, "0 1 1 2 3 5 8 13 21 34", "first 10 Fibonacci terms")])

    # --- D8-GH-MATHS-0098 GCD / LCM
    def b_gcd(g):
        g.probe(g.node("Greatest Common Divisor", ("i", 84), ("i", 36)), 0, 1)
        g.probe(g.node("Least Common Multiple", ("i", 84), ("i", 36)), 0, 2)
        g.probe(g.node("Greatest Common Divisor", ("i", 0), ("i", 5)), 0, 3)
        g.probe(g.node("Least Common Multiple", ("i", 4), ("i", 6)), 0, 4)
        g.probe(g.node("Greatest Common Divisor", ("i", -12), ("i", 18)), 0, 5)
    row("frgcdlcm", "D8-GH-MATHS-0098", "Greatest Common Divisor, Least Common Multiple",
        "84 = 2^2*3*7, 36 = 2^2*3^2: gcd = 2^2*3 = 12, lcm = 2^2*3^2*7 = 252; gcd(0,5) = 5; lcm(4,6) = 12; gcd(-12,18) = 6", b_gcd,
        [(1, "12", "gcd(84,36) = 12"), (2, "252", "lcm(84,36) = 252"), (3, "5", "gcd(0,5) = 5"), (4, "12", "lcm(4,6) = 12"), (5, "6", "gcd(-12,18) = 6")])

    # --- D8-GH-SETS-0010 Cull Nth
    def b_cn(g):
        s = series(g, 0, 1, 10); c = g.node("Cull Nth", None, ("i", 3)); g.wire(s, 0, c, 0); g.probe(c, 0, 1)
        s2 = series(g, 0, 1, 5); c2 = g.node("Cull Nth", None, ("i", 2)); g.wire(s2, 0, c2, 0); g.probe(c2, 0, 2)
    row("frcullnth", "D8-GH-SETS-0010", "Cull Nth",
        "0..9 with N=3: the 3rd, 6th and 9th items (values 2, 5, 8) go, leaving 0 1 3 4 6 7 9; 0..4 with N=2 removes the 2nd and 4th (1, 3), leaving 0 2 4", b_cn,
        [(1, "0 1 3 4 6 7 9", "Cull Nth N=3 of 0..9 removes 2,5,8"), (2, "0 2 4", "Cull Nth N=2 of 0..4 removes 1,3")])

    # --- D8-GH-SETS-0016 Split List
    def b_sl(g):
        s = series(g, 0, 1, 10); sp = g.node("Split List", None, ("i", 4)); g.wire(s, 0, sp, 0)
        g.probe(sp, 0, 1); g.probe(sp, 1, 2)
        s3 = series(g, 0, 1, 3); sp3 = g.node("Split List", None, ("i", 15)); g.wire(s3, 0, sp3, 0)
        g.probe(sp3, 0, 3)
        ln = g.node("List Length"); g.wire(sp3, 1, ln, 0); g.probe(ln, 0, 4)
    row("frsplit", "D8-GH-SETS-0016", "Split List",
        "0..9 split at index 4: A = 0 1 2 3, B = 4 5 6 7 8 9; 0..2 split at 15 (past the end, clamped): A = 0 1 2, B has length 0", b_sl,
        [(1, "0 1 2 3", "A of 0..9 split at 4 = 0 1 2 3"), (2, "4 5 6 7 8 9", "B of 0..9 split at 4 = 4..9"),
         (3, "0 1 2", "split past the end keeps everything in A"), (4, "0", "split past the end leaves B with length 0")])

    # --- D8-GH-SETS-0049 Text Distance
    def b_td(g):
        cases = [("kitten", "sitting", None, 1), ("", "abc", None, 2), ("flaw", "lawn", None, 3),
                 ("Kitten", "kitten", None, 4), ("Kitten", "kitten", ("b", False), 5), ("same", "same", None, 6)]
        for a, b, cs, tag in cases:
            g.probe(g.node("Text Distance", ("t", a), ("t", b), cs), 0, tag)
    row("frtxtdist", "D8-GH-SETS-0049", "Text Distance",
        "Levenshtein: kitten -> sitting = 3 (k->s, e->i, +g); '' -> abc = 3; flaw -> lawn = 2 (-f, +n); Kitten/kitten = 1 case sensitive, 0 ignoring case; same/same = 0", b_td,
        [(1, "3", "kitten -> sitting = 3"), (2, "3", "'' -> abc = 3"), (3, "2", "flaw -> lawn = 2"),
         (4, "1", "Kitten vs kitten = 1 (case sensitive)"), (5, "0", "Kitten vs kitten = 0 ignoring case"), (6, "0", "same vs same = 0")])
    return rows


SECTION = r'''# ---------------------------------------------------------------------------
# {up}_: Flow Register row {rid} ({title}), flowrows_gen.py. The graph flowrows_{name}.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: {closed}.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_{name}.dflow|g" "$HERE/flowrows_{name}_script.txt" > "$TMPW/flowrows_{name}_script.txt"
if [ -n "${{DISPLAY:-}}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_{name}_script.txt" 2>&1)" || {{ echo "$FR_OUT" | tail -20; echo "FAIL {up}_run: script exited non-zero"; fail=1; FR_OUT=""; }}
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_{name}_script.txt" 2>&1)" || {{ echo "$FR_OUT" | tail -20; echo "FAIL {up}_run: script exited non-zero"; fail=1; FR_OUT=""; }}
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


def main():
    for name, rid, title, closed, build, expect in make_rows():
        g = G()
        build(g)
        nobj = 0
        # one baked object per expected value (an empty expectation bakes nothing)
        for _tag, vals, _msg in expect:
            nobj += len(vals.split()) if vals else 0
        with open(os.path.join(HERE, "flowrows_%s.dflow" % name), "w") as f:
            f.write(g.text())
        with open(os.path.join(HERE, "flowrows_%s_script.txt" % name), "w") as f:
            f.write("# Flow Register row %s (%s): player + bake, read back by sections/94_%s.sh (see flowrows_gen.py)\n"
                    "Grasshopper\nGrasshopperPlayer @GRAPH@ Bake=Yes\n@expect_objects %d\nSelAll\nList\n" % (rid, title, name, nobj))
        checks = "\n".join('fr_chk %d "%s" "%s"' % (tag, vals, msg) for tag, vals, msg in expect)
        with open(os.path.join(HERE, "sections", "94_%s.sh" % name), "w") as f:
            f.write(SECTION.format(up=name.upper(), rid=rid, title=title, name=name, closed=closed, nn=len(g.nodes),
                                   nobj=nobj, checks=checks))
        print(name, rid, "nodes", len(g.nodes), "objects", nobj)


if __name__ == "__main__":
    main()
