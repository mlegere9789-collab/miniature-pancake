#! python 3
# Dino 8 reference-output harness for Rhino (Rhino 8 Script Editor, Python 3; RhinoCommon).
# STATUS: UNTESTED against a real Rhino installation. Written from the public RhinoCommon API
# documentation. Every scenario is wrapped so a failure is recorded and the run continues.
#
# SAFETY: read-only with respect to the owner's work.
#  - It never touches the active document. All geometry lives in a headless document
#    (RhinoDoc.CreateHeadless) that is discarded at the end.
#  - It writes only inside the output folder (every path goes through safe_path()).
#  - It makes no network calls and does not change application settings.
#
# RUN: Rhino 8 > Tools > Script Editor > open this file > Run.
#      Output folder: environment variable D8_REF_OUT, else a folder chosen in a dialog,
#      else ~/dino8_reference/rhino_<timestamp>.
# EULA: counsel must confirm that scripted observation is permitted before this is run
#      (see parity/reference/README.md).
import os, sys, json, math, time, hashlib, platform, datetime, traceback

HARNESS_VERSION = "0.1.0"
import Rhino
import Rhino.Geometry as rg
import System

# ---------------------------------------------------------------- output folder
def choose_out():
    out = os.environ.get("D8_REF_OUT")
    if not out:
        try:
            import Rhino.UI
            dlg = Rhino.UI.SelectFolderDialog()
            dlg.Title = "Choose the Dino 8 reference output folder (empty folder recommended)"
            out = dlg.ShowSelectFolderDialog() if hasattr(dlg, "ShowSelectFolderDialog") else None
        except Exception:
            out = None
    if not out:
        out = os.path.join(os.path.expanduser("~"), "dino8_reference",
                           "rhino_" + datetime.datetime.utcnow().strftime("%Y%m%dT%H%M%SZ"))
    out = os.path.realpath(out)
    os.makedirs(out, exist_ok=True)
    return out

OUT = choose_out()

def safe_path(name):
    p = os.path.realpath(os.path.join(OUT, name))
    if not (p == OUT or p.startswith(OUT + os.sep)):
        raise RuntimeError("refusing to write outside the output folder: " + p)
    return p

