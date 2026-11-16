#!/usr/bin/env python3
"""Independent oracle for the CAD2D row sections (tests/sections/97_cad2d_*.sh). Python standard library only.

The app builds a small 2D drawing from a command script (cad2d_rows.py gen CASE DIR writes DIR/CASE.txt) and exports DXF files;
cad2d_rows.py check CASE DIR APPLOG parses those DXF files itself (group-code / value pairs, DXF reference) and compares them with values
computed here by hand: reflections, rotations, scalings and fillet tangent points are closed forms (written next to every expectation),
never read back from the app. The DXF writer prints 9 decimals, so coordinates are compared to 2e-9 (rounding of the 9th decimal).

  gen CASE DIR            writes DIR/CASE.txt (the app script; @T@ is replaced by DIR)
  check CASE DIR APPLOG   prints "ok   ..." / "FAIL ..." lines, exit status 1 when any FAIL
"""
import math
import sys

TOL = 2e-9
failed = 0


def check(cond, what):
    global failed
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        failed += 1


# ---------------------------------------------------------------------------------------------------------------- DXF reading
def parse(path):
    """-> {section: [(type, [(code, value), ...]), ...]}; a record starts at every group 0 (DXF reference, group codes)."""
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


def g(rec, code, default=None):
    for c, v in rec[1]:
        if c == code:
            return v
    return default


def gf(rec, code):
    v = g(rec, code)
    return float(v) if v is not None else None


def entities(path, kind, section='ENTITIES'):
    return [r for r in parse(path).get(section, []) if r[0] == kind]


def block_entities(path, name, kind):
    """Entities of the BLOCKS section between BLOCK (group 2 = name) and ENDBLK."""
    out, inside = [], False
    for r in parse(path).get('BLOCKS', []):
        if r[0] == 'BLOCK':
            inside = g(r, 2) == name
        elif r[0] == 'ENDBLK':
            inside = False
        elif inside and r[0] == kind:
            out.append(r)
    return out


def block_names(path):
    return [g(r, 2) for r in parse(path).get('BLOCKS', []) if r[0] == 'BLOCK' and not (g(r, 2) or '').startswith('*')]


def layer_names(path):
    return [g(r, 2) for r in parse(path).get('TABLES', []) if r[0] == 'LAYER']


def line_key(r):
    return (gf(r, 10), gf(r, 20), gf(r, 11), gf(r, 21))


def close(a, b, tol=TOL):
    return a is not None and b is not None and abs(a - b) <= tol


def has_line(path, x0, y0, x1, y1, layer=None, section='ENTITIES'):
    for r in entities(path, 'LINE', section):
        k = line_key(r)
        fwd = all(close(p, q) for p, q in zip(k, (x0, y0, x1, y1)))
        rev = all(close(p, q) for p, q in zip(k, (x1, y1, x0, y0)))  # a segment is the same segment from either end
        if (fwd or rev) and (layer is None or g(r, 8) == layer):
            return True
    return False


def has_circle(path, cx, cy, rad, layer=None):
    for r in entities(path, 'CIRCLE'):
        if close(gf(r, 10), cx) and close(gf(r, 20), cy) and close(gf(r, 40), rad) and (layer is None or g(r, 8) == layer):
            return True
    return False


def arcs(path):
    return entities(path, 'ARC')


def spline_points(rec, n):
    """Samples a DXF SPLINE (degree group 71, knots group 40, control points 10/20, weights 41) with de Boor's algorithm."""
    deg = int(g(rec, 71))
    knots = [float(v) for c, v in rec[1] if c == 40]
    xs = [float(v) for c, v in rec[1] if c == 10]
    ys = [float(v) for c, v in rec[1] if c == 20]
    ws = [float(v) for c, v in rec[1] if c == 41] or [1.0] * len(xs)
    cp = [(x * w, y * w, w) for x, y, w in zip(xs, ys, ws)]
    t0, t1 = knots[deg], knots[len(cp)]
    out = []
    for i in range(n + 1):
        t = t0 + (t1 - t0) * i / n
        k = deg
        while k < len(cp) - 1 and knots[k + 1] <= t:
            k += 1
        d = [list(cp[j + k - deg]) for j in range(deg + 1)]
        for r in range(1, deg + 1):
            for j in range(deg, r - 1, -1):
                den = knots[j + 1 + k - r] - knots[j + k - deg]
                a = (t - knots[j + k - deg]) / den if den else 0.0
                d[j] = [(1 - a) * d[j - 1][m] + a * d[j][m] for m in range(3)]
        out.append((d[deg][0] / d[deg][2], d[deg][1] / d[deg][2]))
    return out


def polygon_area_perimeter(pts):
    a = per = 0.0
    for i in range(len(pts) - 1):
        (x0, y0), (x1, y1) = pts[i], pts[i + 1]
        a += x0 * y1 - x1 * y0
        per += math.hypot(x1 - x0, y1 - y0)
    return abs(a) / 2, per


def app_log(path):
    try:
        return open(path, encoding='latin-1').read()
    except OSError:
        return ''


# ---------------------------------------------------------------------------------------------------------------- cases
PRE = "SetActiveViewport Top\nNew\n"
CASES = {}


def case(name):
    def deco(fn):
        CASES[name] = fn
        return fn
    return deco


# MODIFY-0003 Rotate -------------------------------------------------------------------------------------------------------
# Rotation by t about c: p' = c + R(t) (p - c) (a counter-clockwise turn about the Z axis, DXF/Euclid convention).
@case('rotate')
def rotate():
    s = PRE + """Line 1,0,0 2,0,0
SelLast
Rotate 0,0,0 90
Circle 5,0,0 1
SelLast
Rotate 0,0,0 -90
Line 0,50,0 10,50,0
SelLast
Rotate 0,50,0 8.660254037844386,55,0 0,60,0
Line 100,0,0 101,0,0
SelLast
Rotate Copy=Yes 100,0,0 45
Line 200,0,0 204,0,0
SelLast
Rotate 200,0,0 180
SelAll
Export @T@/rot.dxf
"""

    def chk(d, log):
        f = d + '/rot.dxf'
        # (1,0)-(2,0) turned 90 degrees about the origin: (x,y) -> (-y,x) gives (0,1)-(0,2)
        check(has_line(f, 0, 1, 0, 2), 'ROT_1: Rotate 90 about (0,0) maps the line (1,0)-(2,0) to (0,1)-(0,2)')
        # circle centre (5,0) turned -90 degrees: (x,y) -> (y,-x) gives (0,-5); the radius stays 1
        check(has_circle(f, 0, -5, 1), 'ROT_2: Rotate -90 about (0,0) maps the circle (5,0) r=1 to (0,-5) r=1')
        # Reference: the reference direction is 30 degrees (point (8.66.., 5) from the base), the new direction 90 degrees: turn by 60 degrees;
        # the far end (10,0) from the base (0,50) goes to (10 cos 60, 50 + 10 sin 60) = (5, 50 + 5 sqrt 3)
        check(has_line(f, 0, 50, 5, 50 + 5 * math.sqrt(3)), 'ROT_3: Rotate by reference 30 -> 90 turns by exactly 60 degrees: (0,50)-(5,50+5*sqrt(3))')
        # Copy=Yes keeps the original and adds the turned copy: (101,0) about (100,0) by 45 degrees = (100 + cos 45, sin 45)
        c45 = math.cos(math.pi / 4)
        check(has_line(f, 100, 0, 101, 0) and has_line(f, 100, 0, 100 + c45, c45), 'ROT_4: Rotate Copy=Yes keeps (100,0)-(101,0) and adds the 45 degree copy')
        # 180 degrees about the end (200,0): (x,y) -> (400 - x, -y) gives (200,0)-(196,0)
        check(has_line(f, 200, 0, 196, 0) and not has_line(f, 200, 0, 204, 0), 'ROT_5: Rotate 180 about (200,0) maps (200,0)-(204,0) to (200,0)-(196,0)')
        check(len(entities(f, 'LINE')) == 5 and len(entities(f, 'CIRCLE')) == 1, 'ROT_6: exactly 5 lines and 1 circle remain (no stray objects)')
    return s, chk


