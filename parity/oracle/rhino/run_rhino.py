# Rhino 8 oracle runner. UNTESTED: written without access to Rhino; the first run must be a 5-case smoke (--limit 5).
#
# Run inside Rhino 8 (Script Editor, Python 3, or `rhinocode script run_rhino.py -- ...`), using ONLY public RhinoCommon
# API calls on geometry built from cases.jsonl. It records measured numbers and flags, never Rhino-created data.
#
#   args (environment variables, since Rhino's script runner has no argv convention):
#     D8_CASES   path to cases.jsonl      D8_OUT   path to write golden jsonl
#     D8_LIMIT   optional max cases       D8_ONLY  optional comma list of case-id prefixes
#
# Clean-room: this file is Observer-side tooling. It contains no Dino 8 implementation and no vendor text.
import json, math, os, sys, time, traceback

import Rhino
import Rhino.Geometry as rg
import scriptcontext as sc

TOL = 0.001


def _doc_tol():
    doc = sc.doc if sc.doc is not None else Rhino.RhinoDoc.ActiveDoc
    if doc is not None:
        try:
            doc.ModelAbsoluteTolerance = TOL
            return doc.ModelAbsoluteTolerance
        except Exception:
            pass
    return TOL


def _plane(move=(0, 0, 0)):
    return rg.Plane(rg.Point3d(*move), rg.Vector3d.ZAxis)


def make_prim(p):
    k = p["prim"]
    mv = p.get("move", [0, 0, 0])
    if k == "box":
        bb = rg.BoundingBox(mv[0], mv[1], mv[2], mv[0] + p["dx"], mv[1] + p["dy"], mv[2] + p["dz"])
        return rg.Brep.CreateFromBox(bb)
    if k == "sphere":
        return rg.Sphere(rg.Point3d(*mv), p["r"]).ToBrep()
    if k == "cylinder":
        cyl = rg.Cylinder(rg.Circle(_plane(mv), p["r"]), p["h"])
        return cyl.ToBrep(True, True)
    if k == "cone":
        cone = rg.Cone(_plane(mv), p["h"], p["r"])
        return cone.ToBrep(True)
    if k == "torus":
        t = rg.Torus(_plane(mv), p["R"], p["r"])
        return rg.Brep.CreateFromRevSurface(t.ToRevSurface(), False, False)
    raise ValueError("unknown prim " + k)


def _first(res):
    if res is None:
        return None
    try:
        res = list(res)
    except TypeError:
        return res
    return res[0] if res else None


def measure_brep(b, want):
    out = {}
    for m in want:
        if m == "area":
            out["area"] = rg.AreaMassProperties.Compute(b).Area
        elif m == "volume":
            vp = rg.VolumeMassProperties.Compute(b)
            out["volume"] = vp.Volume if vp is not None else None
        elif m == "bbox":
            bb = b.GetBoundingBox(True)
            out["bbox"] = [bb.Min.X, bb.Min.Y, bb.Min.Z, bb.Max.X, bb.Max.Y, bb.Max.Z]
        elif m == "valid":
            out["valid"] = bool(b.IsValid)
        elif m == "solid":
            out["solid"] = bool(b.IsSolid)
        elif m == "faces":
            out["faces"] = b.Faces.Count
        elif m == "edges":
            out["edges"] = b.Edges.Count
        elif m == "vertices":
            out["vertices"] = b.Vertices.Count
        elif m == "euler":
            out["euler"] = b.Vertices.Count - b.Edges.Count + b.Faces.Count
    return out


