#!/usr/bin/env python3
"""Independent oracle for the SUDOC_* sections (tests/sections/104_sudoc_*.sh): document-model rows of the SketchUp register (groups, components,
tags, scenes, DXF layers, 2D drafting). Python standard library only.

Same method as cad2d_rows.py (whose DXF reader and check helpers this module imports): the app builds a small drawing from a command script
(`gen CASE DIR` writes DIR/CASE.txt, @T@ is replaced by DIR), exports DXF files, and `check CASE DIR APPLOG` parses those files itself (group codes
per the DXF reference) and compares them with values worked out here by hand. Closed forms are written next to every expectation, never read back
from the app. The DXF writer prints 9 decimals, so coordinates are compared to 2e-9 (the rounding of the 9th decimal).

Every case also carries at least one DECOY: the same matcher asked for a value that is wrong on purpose must NOT match, so a matcher that
accepts anything fails the section.

  gen CASE DIR            writes DIR/CASE.txt
  check CASE DIR APPLOG   prints "ok   ..." / "FAIL ..." lines, exit status 1 when any FAIL
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cad2d_rows as R  # noqa: E402
from cad2d_rows import PRE, check, close, entities, g, gf, has_circle, has_line, has_poly, lwpolys, spline_points  # noqa: E402

TOL = 2e-9
CASES = {}


def case(name):
    def deco(fn):
        CASES[name] = fn
        return fn
    return deco


# ---------------------------------------------------------------------------------------------------------------- DXF helpers
def lines_of(recs):
    """Undirected LINE keys of records: ((x0, y0), (x1, y1)) with the smaller end first, rounded to 6 decimals."""
    out = set()
    for r in recs:
        a = (round(gf(r, 10), 6), round(gf(r, 20), 6))
        b = (round(gf(r, 11), 6), round(gf(r, 21), 6))
        out.add((min(a, b), max(a, b)))
    return out


def block_rec(path, name):
    for r in R.parse(path).get('BLOCKS', []):
        if r[0] == 'BLOCK' and g(r, 2) == name:
            return r
    return None


def block_base(path, name):
    r = block_rec(path, name)
    return (gf(r, 10), gf(r, 20)) if r is not None else None


def inserts(path, name=None):
    """Sorted (x, y) of the INSERT entities (named `name` when given) in the ENTITIES section."""
    return sorted((gf(r, 10), gf(r, 20)) for r in entities(path, 'INSERT') if name is None or g(r, 2) == name)


def poly_pts(r):
    xs = [float(v) for c, v in r[1] if c == 10]
    ys = [float(v) for c, v in r[1] if c == 20]
    return list(zip(xs, ys))


def spline_by_spans(rec, per_span):
    """Samples a DXF SPLINE (degree group 71, knots 40, control points 10/20, weights 41) with de Boor's algorithm, `per_span` points in EVERY knot span
    (so the short curved spans are sampled as finely as the long straight ones): [(x, y), ...] from the start to the end of the curve."""
    deg = int(g(rec, 71))
    knots = [float(v) for c, v in rec[1] if c == 40]
    xs = [float(v) for c, v in rec[1] if c == 10]
    ys = [float(v) for c, v in rec[1] if c == 20]
    ws = [float(v) for c, v in rec[1] if c == 41] or [1.0] * len(xs)
    cp = [(x * w, y * w, w) for x, y, w in zip(xs, ys, ws)]
    t0, t1 = knots[deg], knots[len(cp)]
    cuts = sorted({k for k in knots if t0 <= k <= t1})

    def at(t):
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
    out = []
    for a, b in zip(cuts, cuts[1:]):
        for i in range(per_span):
            out.append(at(a + (b - a) * i / per_span))
    out.append(at(t1))
    return out


def block_polys(path, name):
    return [poly_pts(r) for r in R.block_entities(path, name, 'LWPOLYLINE')]


def rect_pts(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def layer_table(path):
    """{layer: (flags group 70, colour group 62)} of the LAYER table."""
    return {g(r, 2): (g(r, 70), g(r, 62)) for r in R.parse(path).get('TABLES', []) if r[0] == 'LAYER'}


def header_int(path, var):
    """The integer value (group 70) after the HEADER variable `var`, None when absent."""
    try:
        t = open(path, encoding='latin-1').read().split('\n')
    except OSError:
        return None
    for i in range(0, len(t) - 3, 2):
        if t[i].strip() == '9' and t[i + 1].strip() == var and t[i + 2].strip() == '70':
            return int(t[i + 3].strip())
    return None


def clean(pts):
    """A closed polygon's corners: consecutive duplicates (and a repeated first point) removed, collinear middle points dropped."""
    out = []
    for p in pts:
        if not out or math.dist(out[-1], p) > 1e-9:
            out.append(p)
    if len(out) > 1 and math.dist(out[0], out[-1]) <= 1e-9:
        out.pop()
    keep = []
    n = len(out)
    for i in range(n):
        a, b, c = out[i - 1], out[i], out[(i + 1) % n]
        cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
        if abs(cross) > 1e-9:
            keep.append(b)
    return keep


def shoelace(pts):
    return abs(sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))) / 2


def log_lines(log):
    return log.split('\n')


def log_has(log, text):
    return any(text in line for line in log_lines(log))


def expect_lines(log, name):
    """[(wanted, got, ok)] of the app's own `ok   expect_<name> N (got M)` / `FAIL expect_<name> ...` lines."""
    out = []
    for line in log_lines(log):
        m = re.match(r'^(ok  |FAIL) expect_%s (\d+) \(got (\d+)\)' % name, line)
        if m:
            out.append((int(m.group(2)), int(m.group(3)), m.group(1) == 'ok  '))
    return out


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-ORG-0003
# Make Component = Block: a definition with a name and an insertion point (the block base), one instance replacing the selection.
# Definition members are kept in world coordinates (the base is only the reference point), the instance sits at the base.
@case('comp')
def comp():
    s = PRE + """Circle 20,10,0 4
Line 10,10,0 30,10,0
Line 30,10,0 30,20,0
SelAll
Block
12,8,0
Chair
SelAll
Export @T@/cm1.dxf
Insert Chair 50,60,0
Enter
SelAll
Export @T@/cm2.dxf
"""

    def chk(d, log):
        f1, f2 = d + '/cm1.dxf', d + '/cm2.dxf'
        check(R.block_names(f1) == ['Chair'], 'COMP_1: the BLOCKS section defines exactly one definition, Chair (found %s)' % R.block_names(f1))
        b = block_base(f1, 'Chair')
        check(b is not None and close(b[0], 12) and close(b[1], 8), 'COMP_2: the insertion point (axes origin) stored in the definition is the picked base (12,8) (found %s)' % (b,))
        check(not (b is not None and close(b[0], 13) and close(b[1], 8)), 'COMP_2d: decoy, the base (13,8) is NOT what the definition stores')
        circ = [(gf(r, 10), gf(r, 20), gf(r, 40)) for r in R.block_entities(f1, 'Chair', 'CIRCLE')]
        ls = lines_of(R.block_entities(f1, 'Chair', 'LINE'))
        want = {((10, 10), (30, 10)), ((30, 10), (30, 20))}
        check(len(circ) == 1 and close(circ[0][0], 20) and close(circ[0][1], 10) and close(circ[0][2], 4) and ls == want,
              'COMP_3: the definition holds the 3 selected entities: circle centre (20,10) r 4 and the lines (10,10)-(30,10), (30,10)-(30,20) (found %d circle(s), lines %s)' % (len(circ), sorted(ls)))
        check(inserts(f1) == [(12.0, 8.0)], 'COMP_4: one instance exists, at the base (12,8) (found %s)' % inserts(f1))
        check(not entities(f1, 'LINE') and not entities(f1, 'CIRCLE'), 'COMP_5: the selected entities are gone from the model space: only the instance is left')
        check(inserts(f2, 'Chair') == [(12.0, 8.0), (50.0, 60.0)] and R.block_names(f2) == ['Chair'], 'COMP_6: a second Insert at (50,60) adds an instance of the same single definition (found %s, %s)' % (inserts(f2, 'Chair'), R.block_names(f2)))
        check(log_has(log, "Block 'Chair' defined with 3 object(s)"), 'COMP_7: the command reports the definition with 3 objects')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-ORG-0005