# MODIFY-0004 Scale --------------------------------------------------------------------------------------------------------
# Scale by k about b: p' = b + k (p - b), a circle keeps its shape: centre moves like a point and the radius is multiplied by |k|.
@case('scale')
def scale():
    s = PRE + """Circle 1,1,0 3
SelLast
Scale 0,0,0 2
Line 100,0,0 105,0,0
SelLast
Scale 100,0,0 105,0,0 107.5,0,0
Circle 0,200,0 2
SelLast
Scale Copy=Yes 0,200,0 5,200,0 7.5,200,0
Line 300,10,0 304,10,0
SelLast
Scale 302,10,0 0.5
SelAll
Export @T@/scl.dxf
"""

    def chk(d, log):
        f = d + '/scl.dxf'
        check(has_circle(f, 2, 2, 6), 'SCL_1: Scale 2 about (0,0) maps the circle (1,1) r=3 to (2,2) r=6')
        # Reference: reference length 5 (base (100,0) to (105,0)), new length 7.5: factor 7.5/5 = 1.5, so (105,0) goes to (100 + 7.5, 0)
        check(has_line(f, 100, 0, 107.5, 0), 'SCL_2: Scale by reference 5 -> 7.5 (factor 1.5) maps (100,0)-(105,0) to (100,0)-(107.5,0)')
        # Copy=Yes: the original circle (0,200) r=2 stays; the copy is scaled by 7.5/5 = 1.5 about its own centre: r=3
        check(has_circle(f, 0, 200, 2) and has_circle(f, 0, 200, 3), 'SCL_3: Scale Copy=Yes with reference 5 -> 7.5 keeps r=2 and adds the r=3 copy')
        # 0.5 about the midpoint (302,10): ends (300,10),(304,10) go to (301,10),(303,10)
        check(has_line(f, 301, 10, 303, 10), 'SCL_4: Scale 0.5 about the midpoint shrinks (300,10)-(304,10) to (301,10)-(303,10)')
        check(len(entities(f, 'CIRCLE')) == 3 and len(entities(f, 'LINE')) == 2, 'SCL_5: exactly 3 circles and 2 lines remain')
    return s, chk


# MODIFY-0009 Fillet -------------------------------------------------------------------------------------------------------
# Two perpendicular lines meeting at the corner K with radius r: the arc is tangent to both lines at distance r from K (tan 45 = 1),
# its centre is at (r, r) from the corner (for the lines along +x and +y), the arc runs from the point on the y line (angle 180)
# to the point on the x line (angle 270), the lines are trimmed to the tangent points.
@case('fillet')
def fillet():
    s = PRE + """Line 0,0,0 20,0,0
Line 0,0,0 0,20,0
SelNone
Fillet
5
10,0,0
0,10,0
Line 30,0,0 38,0,0
Line 40,5,0 40,20,0
SelNone
Fillet
0
34,0,0
40,12,0
Line 100,100,0 120,100,0
Line 100,100,0 100,120,0
SelNone
Fillet
7.5
110,100,0
100,110,0
Rectangle 200,0,0 220,10,0
SelLast
FilletCorners
2
SelAll
Export @T@/fil.dxf
"""

    def chk(d, log):
        f = d + '/fil.dxf'
        ar = arcs(f)
        check(has_line(f, 5, 0, 20, 0) and has_line(f, 0, 5, 0, 20), 'FIL_1: radius 5 trims the two lines to the tangent points (5,0)-(20,0) and (0,5)-(0,20)')
        got = [a for a in ar if close(gf(a, 10), 5) and close(gf(a, 20), 5) and close(gf(a, 40), 5)]
        ok = False
        for a in got:
            s0, e0 = gf(a, 50) % 360, gf(a, 51) % 360
            ok = ok or (close(s0, 180, 1e-6) and close(e0, 270, 1e-6))
        check(ok, 'FIL_2: the fillet arc has centre (5,5), radius 5 and runs 180 to 270 degrees (tangent to both lines)')
        # radius 0: the lines are extended to the sharp corner (40,0)
        check(has_line(f, 30, 0, 40, 0) and has_line(f, 40, 0, 40, 20), 'FIL_3: radius 0 makes a sharp corner: (30,0)-(40,0) and (40,0)-(40,20) meet at (40,0)')
        check(not [a for a in ar if close(gf(a, 10), 35)], 'FIL_4: radius 0 adds no arc')
        check(has_line(f, 107.5, 100, 120, 100) and has_line(f, 100, 107.5, 100, 120), 'FIL_5: radius 7.5 puts the tangent points at 7.5 from the corner (100,100)')
        check(any(close(gf(a, 10), 107.5) and close(gf(a, 20), 107.5) and close(gf(a, 40), 7.5) for a in ar), 'FIL_6: that arc has centre (107.5,107.5), radius 7.5')
        # all vertices of a polyline: the rounded 20 x 10 rectangle with r=2 has area 20*10 - (4 - pi) r^2 and perimeter 2(20+10) - 8 r + 2 pi r.
        # The exported SPLINE (rational, degree 2) is evaluated here with de Boor's algorithm, independent of the app's kernel.
        sp = entities(f, 'SPLINE')
        check(len(sp) == 1, 'FIL_7: FilletCorners turns the rectangle into one closed curve (one SPLINE in the file)')
        if sp:
            pts = spline_points(sp[0], 40000)
            area, per = polygon_area_perimeter(pts)
            check(abs(area - (200 - (4 - math.pi) * 4)) < 1e-5, 'FIL_8: the rounded rectangle area is 200 - (4 - pi)*4 = %.6f (measured %.6f)' % (200 - (4 - math.pi) * 4, area))
            check(abs(per - (60 - 16 + 4 * math.pi)) < 1e-5, 'FIL_9: the rounded rectangle perimeter is 44 + 4 pi = %.6f (measured %.6f)' % (44 + 4 * math.pi, per))
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            check(close(min(xs), 200, 1e-6) and close(max(xs), 220, 1e-6) and close(min(ys), 0, 1e-6) and close(max(ys), 10, 1e-6), 'FIL_10: the rounded rectangle still spans (200,0)-(220,10)')
    return s, chk