def run_case(c, tol):
    op, a, want = c["op"], c["args"], c["measure"]
    if op == "make":
        return measure_brep(make_prim(a["shape"]), want)
    if op == "boolean":
        A, B = make_prim(a["a"]), make_prim(a["b"])
        if a["op"] == "union":
            res = rg.Brep.CreateBooleanUnion([A, B], tol)
        elif a["op"] == "difference":
            res = rg.Brep.CreateBooleanDifference(A, B, tol)
        else:
            res = rg.Brep.CreateBooleanIntersection(A, B, tol)
        res = list(res) if res is not None else []
        if not res:
            raise RuntimeError("boolean returned no result")
        if len(res) > 1:  # several pieces: sum additive measures, report piece count through faces/edges sums
            tot = {}
            for piece in res:
                for k, v in measure_brep(piece, [m for m in want if m in ("area", "volume", "faces", "edges", "vertices")]).items():
                    tot[k] = tot.get(k, 0) + v
            bb = rg.BoundingBox.Empty
            for piece in res:
                bb.Union(piece.GetBoundingBox(True))
            tot["bbox"] = [bb.Min.X, bb.Min.Y, bb.Min.Z, bb.Max.X, bb.Max.Y, bb.Max.Z]
            tot["valid"] = all(p.IsValid for p in res)
            tot["solid"] = all(p.IsSolid for p in res)
            tot["euler"] = sum(p.Vertices.Count - p.Edges.Count + p.Faces.Count for p in res)
            return {k: v for k, v in tot.items() if k in want}
        return measure_brep(res[0], want)
    if op == "blend_edges":
        b = make_prim(a["shape"])
        idx = list(range(b.Edges.Count))
        r = [a["radius"]] * len(idx)
        bt = rg.BlendType.Fillet if a["blend"] == "fillet" else rg.BlendType.Chamfer
        res = rg.Brep.CreateFilletEdges(b, idx, r, r, bt, rg.RailType.DistanceFromEdge, tol)
        piece = _first(res)
        if piece is None:
            raise RuntimeError("blend returned no result")
        return measure_brep(piece, want)
    if op == "offset_solid":
        b = make_prim(a["shape"])
        # solid=False: for an already-closed input the call returns the single closed offset brep; solid=True returns an empty array.
        res = rg.Brep.CreateOffsetBrep(b, a["distance"], False, True, tol)
        # out-parameters are returned as extra tuple members in Python 3; first member is the Brep array
        arr = res[0] if isinstance(res, tuple) else res
        piece = _first(arr)
        if piece is None:
            raise RuntimeError("offset returned no result")
        return measure_brep(piece, want)
    if op == "extrude_polygon":
        n, rad, h = a["sides"], a["radius"], a["height"]
        pts = [rg.Point3d(rad * math.cos(2 * math.pi * i / n), rad * math.sin(2 * math.pi * i / n), 0) for i in range(n)]
        pts.append(pts[0])
        crv = rg.PolylineCurve(pts)
        srf = rg.Surface.CreateExtrusion(crv, rg.Vector3d(0, 0, h))
        b = srf.ToBrep()
        b = b.CapPlanarHoles(tol) if a.get("cap") else b
        if b is None:
            raise RuntimeError("cap failed")
        return measure_brep(b, want)
    if op == "revolve_rect":
        pts = [rg.Point3d(a["x0"], 0, a["z0"]), rg.Point3d(a["x1"], 0, a["z0"]), rg.Point3d(a["x1"], 0, a["z1"]),
               rg.Point3d(a["x0"], 0, a["z1"]), rg.Point3d(a["x0"], 0, a["z0"])]
        crv = rg.PolylineCurve(pts)
        axis = rg.Line(rg.Point3d(0, 0, 0), rg.Point3d(0, 0, 1))
        rev = rg.RevSurface.Create(crv, axis, 0.0, math.radians(a["angle_deg"]))
        b = rg.Brep.CreateFromRevSurface(rev, a.get("cap", True), a.get("cap", True))
        if b is None:
            raise RuntimeError("revolve failed")
        return measure_brep(b, want)
    if op == "loft_circles":
        c0 = rg.Circle(rg.Plane(rg.Point3d(0, 0, 0), rg.Vector3d.ZAxis), a["r0"]).ToNurbsCurve()
        c1 = rg.Circle(rg.Plane(rg.Point3d(0, 0, a["height"]), rg.Vector3d.ZAxis), a["r1"]).ToNurbsCurve()
        res = rg.Brep.CreateFromLoft([c0, c1], rg.Point3d.Unset, rg.Point3d.Unset, rg.LoftType.Normal, False)
        b = _first(res)
        if b is None:
            raise RuntimeError("loft failed")
        if a.get("cap"):
            b = b.CapPlanarHoles(tol)
        return measure_brep(b, want)
    if op == "intersect_circles":
        A = rg.Circle(rg.Plane(rg.Point3d(0, 0, 0), rg.Vector3d.ZAxis), a["r0"]).ToNurbsCurve()
        B = rg.Circle(rg.Plane(rg.Point3d(a["dist"], 0, 0), rg.Vector3d.ZAxis), a["r1"]).ToNurbsCurve()
        ev = rg.Intersect.Intersection.CurveCurve(A, B, tol, tol)
        pts = [] if ev is None else [[e.PointA.X, e.PointA.Y, e.PointA.Z] for e in ev]
        return {"count": len(pts), "points": pts}
    if op == "section_plane_sphere":
        sph = rg.Sphere(rg.Point3d.Origin, a["r"])
        pl = rg.Plane(rg.Point3d(0, 0, a["z"]), rg.Vector3d.ZAxis)
        res = rg.Intersect.Intersection.PlaneSphere(pl, sph)
        if isinstance(res, tuple):
            kind, circle = res[0], res[1]
        else:
            kind, circle = res, None
        kname = str(kind)  # enum member names are "None" / "Point" / "Circle"; compare by name (the Python binding exposes None as NONE)
        if kname == "Circle":
            return {"count": 1, "length": circle.Circumference}
        if kname == "None":
            return {"count": 0, "length": 0.0}
        return {"count": 1, "length": 0.0}  # Point (tangent contact)
    if op == "section_plane_box":
        b = rg.Brep.CreateFromBox(rg.BoundingBox(0, 0, 0, a["dx"], a["dy"], a["dz"]))
        pl = rg.Plane(rg.Point3d(0, 0, a["z"]), rg.Vector3d.ZAxis)
        res = rg.Intersect.Intersection.BrepPlane(b, pl, tol)
        ok, crvs = (res[0], res[1]) if isinstance(res, tuple) else (True, res)
        crvs = list(crvs) if crvs is not None else []
        return {"count": len(crvs), "length": sum(cv.GetLength() for cv in crvs)}
    if op == "interp_curve":
        pts = [rg.Point3d(*p) for p in a["points"]]
        cv = rg.Curve.CreateInterpolatedCurve(pts, a["degree"])
        bb = cv.GetBoundingBox(True)
        nc = cv.ToNurbsCurve()
        return {"length": cv.GetLength(), "bbox": [bb.Min.X, bb.Min.Y, bb.Min.Z, bb.Max.X, bb.Max.Y, bb.Max.Z],
                "degree": cv.Degree, "ctrlpts": nc.Points.Count}
    if op == "arc":
        arc = rg.Arc(rg.Circle(rg.Plane(rg.Point3d.Origin, rg.Vector3d.ZAxis), a["r"]), math.radians(a["angle_deg"]))
        cv = rg.ArcCurve(arc)
        bb = cv.GetBoundingBox(True)
        return {"length": cv.GetLength(), "bbox": [bb.Min.X, bb.Min.Y, bb.Min.Z, bb.Max.X, bb.Max.Y, bb.Max.Z]}
    raise ValueError("unknown op " + op)


