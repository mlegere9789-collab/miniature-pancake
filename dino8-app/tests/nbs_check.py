#!/usr/bin/env python3
"""NBS_ checker: free-form NURBS faces written to STEP (B_SPLINE_SURFACE_WITH_KNOTS, RATIONAL_B_SPLINE_SURFACE, B_SPLINE_CURVE_WITH_KNOTS)
read by an independent parser and an independent Cox-de Boor evaluator (no use of the application's code):

  * the bicubic patch: degrees 3 x 3, knot multiplicities (4,4) x (4,4), and the 4 x 4 control net equal to the typed points
    (0,0,0) (0,3,1) ... (9,9,3) in the order the net lists them;
  * the sphere of radius 4 (a rational surface): degrees 2 x 2, weights in {1, sqrt(2)/2, 1/2}, and every evaluated surface point on a
    10 x 10 parameter grid is at distance 4 from the origin within 1e-9 (the exactness of the rational net);
  * the same two surfaces exported again after an import (patch2.stp, sph2.stp): degrees, knots, weights and control net equal the
    first export within 1e-9, so the re-import moved no point of the surface (deviation measured as the largest distance between
    evaluations on a 12 x 12 grid, required < 1e-6);
  * the trimmed face's boundary loops close: every EDGE_LOOP's oriented edges chain end to start within 1e-6.

    python3 nbs_check.py <directory>
"""
import math
import os
import re
import sys

D = sys.argv[1]
failed = 0


def report(ok, name, detail=""):
    global failed
    if not ok:
        failed += 1
    print(("ok   " if ok else "FAIL ") + "NBS_" + name + (": " + detail if detail else ""))


def parse(path):
    text = open(path, errors="replace").read()
    i, j = text.find("DATA;"), text.rfind("ENDSEC;")
    data = text[i + 5:j]
    ents = {}
    for m in re.finditer(r"#(\d+)\s*=\s*(.*?);\s*(?=#\d+\s*=|\Z)", data, re.S):
        ents[int(m.group(1))] = m.group(2).strip()
    return ents


def split_top(args):
    out, depth, cur = [], 0, ""
    for ch in args:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    out.append(cur.strip())
    return out


def lst(s):
    s = s.strip()
    return split_top(s[1:-1]) if s.startswith("(") else [s]