# MODIFY-0022 Explode / EXPLODE of a block -----------------------------------------------------------------------------------
@case('explode')
def explode():
    s = PRE + """Rectangle 0,0,0 10,6,0
SelLast
Explode
SelAll
Export @T@/exp_a.dxf
SelAll
Delete
NewLayer Walls
Line 20,0,0 30,0,0
Line 30,0,0 30,5,0
Line 30,5,0 20,0,0
SelAll
Block
20,0,0
TRI
SelAll
Export @T@/exp_b.dxf
SelAll
ExplodeBlock
SelAll
Export @T@/exp_c.dxf
"""

    def chk(d, log):
        a = d + '/exp_a.dxf'
        # a closed 4-vertex polyline (0,0),(10,0),(10,6),(0,6) breaks into its 4 sides
        check(len(entities(a, 'LINE')) == 4 and not entities(a, 'LWPOLYLINE') and not entities(a, 'SPLINE'), 'EXP_1: exploding the closed 4-vertex polyline leaves 4 LINE objects and no polyline')
        check(all(has_line(a, *q) for q in ((0, 0, 10, 0), (10, 0, 10, 6), (10, 6, 0, 6), (0, 6, 0, 0))), 'EXP_2: the 4 lines are the sides (0,0)-(10,0), (10,0)-(10,6), (10,6)-(0,6), (0,6)-(0,0)')
        b = d + '/exp_b.dxf'
        check('TRI' in block_names(b) and len(entities(b, 'INSERT')) == 1 and not entities(b, 'LINE'), 'EXP_3: before exploding, the 3 lines are one INSERT of block TRI')
        c = d + '/exp_c.dxf'
        check(not entities(c, 'INSERT'), 'EXP_4: after ExplodeBlock the block reference is gone')
        check(all(has_line(c, *q, layer='Walls') for q in ((20, 0, 30, 0), (30, 0, 30, 5), (30, 5, 20, 0))) and len(entities(c, 'LINE')) == 3,
              'EXP_5: exactly the 3 lines (20,0)-(30,0), (30,0)-(30,5), (30,5)-(20,0) come back at their place on layer Walls')
    return s, chk


# CMDLINE-0068 Redo ----------------------------------------------------------------------------------------------------------
@case('redo')
def redo():
    s = PRE + """Line 0,0,0 10,0,0
Line 0,5,0 10,5,0
Undo
SelAll
Export @T@/red_1.dxf
Redo
SelAll
Export @T@/red_2.dxf
Undo
Line 0,9,0 10,9,0
Redo
SelAll
Export @T@/red_3.dxf
"""

    def chk(d, log):
        a, b, c = d + '/red_1.dxf', d + '/red_2.dxf', d + '/red_3.dxf'
        check(len(entities(a, 'LINE')) == 1 and has_line(a, 0, 0, 10, 0), 'RED_1: after drawing two lines and Undo only the first line (0,0)-(10,0) is left')
        check(len(entities(b, 'LINE')) == 2 and has_line(b, 0, 0, 10, 0) and has_line(b, 0, 5, 10, 5), 'RED_2: Redo right after Undo brings the second line (0,5)-(10,5) back')
        check(len(entities(c, 'LINE')) == 2 and has_line(c, 0, 0, 10, 0) and has_line(c, 0, 9, 10, 9) and not has_line(c, 0, 5, 10, 5),
              'RED_3: Undo, a new line, then Redo does nothing: the file has (0,0)-(10,0) and (0,9)-(10,9) but not the undone (0,5)-(10,5)')
        check(log.count('Nothing to redo') == 1, 'RED_4: the Redo after a new command printed "Nothing to redo" exactly once (found %d)' % log.count('Nothing to redo'))
    return s, chk


# LAYER-0031 CopyToLayer -----------------------------------------------------------------------------------------------------
@case('copytolayer')
def copytolayer():
    s = PRE + """Line 0,0,0 10,5,0
SelLast
CopyToLayer
NEW
Circle 20,20,0 3
SelLast
CopyToLayer
NEW
SelAll
Export @T@/ctl.dxf
"""

    def chk(d, log):
        f = d + '/ctl.dxf'
        check(has_line(f, 0, 0, 10, 5, layer='Default') and has_line(f, 0, 0, 10, 5, layer='NEW') and len(entities(f, 'LINE')) == 2, 'CTL_1: the line (0,0)-(10,5) exists once on Default and once on NEW, nothing else')
        check(has_circle(f, 20, 20, 3, layer='Default') and has_circle(f, 20, 20, 3, layer='NEW') and len(entities(f, 'CIRCLE')) == 2, 'CTL_2: the circle (20,20) r=3 exists once on Default and once on NEW (same position)')
        check('NEW' in layer_names(f), 'CTL_3: the LAYER table has the new layer NEW (layers: %s)' % layer_names(f))
    return s, chk


# BLOCK-0001 Block / Insert ---------------------------------------------------------------------------------------------------
@case('block')
def block():
    s = PRE + """Line 10,10,0 14,10,0
Line 14,10,0 14,13,0
Line 14,13,0 10,10,0
SelAll
Block
10,10,0
TRI
Insert
TRI
30,40,0
Enter
SelAll
Export @T@/blk.dxf
"""

    def chk(d, log):
        f = d + '/blk.dxf'
        check(block_names(f) == ['TRI'], 'BLK_1: the BLOCKS section defines exactly one block, TRI (found %s)' % block_names(f))
        recs = [r for r in parse(f).get('BLOCKS', []) if r[0] == 'BLOCK' and g(r, 2) == 'TRI']
        bx, by = (gf(recs[0], 10), gf(recs[0], 20)) if recs else (None, None)
        rel = set()
        for r in block_entities(f, 'TRI', 'LINE'):
            k = line_key(r)
            rel.add(tuple(round(v - o, 6) for v, o in zip(k, (bx, by, bx, by))) if bx is not None else None)
        want = {(0, 0, 4, 0), (4, 0, 4, 3), (4, 3, 0, 0)}
        check(rel == want and len(block_entities(f, 'TRI', 'LINE')) == 3, 'BLK_2: the definition holds 3 LINE entities, relative to the base point (10,10): %s (found %s)' % (sorted(want), sorted(x for x in rel if x)))
        ins = sorted((gf(r, 10), gf(r, 20)) for r in entities(f, 'INSERT') if g(r, 2) == 'TRI')
        check(ins == [(10.0, 10.0), (30.0, 40.0)], 'BLK_3: INSERT points are the base point (10,10) of the replacing reference and the picked point (30,40) (found %s)' % ins)
        check(not entities(f, 'LINE'), 'BLK_4: the original lines are gone from the drawing (Delete source option: the lines became the reference)')
    return s, chk