# Make Unique: the third of three instances gets its own copy of the definition; editing that copy leaves the other two alone.
@case('uniq')
def uniq():
    s = PRE + """Rectangle 0,0,0 10,5,0
SelLast
Block
0,0,0
Part
Insert Part 30,0,0
Enter
Insert Part 60,0,0
Enter
SelLast
CreateUniqueBlock Name=PartU
SelLast
BlockEdit
SelLast
Move 0,0,0 0,20,0
BlockEdit
SelAll
Export @T@/uq.dxf
"""

    def chk(d, log):
        f = d + '/uq.dxf'
        check(sorted(R.block_names(f)) == ['Part', 'PartU'], 'UNIQ_1: two definitions exist, Part and its unique copy PartU (found %s)' % sorted(R.block_names(f)))
        check(inserts(f, 'Part') == [(0.0, 0.0), (30.0, 0.0)], 'UNIQ_2: the other two instances still reference Part, at (0,0) and (30,0) (found %s)' % inserts(f, 'Part'))
        check(inserts(f, 'PartU') == [(60.0, 0.0)], 'UNIQ_3: the third instance, at (60,0), references PartU (found %s)' % inserts(f, 'PartU'))
        pp, pu = block_polys(f, 'Part'), block_polys(f, 'PartU')
        check(len(pp) == 1 and has_poly_list(pp, rect_pts(0, 0, 10, 5)), 'UNIQ_4: Part is unchanged by the edit: one rectangle (0,0)-(10,5) (found %s)' % pp)
        check(len(pu) == 1 and has_poly_list(pu, rect_pts(0, 20, 10, 25)), 'UNIQ_5: PartU holds the edited rectangle, moved by +20 in y: (0,20)-(10,25) (found %s)' % pu)
        check(not has_poly_list(pu, rect_pts(0, 0, 10, 5)) and not has_poly_list(pp, rect_pts(0, 20, 10, 25)), 'UNIQ_5d: decoy, the edit reached neither the other definition nor stayed in the copy as the old rectangle')
        check(len(entities(f, 'INSERT')) == 3, 'UNIQ_6: three instances in all (found %d)' % len(entities(f, 'INSERT')))
    return s, chk


def has_poly_list(polys, want):
    return any(R.same_points(p, want) for p in polys)


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-ORG-0009
# Replace Selected: 20 instances of A become instances of B at the same places; A is then unused.
@case('repl')
def repl():
    pts = [(10 * i, 0) for i in range(20)]
    s = PRE + """Rectangle 0,0,0 4,2,0
SelLast
Block
0,0,0
A
"""
    for x, y in pts[1:]:
        s += "Insert A %d,%d,0\nEnter\n" % (x, y)
    s += """Circle 200,50,0 2
SelLast
Block
200,50,0
B
SelBlockInstanceNamed Name=A
ReplaceBlock Name=B
BlockManager
SelAll
Export @T@/rp1.dxf
SelBlockInstance
ExplodeBlock
SelAll
Export @T@/rp2.dxf
"""

    def chk(d, log):
        f1, f2 = d + '/rp1.dxf', d + '/rp2.dxf'
        want = sorted([(float(10 * i), 0.0) for i in range(20)] + [(200.0, 50.0)])
        check(len(entities(f1, 'INSERT', 'ENTITIES')) == 21, 'REPL_1: 21 instances in all, 20 replaced ones and the original B (found %d)' % len(entities(f1, 'INSERT')))
        check(inserts(f1, 'B') == want and not inserts(f1, 'A'), 'REPL_2: every one of the 20 instances now references B at its old place (10 i, 0), i = 0..19, and none references A (found %d B, %d A)' % (len(inserts(f1, 'B')), len(inserts(f1, 'A'))))
        check(R.block_names(f1) == ['B'], 'REPL_3: the old definition A has no instance left (it is not written), B is the only referenced one (found %s)' % R.block_names(f1))
        check(inserts(f1, 'B') != want[:-1], 'REPL_2d: decoy, the list without the original B instance is not the result')
        circ = sorted((round(gf(r, 10), 6), round(gf(r, 20), 6), round(gf(r, 40), 6)) for r in entities(f2, 'CIRCLE'))
        wantc = sorted([(float(10 * i), 0.0, 2.0) for i in range(20)] + [(200.0, 50.0, 2.0)])
        check(circ == wantc and not entities(f2, 'LWPOLYLINE'), 'REPL_4: exploded, the geometry is 21 circles r 2 centred at the instance points (a circle at 10 i, 0, i = 0..19, and (200,50)); no rectangle of A remains (found %d circles, %d polylines)' % (len(circ), len(entities(f2, 'LWPOLYLINE'))))
        check(any(re.search(r"Block 'A': 1 object\(s\), base .*, 0 object\(s\) in instances", x) for x in log_lines(log)) and any(re.search(r"Block 'B': 1 object\(s\), base .*, 21 object\(s\) in instances", x) for x in log_lines(log)),
              'REPL_5: BlockManager lists A as unused (0 objects in instances) and B with 21 (one object per instance)')
        check(log_has(log, "ReplaceBlock: 20 instance(s) now 'B'"), 'REPL_6: the command reports 20 instances replaced')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-ORG-0030
