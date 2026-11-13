#!/usr/bin/env python3
"""Independent DXF reader for the DXFQ_ section (tests/sections/67_dxfq.sh).

Usage: dxfq_check.py <dir holding q1.dxf, q2_live.dxf, q2_baked.dxf> [<extra sys.path dir holding ezdxf>]

It parses the files Dino 8's ExportDxf wrote with its OWN ~100-line group-code reader (no code shared with the app) and checks
every value against a closed-form oracle, and, when the third-party `ezdxf` package can be imported, also lets ezdxf audit the
file, measure the DIMENSIONs and render the HATCH patterns. Every line printed starts with "ok   " or "FAIL " (or "SKIP ").

Oracles (all closed form, none read back from the app):
  * $INSUNITS 4 (the document is in millimetres); layer table: Walls linetype Dashed, Hidden switched off (negative colour),
    Locked flagged 4; LTYPE table defining Continuous and the used linetypes with the pattern lengths the app's table holds
    (Dashed 5,2 -> items 5,-2 total 7; Center 10,2,2,2 -> 10,-2,2,-2 total 16); an object whose own linetype is Center says so.
  * TEXT "Hello": insertion (0,20,0), height 5, rotation 0; TEXT "Slant" made on a construction plane whose x axis is (1,1,0):
    rotation 45, height 2.5, insertion (50,20) typed in that plane = (30,70)/sqrt(2); a STYLE table defining Standard.
  * Ellipse by centre (30,0,0), axis end (38,0,0), other axis end (30,3,0): ELLIPSE entity, centre (30,0,0), major axis vector
    (8,0,0), ratio 3/8, parameters 0..2*pi; the tilted one (centre (60,0,0), axis end (66,8,0), minor 4): major (6,8,0), ratio 0.4.
  * Splines: the interpolated curve passes through its four sample points; the control-point curve keeps its five control points
    and a clamped uniform knot vector; for both, degree + control-point count + 1 = knot count.
  * DIMENSIONs: measured value from the raw definition points (linear 40, aligned 3-4-5 = 50, radius 5, diameter 8, angle 90
    degrees), a geometry block (group 2) that exists and holds entities whose bounding box contains the definition point.
  * HATCH: solid fill flag and boundary area 20 x 10 = 200; ANSI31 (45 deg, 0.125 spacing in acad.pat) at scale 8, rotation 30:
    the pattern definition line a reader draws directly is angle 75, base (0,0), offset (-sin 75, cos 75) (1 across the line) - the SAME family the app's own
    baked lines have (direction 75 deg, perpendicular positions all integer multiples of 1).
"""
import math
import os
import sys

d = sys.argv[1]
if len(sys.argv) > 2 and sys.argv[2]:
    sys.path.insert(0, sys.argv[2])

fails = 0


def chk(cond, name):
    global fails
    if cond:
        print("ok   " + name)
    else:
        print("FAIL " + name)
        fails += 1


def skip(name):
    print("SKIP " + name)


def close(a, b, tol=1e-6):
    return abs(a - b) <= tol * max(1.0, abs(b))