def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def write_json(name, obj):
    with open(safe_path(name), "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2, sort_keys=True, default=str)

# ---------------------------------------------------------------- headless document
TOL = 0.001
doc = Rhino.RhinoDoc.CreateHeadless(None)
doc.ModelUnitSystem = Rhino.UnitSystem.Millimeters
doc.ModelAbsoluteTolerance = TOL
doc.ModelAngleToleranceRadians = math.radians(1.0)

# ---------------------------------------------------------------- measuring helpers
def vol(b):
    p = rg.VolumeMassProperties.Compute(b)
    return None if p is None else p.Volume
def area(g):
    p = rg.AreaMassProperties.Compute(g)
    return None if p is None else p.Area
def centroid(g):
    p = rg.VolumeMassProperties.Compute(g) if isinstance(g, rg.Brep) else rg.AreaMassProperties.Compute(g)
    return None if p is None else [p.Centroid.X, p.Centroid.Y, p.Centroid.Z]
def bbox(g):
    b = g.GetBoundingBox(True)
    return {"min": [b.Min.X, b.Min.Y, b.Min.Z], "max": [b.Max.X, b.Max.Y, b.Max.Z]}
def brep_stats(b):
    return {"valid": b.IsValid, "solid": b.IsSolid, "faces": b.Faces.Count, "edges": b.Edges.Count,
            "vertices": b.Vertices.Count, "volume": vol(b) if b.IsSolid else None,
            "area": area(b), "bbox": bbox(b), "centroid": centroid(b) if b.IsSolid else None}
def first(arr):
    return None if arr is None or len(arr) == 0 else arr[0]

def box(x=100.0, y=60.0, z=40.0):
    return rg.Brep.CreateFromBox(rg.BoundingBox(0, 0, 0, x, y, z))
def cylinder(cx, cy, z0, r, h):
    c = rg.Cylinder(rg.Circle(rg.Plane(rg.Point3d(cx, cy, z0), rg.Vector3d.ZAxis), r), h)
    return c.ToBrep(True, True)

RESULT_BREPS = {}     # name -> Brep, used by the export scenario
files_written = []

def add_to_doc(name, g):
    try:
        if isinstance(g, rg.Brep):
            doc.Objects.AddBrep(g)
        elif isinstance(g, rg.Curve):
            doc.Objects.AddCurve(g)
        elif isinstance(g, rg.Mesh):
            doc.Objects.AddMesh(g)
    except Exception:
        pass

# ---------------------------------------------------------------- scenarios (20)
S = []
def scenario(fn):
    S.append(fn); return fn

@scenario
def s01_box():
    b = box(); RESULT_BREPS["box"] = b
    return {"expected_volume": 240000.0, "expected_area": 2 * (6000 + 4000 + 2400), **brep_stats(b)}
@scenario
def s02_cylinder():
    b = cylinder(0, 0, 0, 15.0, 40.0); RESULT_BREPS["cylinder"] = b
    return {"expected_volume": math.pi * 225 * 40, **brep_stats(b)}
@scenario
def s03_sphere():
    b = rg.Brep.CreateFromSphere(rg.Sphere(rg.Point3d(0, 0, 0), 25.0))
    return {"expected_volume": 4.0 / 3.0 * math.pi * 25 ** 3, "expected_area": 4 * math.pi * 625, **brep_stats(b)}
def corner_edge_index(b):
    for e in b.Edges:
        a, c = e.PointAtStart, e.PointAtEnd
        if abs(a.X) < 1e-6 and abs(a.Y) < 1e-6 and abs(c.X) < 1e-6 and abs(c.Y) < 1e-6:
            return e.EdgeIndex
    return 0
@scenario
def s04_fillet_edge():
    b = box(); i = corner_edge_index(b)
    r = rg.Brep.CreateFilletEdges(b, [i], [5.0], [5.0], rg.BlendType.Fillet, rg.RailType.DistanceFromEdge, TOL)
    f = first(r); RESULT_BREPS["fillet"] = f
    return {"edge_index": i, "result_count": 0 if r is None else len(r),
            "expected_volume_change": -(25 - math.pi * 25 / 4) * 40, **(brep_stats(f) if f else {})}
@scenario
def s05_chamfer_edge():
    b = box(); i = corner_edge_index(b)
    r = rg.Brep.CreateFilletEdges(b, [i], [5.0], [5.0], rg.BlendType.Chamfer, rg.RailType.DistanceFromEdge, TOL)
    f = first(r)
    return {"edge_index": i, "expected_volume_change": -12.5 * 40, **(brep_stats(f) if f else {})}
@scenario
def s06_boolean_difference():
    r = rg.Brep.CreateBooleanDifference(box(), cylinder(50, 30, -5, 10.0, 50.0), TOL)
    f = first(r); RESULT_BREPS["difference"] = f
    return {"expected_volume": 240000.0 - math.pi * 100 * 40, "result_count": 0 if r is None else len(r), **(brep_stats(f) if f else {})}
@scenario
def s07_boolean_union():
    r = rg.Brep.CreateBooleanUnion([box(), cylinder(100, 30, 0, 10.0, 60.0)], TOL)
    f = first(r); RESULT_BREPS["union"] = f
    return {"result_count": 0 if r is None else len(r), **(brep_stats(f) if f else {})}
@scenario
def s08_boolean_intersection():
    r = rg.Brep.CreateBooleanIntersection(box(), cylinder(50, 30, -5, 10.0, 50.0), TOL)
    f = first(r)
    return {"expected_volume": math.pi * 100 * 40, **(brep_stats(f) if f else {})}
def rect(x, y):
    pl = rg.Polyline([rg.Point3d(0, 0, 0), rg.Point3d(x, 0, 0), rg.Point3d(x, y, 0), rg.Point3d(0, y, 0), rg.Point3d(0, 0, 0)])
    return pl.ToPolylineCurve()
@scenario
def s09_offset_curve():
    c = rect(100, 60); out = {}
    for tag, d in (("outward", -5.0), ("inward", 5.0)):
        r = c.Offset(rg.Plane.WorldXY, d, TOL, rg.CurveOffsetCornerStyle.Sharp)
        k = first(r)
        out[tag] = None if k is None else {"length": k.GetLength(), "closed": k.IsClosed, "bbox": bbox(k)}
    out["expected_length_outward_sharp"] = 2 * (110 + 70); out["expected_length_inward"] = 2 * (90 + 50)
    out["note"] = "offset side sign convention is part of what is being observed"
    return out
@scenario
def s10_circle_properties():
    c = rg.ArcCurve(rg.Circle(rg.Plane.WorldXY, 20.0))
    return {"length": c.GetLength(), "expected_length": 2 * math.pi * 20, "area": area(c), "expected_area": math.pi * 400, "centroid": centroid(c)}
@scenario
def s11_extrude_profile():
    e = rg.Extrusion.Create(rect(50, 30), 20.0, True)
    b = e.ToBrep() if e is not None else None
    return {"expected_volume": 50 * 30 * 20, **(brep_stats(b) if b else {})}
@scenario
def s12_revolve_profile():
    prof = rg.Polyline([rg.Point3d(10, 0, 0), rg.Point3d(30, 0, 0), rg.Point3d(30, 0, 20), rg.Point3d(10, 0, 20), rg.Point3d(10, 0, 0)]).ToPolylineCurve()
    rev = rg.RevolveSurface.Create(prof, rg.Line(rg.Point3d(0, 0, 0), rg.Point3d(0, 0, 1)), 0.0, 2 * math.pi)
    b = rg.Brep.CreateFromRevSurface(rev, True, True)
    return {"expected_volume": math.pi * (900 - 100) * 20, **(brep_stats(b) if b else {})}
@scenario
def s13_loft_frustum():
    c1 = rg.ArcCurve(rg.Circle(rg.Plane.WorldXY, 20.0))
    c2 = rg.ArcCurve(rg.Circle(rg.Plane(rg.Point3d(0, 0, 50), rg.Vector3d.ZAxis), 10.0))
    r = rg.Brep.CreateFromLoft([c1, c2], rg.Point3d.Unset, rg.Point3d.Unset, rg.LoftType.Straight, False)
    b = first(r)
    if b is not None:
        capped = b.CapPlanarHoles(TOL)
        b = capped if capped is not None else b
    return {"expected_volume": math.pi * 50 / 3.0 * (400 + 200 + 100), **(brep_stats(b) if b else {})}
@scenario
def s14_transform_rotate_scale():
    b = box(); b.Transform(rg.Transform.Rotation(math.radians(45), rg.Vector3d.ZAxis, rg.Point3d.Origin))
    s = box(); s.Transform(rg.Transform.Scale(rg.Plane.WorldXY, 2.0, 1.0, 0.5))
    return {"rotated": brep_stats(b), "scaled": brep_stats(s), "expected_scaled_volume": 240000.0}
@scenario
def s15_linear_array():
    parts = []
    for i in range(5):
        d = box(); d.Transform(rg.Transform.Translation(120.0 * i, 0, 0)); parts.append(d)
    bb = rg.BoundingBox.Empty
    for p in parts: bb.Union(p.GetBoundingBox(True))
    return {"count": 5, "union_bbox": {"min": [bb.Min.X, bb.Min.Y, bb.Min.Z], "max": [bb.Max.X, bb.Max.Y, bb.Max.Z]},
            "total_volume": sum(vol(p) for p in parts)}
@scenario
def s16_mesh_from_brep():
    out = {}
    for tag, mp in (("default", rg.MeshingParameters.Default), ("fine", rg.MeshingParameters.FastRenderMesh), ("smooth", rg.MeshingParameters.Smooth)):
        ms = rg.Mesh.CreateFromBrep(cylinder(0, 0, 0, 15.0, 40.0), mp)
        m = rg.Mesh(); [m.Append(x) for x in ms]
        out[tag] = {"vertices": m.Vertices.Count, "faces": m.Faces.Count, "triangles": m.Faces.TriangleCount, "quads": m.Faces.QuadCount, "area": area(m), "closed": m.IsClosed}
    return out
@scenario
def s17_curve_intersections():
    a = rg.ArcCurve(rg.Circle(rg.Plane.WorldXY, 20.0))
    l = rg.LineCurve(rg.Point3d(-30, 0, 0), rg.Point3d(30, 0, 0))
    ev = rg.Intersect.Intersection.CurveCurve(a, l, TOL, TOL)
    pts = [[e.PointA.X, e.PointA.Y, e.PointA.Z] for e in ev]
    plane_ev = rg.Intersect.Intersection.BrepPlane(box(), rg.Plane(rg.Point3d(0, 0, 20), rg.Vector3d.ZAxis), TOL)
    return {"line_circle_points": pts, "expected": [[-20, 0, 0], [20, 0, 0]], "box_plane_ok": bool(plane_ev[0]) if plane_ev else False,
            "box_plane_curve_count": len(plane_ev[1]) if plane_ev and plane_ev[1] else 0}
@scenario
def s18_offset_solid_shell():
    r = rg.Brep.CreateOffsetBrep(box(), -2.0, True, False, TOL)
    b = first(r[0]) if r and r[0] else None
    return {"note": "inward solid offset of a box by 2 mm", "expected_inner_volume": 96 * 56 * 36, **(brep_stats(b) if b else {})}
@scenario
def s19_split_by_plane():
    cutter = rg.Brep.CreateFromBox(rg.BoundingBox(-10, -10, 20, 110, 70, 20.001))
    r = box().Split(cutter, TOL)
    return {"piece_count": 0 if r is None else len(r), "pieces": [brep_stats(p) for p in (r or [])]}

@scenario
def s20_export_formats():
    src = RESULT_BREPS.get("difference") or box()
    d2 = Rhino.RhinoDoc.CreateHeadless(None)
    d2.ModelUnitSystem = Rhino.UnitSystem.Millimeters
    d2.Objects.AddBrep(src)
    res = {}
    for ext in ("3dm", "stp", "igs", "obj", "dxf", "stl", "sat", "fbx", "3mf"):
        name = "scenario20_result." + ext
        path = safe_path(name)
        try:
            if ext == "3dm":
                ok = d2.Write3dmFile(path, Rhino.FileIO.FileWriteOptions()) if hasattr(d2, "Write3dmFile") else False
            else:
                ok = d2.Export(path)
            exists = os.path.exists(path)
            res[ext] = {"ok": bool(ok), "exists": exists, "bytes": os.path.getsize(path) if exists else 0,
                        "sha256": sha256_of(path) if exists else None}
            if exists: files_written.append(name)
        except Exception as e:
            res[ext] = {"ok": False, "error": str(e)}
    try:
        f = Rhino.FileIO.File3dm.Read(safe_path("scenario20_result.3dm"))
        vs = [vol(o.Geometry) for o in f.Objects if isinstance(o.Geometry, rg.Brep)]
        res["reimport_3dm_volumes"] = vs
    except Exception as e:
        res["reimport_3dm_volumes"] = "error: " + str(e)
    d2.Dispose()
    return res

# ---------------------------------------------------------------- manifest and command list
def manifest():
    m = {"harness": "dino8-reference-rhino", "harness_version": HARNESS_VERSION,
         "tested_against_real_application": False,
         "utc": datetime.datetime.utcnow().isoformat() + "Z",
         "program": "Rhino", "os": platform.platform(), "python": sys.version,
         "output_dir": OUT, "script_sha256": sha256_of(os.path.realpath(__file__)) if "__file__" in globals() else None,
         "settings": {"units": "Millimeters", "absolute_tolerance": TOL, "angle_tolerance_deg": 1.0}}
    for k in ("Version", "ExeVersion", "ExeServiceRelease", "BuildDate", "ExePlatform", "ExeServicePack"):
        try: m["rhino_" + k] = str(getattr(Rhino.RhinoApp, k))
        except Exception: pass
    try: m["rhinocommon_assembly"] = str(Rhino.RhinoDoc.__module__)
    except Exception: pass
    return m

def command_list():
    out = {}
    try:
        out["english_names_loaded"] = sorted(list(Rhino.Commands.Command.GetCommandNames(True, True)))
        out["localized_names_loaded"] = sorted(list(Rhino.Commands.Command.GetCommandNames(False, True)))
    except Exception as e:
        out["error"] = str(e)
    try:
        out["plugins"] = sorted(str(n) for n in Rhino.PlugIns.PlugIn.GetInstalledPlugIns().Values)
    except Exception as e:
        out["plugins_error"] = str(e)
    return out

def main():
    t0 = time.time()
    meas = {}
    for fn in S:
        sid = fn.__name__
        t = time.time()
        try:
            v = fn(); meas[sid] = {"status": "ok", "values": v, "seconds": round(time.time() - t, 3)}
        except Exception as e:
            meas[sid] = {"status": "error", "error": str(e), "trace": traceback.format_exc(), "seconds": round(time.time() - t, 3)}
    write_json("commands.json", command_list())
    write_json("measurements.json", meas)
    m = manifest(); m["scenario_count"] = len(S); m["scenarios_ok"] = sum(1 for v in meas.values() if v["status"] == "ok")
    m["seconds"] = round(time.time() - t0, 2); m["files"] = sorted(os.listdir(OUT))
    write_json("manifest.json", m)
    doc.Dispose()
    print("Dino 8 reference run finished: %d/%d scenarios ok. Output: %s" % (m["scenarios_ok"], len(S), OUT))

main()