# BLOCK-0092 Ungroup -----------------------------------------------------------------------------------------------------------
@case('ungroup')
def ungroup():
    s = PRE + """Line 0,0,0 10,0,0
Line 0,5,0 10,5,0
Line 0,9,0 10,9,0
SelNone
SelID 1
SelID 2
Group
SelNone
SelGroup
Unknown_MARK_A
SelNone
SelID 1
SelID 2
Ungroup
SelNone
SelGroup
Unknown_MARK_B
SelAll
Export @T@/ung.dxf
"""

    def chk(d, log):
        a = log.split('Unknown command: Unknown_MARK_A')[0]
        mid = log.split('Unknown command: Unknown_MARK_A')[1].split('Unknown command: Unknown_MARK_B')[0]
        sel_a = [l for l in a.split('\n') if 'object(s) selected' in l][-1:]
        sel_b = [l for l in mid.split('\n') if 'object(s) selected' in l]
        check(sel_a == ['history: 2 object(s) selected'], 'UNG_1: after Group of two lines, SelGroup finds exactly those 2 grouped objects (log: %s)' % sel_a)
        # after Ungroup of the group (both members picked, as a click on a group picks all of it) no object is grouped any more:
        # SelGroup selects nothing, and the three lines are untouched
        grouped_after = [l for l in sel_b if 'selected' in l and not l.startswith('history: 2 object(s) selected')]
        check(('history: 2 object(s) selected' not in sel_b[1:2]) and ('history: 1 object(s) selected' not in sel_b[1:]) and (len(sel_b) < 3 or sel_b[-1] != 'history: 2 object(s) selected'),
              'UNG_2: after Ungroup, SelGroup finds no grouped object (log after Ungroup: %s)' % sel_b)
        f = d + '/ung.dxf'
        check(len(entities(f, 'LINE')) == 3 and has_line(f, 0, 0, 10, 0) and has_line(f, 0, 5, 10, 5) and has_line(f, 0, 9, 10, 9), 'UNG_3: all three lines remain unchanged after Ungroup')
    return s, chk


# MODIFY-0039 Rotate3D --------------------------------------------------------------------------------------------------------
def rodrigues(p, a, b, deg):
    """Rotation of point p about the axis through a and b by deg degrees (right-hand rule): Rodrigues' formula."""
    k = [b[i] - a[i] for i in range(3)]
    n = math.sqrt(sum(c * c for c in k))
    k = [c / n for c in k]
    v = [p[i] - a[i] for i in range(3)]
    t = math.radians(deg)
    kv = sum(k[i] * v[i] for i in range(3))
    cr = [k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]]
    return tuple(a[i] + v[i] * math.cos(t) + cr[i] * math.sin(t) + k[i] * kv * (1 - math.cos(t)) for i in range(3))


def cube_edges(x0, y0, z0, s):
    pts = [(x0 + s * (i & 1), y0 + s * ((i >> 1) & 1), z0 + s * ((i >> 2) & 1)) for i in range(8)]
    return [(pts[i], pts[i | b]) for i in range(8) for b in (1, 2, 4) if not i & b]


def has_line3(path, p, q):
    for r in entities(path, 'LINE'):
        a = (gf(r, 10), gf(r, 20), gf(r, 30) or 0.0)
        b = (gf(r, 11), gf(r, 21), gf(r, 31) or 0.0)
        for u, v in ((a, b), (b, a)):
            if all(close(u[i], p[i]) and close(v[i], q[i]) for i in range(3)):
                return True
    return False


@case('rotate3d')
def rotate3d():
    lines = '\n'.join('Line %s,%s,%s %s,%s,%s' % (p + q) for p, q in cube_edges(1, 0, 0, 1))
    far = '\n'.join('Line %s,%s,%s %s,%s,%s' % (p + q) for p, q in cube_edges(5, 10, 0, 2))
    s = PRE + lines + """
SelAll
Rotate3D 0,0,0 0,0,1 90
SelAll
Export @T@/r3d_a.dxf
SelAll
Delete
""" + far + """
SelAll
Rotate3D 0,10,0 1,10,0 90
SelAll
Export @T@/r3d_b.dxf
SelAll
Delete
Line 3,4,5 6,4,5
SelAll
Rotate3D 3,4,5 3,5,5 -30
SelAll
Export @T@/r3d_c.dxf
"""

    def chk(d, log):
        a = d + '/r3d_a.dxf'
        # unit cube [1,2]x[0,1]x[0,1] turned 90 degrees about the Z axis through the origin: (x,y,z) -> (-y,x,z); the vertex (1,0,0) goes to (0,1,0)
        edges = [(rodrigues(p, (0, 0, 0), (0, 0, 1), 90), rodrigues(q, (0, 0, 0), (0, 0, 1), 90)) for p, q in cube_edges(1, 0, 0, 1)]
        check(close(edges[0][0][0], 0, 1e-12) and close(edges[0][0][1], 1, 1e-12), 'R3D_1: the closed form maps the vertex (1,0,0) to (0,1,0)')
        check(len(entities(a, 'LINE')) == 12 and all(has_line3(a, p, q) for p, q in edges), 'R3D_2: all 12 cube edges match the 90 degree rotation about the Z axis through (0,0)')
        # a cube [5,7]x[10,12]x[0,2] turned 90 degrees about the X-parallel axis through (0,10,0): (y,z) -> (10 + (-z), (y - 10)) i.e. y' = 10 - z, z' = y - 10
        b = d + '/r3d_b.dxf'
        edges = [(rodrigues(p, (0, 10, 0), (1, 10, 0), 90), rodrigues(q, (0, 10, 0), (1, 10, 0), 90)) for p, q in cube_edges(5, 10, 0, 2)]
        p0 = rodrigues((5, 12, 0), (0, 10, 0), (1, 10, 0), 90)
        check(close(p0[0], 5, 1e-12) and close(p0[1], 10, 1e-12) and close(p0[2], 2, 1e-12), 'R3D_3: the closed form maps (5,12,0) about the axis y=10,z=0 (X direction) to (5,10,2)')
        check(len(entities(b, 'LINE')) == 12 and all(has_line3(b, p, q) for p, q in edges), 'R3D_4: all 12 edges of a second cube match the 90 degree rotation about the X-parallel axis through (0,10,0)')
        # a line (3,4,5)-(6,4,5) turned -30 degrees about the Y-parallel axis through (3,4,5): the far end (3,0,0) rel goes to 3 cos30, 0, +3 sin30 ... (x,z) -> (x cos t + z sin t, -x sin t + z cos t) with t = -30
        c = d + '/r3d_c.dxf'
        t = math.radians(-30)
        end = (3 + 3 * math.cos(t), 4, 5 - 3 * math.sin(t))
        check(has_line3(c, (3, 4, 5), end), 'R3D_5: Rotate3D -30 degrees about the Y-parallel axis through (3,4,5) maps (6,4,5) to (3+3cos30, 4, 5+3 sin30)')
    return s, chk


# DRAW-0039 Purge ------------------------------------------------------------------------------------------------------------
@case('purge')
def purge():
    s = PRE + """NewLayer Empty
NewLayer Used
Line 0,0,0 5,0,0
SetLayer Default
Line 10,0,0 14,0,0
SelLast
Block
10,0,0
KEEP
Line 30,0,0 34,0,0
SelLast
Block
30,0,0
DROP
SelAll
Export @T@/prg_a.dxf
SelNone
SelID 5
Delete
NewLayer Cur
SelAll
Export @T@/prg_b.dxf
Purge
SelAll
Export @T@/prg_c.dxf
"""

    def chk(d, log):
        a, b, c = d + '/prg_a.dxf', d + '/prg_b.dxf', d + '/prg_c.dxf'
        check(sorted(block_names(a)) == ['DROP', 'KEEP'] and 'Empty' in layer_names(a) and 'Used' in layer_names(a), 'PRG_1: before Purge the file defines blocks KEEP and DROP and layers Empty and Used (blocks %s, layers %s)' % (block_names(a), layer_names(a)))
        check([g(r, 2) for r in entities(b, 'INSERT')] == ['KEEP'] and 'Cur' in layer_names(b), 'PRG_2: after deleting the DROP reference only KEEP is referenced (references: %s)' % [g(r, 2) for r in entities(b, 'INSERT')])
        check('DROP' not in block_names(c), 'PRG_3: Purge removes the unreferenced block DROP (blocks left: %s)' % block_names(c))
        check('KEEP' in block_names(c) and len([r for r in entities(c, 'INSERT') if g(r, 2) == 'KEEP']) == 1, 'PRG_4: Purge keeps the referenced block KEEP and its reference')
        check('Empty' not in layer_names(c), 'PRG_5: Purge removes the empty layer Empty (layers left: %s)' % layer_names(c))
        check('Used' in layer_names(c) and 'Default' in layer_names(c) and 'Cur' in layer_names(c), 'PRG_6: Purge keeps the layer in use (Used), the layer 0 equivalent (Default) and the current layer (Cur, empty): %s' % layer_names(c))
        check(has_line(c, 0, 0, 5, 0, layer='Used'), 'PRG_7: the line on layer Used is untouched')
    return s, chk


