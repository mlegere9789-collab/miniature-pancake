#!/usr/bin/env python3
"""Group the Dino 8 vs Rhino golden divergences into classes and write DIVERGENCE.md.

Uses compare.py's own field comparison (imported from its file, so its tolerance classes are the only ones in play; nothing is widened
here). Run from the repository root:

  python3 -I parity/oracle/rhino/divergence_report.py --dino parity/oracle/rhino/dino_results.jsonl --out parity/oracle/rhino/DIVERGENCE.md

A golden record that carries a truthy `hazard` or `provisional` field is listed separately and is not counted as a defect. So are the
two things the Observer's own spec (specs/rhino8-observed-behaviour.md) declares unusable: BLND-BOX-0011 (a degenerate golden result,
"do not match") and the bounding box of the offset spheres (OFFS-SOL-0001 .. -0006, "mark non-comparable").
"""
import argparse, collections, glob, importlib.util, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# Declared by the spec file, not by a field in the golden record (the golden has no hazard/provisional field yet).
SPEC_HAZARD_IDS = {"BLND-BOX-0011": "spec: degenerate golden result (flagged valid, not solid, bbox reaching 22.2); do not match"}
SPEC_NONCOMPARABLE = {f"OFFS-SOL-{i:04d}": {"bbox"} for i in range(1, 7)}  # spec: offset-sphere bbox reaches 6 pi; use the geometric extent

METRIC = {"area", "volume", "length", "count", "points", "degree", "ctrlpts"}
FLAGS = {"valid", "solid"}
TOPO = {"faces", "edges", "vertices", "euler"}