# Statistics: 12 definitions, 500 instances (8 definitions used 41 times, 4 used 43 times: 8 * 41 + 4 * 43 = 500), one object each.
@case('stat')
def stat():
    counts = [41] * 8 + [43] * 4
    s = PRE
    for k, n in enumerate(counts):
        s += "Circle %d,-20,0 1\nSelLast\nBlock\n%d,-20,0\nD%02d\n" % (10 * k, 10 * k, k)
        for j in range(1, n):
            s += "Insert D%02d %d,%d,0\nEnter\n" % (k, 10 * k, 4 * j)
    s += "SelAll\nExport @T@/st.dxf\nBlockManager\n"

    def chk(d, log):
        f = d + '/st.dxf'
        check(sum(counts) == 500 and len(counts) == 12, 'STAT_0: the construction is 12 definitions and 500 instances (8 x 41 + 4 x 43 = %d)' % sum(counts))
        ins = entities(f, 'INSERT')
        check(len(ins) == 500, 'STAT_1: the file holds exactly 500 INSERT entities (found %d)' % len(ins))
        names = R.block_names(f)
        check(len(names) == 12 and sorted(names) == ['D%02d' % k for k in range(12)], 'STAT_2: exactly 12 BLOCK definitions D00..D11 (found %d)' % len(names))
        per = {}
        for r in ins:
            per[g(r, 2)] = per.get(g(r, 2), 0) + 1
        check(per == {'D%02d' % k: n for k, n in enumerate(counts)}, 'STAT_3: the instances per definition are 41 (D00..D07) and 43 (D08..D11) (found %s)' % sorted(per.items()))
        pts = sorted((round(gf(r, 10), 6), round(gf(r, 20), 6)) for r in ins)
        check(len(set(pts)) == 500, 'STAT_4: all 500 instance points are different (found %d distinct)' % len(set(pts)))
        rows = [re.search(r"Block '(\w+)': (\d+) object\(s\), base .*, (\d+) object\(s\) in instances", x) for x in log_lines(log)]
        rows = [m for m in rows if m]
        check(len(rows) == 12, "STAT_5: BlockManager reports 12 definitions (found %d 'Block' lines)" % len(rows))
        check(sum(int(m.group(3)) for m in rows) == 500 and all(m.group(2) == '1' for m in rows), 'STAT_6: and 500 objects in instances in all, one object per definition (found %d)' % sum(int(m.group(3)) for m in rows))
        check(sum(int(m.group(3)) for m in rows) != 501 and len(rows) != 13, 'STAT_6d: decoy, 501 instances or 13 definitions are not what is reported')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-ORG-0014
# Tags (layers): hide one tag, its objects are hidden, the loose geometry on the default tag stays; the tag list shows each tag.
@case('tags')
def tags():
    s = PRE + """NewLayer Walls
Line 0,0,0 10,0,0
Line 0,1,0 10,1,0
NewLayer Doors
Line 0,2,0 10,2,0
Line 0,3,0 10,3,0
SetLayer Default
Line 0,4,0 10,4,0
LayerOff Walls
SelAll
Export @T@/tg1.dxf
LayerOn Walls
LayerOff Doors
SelAll
Export @T@/tg2.dxf
AllLayersOn
SelAll
Export @T@/tg3.dxf
"""

    def chk(d, log):
        f1, f2, f3 = d + '/tg1.dxf', d + '/tg2.dxf', d + '/tg3.dxf'
        check(has_line(f1, 0, 2, 10, 2, 'Doors') and has_line(f1, 0, 3, 10, 3, 'Doors') and has_line(f1, 0, 4, 10, 4, 'Default') and len(entities(f1, 'LINE')) == 3,
              'TAGS_1: with the tag Walls hidden, the two Doors lines and the loose Default line stay visible, and only those three (found %d lines)' % len(entities(f1, 'LINE')))
        check(not has_line(f1, 0, 0, 10, 0) and not has_line(f1, 0, 1, 10, 1), 'TAGS_2: both lines of the hidden tag Walls (y = 0 and y = 1) are gone')
        check(has_line(f2, 0, 0, 10, 0, 'Walls') and has_line(f2, 0, 1, 10, 1, 'Walls') and has_line(f2, 0, 4, 10, 4, 'Default') and len(entities(f2, 'LINE')) == 3 and not has_line(f2, 0, 2, 10, 2) and not has_line(f2, 0, 3, 10, 3),
              'TAGS_3: LayerOn Walls and LayerOff Doors swap which tag is hidden: Walls and Default lines are visible, the two Doors lines are not')
        check(len(entities(f3, 'LINE')) == 5, 'TAGS_4: AllLayersOn shows all five lines again (found %d)' % len(entities(f3, 'LINE')))
        t1, t3 = layer_table(f1), layer_table(f3)
        check(t1.get('Walls', ('0', '7'))[1].startswith('-') and not t1.get('Doors', ('0', '-'))[1].startswith('-') and not t1.get('Default', ('0', '-'))[1].startswith('-'),
              'TAGS_5: the tag table says Walls is off (negative colour in group 62) and Doors and Default are on (found %s)' % ({k: v[1] for k, v in t1.items()},))
        check(set(t3) >= {'Default', 'Walls', 'Doors'} and all(not v[1].startswith('-') for v in t3.values()), 'TAGS_6: the tag list holds Default, Walls and Doors, all on after AllLayersOn (found %s)' % sorted(t3))
        check(has_line(f3, 0, 1, 10, 1, 'Walls') and not has_line(f3, 0, 1, 10, 1, 'Doors'), 'TAGS_6d: decoy, a line is on the tag it was drawn on (y = 1 on Walls, not on Doors)')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-ORG-0016
# Scenes store visible tags: two saved states (S1: A and B on, C off; S2: B and C on, A off), restored in turn.
@case('lstate')
def lstate():
    s = PRE + """NewLayer A
Line 0,0,0 10,0,0
NewLayer B
Line 0,1,0 10,1,0
NewLayer C
Line 0,2,0 10,2,0
SetLayer Default
LayerOff C
LayerState Save S1
AllLayersOn
LayerOff A
LayerState Save S2
AllLayersOn
LayerState Restore S1
SelAll
Export @T@/ls1.dxf
LayerState Restore S2
SelAll
Export @T@/ls2.dxf
LayerState Restore S1
SelAll
Export @T@/ls3.dxf
"""

    def chk(d, log):
        def vis(f):
            return sorted(round(gf(r, 20)) for r in entities(f, 'LINE'))
        a, b, c = vis(d + '/ls1.dxf'), vis(d + '/ls2.dxf'), vis(d + '/ls3.dxf')
        check(a == [0, 1], 'LSTATE_1: restoring S1 shows exactly the tags A (y = 0) and B (y = 1) (found y = %s)' % a)
        check(b == [1, 2], 'LSTATE_2: restoring S2 toggles exactly A off and C on: B (y = 1) and C (y = 2) are visible (found y = %s)' % b)
        check(c == [0, 1], 'LSTATE_3: restoring S1 again gives the first set back (found y = %s)' % c)
        check(b != a and 1 in a and 1 in b, 'LSTATE_3d: decoy, the two scenes differ, and B is visible in both (it is in neither toggled set)')
        check(log_has(log, "Layer state 'S1' saved") and log_has(log, "Layer state 'S2' saved") and log_has(log, "Layer state 'S2' restored"), 'LSTATE_4: the app reports both states saved and restored')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-ORG-0013