# MODIFY-0051 legacy ARRAY ---------------------------------------------------------------------------------------------------
@case('array')
def array():
    s = PRE + """Circle 0,0,0 1
SelLast
Array 4 3 1 9,9,0
SelAll
Export @T@/arr.dxf
SelAll
Array 400 300 1 9,9,0
"""

    def chk(d, log):
        f = d + '/arr.dxf'
        # the unit cell runs from the circle's lower left corner (-1,-1) to the picked corner (9,9): 10 units between centres, 4 columns, 3 rows
        want = sorted((10.0 * i, 10.0 * j) for i in range(4) for j in range(3))
        got = sorted((gf(r, 10), gf(r, 20)) for r in entities(f, 'CIRCLE') if close(gf(r, 40), 1) and close(gf(r, 30) or 0.0, 0.0))
        check(len(entities(f, 'CIRCLE')) == 12, 'ARR_1: 4 columns x 3 rows make exactly 12 circle objects (found %d)' % len(entities(f, 'CIRCLE')))
        check(len(got) == 12 and all(close(a[0], b[0]) and close(a[1], b[1]) for a, b in zip(got, want)), 'ARR_2: the centres are the grid (10 i, 10 j), i = 0..3, j = 0..2, all r=1 in the plane z=0')
        check('more than 100000 objects' in log, 'ARR_3: a 400 x 300 request (120000 elements) is rejected with the over-limit message')
        check('smoke: frames=30 objects=12 ' in log, 'ARR_4: the rejected request added nothing: 12 objects at the end (%s)' % ([l for l in log.split('\n') if l.startswith('smoke:')] or ['no smoke line']))
    return s, chk


# CMDLINE-0022 selection sets and Previous ----------------------------------------------------------------------------------------
@case('select')
def select():
    s = PRE + """Line 1,1,0 3,1,0
Line 4,1,0 8,1,0
Line 20,20,0 22,22,0
Circle 2,3,0 1
SelNone
SelWindow 0,0,0 5,5,0
SelNone
SelCrossing 0,0,0 5,5,0
SelNone
SelAll
SelNone
SelWindow 0,0,0 5,5,0
SelNone
SelPrev
Move 0,0,0 100,0,0
SelAll
Export @T@/sel.dxf
"""

    def chk(d, log):
        sel = [l.split('history: ')[1] for l in log.split('\n') if 'object(s) selected' in l and 'history:' in l]
        # window (0,0)-(5,5) holds fully: line (1,1)-(3,1) and circle (2,3) r=1 -> 2; crossing adds the line (4,1)-(8,1) that crosses x=5 -> 3;
        # All -> 4; window again -> 2; Previous brings that 2 back
        want = ['2 object(s) selected', '3 object(s) selected', '4 object(s) selected', '2 object(s) selected', '2 object(s) selected (previous selection)']
        check(sel[:5] == want, 'SEL_1: Window 2, Crossing 3, All 4, Window 2, Previous 2 (log: %s)' % sel[:6])
        f = d + '/sel.dxf'
        # Move on the Previous set shifts exactly the two windowed objects by (100,0); the crossing line and the far line stay
        check(has_line(f, 101, 1, 103, 1) and has_circle(f, 102, 3, 1), 'SEL_2: Move 100,0 on the Previous selection moves the enclosed line and circle: (101,1)-(103,1) and (102,3) r=1')
        check(has_line(f, 4, 1, 8, 1) and has_line(f, 20, 20, 22, 22), 'SEL_3: the crossing line (4,1)-(8,1) and the far line (20,20)-(22,22) were not in the set and stay')
    return s, chk


# ---------------------------------------------------------------------------------------------------------------- round 2
# Rows whose AutoCAD behaviour was implemented after the first measurement (register notes "needs implementation").
# Every expected number below is a closed form written next to its check.
def lwpolys(path):
    """-> [(closed, [(x, y), ...])] for every LWPOLYLINE (group 70 bit 1 = closed; vertices are the 10/20 pairs in order)."""
    out = []
    for r in entities(path, 'LWPOLYLINE'):
        xs = [float(v) for c, v in r[1] if c == 10]
        ys = [float(v) for c, v in r[1] if c == 20]
        out.append((int(g(r, 70, '0')) & 1 == 1, list(zip(xs, ys))))
    return out


def same_points(got, want, tol=TOL):
    """The two point lists hold the same points, in any order (each wanted point is matched once)."""
    if len(got) != len(want):
        return False
    rest = list(got)
    for w in want:
        hit = None
        for i, q in enumerate(rest):
            if abs(q[0] - w[0]) <= tol and abs(q[1] - w[1]) <= tol:
                hit = i
                break
        if hit is None:
            return False
        rest.pop(hit)
    return True


def has_poly(path, want):
    return any(same_points(pts, want) for _, pts in lwpolys(path))


# MODIFY-0001 MOVE with a displacement -------------------------------------------------------------------------------------
# MOVE help: Enter at the second-point prompt takes the first point as a displacement from the origin, so "Move 3,4,0" shifts by (3,4).
@case('move')
def move():
    s = PRE + """Line 0,0,0 4,0,0
Circle 10,10,0 2
SelAll
Move 3,4,0
Enter
Line 100,0,0 101,0,0
SelLast
Move 0,0,0 3,4,0
Line 200,0,0 206,0,0
SelLast
Move -5,2,0
Enter
SelAll
Export @T@/mov.dxf
"""

    def chk(d, log):
        f = d + '/mov.dxf'
        check(has_line(f, 3, 4, 7, 4), 'MOV_1: displacement 3,4,0 (Enter at the second point) moves the line (0,0)-(4,0) to (3,4)-(7,4)')
        check(has_circle(f, 13, 14, 2), 'MOV_2: the same displacement moves the circle (10,10) r=2 to (13,14)')
        check(has_line(f, 103, 4, 104, 4), 'MOV_3: the two-point form still works: base (0,0) to (3,4) moves (100,0)-(101,0) to (103,4)-(104,4)')
        check(has_line(f, 195, 2, 201, 2), 'MOV_4: a negative displacement -5,2,0 moves (200,0)-(206,0) to (195,2)-(201,2)')
        check(len(entities(f, 'LINE')) == 3 and len(entities(f, 'CIRCLE')) == 1, 'MOV_5: exactly 3 lines and 1 circle remain (nothing was copied)')
    return s, chk