def load_compare():
    spec = importlib.util.spec_from_file_location("rhino_compare", os.path.join(HERE, "compare.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def ranges(ids):
    """Compact id list: BOOL-BXSP-0010..0011, BOOL-BXSP-0017."""
    by = collections.defaultdict(list)
    for i in sorted(ids):
        m = re.match(r"^(.*)-(\d+)$", i)
        by[m.group(1)].append(int(m.group(2))) if m else by[i].append(None)
    out = []
    for pre, nums in by.items():
        if nums == [None]:
            out.append(pre)
            continue
        run = [nums[0]]
        for n in nums[1:] + [None]:
            if n is not None and n == run[-1] + 1:
                run.append(n)
                continue
            out.append(f"{pre}-{run[0]:04d}" + (f"..{run[-1]:04d}" if len(run) > 1 else ""))
            run = [n]
    return ", ".join(out)


# ---- relation of the two Boolean operands (from the case arguments only) ---------------------------------------------------
def shape_box(s):
    x, y, z = s["move"]
    p = s["prim"]
    if p == "box":
        return (x, y, z, x + s["dx"], y + s["dy"], z + s["dz"])
    if p == "sphere":
        return (x - s["r"], y - s["r"], z - s["r"], x + s["r"], y + s["r"], z + s["r"])
    if p == "cylinder" or p == "cone":
        return (x - s["r"], y - s["r"], z, x + s["r"], y + s["r"], z + s["h"])
    if p == "torus":
        e = s["R"] + s["r"]
        return (x - e, y - e, z - s["r"], x + e, y + e, z + s["r"])
    raise ValueError(p)


def bool_relation(args):
    a, b = args["a"], args["b"]
    A, B = shape_box(a), shape_box(b)
    eps = 1e-9
    if a["prim"] == b["prim"] and all(abs(u - v) < eps for u, v in zip(A, B)):
        return "identical operands"
    gap = max(max(B[i] - A[i + 3], A[i] - B[i + 3]) for i in range(3))
    if gap > eps:
        return "disjoint operands"
    if abs(gap) <= eps:
        return "operands touching"
    inside = all(B[i] >= A[i] - eps and B[i + 3] <= A[i + 3] + eps for i in range(3))
    if inside:
        strict = all(B[i] > A[i] + eps and B[i + 3] < A[i + 3] - eps for i in range(3))
        return "tool strictly inside" if strict else "tool inside, sharing part of the boundary"
    inside_rev = all(A[i] >= B[i] - eps and A[i + 3] <= B[i + 3] + eps for i in range(3))
    if inside_rev:
        return "base inside tool"
    return "overlapping operands"


def group_label(case):
    """Short label of the operation family a case belongs to (id prefix + the argument that changes the route)."""
    cid, a = case["id"], case["args"]
    pre = cid.rsplit("-", 1)[0]
    if case["op"] == "blend_edges":
        return f"{pre} {a['blend']} all edges"
    if case["op"] == "offset_solid":
        return f"{pre} {a['shape']['prim']}"
    if case["op"] == "boolean":
        return f"{pre} {a['op']}"
    if case["op"] == "make":
        return f"{pre} {a['shape']['prim']}"
    return pre


def rel_err(g, d):
    try:
        return abs(g - d) / max(abs(g), abs(d), 1e-12)
    except TypeError:
        return None


NOTES = """
## Reading the classes (what each divergence is, from the numbers in the two result files)

Nothing below was tuned: every statement is read off dino_results.jsonl and the golden, and each closed form is a standard formula.

1. **Both succeed, a metric is outside tolerance.**
   * EXTR-POLY (16) and LOFT-CIRC (4): the golden volume is NEGATIVE for a valid closed solid (the reference's orientation convention); Dino 8 returns
     the positive closed-form volume (triangle R=1, h=1: 1.29904; the frustum cases agree with pi*h*(r0^2+r0*r1+r1^2)/3). The 4 loft rows differ in
     nothing else. EXTR-POLY also differs in topology counts: the reference splits each cap boundary at the polygon corners (V=2n, E=2n+1, F=3), Dino 8
     keeps each cap boundary as one closed edge (V=4, E=5, F=3). The sign is an Observer query (below), the counts are a representation difference.
   * REVO-RECT-0001..0009 (revolve of a rectangle through 90, 180, 270 degrees, cap=true): Dino 8 returns the capped solid (3 faces, solid, V-E+F=2); the
     reference returns the single uncapped revolution surface (1 face, not solid), so area and volume differ by the two end caps. The 360 degree rows
     (REVO-RECT-0010..0012) match.
   * Booleans with a curved operand (BOOL-BXSP, BOOL-CYCY, BOOL-SPSP): area/volume off by 2e-4 .. 6e-3 on ordinary rows, 1e-2 .. 3e-1 on the thin
     slivers (BOOL-BXSP-0024, -0025, volume 0.00047 at -0025). The same rows carry far more edges than the reference (96 against 16 at BOOL-BXSP-0012, 204 against 9 at BOOL-CYCY-0005): the general
     engine bounds the trimmed faces with dense polylines of the intersection curve, which is also where the area error comes from.
   * BLND-BOX-0002..0004 (fillet of all 12 edges, r = 0.25, 0.5, 0.99): Dino 8 equals the exact rolling-ball closed form with spherical corners
     (area 6(a-2r)^2 + 12(a-2r)*pi*r/2 + 4*pi*r^2 = 21.35398, 18.56637, 12.69192 and Steiner's volume 7.69902, 6.87979, 4.25151 for a=2) to 10
     digits; the reference is 0.2%, 0.8%, 4.5% smaller in area, so its corner patches are not spherical. Same rows match at r=0.05.
   * CURV-INTP (6): the global interpolant of Dino 8 has one control point per data point (4, 6, 9 for degree 2 and 3 alike); the reference's degree 3
     curve has n+2 (6, 8, 11) and a different shape: length 0.1% .. 2.5% off, bounding box off by up to 0.2. Degree 2 control point counts agree.
   * INTS-CRCR-0001 (two identical circles) 282 hits against 1 point, INTS-CRCR-0005 (circles tangent externally) 7 hits against 1: IntersectCurves does
     not merge the cluster of hits inside the tolerance band. INTS-PLSP-0002, -0006 (plane tangent to the sphere): the exact section returns no contour
     where the reference returns the tangent point (the analytic IntersectPlaneSphere does report it: `alt` in the record). INTS-PLSP-0005 (plane
     0.001 below the pole, true circle length 0.28092): the exact section returns 0.24439. INTS-PLBX-0006 (plane on the top face of the box): 1 contour
     against the reference's 2.

2. **Both succeed, only topology counts differ (27).** Area, volume and bounding box agree.
   * BOOL-BXBX union/difference (8): Dino 8 leaves the coplanar faces of the planar engine's result split (26 faces where the reference has 12, 14 where
     it has 8, 10 where it has 6); no coplanar-face merge. BOOL-SPSP-0004, -0005, -0007: the counts differ through the polyline edges (48 edges against 2).
   * OFFS-SOL cylinder (6): Dino 8 gives 3 faces / 3 edges / 2 vertices (the cylinder with its radius and caps moved); the reference gives 4 faces and 6 or 10 edges.
   * OFFS-SOL box with positive distance (4): the reference result has 36 edges and 32 vertices (the offset corners carry extra topology) with the same
     sharp-cornered shape (area and volume equal the mitred closed forms, e.g. 59.44 and 29.568 at 0.1); Dino 8 returns the plain mitred box (12 / 8).
   * OFFS-SOL sphere (6): V=2 against the reference's V=4 and Euler 2 against 4 (a degenerate-trim sphere in the reference); area and volume agree.

3. **Dino 8 returns a result where Rhino refused (16).** The Dino 8 results are geometrically valid: box minus a sphere strictly inside it is the box with
   an inner void (volume 64 - 14.137 = 49.863, 7 faces), the intersection with an enclosed tool is the tool (14.137), a difference with a disjoint or
   touching tool returns the operand unchanged, identical boxes give the box. The spec (rhino8-observed-behaviour.md) asks Dino 8 to reproduce the
   reference's refusal classes; whether to refuse is a parity decision, recorded here and not decided here.

4. **Both succeed, only the bounding box differs (9 rows here; more inside the other classes).** See the bounding box paragraph above: where the
   closed tessellation's box agrees, the kernel's `GetTightBoundingBox()` is the cause (it takes the whole supporting surface of a trimmed face, e.g.
   -0.2..8.2 for a box union whose faces are planar).

5. **Dino 8 refuses where Rhino succeeded (8, plus BLND-BOX-0011 listed separately).**
   * BLND-BOX-0007..0010 (chamfer of all 12 edges, d = 0.05 .. 0.99): `ChamferConvexEdges` declines because the edges share vertices (its documented
     scope); chamfering the edges one after another with `ChamferConvexEdge` fails on the 9th edge ("faces meet the edge at an endpoint but none
     has the simple trihedral corner pattern"). A separate probe of 30 edge orders (the natural one, its reverse, 28 random) at d = 0.05 and 0.5 found
     none that completes. The chamfer corner (three chamfers meeting) has no kernel entry for the batch case.
   * BOOL-BXSP-0005 (box united with a sphere centred on its face plane), BOOL-CYCY-0004 (union), -0009 (difference), -0014 (intersection) (coaxial
     cylinders whose radii differ by 0.001, the document tolerance): the general engine's result cannot be sewn into a closed solid
     (`BooleanNotClosed`) or the reassembly throws (`BooleanReassemblyError`). These are the near-coincident and tolerance-edge cases.

## How the Dino 8 results were produced

`dino8-kernel/tests/extra/oracle_rhino.cpp` (test `extra::OracleRhino`, run with `DINO8_TEST_FILTER=OracleRhino`) reads cases.jsonl, runs each case in a
forked child with a 60 s alarm and prints one `D8RES` record per case; `fetch_dino_results.py` collects them from the CI job log. Operations are the
kernel calls the app reaches: Booleans take the planar engine for planar-faced operands and otherwise the general engine with SealGeneralBooleanResult and the
app's sewing fallback (the app's mesh cross-check and mesh fallback are not replicated: a mesh has no faces or validity to measure); offsets of a whole
sphere or a plain cylinder are the same primitive with the radius moved (the app's TryExactOffsetSolid), a convex planar solid goes to
OffsetSolidConvexPlanar; all-edge fillet is FilletConvexEdges, all-edge chamfer is ChamferConvexEdges and then one ChamferConvexEdge per edge; sections are
SectionBrepByPlaneJoined; curve/curve is IntersectCurves; bounding boxes are the kernel's GetTightBoundingBox; document tolerance 0.001 wherever the
kernel takes one. An operation that returns no geometry is recorded as ok=false, error `EmptyResult`. `dino_results.jsonl` is the unedited CI output.
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", default=os.path.join(HERE, "cases.jsonl"))
    ap.add_argument("--golden", default=os.path.join(HERE, "golden", "*.jsonl"))
    ap.add_argument("--dino", default=os.path.join(HERE, "dino_results.jsonl"))
    ap.add_argument("--out", default=os.path.join(HERE, "DIVERGENCE.md"))
    ap.add_argument("--json", help="also write the per-row classification as JSON lines")
    a = ap.parse_args()

    cmp = load_compare()
    cases = cmp.load([a.cases])
    golden = cmp.load(sorted(glob.glob(a.golden)))
    dino = cmp.load([a.dino])

    rows = []  # one dict per case
    for cid in sorted(golden):
        g, c = golden[cid], cases[cid]
        d = dino.get(cid)
        row = {"id": cid, "family": c["family"], "op": c["op"], "label": group_label(c), "status": None, "class": None, "diffs": [],
               "g_err": g.get("error"), "d_err": None, "note": ""}
        marked = [k for k in ("hazard", "provisional") if g.get(k)]
        if cid in SPEC_HAZARD_IDS:
            marked.append("spec-hazard")
        if d is None:
            row.update(status="missing", **{"class": "missing"})
            rows.append(row)
            continue
        row["d_err"] = d.get("error")
        row["note"] = d.get("note", "")
        row["route"] = d.get("route", "")
        if marked:
            row.update(status="excluded", reason=", ".join(marked) + (": " + SPEC_HAZARD_IDS[cid] if cid in SPEC_HAZARD_IDS else ""))
        if g.get("ok") != d.get("ok"):
            row["status"] = row["status"] or "diverge"
            row["class"] = "dino-succeeds-rhino-refused" if d.get("ok") else "dino-refuses-rhino-succeeded"
            if d.get("ok"):
                row["relation"] = bool_relation(c["args"]) if c["op"] == "boolean" else ""
            rows.append(row)
            continue
        if not g.get("ok"):
            row["status"] = row["status"] or "match"
            rows.append(row)
            continue
        tol = float(g.get("tolerance", 0.001))
        diffs = []
        for k, gv in g["out"].items():
            if k in SPEC_NONCOMPARABLE.get(cid, ()):
                continue
            dv = d.get("out", {}).get(k)
            if dv is None or not cmp.cmp_out(k, gv, dv, tol):
                diffs.append({"field": k, "rhino": gv, "dino": dv})
        row["diffs"] = diffs
        for x in diffs:
            if x["field"] == "volume" and x["dino"] is not None and x["rhino"] * x["dino"] < 0 and cmp.close(abs(x["rhino"]), abs(x["dino"]), 1e-4, tol):
                row["volume_sign_only"] = True
            if x["field"] == "bbox" and d.get("bbox_mesh"):
                row["bbox_mesh_agrees"] = bool(cmp.cmp_out("bbox", x["rhino"], d["bbox_mesh"], tol))
        if not diffs:
            row["status"] = row["status"] or "match"
        else:
            row["status"] = row["status"] or "diverge"
            fields = {x["field"] for x in diffs}
            if fields & METRIC:
                row["class"] = "metric-mismatch"
            elif fields & FLAGS:
                row["class"] = "flag-mismatch"
            elif "bbox" in fields:
                row["class"] = "bbox-only"
            else:
                row["class"] = "topology-counts-only"
        rows.append(row)

    n = collections.Counter(r["status"] for r in rows)
    defects = [r for r in rows if r["status"] == "diverge"]
    excluded = [r for r in rows if r["status"] == "excluded"]
    by_class = collections.defaultdict(list)
    for r in defects:
        by_class[r["class"]].append(r)

    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            for r in rows:
                f.write(json.dumps(r) + "\n")

    builds = sorted({d.get("build", "?") for d in dino.values()})
    gbuilds = sorted({g.get("build", "?") for g in golden.values()})
    L = []
    w = L.append
    w("# Dino 8 vs Rhino 8 divergence report")
    w("")
    w(f"Dino 8 build `{', '.join(builds)}` (kernel runner `dino8-kernel/tests/extra/oracle_rhino.cpp`, run by CI through kvlite), Rhino golden build "
      f"`{', '.join(gbuilds)}`, {len(golden)} cases, document tolerance 0.001. Field comparison and tolerance classes are compare.py's own, unchanged.")
    w("")
    w(f"**{n['match']} match, {len(defects)} diverge, {len(excluded)} excluded (golden marked provisional/hazard or declared unusable by the spec), "
      f"{n['missing']} missing** (compare.py alone, without the exclusions, counts everything not matching as diverging).")
    w("")
    w("Regenerate: `python3 -I parity/oracle/rhino/divergence_report.py` (reads `dino_results.jsonl`, the golden and `cases.jsonl`).")
    w("")

    # ranking
    order = ["dino-succeeds-rhino-refused", "dino-refuses-rhino-succeeded", "metric-mismatch", "flag-mismatch", "topology-counts-only", "bbox-only"]
    titles = {
        "dino-succeeds-rhino-refused": "Dino 8 returns a result where Rhino refused",
        "dino-refuses-rhino-succeeded": "Dino 8 refuses or throws where Rhino succeeded",
        "metric-mismatch": "Both succeed, a metric (area, volume, length, count, points, degree, control points) is outside tolerance",
        "flag-mismatch": "Both succeed, valid/solid flag differs (metrics agree)",
        "topology-counts-only": "Both succeed, metrics and bbox agree, only face/edge/vertex/Euler counts differ",
        "bbox-only": "Both succeed, only the bounding box differs",
    }
    ranked = sorted(by_class, key=lambda k: -len(by_class[k]))
    w("## Classes, ranked by rows blocked")
    w("")
    w("| rank | class | rows |")
    w("|---|---|---|")
    for i, k in enumerate(ranked, 1):
        w(f"| {i} | {titles[k]} | {len(by_class[k])} |")
    w("")
    w("A row is placed in exactly one class (the first that applies: outcome differs, then metric, then flag, then bbox, then counts).")
    w("")

    # field incidence (a row counts once per differing field)
    inc = collections.Counter()
    for r in defects:
        for x in r["diffs"]:
            inc[x["field"]] += 1
    if inc:
        w("Field incidence over the rows where both succeeded (a row counts once per differing field): " +
          ", ".join(f"{k} {v}" for k, v in inc.most_common()) + ".")
        w("")

    bb = [r for r in defects if "bbox_mesh_agrees" in r]
    if bb:
        yes = [r for r in bb if r["bbox_mesh_agrees"]]
        w(f"Bounding box: the kernel's tight box differs from the golden in {len(bb)} rows; for {len(yes)} of them the bounding box of the closed "
          f"tessellation (diagnostic field `bbox_mesh` in dino_results.jsonl, within the 0.001 window) agrees with the golden, so the difference there is the "
          f"kernel's tight box ignoring face trims, not a different shape: {ranges([r['id'] for r in yes])}. The other {len(bb) - len(yes)} differ either way.")
        w("")

    def group_table(rs, key, extra=None):
        g = collections.defaultdict(list)
        for r in rs:
            g[key(r)].append(r)
        out = []
        for k, v in sorted(g.items(), key=lambda kv: (-len(kv[1]), kv[0])):
            out.append((k, v))
        return out

    for k in ranked:
        rs = by_class[k]
        w(f"## {len(rs)} rows: {titles[k]}")
        w("")
        if k == "dino-succeeds-rhino-refused":
            w("| operands (from the case arguments) | operation | rows | cases |")
            w("|---|---|---|---|")
            for (rel, lab), v in group_table(rs, lambda r: (r.get("relation") or "-", r["label"])):
                w(f"| {rel} | {lab} | {len(v)} | {ranges([r['id'] for r in v])} |")
            w("")
            w("Rhino's refusal class is `RuntimeError` throughout; Dino 8 returned geometry (see `route` in dino_results.jsonl). Counts and metrics of the "
              "geometry Dino 8 returned are not compared (the golden has none).")
        elif k == "dino-refuses-rhino-succeeded":
            w("| operation | Dino 8 refusal | rows | cases |")
            w("|---|---|---|---|")
            for (lab, err), v in group_table(rs, lambda r: (r["label"], r["d_err"])):
                w(f"| {lab} | `{err}` | {len(v)} | {ranges([r['id'] for r in v])} |")
            w("")
            msgs = collections.OrderedDict()
            for r in rs:
                m = (r["note"] or "").split("; ")
                msgs.setdefault((r["label"], r["d_err"]), m[-1][:150] if m else "")
            for (lab, err), m in msgs.items():
                if m:
                    w(f"* {lab}: Dino 8 says: \"{m}\"")
        else:
            w("| operation | rows | differing fields (rows) | worst relative error in area/volume/length (volume sign flips excluded) | volume differs only by sign (rows) | cases |")
            w("|---|---|---|---|---|---|")
            for lab, v in group_table(rs, lambda r: r["label"]):
                f = collections.Counter(x["field"] for r in v for x in r["diffs"])
                worst = 0.0
                for r in v:
                    for x in r["diffs"]:
                        if x["field"] in ("area", "volume", "length") and x["dino"] is not None:
                            if x["field"] == "volume" and r.get("volume_sign_only"):
                                continue
                            e = rel_err(x["rhino"], x["dino"])
                            if e is not None:
                                worst = max(worst, e)
                signs = sum(1 for r in v if r.get("volume_sign_only"))
                w(f"| {lab} | {len(v)} | {', '.join(f'{a} {b}' for a, b in f.most_common())} | {worst:.2e} | {signs} | {ranges([r['id'] for r in v])} |")
        w("")

    # excluded
    w("## Listed separately, not counted as defects")
    w("")
    if excluded:
        w("| case | why | outcome |")
        w("|---|---|---|")
        for r in excluded:
            outcome = "dino differs" if r["class"] else "dino agrees"
            w(f"| {r['id']} | {r.get('reason', '')} | {outcome} |")
    else:
        w("None carry a `hazard`/`provisional` field in the golden records (no such field is present yet).")
    w("")
    nc = [f"{cid} ({', '.join(sorted(f))})" for cid, f in sorted(SPEC_NONCOMPARABLE.items())]
    w("Fields the spec declares non-comparable and this report leaves out of the comparison: " + "; ".join(nc) + ".")
    w("")

    # golden queries for the observer (not excluded)
    q = []
    for cid in sorted(golden):
        g = golden[cid]
        if g.get("ok") and g["out"].get("solid") and g["out"].get("valid") and g["out"].get("volume", 0) < 0:
            q.append(cid)
    sp = golden.get("BOOL-SPSP-0003")
    if q or sp:
        w("## Golden values the Observer should re-check (still counted above)")
        w("")
        w(f"* Negative volume reported for a valid closed solid (orientation convention of the reference, not a geometric volume): {ranges(q)}. "
          "Dino 8 reports the positive volume; the comparer treats the sign as a mismatch.")
        w("")

    if sp and sp.get("ok"):
        w(f"* BOOL-SPSP-0003 (union of two unit spheres whose surfaces touch at one point): the golden reports one sphere (volume {sp['out']['volume']}, "
          f"{sp['out']['faces']} face); the union of the two is two spheres, volume twice that. Dino 8 returns the two.")
        w("")
    w(NOTES.strip("\n"))
    w("")
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    print(f"match {n['match']}, diverge {len(defects)}, excluded {len(excluded)}, missing {n['missing']}")
    for k in ranked:
        print(f"  {len(by_class[k]):3d}  {k}")


if __name__ == "__main__":
    sys.exit(main())