# Lock: a locked group cannot be selected, so no edit reaches it; Unlock makes it editable again.
@case('lock')
def lock():
    s = PRE + """Line 0,0,0 10,0,0
Line 0,5,0 10,5,0
Line 0,9,0 10,9,0
SelID 1 2
Group
SelNone
SelGroup
Lock
SelNone
SelID 1
@expect_selected 0
SelGroup
@expect_selected 0
SelAll
@expect_selected 1
Move 0,0,0 0,2,0
SelNone
Unlock
SelAll
Export @T@/lk1.dxf
SelNone
SelGroup
@expect_selected 2
Move 0,0,0 0,1,0
SelAll
Export @T@/lk2.dxf
"""

    def chk(d, log):
        ex = expect_lines(log, 'selected')
        check(ex == [(0, 0, True), (0, 0, True), (1, 1, True), (2, 2, True)], 'LOCK_1: the app reports the selections as expected: the locked group cannot be selected by id (0) or as a group (0), SelAll reaches only the unlocked line (1), and after Unlock the group is selectable (2) (found %s)' % ex)
        f1, f2 = d + '/lk1.dxf', d + '/lk2.dxf'
        check(has_line(f1, 0, 0, 10, 0) and has_line(f1, 0, 5, 10, 5), 'LOCK_2: the locked group is unchanged by the Move that acted on the selectable objects: its lines stay at y = 0 and y = 5')
        check(has_line(f1, 0, 11, 10, 11) and not has_line(f1, 0, 9, 10, 9), 'LOCK_3: the unlocked line did move: y = 9 + 2 = 11')
        check(has_line(f2, 0, 1, 10, 1) and has_line(f2, 0, 6, 10, 6) and has_line(f2, 0, 11, 10, 11) and len(entities(f2, 'LINE')) == 3, 'LOCK_4: after Unlock the group can be edited: Move by +1 gives y = 1 and y = 6, the third line stays at 11')
        check(not has_line(f1, 0, 1, 10, 1), 'LOCK_4d: decoy, the group had not moved while it was locked (no line at y = 1 in the first file)')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-ORG-0018
# Entities inside a group keep their own tag; hiding a tag hides only its entities of the group; loose geometry stays on Default.
@case('grptag')
def grptag():
    s = PRE + """NewLayer A
Line 0,0,0 10,0,0
Line 0,1,0 10,1,0
NewLayer B
Line 0,2,0 10,2,0
Line 0,3,0 10,3,0
SetLayer Default
Line 0,9,0 10,9,0
SelID 1 2 3 4
Group
SelNone
SelGroup
Export @T@/gt1.dxf
SelNone
LayerOff A
SelAll
Export @T@/gt2.dxf
AllLayersOn
LayerOff B
SelAll
Export @T@/gt3.dxf
"""

    def chk(d, log):
        f1, f2, f3 = d + '/gt1.dxf', d + '/gt2.dxf', d + '/gt3.dxf'
        check(has_line(f1, 0, 0, 10, 0, 'A') and has_line(f1, 0, 1, 10, 1, 'A') and has_line(f1, 0, 2, 10, 2, 'B') and has_line(f1, 0, 3, 10, 3, 'B') and len(entities(f1, 'LINE')) == 4,
              'GRPTAG_1: the group holds 4 lines (SelGroup reaches exactly those) and each keeps the tag it was drawn on: y = 0, 1 on A and y = 2, 3 on B')
        check(not has_line(f1, 0, 9, 10, 9), 'GRPTAG_2: the loose line (y = 9) is not part of the group')
        check(has_line(f2, 0, 2, 10, 2, 'B') and has_line(f2, 0, 3, 10, 3, 'B') and has_line(f2, 0, 9, 10, 9, 'Default') and len(entities(f2, 'LINE')) == 3,
              'GRPTAG_3: with tag A hidden only the group entities on B and the loose Default line are visible (3 lines)')
        check(has_line(f3, 0, 0, 10, 0, 'A') and has_line(f3, 0, 1, 10, 1, 'A') and has_line(f3, 0, 9, 10, 9, 'Default') and len(entities(f3, 'LINE')) == 3,
              'GRPTAG_4: with tag B hidden instead, the group entities on A and the loose line are visible (3 lines)')
        check(not has_line(f2, 0, 0, 10, 0) and not has_line(f3, 0, 2, 10, 2), 'GRPTAG_4d: decoy, an entity on a hidden tag is not exported just because its group is visible')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-LAYOUT-0007