# MODIFY-0002 COPY: Mode Single/Multiple and the linear Array option -----------------------------------------------------
# Array: the item count INCLUDES the original; the second point is the spacing between items (Fit=Yes: the position of the last item).
@case('copy')
def copy():
    s = PRE + """Circle 0,0,0 1
SelLast
Copy Mode=Single 0,0,0 10,0,0 20,0,0
Circle 0,50,0 1
SelLast
Copy 0,50,0 5,50,0 5,60,0
Enter
Circle 0,100,0 1
SelLast
Copy 0,100,0 Array 4 10,100,0
Circle 0,150,0 1
SelLast
Copy 0,150,0 Array 4 Fit=Yes 90,150,0
SelAll
Export @T@/cpy.dxf
"""

    def chk(d, log):
        f = d + '/cpy.dxf'
        check(has_circle(f, 0, 0, 1) and has_circle(f, 10, 0, 1) and not has_circle(f, 20, 0, 1), 'CPY_1: Mode=Single makes one copy (10,0) and ends: no circle at (20,0)')
        check(has_circle(f, 0, 50, 1) and has_circle(f, 5, 50, 1) and has_circle(f, 5, 60, 1), 'CPY_2: Multiple (the default) keeps copying until Enter: copies at (5,50) and (5,60)')
        check(all(has_circle(f, 10.0 * i, 100, 1) for i in range(4)) and not has_circle(f, 40, 100, 1), 'CPY_3: Array of 4 items (original included) with spacing 10 gives centres x = 0, 10, 20, 30 and no 5th')
        # Fit: the last of the 4 items sits on the second point (90): the spacing is 90 / (4 - 1) = 30
        check(all(has_circle(f, 30.0 * i, 150, 1) for i in range(4)), 'CPY_4: Array 4 with Fit to x=90 spreads the items at 0, 30, 60, 90')
        n = len(entities(f, 'CIRCLE'))
        check(n == 2 + 3 + 4 + 4, 'CPY_5: 13 circles in all (2 + 3 + 4 + 4), found %d' % n)
    return s, chk


# DRAW-0001 LINE with Close and Undo ------------------------------------------------------------------------------------------
@case('lines')
def lines():
    s = PRE + """Lines 0,0,0 10,0,0 10,5,0 Close
Lines 100,0,0 110,0,0 110,5,0 Undo 120,0,0 Enter
Lines 200,0,0 210,0,0 Close Enter
SelAll
Export @T@/lin.dxf
"""

    def chk(d, log):
        f = d + '/lin.dxf'
        check(has_line(f, 0, 0, 10, 0) and has_line(f, 10, 0, 10, 5) and has_line(f, 10, 5, 0, 0), 'LIN_1: Close after three points makes the third segment back to the start: (10,5)-(0,0)')
        check(has_line(f, 100, 0, 110, 0) and has_line(f, 110, 0, 120, 0) and not has_line(f, 110, 0, 110, 5), 'LIN_2: Undo removes only the last segment (110,0)-(110,5); the chain continues from (110,0) to (120,0)')
        check(has_line(f, 200, 0, 210, 0) and not has_line(f, 210, 0, 200, 0) and len([1 for r in entities(f, 'LINE') if close(gf(r, 10), 200)]) == 1, 'LIN_3: Close with a single segment is refused (a closing segment needs two): only (200,0)-(210,0) exists')
        check(len(entities(f, 'LINE')) == 6, 'LIN_4: 3 + 2 + 1 = 6 separate LINE objects, found %d' % len(entities(f, 'LINE')))
    return s, chk


# DRAW-0005 CIRCLE 2-point and tangent-tangent-radius ------------------------------------------------------------------------
# 2P: centre = midpoint of the diameter, radius = half its length. TTR for two lines: the centre is at distance r from both lines.
@case('circle')
def circle():
    s = PRE + """Circle 2Point 0,0,0 6,0,0
Circle 2P 10,10,0 10,18,0
Line 100,0,0 120,0,0
Line 100,0,0 100,20,0
Circle TTR 110,0,0 100,10,0 5
Line 300,0,0 320,0,0
Line 300,0,0 310,10,0
Circle TTR 310,0,0 308,8,0 5
SelAll
Export @T@/cir.dxf
"""

    def chk(d, log):
        f = d + '/cir.dxf'
        check(has_circle(f, 3, 0, 3), 'CIR_1: 2Point (0,0)-(6,0): centre = midpoint (3,0), radius = 6/2 = 3')
        check(has_circle(f, 10, 14, 4), 'CIR_2: 2P (10,10)-(10,18): centre (10,14), radius 4')
        # lines along the two axes from (100,0), r=5: the circle nearest the picks is in the quadrant of the picks: centre (105,5)
        check(has_circle(f, 105, 5, 5), 'CIR_3: TTR r=5 between the lines y=0 and x=100 (picks near (110,0) and (100,10)) has its centre at (105,5)')
        # lines y=0 and y=x-300 (45 degrees): the centre lies on the 22.5 degree bisector at height r: x = 300 + r / tan(22.5 deg) = 300 + 5 (1 + sqrt 2)
        check(has_circle(f, 300 + 5 * (1 + math.sqrt(2)), 5, 5), 'CIR_4: TTR r=5 between the lines y=0 and y=x-300 has its centre at (300 + 5(1+sqrt 2), 5) = (%.9f, 5)' % (300 + 5 * (1 + math.sqrt(2))))
        check(len(entities(f, 'CIRCLE')) == 4 and len(entities(f, 'LINE')) == 4, 'CIR_5: 4 circles and the 4 construction lines remain')
    return s, chk