def _round(o):
    if isinstance(o, float):
        return float("%.12g" % o)
    if isinstance(o, list):
        return [_round(x) for x in o]
    if isinstance(o, dict):
        return {k: _round(v) for k, v in o.items()}
    return o


def main():
    cases_path = os.environ["D8_CASES"]
    out_path = os.environ["D8_OUT"]
    limit = int(os.environ.get("D8_LIMIT", "0") or 0)
    only = [s for s in os.environ.get("D8_ONLY", "").split(",") if s]
    tol = _doc_tol()
    build = str(Rhino.RhinoApp.Version) if hasattr(Rhino.RhinoApp, "Version") else "unknown"
    n = 0
    with open(cases_path, encoding="utf-8") as fin, open(out_path, "w", encoding="utf-8") as fout:
        for line in fin:
            if not line.strip():
                continue
            c = json.loads(line)
            if only and not any(c["id"].startswith(p) for p in only):
                continue
            if limit and n >= limit:
                break
            n += 1
            t0 = time.time()
            rec = {"id": c["id"], "build": build, "tolerance": tol}
            try:
                rec["out"] = _round(run_case(c, tol))
                rec["ok"] = True
            except Exception as e:  # Rhino's refusal/exception type IS data (behaviour on hard cases)
                rec["ok"] = False
                rec["error"] = type(e).__name__
                rec["out"] = {}
            rec["ms"] = int((time.time() - t0) * 1000)
            fout.write(json.dumps(rec, sort_keys=True, separators=(",", ":")) + "\n")
    print("wrote %d golden records to %s (build %s, tolerance %g)" % (n, out_path, build, tol))


main()