# Lines with typed length and angle (AutoCAD polar entry @d<a) and a line style (linetype).
@case('line')
def line():
    s = PRE + """SetCustomLinetype Cut 6,1.5,0.5,1.5
Line 10,10,0 @100<30
SelLast
SetLinetype Cut
SelNone
Line 0,0,0 @50<135
Line 5,5,0 @20<0
SelAll
Export @T@/ln.dxf
"""

    def chk(d, log):
        f = d + '/ln.dxf'
        c30, s30 = math.cos(math.radians(30)), math.sin(math.radians(30))
        check(has_line(f, 10, 10, 10 + 100 * c30, 10 + 100 * s30), 'LINE_1: typed 100 at 30 degrees from (10,10) ends at (10 + 100 cos 30, 10 + 100 sin 30) = (%.9f, %.9f)' % (10 + 100 * c30, 10 + 100 * s30))
        c135, s135 = math.cos(math.radians(135)), math.sin(math.radians(135))
        check(has_line(f, 0, 0, 50 * c135, 50 * s135), 'LINE_2: typed 50 at 135 degrees from the origin ends at (50 cos 135, 50 sin 135) = (%.9f, %.9f)' % (50 * c135, 50 * s135))
        check(has_line(f, 5, 5, 25, 5), 'LINE_3: typed 20 at 0 degrees from (5,5) ends at (25,5)')
        check(not has_line(f, 10, 10, 10 + 100 * s30, 10 + 100 * c30), 'LINE_3d: decoy, sine and cosine swapped (angle 60 degrees) is not what was drawn')
        ln = [r for r in entities(f, 'LINE') if close(gf(r, 10), 10) and close(gf(r, 20), 10)]
        others = [r for r in entities(f, 'LINE') if not (close(gf(r, 10), 10) and close(gf(r, 20), 10))]
        check(len(ln) == 1 and g(ln[0], 6) == 'Cut' and all(g(r, 6) is None for r in others), 'LINE_4: the line style is applied to that line only: group 6 = Cut on the typed line (found %s), none on the other two' % (g(ln[0], 6) if ln else None))
        lt = [r for r in R.parse(f).get('TABLES', []) if r[0] == 'LTYPE' and g(r, 2) == 'Cut']
        pat = [float(v) for c, v in lt[0][1] if c == 49] if lt else []
        check(pat == [6.0, -1.5, 0.5, -1.5] and lt and close(gf(lt[0], 40), 9.5), 'LINE_5: the style Cut is in the linetype table as dash 6, gap 1.5, dot 0.5, gap 1.5 with total length 9.5 (found %s)' % (pat,))
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-LAYOUT-0010
# A 100 x 60 rectangle with corner radius 8: the exact rounded rectangle. The exported exact rational SPLINE is evaluated with de Boor's algorithm
# here; every sample must lie on a side line or on a quarter circle of radius 8 about (8,8), (92,8), (92,52), (8,52) (so each arc is tangent to the
# two sides it joins), and the area and perimeter equal the closed forms.
@case('rrect')
def rrect():
    s = PRE + """Rectangle 0,0,0 100,60,0
SelLast
FilletCorners
8
SelAll
Export @T@/rr.dxf
"""

    def chk(d, log):
        f = d + '/rr.dxf'
        sp = entities(f, 'SPLINE')
        check(len(sp) == 1, 'RRECT_1: FilletCorners turns the rectangle into one closed curve: one SPLINE in the file (found %d)' % len(sp))
        if len(sp) != 1:
            return
        r = 8.0
        pts = spline_by_spans(sp[0], 20000)
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        check(close(min(xs), 0, 1e-7) and close(max(xs), 100, 1e-7) and close(min(ys), 0, 1e-7) and close(max(ys), 60, 1e-7), 'RRECT_2: the bounding box is exactly (0,0)-(100,60) (found x %.9f..%.9f, y %.9f..%.9f)' % (min(xs), max(xs), min(ys), max(ys)))
        worst, on_arc = 0.0, 0
        for x, y in pts:
            cx = 8 if x < 8 else (92 if x > 92 else None)
            cy = 8 if y < 8 else (52 if y > 52 else None)
            if cx is not None and cy is not None:
                worst = max(worst, abs(math.hypot(x - cx, y - cy) - r))
                on_arc += 1
            else:
                worst = max(worst, min(abs(x), abs(x - 100), abs(y), abs(y - 60)))
        check(worst < 5e-8 and on_arc > 50000, 'RRECT_3: every sample is on a side or on a radius-8 quarter circle about a corner centre (worst residual %.3g, bound 5e-8 from the 9 decimals of the file; %d of %d samples on arcs)' % (worst, on_arc, len(pts)))
        area, per = R.polygon_area_perimeter(pts)
        want_a, want_p = 6000 - (4 - math.pi) * r * r, 320 - 8 * r + 2 * math.pi * r
        # the sampled polygon is inscribed: its area is short by about 4 x 20000 x (2/3) s^3 / (8 r) with s = (pi r / 2) / 20000 = 6.3e-4, i.e. 2e-7
        check(abs(area - want_a) < 1e-5 and abs(per - want_p) < 1e-5, 'RRECT_4: area 6000 - (4 - pi) 64 = %.6f and perimeter 320 - 64 + 16 pi = %.6f (found %.6f and %.6f; bound 1e-5, the inscribed polygon of 20000 samples per span is short by about 2e-7)' % (want_a, want_p, area, per))
        check(abs(area - 6000) > 1.0 and abs(per - 320) > 1.0, 'RRECT_4d: decoy, the rounded shape is not the sharp 100 x 60 rectangle (area 6000, perimeter 320)')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-LAYOUT-0011
# Typed circle r = 25, ellipse 60 x 30 (semi-axes 30 and 15), hexagon inscribed in the radius-25 circle.
@case('shapes')
def shapes():
    s = PRE + """Circle 0,0,0 25
Ellipse 100,0,0 130,0,0 100,15,0
Polygon NumSides=6 200,0,0 225,0,0
SelAll
Export @T@/sh.dxf
"""

    def chk(d, log):
        f = d + '/sh.dxf'
        check(has_circle(f, 0, 0, 25), 'SHAPES_1: the circle has centre (0,0) and radius 25 (area pi 625 = %.9f)' % (math.pi * 625))
        check(not has_circle(f, 0, 0, 24), 'SHAPES_1d: decoy, radius 24 is not found')
        el, sp = entities(f, 'ELLIPSE'), entities(f, 'SPLINE')
        ok_major = False
        if el:
            r = el[0]
            mx, my = gf(r, 11), gf(r, 21)
            major = math.hypot(mx, my)
            ok_major = close(gf(r, 10), 100, 1e-8) and close(gf(r, 20), 0, 1e-8) and close(major, 30, 1e-8) and abs(my) < 1e-8 and close(gf(r, 40) * major, 15, 1e-8)
            area = math.pi * major * gf(r, 40) * major
            check(ok_major and abs(area - math.pi * 450) < 1e-6, 'SHAPES_2: the ellipse is centred (100,0) with semi-axes 30 and 15 (ratio 0.5): area pi 30 15 = %.6f (found %s)' % (math.pi * 450, ok_major))
        elif sp:
            pts = spline_points(sp[0], 4000)
            worst = max(abs(((x - 100) / 30) ** 2 + (y / 15) ** 2 - 1) for x, y in pts)
            area = R.polygon_area_perimeter(pts)[0]
            check(worst < 1e-8 and abs(area - math.pi * 450) < 1e-3, 'SHAPES_2: the ellipse curve satisfies ((x-100)/30)^2 + (y/15)^2 = 1 (worst %.3g) and has area pi 450 = %.6f (found %.6f)' % (worst, math.pi * 450, area))
        else:
            check(False, 'SHAPES_2: no ELLIPSE or SPLINE in the file')
        want = [(200 + 25 * math.cos(math.radians(60 * k)), 25 * math.sin(math.radians(60 * k))) for k in range(6)]
        check(has_poly(f, want), 'SHAPES_3: the hexagon inscribed in the radius-25 circle about (200,0) has the corners (200 + 25 cos 60k, 25 sin 60k), k = 0..5')
        polys = [p for _, p in lwpolys(f) if len(p) == 6]
        area6 = shoelace(polys[0]) if polys else 0.0
        check(abs(area6 - 3 * math.sqrt(3) / 2 * 625) < 1e-6, 'SHAPES_4: its area is 3 sqrt 3 / 2 s^2 with the side s = 25: %.6f (found %.6f)' % (3 * math.sqrt(3) / 2 * 625, area6))
        check(not has_poly(f, [(200 + 24 * math.cos(math.radians(60 * k)), 24 * math.sin(math.radians(60 * k))) for k in range(6)]), 'SHAPES_4d: decoy, a hexagon of circumradius 24 is not found')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-LAYOUT-0013