# DRAW-0009 POLYGON: Inscribed, Circumscribed and Edge ----------------------------------------------------------------------
@case('polygon')
def polygon():
    s = PRE + """Polygon NumSides=6 0,0,0 10,0,0
Polygon NumSides=6 Circumscribed=Yes 100,0,0 110,0,0
Polygon NumSides=4 Edge 200,0,0 204,0,0
Polygon NumSides=5 Edge 300,0,0 304,0,0
Polygon NumSides=4 Circumscribed=Yes 400,0,0 400,5,0
SelAll
Export @T@/plg.dxf
"""

    def chk(d, log):
        f = d + '/plg.dxf'
        ps = lwpolys(f)
        check(len(ps) == 5 and all(c for c, _ in ps), 'PLG_0: five closed polylines (found %d)' % len(ps))
        # inscribed: corners on the circle of radius 10 from angle 0 in steps of 60 degrees; the side is then 10
        check(has_poly(f, [(10 * math.cos(math.radians(60 * k)), 10 * math.sin(math.radians(60 * k))) for k in range(6)]), 'PLG_1: inscribed hexagon in R=10: corners (10 cos 60k, 10 sin 60k)')
        # circumscribed about the circle of radius 10: corners at R = 10 / cos 30 = 11.547005 at angles 30 + 60k (the picked direction 0 is an edge middle)
        R = 10 / math.cos(math.radians(30))
        check(has_poly(f, [(100 + R * math.cos(math.radians(30 + 60 * k)), R * math.sin(math.radians(30 + 60 * k))) for k in range(6)]), 'PLG_2: circumscribed hexagon about the radius-10 circle: corners at R=%.9f, angles 30 + 60k' % R)
        # edge (200,0)-(204,0), 4 sides, counterclockwise: the square on the left (above) of the edge
        check(has_poly(f, [(200, 0), (204, 0), (204, 4), (200, 4)]), 'PLG_3: Edge (200,0)-(204,0) with 4 sides gives the square (200,0),(204,0),(204,4),(200,4)')
        # edge (300,0)-(304,0), 5 sides: side 4 everywhere, area = n s^2 / (4 tan(pi/n)), the edge is one of the sides, all on the left (y >= 0)
        pent = [pts for _, pts in ps if len(pts) == 5 and all(abs(x - 302) < 5 for x, _ in pts)]
        ok = False
        if pent:
            q = pent[0]
            sides = [math.hypot(q[(i + 1) % 5][0] - q[i][0], q[(i + 1) % 5][1] - q[i][1]) for i in range(5)]
            area = abs(sum(q[i][0] * q[(i + 1) % 5][1] - q[(i + 1) % 5][0] * q[i][1] for i in range(5))) / 2
            ok = (all(abs(x - 4) < 1e-8 for x in sides) and abs(area - 5 * 16 / (4 * math.tan(math.pi / 5))) < 1e-7 and all(y > -1e-9 for _, y in q)
                  and any(abs(x - 300) < TOL and abs(y) < TOL for x, y in q) and any(abs(x - 304) < TOL and abs(y) < TOL for x, y in q))
        check(ok, 'PLG_4: Edge (300,0)-(304,0) with 5 sides: all sides 4, area 5*16/(4 tan 36 deg) = %.6f, on the left of the edge, edge endpoints are corners' % (5 * 16 / (4 * math.tan(math.pi / 5))))
        # 4 sides circumscribed about the radius-5 circle at (400,0), picked direction 90 degrees (an edge middle): the square [395,405] x [-5,5]
        check(has_poly(f, [(395, 5), (395, -5), (405, -5), (405, 5)]), 'PLG_5: circumscribed square about the radius-5 circle at (400,0) is [395,405] x [-5,5]')
    return s, chk


# MODIFY-0010 CHAMFER with two distances and with distance and angle ----------------------------------------------------------
# Perpendicular lines meeting at the corner K: tangent points K + d1 u and K + d2 v. Angle method: law of sines in the triangle
# K, P (on line 1, KP = d1), Q (on line 2): KQ = d1 sin(a) / sin(corner angle + a) with a the angle at P.
@case('chamfer')
def chamfer():
    s = PRE + """Line 0,0,0 20,0,0
Line 0,0,0 0,20,0
Chamfer Distance=3 Distance2=4 10,0,0 0,10,0
Line 100,0,0 120,0,0
Line 100,0,0 100,20,0
Chamfer Distance=3 Distance2=4 100,10,0 110,0,0
Line 200,0,0 220,0,0
Line 200,0,0 200,20,0
Chamfer Method=Angle Distance=3 Angle=30 210,0,0 200,10,0
SelAll
Export @T@/chm.dxf
"""

    def chk(d, log):
        f = d + '/chm.dxf'
        check(has_line(f, 3, 0, 0, 4) and has_line(f, 3, 0, 20, 0) and has_line(f, 0, 4, 0, 20), 'CHM_1: distances 3 (first line, x axis) and 4 (second line): chamfer (3,0)-(0,4) of length 5, lines trimmed to (3,0)-(20,0) and (0,4)-(0,20)')
        # the first distance belongs to the first PICKED line: picking the vertical line first puts 3 on it: tangent points (100,3) and (104,0)
        check(has_line(f, 100, 3, 104, 0) and has_line(f, 100, 3, 100, 20) and has_line(f, 104, 0, 120, 0), 'CHM_2: picking the vertical line first puts distance 3 on it: chamfer (100,3)-(104,0)')
        # a = 30 deg at the first tangent point (3 from K): KQ = 3 sin 30 / sin(90 + 30) = 1.5 / 0.8660254 = sqrt 3
        check(has_line(f, 203, 0, 200, math.sqrt(3)) and has_line(f, 203, 0, 220, 0) and has_line(f, 200, math.sqrt(3), 200, 20), 'CHM_3: distance 3 and angle 30: second tangent point at 3 sin 30 / sin 120 = sqrt 3 = %.9f: chamfer (203,0)-(200,%.9f)' % (math.sqrt(3), math.sqrt(3)))
        check(len(entities(f, 'LINE')) == 9, 'CHM_4: three chamfers make 3 lines each (9 in all), found %d' % len(entities(f, 'LINE')))
    return s, chk


# MODIFY-0007 TRIM with Edge = Extend ---------------------------------------------------------------------------------------
@case('trim')
def trim():
    s = PRE + """Line 0,30,0 10,30,0
Line 5,32,0 5,38,0
SelLast
Trim 8,30,0
Enter
SelNone
Line 0,0,0 10,0,0
Line 5,2,0 5,8,0
SelLast
Trim EdgeExtend=Yes 8,0,0
Enter
SelAll
Export @T@/trm.dxf
"""

    def chk(d, log):
        f = d + '/trm.dxf'
        # the cutter x=5 stops at y=32, 2 above the line y=30 it never reaches: without Edge Extend there is no intersection, nothing is trimmed
        check(has_line(f, 0, 30, 10, 30), 'TRM_1: without Edge=Extend a cutter that stops short of the line trims nothing: (0,30)-(10,30) stays whole')
        # with Edge=Extend the cutter x=5 is prolonged down to y=0 (the implied intersection (5,0)); the pick at x=8 removes the part from 5 to 10
        check(has_line(f, 0, 0, 5, 0) and not has_line(f, 0, 0, 10, 0), 'TRM_2: with Edge=Extend the implied intersection (5,0) trims the line at the pick x=8: (0,0)-(5,0)')
        check(has_line(f, 5, 2, 5, 8) and has_line(f, 5, 32, 5, 38), 'TRM_3: the cutting edges themselves are unchanged')
        check(len(entities(f, 'LINE')) == 4, 'TRM_4: 4 lines remain, found %d' % len(entities(f, 'LINE')))
    return s, chk


# MODIFY-0008 EXTEND to boundary edges ----------------------------------------------------------------------------------------
@case('extend')
def extend():
    s = PRE + """Line 0,0,0 5,0,0
Line 0,20,0 3,20,0
Line 0,60,0 5,60,0
Line 0,80,0 5,80,0
Line 10,-5,0 10,30,0
SelNone
Extend Boundary SelLast Enter 4,0,0 2,20,0 4,60,0 EdgeExtend=Yes 4,80,0 Enter
SelAll
Export @T@/ext.dxf
"""

    def chk(d, log):
        f = d + '/ext.dxf'
        check(has_line(f, 0, 0, 10, 0), 'EXT_1: the line (0,0)-(5,0) is extended at its picked end up to the boundary line x=10: (0,0)-(10,0)')
        check(has_line(f, 0, 20, 10, 20), 'EXT_2: the short line (0,20)-(3,20) reaches the boundary at (10,20)')
        # the boundary x=10 only spans y -5..30: the lines at y=60 and y=80 do not reach it unless its line is prolonged (Edge=Extend)
        check(has_line(f, 0, 60, 5, 60), 'EXT_3: without Edge=Extend a line whose extension misses the boundary is left alone: (0,60)-(5,60)')
        check(has_line(f, 0, 80, 10, 80), 'EXT_4: with Edge=Extend the boundary line x=10 is prolonged and the line at y=80 ends at (10,80)')
        check(has_line(f, 10, -5, 10, 30) and len(entities(f, 'LINE')) == 5, 'EXT_5: the boundary is unchanged and 5 lines remain (found %d)' % len(entities(f, 'LINE')))
    return s, chk


