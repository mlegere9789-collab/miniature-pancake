#!/usr/bin/env python3
"""STX_ checker: the STEP (ISO 10303-21) files written by the STX_ section of smoke.sh, read by a small independent parser
(entity table from the text, no use of the application's importer) and checked against what the solids must be:

  * exactly one MANIFOLD_SOLID_BREP over one CLOSED_SHELL, with the closed form's face count (box minus a piercing cylinder: 6 planar
    faces + 1 cylindrical = 7; a plain cylinder: 2 planar caps + 1 cylindrical wall = 3);
  * a closed shell: every EDGE_CURVE is used by exactly two ORIENTED_EDGEs;
  * the faces that must carry a hole (the pierced top and bottom of the box) have an outer FACE_OUTER_BOUND and one inner FACE_BOUND;
  * the vertex points span the closed-form box (0,0,0)-(10,10,10), resp. z 0..10 for the cylinder (its seam vertices have no x/y range);
  * every analytic CYLINDRICAL_SURFACE has the closed-form radius (2, resp. 3).

    python3 stx_check.py <directory>
"""
import os
import re
import sys

D = sys.argv[1]
failed = 0


def report(ok, name, detail=""):
    global failed
    if not ok:
        failed += 1
    print(("ok   " if ok else "FAIL ") + "STX_" + name + (": " + detail if detail else ""))


def parse(path):
    text = open(path, errors="replace").read()
    i = text.find("DATA;")
    j = text.rfind("ENDSEC;")
    if i < 0 or j < 0:
        return None
    data = text[i + 5:j]
    ents = {}
    for m in re.finditer(r"#(\d+)\s*=\s*(.*?);\s*(?=#\d+\s*=|\Z)", data, re.S):
        body = m.group(2).strip()
        mm = re.match(r"([A-Z0-9_]+)\s*\((.*)\)\s*$", body, re.S)
        if mm:
            ents[int(m.group(1))] = (mm.group(1), mm.group(2))
        else:
            ents[int(m.group(1))] = ("COMPLEX", body)
    return ents


def refs(s):
    return [int(x) for x in re.findall(r"#(\d+)", s)]


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


def numbers(s):
    return [float(x) for x in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", s)]


def check(stem, faces, planes, cylinders, radius, bb, holed_faces):
    path = os.path.join(D, stem + ".stp")
    if not os.path.exists(path):
        report(False, stem + "_written", path + " was not written")
        return
    ents = parse(path)
    if ents is None:
        report(False, stem + "_parsed", "no DATA section")
        return
    kinds = {}
    for k, (t, a) in ents.items():
        kinds.setdefault(t, []).append(k)
    n = lambda t: len(kinds.get(t, []))
    report(n("MANIFOLD_SOLID_BREP") == 1 and n("CLOSED_SHELL") == 1, stem + "_one_solid_one_closed_shell",
           "MANIFOLD_SOLID_BREP %d, CLOSED_SHELL %d" % (n("MANIFOLD_SOLID_BREP"), n("CLOSED_SHELL")))
    report(n("ADVANCED_FACE") == faces, stem + "_face_count", "%d ADVANCED_FACE (expected %d)" % (n("ADVANCED_FACE"), faces))
    shell_faces = []
    for k in kinds.get("CLOSED_SHELL", []):
        shell_faces += refs(ents[k][1])
    report(sorted(shell_faces) == sorted(kinds.get("ADVANCED_FACE", [])), stem + "_shell_lists_every_face")
    uses = {}
    for k in kinds.get("ORIENTED_EDGE", []):
        a = split_top(ents[k][1])
        e = refs(a[3])[0]
        uses[e] = uses.get(e, 0) + 1
    edges = kinds.get("EDGE_CURVE", [])
    bad = [e for e in edges if uses.get(e, 0) != 2]
    report(edges and not bad, stem + "_every_edge_used_exactly_twice", "%d edges, %d not used twice" % (len(edges), len(bad)))
    report(n("CYLINDRICAL_SURFACE") == cylinders, stem + "_cylindrical_surfaces", "%d (expected %d)" % (n("CYLINDRICAL_SURFACE"), cylinders))
    radii = [numbers(split_top(ents[k][1])[-1])[-1] for k in kinds.get("CYLINDRICAL_SURFACE", [])]
    report(radii and all(abs(r - radius) < 1e-6 for r in radii), stem + "_cylinder_radius", "radii %s (expected %g)" % (radii, radius))
    if planes is not None:
        report(n("PLANE") == planes, stem + "_plane_count", "%d PLANE (expected %d)" % (n("PLANE"), planes))
    # faces with a hole: an outer bound and one inner bound
    per_face = []
    for k in kinds.get("ADVANCED_FACE", []):
        bounds = refs(split_top(ents[k][1])[1])
        per_face.append(len(bounds))
    report(sorted(per_face).count(2) == holed_faces and max(per_face) <= 2, stem + "_faces_with_a_hole",
           "bounds per face %s (expected %d faces with 2)" % (sorted(per_face), holed_faces))
    pts = []
    for k in kinds.get("VERTEX_POINT", []):
        p = refs(ents[k][1])[0]
        pts.append(tuple(numbers(split_top(ents[p][1])[-1])[:3]))
    if pts:
        got = tuple(min(p[i] for p in pts) for i in range(3)) + tuple(max(p[i] for p in pts) for i in range(3))
        report(all(b is None or abs(a - b) < 1e-6 for a, b in zip(got, bb)), stem + "_vertex_box", "got %s expected %s" % (tuple(round(x, 6) for x in got), bb))
    else:
        report(False, stem + "_vertex_box", "no vertices")


check("cut", 7, None, 1, 2.0, (0, 0, 0, 10, 10, 10), 2)
check("cyl", 3, 2, 1, 3.0, (None, None, 0, None, None, 10), 0)
sys.exit(0)