# Offset a 100 x 60 rectangle by 5 inside (90 x 50) and outside (110 x 70); an open line offset to both sides.
@case('offs')
def offs():
    s = PRE + """Rectangle 0,0,0 100,60,0
SelLast
Offset Distance=5 50,30,0
Rectangle 200,0,0 300,60,0
SelLast
Offset Distance=5 350,30,0
Line 0,100,0 100,100,0
SelLast
Offset Distance=5 BothSides=Yes 50,110,0
SelAll
Export @T@/of.dxf
"""

    def chk(d, log):
        f = d + '/of.dxf'
        polys = [clean(p) for _, p in lwpolys(f)]
        check(any(R.same_points(p, rect_pts(5, 5, 95, 55)) for p in polys), 'OFFS_1: offset 5 to the inside gives the 90 x 50 rectangle (5,5)-(95,55)')
        check(any(R.same_points(p, rect_pts(195, -5, 305, 65)) for p in polys), 'OFFS_2: offset 5 to the outside gives the 110 x 70 rectangle (195,-5)-(305,65)')
        check(has_line(f, 0, 105, 100, 105) and has_line(f, 0, 95, 100, 95) and has_line(f, 0, 100, 100, 100), 'OFFS_3: the open line offset to both sides leaves the line and adds (0,105)-(100,105) and (0,95)-(100,95)')
        check(sum(1 for p in polys if len(p) == 4) == 4, 'OFFS_4: four rectangles in all (the two sources and the two offsets) (found %d)' % sum(1 for p in polys if len(p) == 4))
        check(not any(R.same_points(p, rect_pts(-5, -5, 105, 65)) for p in polys), 'OFFS_4d: decoy, the outward offset of the first rectangle was not made (the inside pick offsets inward)')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-LAYOUT-0015
# Extend four lines (two horizontal, two sloping) to a sloping boundary y = 0.4 (x - 20): the new end points lie on the boundary.
@case('ext')
def ext():
    s = PRE + """Line 0,5,0 10,5,0
Line 0,8,0 10,8,0
Line 0,30,0 10,20,0
Line 0,45,0 10,35,0
Line 20,0,0 80,24,0
SelNone
Extend Boundary SelLast Enter 9,5,0 9,8,0 9,21,0 9,36,0 Enter
SelAll
Export @T@/ex.dxf
"""

    def chk(d, log):
        f = d + '/ex.dxf'
        # boundary y = 0.4 (x - 20). Horizontal y0: x = 20 + y0 / 0.4. Line (t, c - t): c - t = 0.4 (t - 20) -> t = (c + 8) / 1.4.
        hit1 = (20 + 5 / 0.4, 5)
        hit2 = (20 + 8 / 0.4, 8)
        t3 = (30 + 8) / 1.4
        t4 = (45 + 8) / 1.4
        want = [((0, 5), hit1), ((0, 8), hit2), ((0, 30), (t3, 30 - t3)), ((0, 45), (t4, 45 - t4))]
        for i, (a, b) in enumerate(want, 1):
            check(has_line(f, a[0], a[1], b[0], b[1]), 'EXT_%d: the line from (%g,%g) is extended to (%.9f, %.9f) on the boundary' % (i, a[0], a[1], b[0], b[1]))
        dev = 0.0
        for _, b in want:
            dev = max(dev, abs(b[1] - 0.4 * (b[0] - 20)) / math.hypot(0.4, 1))
        check(dev < 1e-12, 'EXT_5: the four expected end points lie on the boundary line (distance %.3g)' % dev)
        check(has_line(f, 20, 0, 80, 24) and len(entities(f, 'LINE')) == 5, 'EXT_6: the boundary is unchanged and there are 5 lines (found %d)' % len(entities(f, 'LINE')))
        check(not has_line(f, 0, 5, 10, 5) and not has_line(f, 0, 30, 10, 20) and not has_line(f, 0, 8, 10, 8) and not has_line(f, 0, 45, 10, 35), 'EXT_6d: decoy, none of the original short lines (to x = 10) is left behind')
        if R.failed:
            print('note EXT: lines in the file: %s' % sorted((round(gf(r, 10), 6), round(gf(r, 20), 6), round(gf(r, 11), 6), round(gf(r, 21), 6)) for r in entities(f, 'LINE')))
            print('note EXT: app lines: %s' % [x for x in log_lines(log) if 'Extend' in x][:12])
    return s, chk


# The same four lines extended with one Extend command per line (one pick each).
@case('ext1')
def ext1():
    s = PRE + """Line 0,5,0 10,5,0
Line 0,8,0 10,8,0
Line 0,30,0 10,20,0
Line 0,45,0 10,35,0
Line 20,0,0 80,24,0
"""
    for pick in ('9,5,0', '9,8,0', '9,21,0', '9,36,0'):
        s += "SelNone\nExtend Boundary SelLast Enter %s Enter\n" % pick
    s += "SelAll\nExport @T@/ex1.dxf\n"

    def chk(d, log):
        f = d + '/ex1.dxf'
        t3, t4 = (30 + 8) / 1.4, (45 + 8) / 1.4
        want = [((0, 5), (20 + 5 / 0.4, 5)), ((0, 8), (20 + 8 / 0.4, 8)), ((0, 30), (t3, 30 - t3)), ((0, 45), (t4, 45 - t4))]
        for i, (a, b) in enumerate(want, 1):
            check(has_line(f, a[0], a[1], b[0], b[1]), 'EXT1_%d: the line from (%g,%g) is extended to (%.9f, %.9f) on the boundary y = 0.4 (x - 20)' % (i, a[0], a[1], b[0], b[1]))
        check(has_line(f, 20, 0, 80, 24) and len(entities(f, 'LINE')) == 5, 'EXT1_5: the boundary is unchanged and there are 5 lines (found %d)' % len(entities(f, 'LINE')))
        check(not has_line(f, 0, 45, 10, 35) and not has_line(f, 0, 30, 10, 20), 'EXT1_5d: decoy, the original sloping lines (to x = 10) are not left behind')
        if R.failed:
            print('note EXT1: lines in the file: %s' % sorted((round(gf(r, 10), 6), round(gf(r, 20), 6), round(gf(r, 11), 6), round(gf(r, 21), 6)) for r in entities(f, 'LINE')))
            print('note EXT1: app lines: %s' % [x for x in log_lines(log) if 'Extend' in x][:12])
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-LAYOUT-0016
# Fillet radius 10 between perpendicular lines meeting at K = (50,30) along u = (cos 30, sin 30) and v = (-sin 30, cos 30).
C30, S30 = math.cos(math.radians(30)), math.sin(math.radians(30))
K = (50.0, 30.0)
U, V = (C30, S30), (-S30, C30)


def at(t, w):
    return (K[0] + t * w[0], K[1] + t * w[1])


def angeq(a, b, tol=1e-6):
    """Two angles in degrees are the same direction (modulo 360)."""
    return abs((a - b + 180.0) % 360.0 - 180.0) <= tol


def fmt(p):
    return '%r,%r,0' % (p[0], p[1])


