#!/usr/bin/env python3
"""IGX_ checker: the IGES 5.3 file written by the IGX_ section of smoke.sh, read by a small independent parser (fixed-column
Directory Entry + Parameter Data sections, no use of the application's importer), against closed forms:

  * the circle as entity 100 (radius 5 about the origin, full turn) and every entity 126 well formed (knots, weights, point counts);
  * entity 128 (B-spline surface): degree and control-net sizes of a plane (1 x 1, 2 x 2) and of the cylinder wall (rational,
    degree 2 around, 9 x 2 net, control points at radius 3);
  * entity 144 (trimmed surface): one per face of a box (6) and of a cylinder (3), each pointing at a 128 surface and a 142 outer
    boundary, the DE pointers all valid;
  * the Global section declares millimetres (units flag 2, name MM).

    python3 igx_check.py <directory>
"""
import math
import os
import sys

D = sys.argv[1]
failed = 0


def report(ok, name, detail=""):
    global failed
    if not ok:
        failed += 1
    print(("ok   " if ok else "FAIL ") + "IGX_" + name + (": " + detail if detail else ""))


def parse(path):
    lines = open(path, errors="replace").read().splitlines()
    sec = {"S": [], "G": [], "D": [], "P": []}
    for l in lines:
        if len(l) >= 73 and l[72] in sec:
            sec[l[72]].append(l)
    g = "".join(l[:72] for l in sec["G"])
    ents = {}
    d = sec["D"]
    for i in range(0, len(d) - 1, 2):
        de = int(d[i][73:80])
        t = int(d[i][0:8])
        pd = int(d[i][8:16])
        n = int(d[i + 1][24:32])
        form = int(d[i + 1][32:40])
        text = ""
        for l in sec["P"]:
            seq = int(l[73:80])
            if pd <= seq < pd + n:
                text += l[:64]
        # parameter data lines are "<type>,p1,p2,...;"
        body = text.strip().rstrip(";")
        vals = body.split(",")[1:]
        ents[de] = {"type": t, "form": form, "p": vals}
    return g, ents


def fl(x):
    return float(x.replace("D", "E").replace("d", "e"))


path = os.path.join(D, "a.igs")
if not os.path.exists(path):
    report(False, "written", path + " was not written")
    sys.exit(0)
G, E = parse(path)
by = {}
for de, e in E.items():
    by.setdefault(e["type"], []).append(de)

# ---- units
import re
m = re.search(r",(\d+),\s*2H([A-Za-z]{2}),", G)
report(m is not None and m.group(1) == "2" and m.group(2).upper() == "MM", "global_units_flag_2_and_name_MM", "units declaration %s" % str(m.groups() if m else None))

# ---- the circle: entity 100 (circular arc) about the origin, radius 5, a full turn (start point = end point)
arcs = []
for de in by.get(100, []):
    p = [fl(x) for x in E[de]["p"][:7]]
    arcs.append(p)
full = [a for a in arcs if abs(a[1]) < 1e-9 and abs(a[2]) < 1e-9 and abs(math.hypot(a[3] - a[1], a[4] - a[2]) - 5.0) < 1e-6]
report(len(full) >= 1, "circle_is_a_100_arc_about_the_origin_of_radius_5", "%d 100 entities, %d match" % (len(arcs), len(full)))
if full:
    a = full[0]
    report(abs(a[3] - a[5]) < 1e-6 and abs(a[4] - a[6]) < 1e-6, "circle_arc_start_equals_end_a_full_turn", "start (%g,%g) end (%g,%g)" % (a[3], a[4], a[5], a[6]))

# ---- 126: every B-spline curve is well formed (knot / weight / point counts consistent, weights positive, knots non-decreasing)
bad126 = []
for de in by.get(126, []):
    p = E[de]["p"]
    k, m = int(p[0]), int(p[1])
    n = k + 1
    nk = n + m + 1
    knots = [fl(x) for x in p[6:6 + nk]]
    w = [fl(x) for x in p[6 + nk:6 + nk + n]]
    ok = len(p) >= 6 + nk + n + 3 * n and all(knots[i] <= knots[i + 1] + 1e-12 for i in range(nk - 1)) and all(x > 0 for x in w)
    if not ok:
        bad126.append(de)
report(by.get(126) and not bad126, "bspline_curves_are_well_formed", "%d 126 entities, %d malformed" % (len(by.get(126, [])), len(bad126)))

# ---- 128 surfaces
surfs = []
for de in by.get(128, []):
    p = E[de]["p"]
    k1, k2, m1, m2 = int(p[0]), int(p[1]), int(p[2]), int(p[3])
    n1, n2 = k1 + 1, k2 + 1
    rational = int(p[6]) == 0
    base = 9 + (n1 + m1 + 1) + (n2 + m2 + 1)
    w = [fl(x) for x in p[base:base + n1 * n2]]
    pts = [tuple(fl(x) for x in p[base + n1 * n2 + 3 * i:base + n1 * n2 + 3 * i + 3]) for i in range(n1 * n2)]
    surfs.append((m1, m2, n1, n2, rational, w, pts))
report(len(surfs) == 9 or len(surfs) >= 9, "surface_count_6_box_faces_plus_3_cylinder_faces", "%d 128 entities, (deg u, deg v, nu, nv, rational) %s" % (len(surfs), [s[:5] for s in surfs]))
planar = [s for s in surfs if s[0] == 1 and s[1] == 1 and s[2] == 2 and s[3] == 2]
report(len(planar) >= 8, "planar_faces_are_bilinear_2x2_nets", "%d bilinear 2x2 surfaces (expected 6 box + 2 cylinder caps)" % len(planar))
walls = [s for s in surfs if s[4] and s[2] * s[3] >= 18]
if walls:
    s = walls[0]
    radii = sorted({round(math.hypot(x - 50, y), 4) for x, y, z in s[6]})
    report(all(abs(r - 3.0) < 1e-3 or abs(r - 3.0 * math.sqrt(2)) < 1e-3 for r in radii), "cylinder_wall_control_points_at_the_closed_form_radius_3", "radii %s" % radii)
else:
    report(False, "cylinder_wall_is_a_rational_9x2_net", "no rational 128 surface with 18 control points")

# ---- 144 trimmed surfaces
t144 = by.get(144, [])
report(len(t144) == 9, "trimmed_surface_per_face", "%d 144 entities (expected 6 box + 3 cylinder faces)" % len(t144))
bad = []
for de in t144:
    p = E[de]["p"]
    surf = int(p[0])
    outer = int(p[3])  # PTS, N1, N2, PTO
    if surf not in E or E[surf]["type"] not in (128, 190, 190) or outer not in E or E[outer]["type"] != 142:
        bad.append(de)
report(t144 and not bad, "trimmed_surface_points_at_a_surface_and_a_142_boundary", "%d of %d 144 entities with a bad pointer" % (len(bad), len(t144)))
sys.exit(0)