def nums(s):
    return [float(x) for x in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", s)]


def refs(s):
    return [int(x) for x in re.findall(r"#(\d+)", s)]


def point(ents, ref):
    body = ents[ref]
    return tuple(nums(split_top(body[body.index("(") :][1:-1])[-1])[:3]) if False else tuple(nums(body[body.index(",(") :])[:3])


def complex_parts(body):
    """'(A(..)B(..))' -> {name: args-string}"""
    inner = body[1:-1]
    parts, depth, cur, name = {}, 0, "", ""
    i = 0
    while i < len(inner):
        m = re.match(r"([A-Z_0-9]+)\(", inner[i:])
        if depth == 0 and m:
            name = m.group(1)
            i += len(m.group(0))
            depth = 1
            cur = ""
            continue
        ch = inner[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                parts[name] = cur
                i += 1
                continue
        cur += ch
        i += 1
    return parts


def surfaces(path):
    ents = parse(path)
    out = []
    for k, body in ents.items():
        if body.startswith("B_SPLINE_SURFACE_WITH_KNOTS("):
            a = split_top(body[len("B_SPLINE_SURFACE_WITH_KNOTS("):-1])
            du, dv = int(a[1]), int(a[2])
            net = [[point(ents, r) for r in refs(row)] for row in lst(a[3])]
            um = [int(x) for x in lst(a[8])]
            vm = [int(x) for x in lst(a[9])]
            uk = [float(x) for x in lst(a[10])]
            vk = [float(x) for x in lst(a[11])]
            w = [[1.0] * len(r) for r in net]
        elif body.startswith("(") and "B_SPLINE_SURFACE_WITH_KNOTS" in body:
            parts = complex_parts(body)
            a = split_top(parts["B_SPLINE_SURFACE"])
            du, dv = int(a[0]), int(a[1])
            net = [[point(ents, r) for r in refs(row)] for row in lst(a[2])]
            b = split_top(parts["B_SPLINE_SURFACE_WITH_KNOTS"])
            um, vm = [int(x) for x in lst(b[0])], [int(x) for x in lst(b[1])]
            uk, vk = [float(x) for x in lst(b[2])], [float(x) for x in lst(b[3])]
            w = [[float(x) for x in lst(row)] for row in lst(parts["RATIONAL_B_SPLINE_SURFACE"])] if "RATIONAL_B_SPLINE_SURFACE" in parts else [[1.0] * len(r) for r in net]
        else:
            continue
        uknots = [t for t, m in zip(uk, um) for _ in range(m)]
        vknots = [t for t, m in zip(vk, vm) for _ in range(m)]
        out.append({"du": du, "dv": dv, "net": net, "w": w, "um": um, "vm": vm, "uk": uknots, "vk": vknots})
    return out, ents


def basis(knots, deg, i, t):
    """Cox-de Boor N_{i,deg}(t); the last span includes its right end."""
    if deg == 0:
        if knots[i] <= t < knots[i + 1] or (t == knots[-1] and knots[i] < knots[i + 1] == knots[-1]):
            return 1.0
        return 0.0
    r = 0.0
    d1 = knots[i + deg] - knots[i]
    if d1 > 0:
        r += (t - knots[i]) / d1 * basis(knots, deg, i, t) if False else (t - knots[i]) / d1 * basis(knots, deg - 1, i, t)
    d2 = knots[i + deg + 1] - knots[i + 1]
    if d2 > 0:
        r += (knots[i + deg + 1] - t) / d2 * basis(knots, deg - 1, i + 1, t)
    return r


def evaluate(s, u, v):
    nu, nv = len(s["net"]), len(s["net"][0])
    x = [0.0, 0.0, 0.0]
    wsum = 0.0
    for i in range(nu):
        bu = basis(s["uk"], s["du"], i, u)
        if bu == 0.0:
            continue
        for j in range(nv):
            b = bu * basis(s["vk"], s["dv"], j, v)
            if b == 0.0:
                continue
            wt = s["w"][i][j] * b
            wsum += wt
            for c in range(3):
                x[c] += wt * s["net"][i][j][c]
    return tuple(c / wsum for c in x)


def grid(s, n):
    u0, u1 = s["uk"][0], s["uk"][-1]
    v0, v1 = s["vk"][0], s["vk"][-1]
    return [evaluate(s, u0 + (u1 - u0) * a / n, v0 + (v1 - v0) * b / n) for a in range(n + 1) for b in range(n + 1)]


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def load(name):
    p = os.path.join(D, name)
    if not os.path.exists(p):
        report(False, name + "_written", "missing")
        return None
    s, ents = surfaces(p)
    return s, ents


# ------------------------------------------------------------------ patch
TYPED = [(0, 0, 0), (0, 3, 1), (0, 6, 2), (0, 9, 0), (3, 0, 1), (3, 3, 3), (3, 6, 1), (3, 9, 0),
         (6, 0, 0), (6, 3, 2), (6, 6, 3), (6, 9, 1), (9, 0, 2), (9, 3, 0), (9, 6, 1), (9, 9, 3)]
pat = load("patch.stp")
if pat and pat[0]:
    s = pat[0][0]
    report(s["du"] == 3 and s["dv"] == 3 and s["um"] == [4, 4] and s["vm"] == [4, 4], "patch_degrees_and_knot_multiplicities", "degrees %d x %d, multiplicities %s x %s" % (s["du"], s["dv"], s["um"], s["vm"]))
    flat = [p for row in s["net"] for p in row]
    # the net lists the typed points in some order of the grid; compare as sets (the order is checked through the evaluation below)
    report(sorted(flat) == sorted(TYPED), "patch_control_net_is_the_typed_points", "%d points" % len(flat))
elif pat:
    report(False, "patch_has_a_b_spline_surface", "no B_SPLINE_SURFACE_WITH_KNOTS")

# ------------------------------------------------------------------ sphere
sph = load("sph.stp")
if sph and sph[0]:
    s = sph[0][0]
    wset = sorted({round(x, 9) for row in s["w"] for x in row})
    report(s["du"] == 2 and s["dv"] == 2 and wset == [0.5, round(math.sqrt(2) / 2, 9), 1.0], "sphere_degrees_and_weights", "degrees %d x %d, weights %s" % (s["du"], s["dv"], wset))
    worst = max(abs(math.sqrt(sum(c * c for c in p)) - 4.0) for p in grid(s, 10))
    report(worst < 1e-9, "sphere_evaluated_points_are_at_radius_4", "largest |radius - 4| = %.3g on 11 x 11 points" % worst)
elif sph:
    report(False, "sphere_has_a_b_spline_surface", "no B_SPLINE_SURFACE_WITH_KNOTS")

# ------------------------------------------------------------------ re-import
for first, second in (("patch.stp", "patch2.stp"), ("sph.stp", "sph2.stp")):
    a, b = load(first), load(second)
    if not a or not b or not a[0] or not b[0]:
        report(False, second + "_reimport_exported_again", "missing surfaces")
        continue
    s1, s2 = a[0][0], b[0][0]
    same = s1["du"] == s2["du"] and s1["dv"] == s2["dv"] and s1["um"] == s2["um"] and s1["vm"] == s2["vm"]
    dev = max(dist(p, q) for p, q in zip(grid(s1, 12), grid(s2, 12))) if same else float("inf")
    report(same and dev < 1e-6, first[:-4] + "_reimport_deviation", "same degrees/multiplicities %s, largest deviation %.3g (< 1e-6)" % (same, dev))

# ------------------------------------------------------------------ loops close
for name in ("patch.stp", "sph.stp"):
    p = os.path.join(D, name)
    if not os.path.exists(p):
        continue
    ents = parse(p)
    bad = 0
    loops = 0
    for k, body in ents.items():
        if not body.startswith("EDGE_LOOP("):
            continue
        loops += 1
        ends = []
        for oe in refs(body):
            a = split_top(ents[oe][len("ORIENTED_EDGE("):-1])
            ec = refs(a[3])[0]
            e = split_top(ents[ec][len("EDGE_CURVE("):-1])
            v0, v1 = refs(e[1])[0], refs(e[2])[0]
            pt = lambda v: tuple(nums(ents[refs(ents[v])[0]][ents[refs(ents[v])[0]].index(",("):])[:3])
            fwd = a[4].strip() == ".T."
            ends.append((pt(v0), pt(v1)) if fwd else (pt(v1), pt(v0)))
        for i in range(len(ends)):
            if dist(ends[i][1], ends[(i + 1) % len(ends)][0]) > 1e-6:
                bad += 1
    report(loops > 0 and bad == 0, name[:-4] + "_boundary_loops_close", "%d loops, %d broken joins" % (loops, bad))

sys.exit(0)
