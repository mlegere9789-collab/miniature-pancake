#!/usr/bin/env python3
"""Deterministic generator of the Rhino oracle case corpus (cases.jsonl).

Every case is authored here from our own parameter sweeps; nothing comes from Rhino samples/templates/help.
A case is language-neutral: the Rhino runner and the Dino 8 runner interpret the same record.

Shape of a record:
  {"id": "...", "op": "<operation>", "args": {...}, "measure": ["area","volume",...], "family": "..."}
Primitive spec (used inside args):  {"prim":"box|sphere|cylinder|cone|torus", ...dims..., "move":[x,y,z]}
All lengths in model units; the runner sets the document tolerance to 0.001 and records it.
"""
import argparse, json, itertools, math, sys

MEASURE_SOLID = ["area", "volume", "bbox", "valid", "solid", "faces", "edges", "vertices", "euler"]


def prim(kind, move=(0, 0, 0), **dims):
    d = {"prim": kind, "move": list(move)}
    d.update(dims)
    return d


def cases():
    out = []

    def add(cid, op, args, measure, family):
        out.append({"id": cid, "op": op, "args": args, "measure": measure, "family": family})

    # --- primitives (closed forms exist for every output) ---
    n = 0
    for dx, dy, dz in itertools.product([1, 2.5, 10], [1, 4], [1, 7.25]):
        n += 1
        add(f"PRIM-BOX-{n:04d}", "make", {"shape": prim("box", dx=dx, dy=dy, dz=dz)}, MEASURE_SOLID, "primitive")
    for i, r in enumerate([0.5, 1, 3.75, 10], 1):
        add(f"PRIM-SPH-{i:04d}", "make", {"shape": prim("sphere", r=r)}, MEASURE_SOLID, "primitive")
    n = 0
    for r, h in itertools.product([0.5, 2, 5], [1, 3, 12]):
        n += 1
        add(f"PRIM-CYL-{n:04d}", "make", {"shape": prim("cylinder", r=r, h=h)}, MEASURE_SOLID, "primitive")
        add(f"PRIM-CON-{n:04d}", "make", {"shape": prim("cone", r=r, h=h)}, MEASURE_SOLID, "primitive")
    n = 0
    for R, r in [(5, 1), (5, 2), (10, 0.5), (3, 1.5)]:
        n += 1
        add(f"PRIM-TOR-{n:04d}", "make", {"shape": prim("torus", R=R, r=r)}, MEASURE_SOLID, "primitive")

    # --- Booleans: box vs sphere / cylinder / box sweeps over centre offset ---
    n = 0
    for bop in ["union", "difference", "intersection"]:
        for off in [0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 3.49, 3.5, 3.51]:  # box spans -2..2; sphere r=1.5 touches the x=2 face from outside at 3.5
            n += 1
            add(f"BOOL-BXSP-{n:04d}", "boolean",
                {"op": bop, "a": prim("box", dx=4, dy=4, dz=4, move=(-2, -2, -2)),
                 "b": prim("sphere", r=1.5, move=(off, 0, 0))}, MEASURE_SOLID, "boolean")
    n = 0
    for bop in ["union", "difference", "intersection"]:
        for off in [0.0, 1.0, 2.0, 3.99, 4.0, 4.01]:
            n += 1
            add(f"BOOL-BXBX-{n:04d}", "boolean",
                {"op": bop, "a": prim("box", dx=4, dy=4, dz=4), "b": prim("box", dx=4, dy=4, dz=4, move=(off, off / 2, 0))},
                MEASURE_SOLID, "boolean")
    n = 0
    for bop in ["union", "difference", "intersection"]:
        for r2 in [0.5, 0.999, 1.0, 1.001, 1.5]:  # coaxial / tangent cylinders of radius 1
            n += 1
            add(f"BOOL-CYCY-{n:04d}", "boolean",
                {"op": bop, "a": prim("cylinder", r=1, h=4),
                 "b": prim("cylinder", r=r2, h=2, move=(0, 0, 1))}, MEASURE_SOLID, "boolean")
    n = 0
    for bop in ["union", "difference"]:
        for dist in [1.5, 1.999, 2.0, 2.001, 2.5]:  # two spheres of radius 1, centre distance (tangent at 2.0)
            n += 1
            add(f"BOOL-SPSP-{n:04d}", "boolean",
                {"op": bop, "a": prim("sphere", r=1), "b": prim("sphere", r=1, move=(dist, 0, 0))},
                MEASURE_SOLID, "boolean")

    # --- fillet / chamfer of all edges of a box ---
    n = 0
    for blend in ["fillet", "chamfer"]:
        for rad in [0.05, 0.25, 0.5, 0.99, 1.0, 1.01]:  # box 2x2x2: radius 1.0 consumes the edge
            n += 1
            add(f"BLND-BOX-{n:04d}", "blend_edges",
                {"shape": prim("box", dx=2, dy=2, dz=2), "blend": blend, "radius": rad, "edges": "all"},
                MEASURE_SOLID, "blend")

    # --- offset ---
    n = 0
    for shape in [prim("sphere", r=3), prim("box", dx=4, dy=3, dz=2), prim("cylinder", r=2, h=5)]:
        for dist in [-0.5, -0.1, 0.1, 0.5, 1.0, 2.0]:
            n += 1
            add(f"OFFS-SOL-{n:04d}", "offset_solid", {"shape": shape, "distance": dist}, MEASURE_SOLID, "offset")

    # --- extrude / revolve / loft (profile curves defined by points) ---
    n = 0
    for sides, rad, h in itertools.product([3, 4, 6, 12], [1, 2.5], [1, 5]):
        n += 1
        add(f"EXTR-POLY-{n:04d}", "extrude_polygon", {"sides": sides, "radius": rad, "height": h, "cap": True},
            MEASURE_SOLID, "extrude")
    n = 0
    for angle in [90, 180, 270, 360]:
        for rr in [0.5, 1, 2]:
            n += 1
            add(f"REVO-RECT-{n:04d}", "revolve_rect",
                {"x0": 2, "x1": 2 + rr, "z0": 0, "z1": 1.5, "angle_deg": angle, "cap": True}, MEASURE_SOLID, "revolve")
    n = 0
    for r0, r1, h in [(2, 2, 3), (2, 1, 3), (1, 3, 2), (2, 0.5, 6)]:
        n += 1
        add(f"LOFT-CIRC-{n:04d}", "loft_circles", {"r0": r0, "r1": r1, "height": h, "cap": True}, MEASURE_SOLID, "loft")

    # --- intersections ---
    n = 0
    for d in [0, 0.5, 1.0, 1.9, 2.0, 2.1]:
        n += 1
        add(f"INTS-CRCR-{n:04d}", "intersect_circles",
            {"r0": 1, "r1": 1, "dist": d}, ["count", "points"], "intersect")
    n = 0
    for z in [-1.2, -1.0, 0.0, 0.5, 0.999, 1.0, 1.001, 1.2]:
        n += 1
        add(f"INTS-PLSP-{n:04d}", "section_plane_sphere", {"r": 1, "z": z}, ["count", "length"], "intersect")
    n = 0
    for z in [0.1, 0.5, 1.0, 1.5, 1.99, 2.0]:
        n += 1
        add(f"INTS-PLBX-{n:04d}", "section_plane_box", {"dx": 2, "dy": 3, "dz": 2, "z": z}, ["count", "length"], "intersect")

    # --- curves ---
    n = 0
    for npts in [4, 6, 9]:
        for deg in [2, 3]:
            n += 1
            add(f"CURV-INTP-{n:04d}", "interp_curve",
                {"points": [[i, math.sin(i * 0.9) * 1.5, math.cos(i * 0.4)] for i in range(npts)], "degree": deg},
                ["length", "bbox", "degree", "ctrlpts"], "curve")
    for i, (r, ang) in enumerate([(1, 90), (2, 180), (3, 270), (0.5, 359)], 1):
        add(f"CURV-ARC-{i:04d}", "arc", {"r": r, "angle_deg": ang}, ["length", "bbox"], "curve")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="cases.jsonl")
    a = ap.parse_args()
    cs = cases()
    ids = [c["id"] for c in cs]
    assert len(ids) == len(set(ids)), "duplicate case ids"
    with open(a.out, "w", encoding="utf-8") as f:
        for c in cs:
            f.write(json.dumps(c, sort_keys=True, separators=(",", ":")) + "\n")
    fam = {}
    for c in cs:
        fam[c["family"]] = fam.get(c["family"], 0) + 1
    print(f"wrote {len(cs)} cases to {a.out}: {fam}")


if __name__ == "__main__":
    sys.exit(main())