@case('fil')
def fil():
    s = PRE + "Line %s %s\nLine %s %s\nSelNone\nFillet\n10\n%s\n%s\nSelAll\nExport @T@/fl.dxf\n" % (fmt(K), fmt(at(40, U)), fmt(K), fmt(at(40, V)), fmt(at(20, U)), fmt(at(20, V)))

    def chk(d, log):
        f = d + '/fl.dxf'
        ar = R.arcs(f)
        c = (K[0] + 10 * (U[0] + V[0]), K[1] + 10 * (U[1] + V[1]))
        check(len(ar) == 1 and close(gf(ar[0], 10), c[0]) and close(gf(ar[0], 20), c[1]) and close(gf(ar[0], 40), 10), 'FIL_1: one arc, radius 10 and centre K + 10 u + 10 v = (%.9f, %.9f) (found %d arcs)' % (c[0], c[1], len(ar)))
        check(len(ar) == 1 and angeq(gf(ar[0], 50), 210) and angeq(gf(ar[0], 51), 300), 'FIL_2: it runs counter-clockwise from 210 to 300 degrees: the directions from the centre to the tangent points are -u (210) and -v (300)')
        p1, p2 = at(10, U), at(10, V)
        check(has_line(f, p1[0], p1[1], at(40, U)[0], at(40, U)[1]) and has_line(f, p2[0], p2[1], at(40, V)[0], at(40, V)[1]), 'FIL_3: the lines are trimmed to the tangent points K + 10 u = (%.9f, %.9f) and K + 10 v = (%.9f, %.9f)' % (p1[0], p1[1], p2[0], p2[1]))
        d1 = abs((c[0] - K[0]) * U[1] - (c[1] - K[1]) * U[0])
        d2 = abs((c[0] - K[0]) * V[1] - (c[1] - K[1]) * V[0])
        check(close(d1, 10, 1e-12) and close(d2, 10, 1e-12), 'FIL_4: the centre is 10 from both lines (distances %.12f and %.12f), so the arc touches each line in exactly one point' % (d1, d2))
        check(not any(close(gf(a, 40), 5) for a in ar) and len(entities(f, 'LINE')) == 2, 'FIL_4d: decoy, there is no radius-5 arc and only the two trimmed lines remain (found %d lines)' % len(entities(f, 'LINE')))
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-LAYOUT-0017
@case('chm')
def chm():
    s = PRE + "Line %s %s\nLine %s %s\nChamfer Distance=10 Distance2=10 %s %s\nSelAll\nExport @T@/ch.dxf\n" % (fmt(K), fmt(at(40, U)), fmt(K), fmt(at(40, V)), fmt(at(20, U)), fmt(at(20, V)))

    def chk(d, log):
        f = d + '/ch.dxf'
        p1, p2 = at(10, U), at(10, V)
        check(has_line(f, p1[0], p1[1], p2[0], p2[1]), 'CHM_1: the new segment joins K + 10 u = (%.9f, %.9f) and K + 10 v = (%.9f, %.9f)' % (p1[0], p1[1], p2[0], p2[1]))
        check(close(math.dist(p1, K), 10, 1e-12) and close(math.dist(p2, K), 10, 1e-12) and close(math.dist(p1, p2), 10 * math.sqrt(2), 1e-12), 'CHM_2: its end points are 10 from the corner along each line and it is 10 sqrt 2 long')
        check(has_line(f, p1[0], p1[1], at(40, U)[0], at(40, U)[1]) and has_line(f, p2[0], p2[1], at(40, V)[0], at(40, V)[1]), 'CHM_3: both lines are trimmed to those end points')
        check(len(entities(f, 'LINE')) == 3, 'CHM_3d: decoy, exactly three lines (two trimmed lines and the chamfer) remain (found %d)' % len(entities(f, 'LINE')))
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-LAYOUT-0021
# Align Left: five objects of different widths at different x end with the same minimum x (the smallest of them, 2.5).
@case('align')
def align():
    s = PRE + """Rectangle 7,0,0 17,4,0
Line 3.5,10,0 12.5,12,0
Rectangle 12,20,0 30,25,0
Line 2.5,30,0 4.5,36,0
Rectangle 20,40,0 28,41.5,0
SelAll
Align
Left
SelAll
Export @T@/al.dxf
"""

    def chk(d, log):
        f = d + '/al.dxf'
        polys = [p for _, p in lwpolys(f)]
        lns = entities(f, 'LINE')
        mins = [min(x for x, _ in p) for p in polys] + [min(gf(r, 10), gf(r, 11)) for r in lns]
        check(len(mins) == 5 and all(close(m, 2.5) for m in mins), 'ALIGN_1: all five objects have the minimum x 2.5 (the smallest of 7, 3.5, 12, 2.5, 20) (found %s)' % sorted(round(m, 9) for m in mins))
        check(has_poly(f, rect_pts(2.5, 0, 12.5, 4)) and has_poly(f, rect_pts(2.5, 20, 20.5, 25)) and has_poly(f, rect_pts(2.5, 40, 10.5, 41.5)), 'ALIGN_2: the rectangles keep their size and y: 10 x 4 at y 0, 18 x 5 at y 20, 8 x 1.5 at y 40, shifted in x only')
        check(has_line(f, 2.5, 10, 11.5, 12) and has_line(f, 2.5, 30, 4.5, 36), 'ALIGN_3: the lines keep their length and y: (3.5,10)-(12.5,12) moved to (2.5,10)-(11.5,12), the leftmost line (2.5,30)-(4.5,36) did not move')
        check(not any(close(m, 7) or close(m, 20) for m in mins), 'ALIGN_3d: decoy, no object stayed at its old minimum x (7 or 20)')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-IO-0004
# 20 tags, each with one entity (7 lines, 7 circles, 6 rectangles), exported to DXF, read back and exported again: layers, counts, coordinates.
def dxf_model():
    items = []   # (layer, kind, data)
    for i in range(20):
        name = 'Tag%02d' % (i + 1)
        if i < 7:
            items.append((name, 'LINE', (i * 10 + 0.123456789, 1.5, i * 10 + 7.987654321, 9.25)))
        elif i < 14:
            items.append((name, 'CIRCLE', (i * 10 + 0.5, 3.25, 1.75 + 0.125 * (i - 7))))
        else:
            items.append((name, 'RECT', (i * 10 + 1.0, 2.0, i * 10 + 6.5, 8.25)))
    return items