def pairs_of(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    out = []
    for i in range(0, len(lines) - 1, 2):
        try:
            out.append((int(lines[i].strip()), lines[i + 1].strip()))
        except ValueError:
            pass
    return out


def records(prs):
    """Split a flat group list into records, one per group-code-0."""
    recs = []
    for c, v in prs:
        if c == 0:
            recs.append([v, []])
        elif recs:
            recs[-1][1].append((c, v))
    return recs


class Dxf:
    def __init__(self, path):
        prs = pairs_of(path)
        self.header = {}
        self.tables = {}  # table name -> [records]
        self.blocks = {}  # block name -> [entity records]
        self.entities = []
        i = 0
        n = len(prs)
        while i < n:
            if prs[i] == (0, "SECTION") and i + 1 < n:
                name = prs[i + 1][1]
                j = i + 2
                while j < n and prs[j] != (0, "ENDSEC"):
                    j += 1
                body = prs[i + 2:j]
                if name == "HEADER":
                    cur = None
                    for c, v in body:
                        if c == 9:
                            cur = v
                            self.header[cur] = []
                        elif cur:
                            self.header[cur].append((c, v))
                elif name == "TABLES":
                    tab = None
                    for r in records(body):
                        if r[0] == "TABLE":
                            tab = dict(r[1]).get(2)
                            self.tables[tab] = []
                        elif r[0] != "ENDTAB" and tab is not None:
                            self.tables[tab].append(r)
                elif name == "BLOCKS":
                    cur = None
                    for r in records(body):
                        if r[0] == "BLOCK":
                            cur = dict(r[1]).get(2)
                            self.blocks[cur] = []
                        elif r[0] == "ENDBLK":
                            cur = None
                        elif cur is not None:
                            self.blocks[cur].append(r)
                elif name == "ENTITIES":
                    self.entities = records(body)
                i = j
            i += 1

    def of(self, typ):
        return [e for e in self.entities if e[0] == typ]


def g(rec, code, default=None):
    for c, v in rec[1]:
        if c == code:
            return v
    return default


def gf(rec, code, default=None):
    v = g(rec, code)
    return float(v) if v is not None else default


def gl(rec, code):
    return [float(v) for c, v in rec[1] if c == code]


def pt(rec, base):
    return (gf(rec, base, 0.0), gf(rec, base + 10, 0.0), gf(rec, base + 20, 0.0))


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


# ---------------------------------------------------------------------------------------------------------------------------
q1 = Dxf(os.path.join(d, "q1.dxf"))

# --- header units ------------------------------------------------------------------------------------------------------
chk(dict(q1.header.get("$INSUNITS", [])).get(70) == "4", "DXFQ units: $INSUNITS is 4 (millimetres) for a millimetre document")

# --- layer table -------------------------------------------------------------------------------------------------------
layers = {g(r, 2): r for r in q1.tables.get("LAYER", [])}
chk(set(layers) >= {"Default", "Walls", "Hidden", "Locked"}, "DXFQ layers: the four layers of the document are in the LAYER table (%s)" % sorted(layers))
if {"Walls", "Hidden", "Locked", "Default"} <= set(layers):
    chk(g(layers["Walls"], 6) == "Dashed", "DXFQ layer: Walls carries its own linetype Dashed (the table says '%s')" % g(layers["Walls"], 6))
    chk(g(layers["Default"], 6) == "Continuous", "DXFQ layer: Default stays Continuous")
    chk(int(g(layers["Hidden"], 62, "1")) < 0, "DXFQ layer: Hidden (switched off) has a negative colour index")
    chk(int(g(layers["Locked"], 70, "0")) & 4 == 4, "DXFQ layer: Locked has flag bit 4")

# --- linetype table ----------------------------------------------------------------------------------------------------
ltypes = {g(r, 2): r for r in q1.tables.get("LTYPE", [])}
chk({"Continuous", "Dashed", "Center"} <= set(ltypes), "DXFQ LTYPE: the table defines Continuous, Dashed and Center (%s)" % sorted(ltypes))
if "Dashed" in ltypes:
    r = ltypes["Dashed"]
    chk(g(r, 73) == "2" and gl(r, 49) == [5.0, -2.0] and close(gf(r, 40, -1), 7.0), "DXFQ LTYPE: Dashed is the dash 5 / gap 2 pattern, total 7 (%s)" % gl(r, 49))
if "Center" in ltypes:
    r = ltypes["Center"]
    chk(g(r, 73) == "4" and gl(r, 49) == [10.0, -2.0, 2.0, -2.0] and close(gf(r, 40, -1), 16.0), "DXFQ LTYPE: Center is 10 / -2 / 2 / -2, total 16 (%s)" % gl(r, 49))
lines = q1.of("LINE")
by_y = {round(gf(e, 20, 0.0), 6): e for e in lines}
if 5.0 in by_y:
    chk(g(by_y[5.0], 6) == "Center", "DXFQ object linetype: the line whose own linetype is Center says so (group 6 = '%s')" % g(by_y[5.0], 6))
    chk(g(by_y[5.0], 8) == "Walls", "DXFQ object layer: that line is on Walls")
else:
    chk(False, "DXFQ object linetype: the y=5 line is missing")

# --- text --------------------------------------------------------------------------------------------------------------
styles = {g(r, 2) for r in q1.tables.get("STYLE", [])}
chk("Standard" in styles, "DXFQ STYLE: the table defines the Standard text style the TEXT entities use (%s)" % sorted(styles))
texts = {g(e, 1): e for e in q1.of("TEXT")}
chk({"Hello", "Slant"} <= set(texts), "DXFQ text: both strings are written as TEXT entities (%s)" % sorted(texts))
if "Hello" in texts:
    e = texts["Hello"]
    chk(dist(pt(e, 10), (0, 20, 0)) < 1e-6 and close(gf(e, 40, 0), 5.0) and abs(gf(e, 50, 0.0)) < 1e-6,
        "DXFQ text: 'Hello' at (0,20,0), height 5, rotation 0")
    chk(g(e, 7, "Standard") in styles or g(e, 7) is None and "Standard" in styles, "DXFQ text: its style (group 7 '%s') is defined" % g(e, 7))
if "Slant" in texts:
    e = texts["Slant"]
    r2 = math.sqrt(0.5)
    want = (30 * r2, 70 * r2, 0.0)  # typed (50,20) on a construction plane with x axis (1,1,0)/sqrt2, y axis (-1,1,0)/sqrt2
    chk(dist(pt(e, 10), want) < 1e-6 and close(gf(e, 40, 0), 2.5) and close(gf(e, 50, -1), 45.0),
        "DXFQ text: 'Slant' (typed (50,20) on the 45-degree plane) at (30,70)/sqrt2, height 2.5, rotation 45 (%s at %s)" % (g(e, 50), pt(e, 10)))

# --- ellipses ----------------------------------------------------------------------------------------------------------
ell = q1.of("ELLIPSE")
chk(len(ell) == 2, "DXFQ ellipse: two ELLIPSE entities (the file has %d; the ellipses are %s)" % (len(ell), "SPLINEs" if q1.of("SPLINE") else "missing"))
for centre, major, ratio, label in (((30, 0, 0), (8, 0, 0), 0.375, "axis-aligned"), ((60, 0, 0), (6, 8, 0), 0.4, "tilted")):
    hit = [e for e in ell if dist(pt(e, 10), centre) < 1e-6]
    if len(hit) != 1:
        chk(False, "DXFQ ellipse %s: ELLIPSE centred at %s" % (label, centre))
        continue
    e = hit[0]
    chk(dist(pt(e, 11), major) < 1e-6 and close(gf(e, 40, 0), ratio) and abs(gf(e, 41, -1)) < 1e-9 and close(gf(e, 42, -1), 2 * math.pi),
        "DXFQ ellipse %s: major axis %s, ratio %g, parameters 0..2pi (%s, %s, %s..%s)" % (label, major, ratio, pt(e, 11), g(e, 40), g(e, 41), g(e, 42)))


# --- splines -----------------------------------------------------------------------------------------------------------
def spline_of(rec):
    deg = int(g(rec, 71))
    knots = gl(rec, 40)
    xs, ys, zs = gl(rec, 10), gl(rec, 20), gl(rec, 30)
    cps = list(zip(xs, ys, zs if len(zs) == len(xs) else [0.0] * len(xs)))
    return deg, knots, cps, gl(rec, 41)


def deboor_eval(deg, knots, cps, w, t):
    n = len(cps)
    k = None
    for i in range(deg, n):
        if knots[i] <= t < knots[i + 1]:
            k = i
            break
    if k is None:
        k = n - 1
    dd = []
    for j in range(deg + 1):
        p = cps[j + k - deg]
        wt = w[j + k - deg] if w else 1.0
        dd.append([p[0] * wt, p[1] * wt, p[2] * wt, wt])
    for r in range(1, deg + 1):
        for j in range(deg, r - 1, -1):
            den = knots[j + 1 + k - r] - knots[j + k - deg]
            a = 0.0 if den == 0 else (t - knots[j + k - deg]) / den
            dd[j] = [(1 - a) * dd[j - 1][m] + a * dd[j][m] for m in range(4)]
    h = dd[deg]
    return (h[0] / h[3], h[1] / h[3], h[2] / h[3])


splines = q1.of("SPLINE")
chk(len(splines) == 2, "DXFQ spline: the interpolated curve and the control-point curve are the only SPLINEs (%d found)" % len(splines))
interp = cpcurve = None
for s in splines:
    deg, knots, cps, w = spline_of(s)
    chk(len(knots) == len(cps) + deg + 1 and int(g(s, 72, "-1")) == len(knots) and int(g(s, 73, "-1")) == len(cps),
        "DXFQ spline: %d control points, degree %d -> %d knots (group 72 says %s)" % (len(cps), deg, len(knots), g(s, 72)))
    chk(all(close(knots[i], knots[0]) for i in range(deg + 1)) and all(close(knots[-1 - i], knots[-1]) for i in range(deg + 1)),
        "DXFQ spline: knot vector is clamped (first and last %d knots repeat)" % (deg + 1))
    if len(cps) == 5:
        cpcurve = (deg, knots, cps, w)
    elif len(cps) == 4:
        interp = (deg, knots, cps, w)
if cpcurve:
    deg, knots, cps, w = cpcurve
    want = [(0, 60, 0), (5, 70, 0), (10, 50, 0), (15, 60, 0), (20, 70, 0)]
    chk(deg == 3 and all(dist(a, b) < 1e-6 for a, b in zip(cps, want)), "DXFQ spline: the control-point curve keeps degree 3 and its five control points")
    span = knots[-1] - knots[0]
    inner = [(k - knots[0]) / span for k in knots[4:-4]]
    chk(inner == [] or all(close(x, 0.5) for x in inner), "DXFQ spline: five control points of degree 3 give one interior knot at the middle (%s)" % inner)
if interp:
    deg, knots, cps, w = interp
    through = [(0, 40, 0), (5, 45, 0), (10, 40, 0), (15, 45, 0)]
    worst = 0.0
    for p in through:
        best = 1e30
        t0, t1 = knots[0], knots[-1]
        ts = [t0 + (t1 - t0) * i / 4000.0 for i in range(4001)]
        pts = [deboor_eval(deg, knots, cps, w, min(t, t1 - 1e-12)) for t in ts]
        bi = min(range(len(ts)), key=lambda i: dist(pts[i], p))
        lo, hi = ts[max(bi - 1, 0)], ts[min(bi + 1, 4000)]
        for _ in range(60):
            m1, m2 = lo + (hi - lo) / 3, hi - (hi - lo) / 3
            if dist(deboor_eval(deg, knots, cps, w, min(m1, t1 - 1e-12)), p) < dist(deboor_eval(deg, knots, cps, w, min(m2, t1 - 1e-12)), p):
                hi = m2
            else:
                lo = m1
        best = dist(deboor_eval(deg, knots, cps, w, min((lo + hi) / 2, t1 - 1e-12)), p)
        worst = max(worst, best)
    chk(worst < 1e-6, "DXFQ spline: the interpolated curve, evaluated from the DXF knots and control points, passes through all four sample points (worst miss %.2g)" % worst)


# --- dimensions --------------------------------------------------------------------------------------------------------
def bbox_of_block(name):
    pts = []
    for e in q1.blocks.get(name, []):
        t = e[0]
        if t == "LINE":
            pts += [pt(e, 10), pt(e, 11)]
        elif t in ("LWPOLYLINE",):
            xs, ys = gl(e, 10), gl(e, 20)
            pts += [(x, y, 0.0) for x, y in zip(xs, ys)]
        elif t in ("VERTEX", "POINT", "TEXT", "MTEXT", "SOLID", "INSERT"):
            pts.append(pt(e, 10))
        elif t == "CIRCLE":
            c, r = pt(e, 10), gf(e, 40, 0.0)
            pts += [(c[0] - r, c[1] - r, 0.0), (c[0] + r, c[1] + r, 0.0)]
        elif t == "ARC":
            c, r = pt(e, 10), gf(e, 40, 0.0)
            pts += [(c[0] - r, c[1] - r, 0.0), (c[0] + r, c[1] + r, 0.0)]
        elif t == "SPLINE":
            pts += list(zip(gl(e, 10), gl(e, 20), [0.0] * len(gl(e, 10))))
    return pts


dims = q1.of("DIMENSION")
chk(len(dims) == 5, "DXFQ dimension: five DIMENSION entities (%d)" % len(dims))
want_dims = {0: ("linear", 40.0), 1: ("aligned", 50.0), 4: ("radius", 5.0), 3: ("diameter", 8.0), 5: ("angle", 90.0)}
for dm in dims:
    flag = int(g(dm, 70, "0"))
    key = flag & 15
    if key not in want_dims:
        chk(False, "DXFQ dimension: unknown dimension type flag %d" % flag)
        continue
    kind, value = want_dims[key]
    if kind == "linear":
        meas = abs(pt(dm, 14)[0] - pt(dm, 13)[0])  # horizontal (rotation 0) dimension measures the x distance
        anchor = pt(dm, 10)
    elif kind == "aligned":
        meas = dist(pt(dm, 13), pt(dm, 14))
        anchor = pt(dm, 10)
    elif kind == "radius":
        meas = dist(pt(dm, 10), pt(dm, 15))  # 10 = centre, 15 = point on the circle
        anchor = pt(dm, 15)
    elif kind == "diameter":
        meas = dist(pt(dm, 10), pt(dm, 15))  # chord through the centre
        anchor = pt(dm, 15)
    else:
        v1 = tuple(a - b for a, b in zip(pt(dm, 13), pt(dm, 15)))
        v2 = tuple(a - b for a, b in zip(pt(dm, 14), pt(dm, 15)))
        meas = math.degrees(math.acos(sum(a * b for a, b in zip(v1, v2)) / (math.hypot(*v1) * math.hypot(*v2))))
        anchor = pt(dm, 10)
    chk(close(meas, value, 1e-6), "DXFQ dimension %s: the definition points measure %g (got %.9g)" % (kind, value, meas))
    blk = g(dm, 2)
    chk(blk is not None and blk in q1.blocks and len(q1.blocks[blk]) > 0,
        "DXFQ dimension %s: group 2 names a geometry block that exists and holds entities (%s)" % (kind, blk))
    if blk in q1.blocks and q1.blocks[blk]:
        pts = bbox_of_block(blk)
        if pts:
            lo = [min(p[i] for p in pts) for i in range(2)]
            hi = [max(p[i] for p in pts) for i in range(2)]
            inside = all(lo[i] - 1e-6 <= anchor[i] <= hi[i] + 1e-6 for i in range(2))
            chk(inside, "DXFQ dimension %s: its geometry block covers the definition point %s (box %s-%s)" % (kind, tuple(round(x, 4) for x in anchor[:2]), tuple(round(x, 3) for x in lo), tuple(round(x, 3) for x in hi)))

# --- hatch -------------------------------------------------------------------------------------------------------------


def hatch_of(rec):
    prs = rec[1]
    out = {"solid": int(g(rec, 70, "-1")), "name": g(rec, 2), "angle": gf(rec, 52, 0.0), "scale": gf(rec, 41, 1.0), "paths": [], "lines": []}
    i = 0
    n = len(prs)
    while i < n:
        c, v = prs[i]
        if c == 93:
            cnt = int(v)
            vs = []
            i += 1
            while len(vs) < cnt and i < n:
                if prs[i][0] == 10:
                    x = float(prs[i][1])
                    y = float(prs[i + 1][1]) if i + 1 < n and prs[i + 1][0] == 20 else 0.0
                    vs.append((x, y))
                    i += 2
                else:
                    i += 1
            out["paths"].append(vs)
            continue
        if c == 78:
            cnt = int(v)
            i += 1
            for _ in range(cnt):
                ln = {}
                while i < n and prs[i][0] != 79:
                    ln[prs[i][0]] = float(prs[i][1])
                    i += 1
                dn = int(prs[i][1]) if i < n else 0
                i += 1
                ln["dashes"] = [float(prs[i + k][1]) for k in range(dn)]
                i += dn
                out["lines"].append(ln)
            continue
        i += 1
    return out


def area(vs):
    return abs(sum(vs[i][0] * vs[(i + 1) % len(vs)][1] - vs[(i + 1) % len(vs)][0] * vs[i][1] for i in range(len(vs)))) / 2.0


h1 = q1.of("HATCH")
chk(len(h1) == 1, "DXFQ hatch solid: one HATCH entity")
if h1:
    h = hatch_of(h1[0])
    chk(h["solid"] == 1 and h["paths"] and close(area(h["paths"][0]), 200.0), "DXFQ hatch solid: solid-fill flag and boundary area 20 x 10 = 200 (got %s)" % (area(h["paths"][0]) if h["paths"] else None))

q2 = Dxf(os.path.join(d, "q2_live.dxf"))
q2b = Dxf(os.path.join(d, "q2_baked.dxf"))
hp = q2.of("HATCH")
chk(len(hp) == 1, "DXFQ hatch pattern: one HATCH entity for the ANSI31 hatch")
ang_expected = 45.0 + 30.0
if hp:
    h = hatch_of(hp[0])
    chk(h["name"] == "ANSI31" and h["solid"] == 0 and h["paths"] and close(area(h["paths"][0]), 200.0), "DXFQ hatch pattern: ANSI31, pattern fill, boundary area 200")
    chk(close(h["angle"], 30.0) and close(h["scale"], 8.0), "DXFQ hatch pattern: the applied rotation 30 and scale 8 are recorded (groups 52 / 41)")
    if h["lines"]:
        ln = h["lines"][0]
        chk(close(ln.get(53, -999) % 180.0, ang_expected % 180.0, 1e-9),
            "DXFQ hatch pattern: the definition line is already rotated, 45 + 30 = %g degrees, as a reader that draws the lines directly needs (got %s)" % (ang_expected, ln.get(53)))
        ra = math.radians(ang_expected)
        chk(abs(ln.get(45, 9) - (-math.sin(ra))) < 1e-6 and abs(ln.get(46, 9) - math.cos(ra)) < 1e-6,
            "DXFQ hatch pattern: the definition offset is already scaled and rotated, 0.125 x 8 = 1 across the 75-degree line: (-sin 75, cos 75) (got %s, %s)" % (ln.get(45), ln.get(46)))
        chk(abs(ln.get(43, 9)) < 1e-9 and abs(ln.get(44, 9)) < 1e-9, "DXFQ hatch pattern: the base point is the origin (%s, %s)" % (ln.get(43), ln.get(44)))
    else:
        chk(False, "DXFQ hatch pattern: the HATCH has a definition line")
baked = q2b.of("LINE")
chk(len(baked) > 10, "DXFQ hatch baked: the app's own clipped hatch lines are LINE entities when its boundary is gone (%d)" % len(baked))
if baked:
    ok_dir = ok_pos = True
    ca, sa = math.cos(math.radians(ang_expected)), math.sin(math.radians(ang_expected))
    for e in baked:
        a, b = pt(e, 10), pt(e, 11)
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln_ = math.hypot(dx, dy)
        if ln_ < 1e-9:
            continue
        if abs((dx * sa - dy * ca) / ln_) > 1e-6:
            ok_dir = False
        t = -a[0] * sa + a[1] * ca  # perpendicular coordinate of the line, origin at (0,0)
        if abs(t - round(t)) > 1e-6:
            ok_pos = False
    chk(ok_dir, "DXFQ hatch baked: every app hatch line runs at %g degrees" % ang_expected)
    chk(ok_pos, "DXFQ hatch baked: every app hatch line lies at an integer multiple of the scaled spacing 1 from the origin, i.e. exactly on the family the DXF definition line describes")

# --- ezdxf (optional, third party) -------------------------------------------------------------------------------------
try:
    import ezdxf
    from ezdxf.render import hatching
except Exception as ex:  # not installed
    skip("DXFQ ezdxf: the package is not importable (%s); the closed-form checks above are the whole verdict" % ex)
    ezdxf = None
if ezdxf:
    doc = ezdxf.readfile(os.path.join(d, "q1.dxf"))
    meas = {}
    for e in doc.modelspace().query("DIMENSION"):
        try:
            meas[e.dxf.dimtype & 7] = e.get_measurement()
        except Exception as ex:
            meas[e.dxf.dimtype & 7] = ex
    aud = doc.audit()
    chk(len(aud.errors) == 0 and len(aud.fixes) == 0,
        "DXFQ ezdxf: audit of the whole file finds nothing to fix (errors %d, fixes %d: %s)" % (len(aud.errors), len(aud.fixes), "; ".join(f.message[:70] for f in list(aud.errors) + list(aud.fixes))[:260]))
    doc = ezdxf.readfile(os.path.join(d, "q1.dxf"))  # audit may have removed entities; read again for the measurements
    ms = {e.dxf.dimtype & 7: e for e in doc.modelspace().query("DIMENSION")}
    for key, (kind, value) in {1: ("aligned", 50.0), 0: ("linear", 40.0), 4: ("radius", 5.0), 3: ("diameter", 8.0)}.items():
        e = ms.get(key)
        if e is None:
            chk(False, "DXFQ ezdxf: a %s DIMENSION is there" % kind)
            continue
        try:
            m = e.get_measurement()
            chk(close(float(m), value, 1e-6), "DXFQ ezdxf: %s dimension measures %g (ezdxf says %s)" % (kind, value, m))
        except Exception as ex:
            chk(False, "DXFQ ezdxf: %s dimension measurement failed (%s)" % (kind, ex))
    e = ms.get(5)
    if e is not None:
        try:
            m = e.get_measurement()
            chk(close(math.degrees(float(m)) if abs(float(m)) < 7 else float(m), 90.0, 1e-6), "DXFQ ezdxf: angular dimension measures 90 degrees (ezdxf says %s)" % m)
        except Exception as ex:
            chk(False, "DXFQ ezdxf: angular dimension measurement failed (%s)" % ex)
    # hatch: ezdxf draws the pattern lines straight from the definition; they must be the app's baked lines
    doc2 = ezdxf.readfile(os.path.join(d, "q2_live.dxf"))
    hh = list(doc2.modelspace().query("HATCH"))
    if hh:
        segs = [(tuple(s)[:2], tuple(e)[:2]) for s, e in hatching.hatch_entity(hh[0], jiggle_origin=False)]
        segs = [(a, b) for a, b in segs if math.dist(a, b) > 1e-6]
        chk(len(segs) > 10, "DXFQ ezdxf: draws %d pattern lines for the ANSI31 hatch" % len(segs))

        def key_of(a, b):
            dx, dy = b[0] - a[0], b[1] - a[1]
            ln_ = math.hypot(dx, dy)
            ux, uy = dx / ln_, dy / ln_
            if ux < -1e-12 or (abs(ux) <= 1e-12 and uy < 0):
                ux, uy = -ux, -uy
            t = -a[0] * uy + a[1] * ux
            s0, s1 = sorted((a[0] * ux + a[1] * uy, b[0] * ux + b[1] * uy))
            return (round(math.degrees(math.atan2(uy, ux)), 4), round(t, 4), round(s0, 4), round(s1, 4))

        ez = sorted(key_of(a, b) for a, b in segs)
        app = sorted(key_of(pt(e, 10)[:2], pt(e, 11)[:2]) for e in baked if dist(pt(e, 10), pt(e, 11)) > 1e-6)
        same = len(ez) == len(app) and all(all(abs(x - y) < 2e-3 for x, y in zip(u, v)) for u, v in zip(ez, app))
        chk(same, "DXFQ ezdxf: the lines ezdxf draws from the exported HATCH are the app's own %d hatch lines (ezdxf %d)" % (len(app), len(ez)))

print("dxfq_check: %d failure(s)" % fails)
sys.exit(1 if fails else 0)