# MODIFY-0012 OFFSET Through and Erase ---------------------------------------------------------------------------------------
@case('offset')
def offset():
    s = PRE + """Line 0,0,0 20,0,0
SelLast
Offset Through 5,3,0
Circle 0,100,0 5
SelLast
Offset Through 0,112,0
Circle 0,200,0 5
SelLast
Offset Through 0,202,0
Line 300,0,0 320,0,0
SelLast
Offset Erase=Yes Distance=2 305,10,0
SelAll
Export @T@/off.dxf
"""

    def chk(d, log):
        f = d + '/off.dxf'
        check(has_line(f, 0, 0, 20, 0) and has_line(f, 0, 3, 20, 3), 'OFF_1: Through (5,3) offsets the line y=0 to y=3 (distance 3 from the point to the line): (0,3)-(20,3); the source stays')
        # (0,112) is 7 outside the circle (centre (0,100), r=5: the nearest point is (0,105)) -> r = 5 + 7 = 12
        check(has_circle(f, 0, 100, 5) and has_circle(f, 0, 100, 12), 'OFF_2: Through (0,112) offsets the circle r=5 to r=5+7=12')
        # (0,202) is 3 inside the circle (centre (0,200), r=5: nearest point (0,205)): r = 5 - 3 = 2
        check(has_circle(f, 0, 200, 5) and has_circle(f, 0, 200, 2), 'OFF_3: Through (0,202) offsets the circle r=5 inward to r=5-3=2')
        check(has_line(f, 300, 2, 320, 2) and not has_line(f, 300, 0, 320, 0), 'OFF_4: Erase=Yes with distance 2 towards y>0 leaves only the offset (300,2)-(320,2); the source (300,0)-(320,0) is deleted')
        check(len(entities(f, 'LINE')) == 3 and len(entities(f, 'CIRCLE')) == 4, 'OFF_5: 3 lines and 4 circles remain (found %d and %d)' % (len(entities(f, 'LINE')), len(entities(f, 'CIRCLE'))))
    return s, chk


# MODIFY-0021 JOIN, Close option: one arc becomes a circle ----------------------------------------------------------------------
@case('joinclose')
def joinclose():
    s = PRE + """Arc 0,0,0 5,0,0 0,5,0
SelLast
JoinClose
Arc 20,0,0 25,0,0 15,0,0
SelLast
JoinClose
Line 50,0,0 60,0,0
SelLast
JoinClose
SelAll
Export @T@/jcl.dxf
"""

    def chk(d, log):
        f = d + '/jcl.dxf'
        check(has_circle(f, 0, 0, 5), 'JCL_1: closing the quarter arc (centre (0,0), r=5, 0 to 90 degrees) gives the circle centre (0,0) r=5')
        check(has_circle(f, 20, 0, 5), 'JCL_2: closing the half arc about (20,0) r=5 gives the circle centre (20,0) r=5')
        check(not arcs(f), 'JCL_3: no ARC object remains')
        check(has_line(f, 50, 0, 60, 0) and len(entities(f, 'LINE')) == 1 and len(entities(f, 'CIRCLE')) == 2, 'JCL_4: a line is not an arc: it stays, 1 line and 2 circles in all')
    return s, chk


# MODIFY-0014 ARRAYPOLAR with Rotate Items off ---------------------------------------------------------------------------------
# Rotate Items = No: the copies keep their orientation; the base point (middle of the selection) goes round the centre.
@case('arraypolar')
def arraypolar():
    s = PRE + """Line 10,0,0 14,0,0
SelLast
ArrayPolar RotateItems=No 0,0,0 4 360
Line 110,0,0 114,0,0
SelLast
ArrayPolar 100,0,0 4 360
SelAll
Export @T@/arp.dxf
"""

    def chk(d, log):
        f = d + '/arp.dxf'
        # base point = middle (12,0); it goes to (0,12), (-12,0), (0,-12): the copies are the same horizontal 4-long line centred there
        check(has_line(f, 10, 0, 14, 0) and has_line(f, -2, 12, 2, 12) and has_line(f, -14, 0, -10, 0) and has_line(f, -2, -12, 2, -12), 'ARP_1: RotateItems=No: 4 horizontal lines centred at (12,0), (0,12), (-12,0), (0,-12)')
        # rotated copies about (100,0): (110,0)-(114,0) turned by 90, 180, 270 degrees
        check(has_line(f, 110, 0, 114, 0) and has_line(f, 100, 10, 100, 14) and has_line(f, 90, 0, 86, 0) and has_line(f, 100, -10, 100, -14), 'ARP_2: the default rotates the items: (100,10)-(100,14), (90,0)-(86,0), (100,-10)-(100,-14)')
        check(len(entities(f, 'LINE')) == 8, 'ARP_3: 8 lines in all, found %d' % len(entities(f, 'LINE')))
    return s, chk


# MODIFY-0018 ALIGN with source / destination point pairs ---------------------------------------------------------------------
# Pair 1 moves s1 onto d1; pair 2 turns about d1 so that s1->s2 points along d1->d2 (and Scale=Yes scales by |d1d2|/|s1s2|).
@case('alignpts')
def alignpts():
    s = PRE + """Line 0,0,0 4,0,0
SelLast
AlignPoints 0,0,0 10,10,0 4,0,0 10,20,0
Line 100,0,0 104,0,0
SelLast
AlignPoints Scale=Yes 100,0,0 110,10,0 104,0,0 110,20,0
Line 200,0,0 204,0,0
SelLast
AlignPoints 200,0,0 210,5,0
Enter
SelAll
Export @T@/aln.dxf
"""

    def chk(d, log):
        f = d + '/aln.dxf'
        # source direction (4,0) = angle 0, destination direction (0,10) = angle 90: a quarter turn about (10,10); length stays 4
        check(has_line(f, 10, 10, 10, 14), 'ALN_1: two pairs without Scale: (0,0)-(4,0) becomes (10,10)-(10,14) (turned 90 degrees, length 4)')
        # with Scale the length becomes |d1 d2| = 10: the line is (110,10)-(110,20)
        check(has_line(f, 110, 10, 110, 20), 'ALN_2: two pairs with Scale=Yes: (100,0)-(104,0) becomes (110,10)-(110,20), length 10 = the destination distance')
        check(has_line(f, 210, 5, 214, 5), 'ALN_3: one pair then Enter is a plain move by d1 - s1 = (10,5): (210,5)-(214,5)')
        check(len(entities(f, 'LINE')) == 3, 'ALN_4: 3 lines, found %d' % len(entities(f, 'LINE')))
    return s, chk


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    mode, name, d = sys.argv[1], sys.argv[2], sys.argv[3]
    if name not in CASES:
        print('FAIL cad2d_rows: unknown case %s' % name)
        return 1
    script, chk = CASES[name]()
    if mode == 'gen':
        open('%s/%s.txt' % (d, name), 'w').write(script.replace('@T@', d))
        return 0
    chk(d, app_log(sys.argv[4]) if len(sys.argv) > 4 else '')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