@case('dxflay')
def dxflay():
    s = PRE
    for name, kind, v in dxf_model():
        s += "NewLayer %s\n" % name
        if kind == 'LINE':
            s += "Line %r,%r,0 %r,%r,0\n" % v
        elif kind == 'CIRCLE':
            s += "Circle %r,%r,0 %r\n" % v
        else:
            s += "Rectangle %r,%r,0 %r,%r,0\n" % v
    s += "SelAll\nExport @T@/dl1.dxf\nNew\nOpen @T@/dl1.dxf\nSelAll\nExport @T@/dl2.dxf\n"

    def chk(d, log):
        for tag, f in (('file', d + '/dl1.dxf'), ('round trip', d + '/dl2.dxf')):
            lt = layer_table(f)
            names = ['Tag%02d' % (i + 1) for i in range(20)]
            check(all(n in lt for n in names), 'DXFLAY_1 (%s): the 20 tags Tag01..Tag20 are 20 layers of the file (found %d of them)' % (tag, sum(1 for n in names if n in lt)))
            ok = True
            bad = []
            for name, kind, v in dxf_model():
                if kind == 'LINE':
                    hit = [r for r in entities(f, 'LINE') if g(r, 8) == name and close(gf(r, 10), v[0], 1e-6) and close(gf(r, 20), v[1], 1e-6) and close(gf(r, 11), v[2], 1e-6) and close(gf(r, 21), v[3], 1e-6)]
                elif kind == 'CIRCLE':
                    hit = [r for r in entities(f, 'CIRCLE') if g(r, 8) == name and close(gf(r, 10), v[0], 1e-6) and close(gf(r, 20), v[1], 1e-6) and close(gf(r, 40), v[2], 1e-6)]
                else:
                    hit = [r for r in entities(f, 'LWPOLYLINE') if g(r, 8) == name and R.same_points(poly_pts(r), rect_pts(*v), 1e-6)]
                if len(hit) != 1:
                    ok = False
                    bad.append(name)
            check(ok, 'DXFLAY_2 (%s): every tag has exactly its one entity, with the coordinates of the drawing within 1e-6 (wrong: %s)' % (tag, bad))
            n = len(entities(f, 'LINE')) + len(entities(f, 'CIRCLE')) + len(entities(f, 'LWPOLYLINE'))
            check(n == 20, 'DXFLAY_3 (%s): 20 entities in all: 7 lines, 7 circles, 6 rectangles (found %d, %d, %d, %d)' % (tag, n, len(entities(f, 'LINE')), len(entities(f, 'CIRCLE')), len(entities(f, 'LWPOLYLINE'))))
            check(header_int(f, '$INSUNITS') == 4, 'DXFLAY_4 (%s): the header unit is 4 (millimetres, DXF reference table of $INSUNITS) for the default millimetre document (found %s)' % (tag, header_int(f, '$INSUNITS')))
        f = d + '/dl1.dxf'
        check(not any(g(r, 8) == 'Tag01' for r in entities(f, 'CIRCLE')) and header_int(f, '$INSUNITS') != 6, 'DXFLAY_4d: decoy, Tag01 holds no circle and the unit is not 6 (metres)')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-LAYOUT-0022
# Standard editing: Paste in place reproduces the source coordinates exactly (Cut writes the clipboard, Paste reads it back where it was), Lock keeps an
# object out of reach of edits, Delete (erase) removes the selection, Undo restores it. Coordinates have 9 decimals, so the 9-decimal DXF is exact.
EDIT_LINES = [(0.123456789, 3.25, 17.5, 8.765432109), (2.5, -4.75, 11.111111111, 6.5), (-3.3, 0.7, 4.4, -9.987654321)]


@case('edit')
def edit():
    s = PRE
    for a, b, c, d in EDIT_LINES:
        s += "Line %r,%r,0 %r,%r,0\n" % (a, b, c, d)
    s += """SelAll
Cut
@expect_objects 0
Paste
@expect_objects 3
SelAll
Export @T@/ed1.dxf
SelNone
SelLast
Lock
SelAll
@expect_selected 2
Delete
@expect_objects 1
Undo
@expect_objects 3
Unlock
SelAll
Export @T@/ed2.dxf
"""

    def chk(d, log):
        f1, f2 = d + '/ed1.dxf', d + '/ed2.dxf'
        ex = expect_lines(log, 'objects')
        check(ex == [(0, 0, True), (3, 3, True), (1, 1, True), (3, 3, True)], 'EDIT_1: the object counts the app reports are 0 after Cut, 3 after Paste, 1 after Delete of the 2 selectable objects (the locked one stays) and 3 after Undo (found %s)' % ex)
        ex = expect_lines(log, 'selected')
        check(ex == [(2, 2, True)], 'EDIT_2: with the last line locked, SelAll reaches only the other 2 (found %s)' % ex)
        check(all(has_line(f1, *ln) for ln in EDIT_LINES) and len(entities(f1, 'LINE')) == 3, 'EDIT_3: Paste in place gives back the three lines at their coordinates, 9 decimals (found %d lines)' % len(entities(f1, 'LINE')))
        check(all(has_line(f2, *ln) for ln in EDIT_LINES) and len(entities(f2, 'LINE')) == 3, 'EDIT_4: after Delete and Undo the same three lines are there again, unchanged (found %d lines)' % len(entities(f2, 'LINE')))
        a, b, c2, d2 = EDIT_LINES[0]
        check(not has_line(f1, a + 1e-6, b, c2, d2) and not has_line(f1, a, b, c2, d2 + 1e-6), 'EDIT_4d: decoy, a line moved by 1e-6 is not what the file holds')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-LAYOUT-0018
# Join of overlapping shapes: the planar union of the rectangles (0,0)-(60,40) and (40,20)-(100,70) is the 8-corner outline
# (0,0) (60,0) (60,20) (100,20) (100,70) (40,70) (40,40) (0,40): area 60 40 + 60 50 - 20 20 = 5000, perimeter 340.
@case('join')
def join():
    s = PRE + """Rectangle 0,0,0 60,40,0
Rectangle 40,20,0 100,70,0
SelNone
SelID 1 2
PlanarUnion
SelLast
Export @T@/jn.dxf
"""

    def chk(d, log):
        f = d + '/jn.dxf'
        polys = [clean(p) for _, p in lwpolys(f)]
        want = [(0, 0), (60, 0), (60, 20), (100, 20), (100, 70), (40, 70), (40, 40), (0, 40)]
        check(len(polys) == 1 and R.same_points(polys[0], want), 'JOIN_1: the result is one closed outline with the 8 corners (0,0) (60,0) (60,20) (100,20) (100,70) (40,70) (40,40) (0,40) (found %s)' % (polys,))
        if len(polys) == 1:
            p = polys[0]
            per = sum(math.dist(p[i], p[(i + 1) % len(p)]) for i in range(len(p)))
            check(abs(shoelace(p) - 5000) < 1e-6 and abs(per - 340) < 1e-6, 'JOIN_2: its area is 60 40 + 60 50 - 20 20 = 5000 and its perimeter 340 (found %.6f and %.6f)' % (shoelace(p), per))
        check(not any(R.same_points(p, rect_pts(0, 0, 100, 70)) for p in polys) and len(polys) == 1, 'JOIN_2d: decoy, the result is not the bounding rectangle of the two shapes')
    return s, chk


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    mode, name, d = sys.argv[1], sys.argv[2], sys.argv[3]
    if name not in CASES:
        print('FAIL sudoc_rows: unknown case %s' % name)
        return 1
    script, chk = CASES[name]()
    if mode == 'gen':
        open('%s/%s.txt' % (d, name), 'w').write(script.replace('@T@', d))
        return 0
    chk(d, R.app_log(sys.argv[4]) if len(sys.argv) > 4 else '')
    return 1 if R.failed else 0


if __name__ == '__main__':
    sys.exit(main())
