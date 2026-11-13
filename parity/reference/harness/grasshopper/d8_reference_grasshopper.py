#! python 3
# Dino 8 reference-output harness for Grasshopper (run from the Rhino 8 Script Editor, Python 3).
# STATUS: UNTESTED against a real Rhino/Grasshopper installation. Written from the public
# Grasshopper SDK documentation (Grasshopper.Kernel, GH_Document, GH_ComponentServer).
#
# SAFETY: read-only with respect to the owner's work. It never touches an open Grasshopper
# document: it builds its own GH_Document objects in memory, solves them, reads the outputs and
# discards them. Writes only inside the output folder. No network. No settings changes.
#
# PREPARE: start Rhino 8, type the command Grasshopper once (so the plug-in is loaded), then run
# this file from Tools > Script Editor. Output folder: D8_REF_OUT or a dialog, else
# ~/dino8_reference/grasshopper_<timestamp>.
# EULA: counsel must confirm that scripted observation is permitted before this is run.
import os, sys, json, math, time, hashlib, platform, datetime, traceback
HARNESS_VERSION = "0.1.0"
import Rhino
import Rhino.Geometry as rg
import System
import Grasshopper
from Grasshopper.Kernel import GH_Document
from Grasshopper.Kernel.Data import GH_Path
from Grasshopper.Kernel.Types import GH_Number, GH_Integer, GH_Point, GH_Boolean, GH_String

def choose_out():
    out = os.environ.get("D8_REF_OUT")
    if not out:
        try:
            import Rhino.UI
            dlg = Rhino.UI.SelectFolderDialog(); dlg.Title = "Choose the Dino 8 reference output folder"
            out = dlg.ShowSelectFolderDialog()
        except Exception:
            out = None
    if not out:
        out = os.path.join(os.path.expanduser("~"), "dino8_reference", "grasshopper_" + datetime.datetime.utcnow().strftime("%Y%m%dT%H%M%SZ"))
    out = os.path.realpath(out); os.makedirs(out, exist_ok=True); return out
OUT = choose_out()
def safe_path(n):
    p = os.path.realpath(os.path.join(OUT, n))
    if not (p == OUT or p.startswith(OUT + os.sep)): raise RuntimeError("outside output folder: " + p)
    return p
def write_json(n, o):
    with open(safe_path(n), "w", encoding="utf-8") as f: json.dump(o, f, indent=2, sort_keys=True, default=str)
def sha256_of(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""): h.update(c)
    return h.hexdigest()

SERVER = Grasshopper.Instances.ComponentServer

# ---------------------------------------------------------------- component catalogue (enumerable)
def catalogue():
    rows = []
    for p in SERVER.ObjectProxies:
        try:
            d = p.Desc
            rows.append({"name": d.Name, "nickname": d.NickName, "category": d.Category, "subcategory": d.SubCategory,
                         "guid": str(p.Guid), "kind": str(p.Kind), "exposure": str(d.Exposure), "obsolete": bool(p.Obsolete),
                         "library": str(p.LibraryGuid), "description": d.Description})
        except Exception as e:
            rows.append({"error": str(e)})
    return rows
def libraries():
    out = []
    try:
        for a in Grasshopper.Instances.ComponentServer.Libraries:
            out.append({"name": a.Name, "version": a.Version, "author": a.AuthorName, "id": str(a.Id), "location": a.Location})
    except Exception as e:
        out.append({"error": str(e)})
    return out

# ---------------------------------------------------------------- headless document helpers
def find(name, category=None, sub=None):
    best = None
    for p in SERVER.ObjectProxies:
        d = p.Desc
        if p.Obsolete or d.Name != name: continue
        if category and d.Category != category: continue
        if sub and d.SubCategory != sub: continue
        best = p; break
    if best is None: raise RuntimeError("component not found: %s/%s/%s" % (category, sub, name))
    return best.CreateInstance()

def num(v): return GH_Number(float(v))
def pt(x, y, z): return GH_Point(rg.Point3d(x, y, z))

def run(doc, comp, inputs):
    """inputs: list of per-input lists of GH types (or None to leave empty). Returns list of outputs, each a list of python values."""
    doc.AddObject(comp, False)
    for i, vals in enumerate(inputs):
        if vals is None: continue
        prm = comp.Params.Input[i]
        prm.PersistentData.Clear()
        for v in vals: prm.PersistentData.Append(v)
    comp.ExpireSolution(False)
    doc.NewSolution(False)
    res = []
    for o in comp.Params.Output:
        items = []
        for br in o.VolatileData.Branches:
            for g in br:
                try: items.append(g.Value if hasattr(g, "Value") else g)
                except Exception: items.append(g)
        res.append(items)
    msgs = [str(m) for m in comp.RuntimeMessages(Grasshopper.Kernel.GH_RuntimeMessageLevel.Error)] if hasattr(comp, "RuntimeMessages") else []
    return res, msgs

def bb(g):
    b = g.GetBoundingBox(True); return {"min": [b.Min.X, b.Min.Y, b.Min.Z], "max": [b.Max.X, b.Max.Y, b.Max.Z]}
def describe(x):
    if isinstance(x, (int, float, bool, str)) or x is None: return x
    if isinstance(x, rg.Point3d): return [x.X, x.Y, x.Z]
    if isinstance(x, rg.Vector3d): return [x.X, x.Y, x.Z]
    if isinstance(x, rg.Brep):
        p = rg.VolumeMassProperties.Compute(x) if x.IsSolid else None
        return {"type": "Brep", "solid": x.IsSolid, "faces": x.Faces.Count, "volume": None if p is None else p.Volume, "bbox": bb(x)}
    if isinstance(x, rg.Curve): return {"type": "Curve", "length": x.GetLength(), "closed": x.IsClosed, "bbox": bb(x)}
    if isinstance(x, rg.Mesh): return {"type": "Mesh", "vertices": x.Vertices.Count, "faces": x.Faces.Count}
    if isinstance(x, rg.Surface): return {"type": "Surface", "bbox": bb(x)}
    return str(x)
def outs(res): return [[describe(v) for v in o] for o in res]

S = []
def scenario(fn): S.append(fn); return fn
def one(name, inputs, cat=None, sub=None):
    doc = GH_Document()
    try:
        comp = find(name, cat, sub)
        res, msgs = run(doc, comp, inputs)
        return {"component": name, "outputs": outs(res), "errors": msgs, "input_names": [p.Name for p in comp.Params.Input], "output_names": [p.Name for p in comp.Params.Output]}
    finally:
        doc.Dispose() if hasattr(doc, "Dispose") else None

@scenario
def s01_series():            return one("Series", [[num(0)], [num(2.5)], [GH_Integer(6)]], "Sets", "Sequence")
@scenario
def s02_range():             return one("Range", [[ GH_Number(0)], [num(10)]], "Sets", "Sequence")
@scenario
def s03_addition():          return one("Addition", [[num(3.5)], [num(4.25)]], "Maths", "Operators")
@scenario
def s04_circle_cnr():        return one("Circle", [[GH_Point(rg.Point3d.Origin)], [num(20)]], "Curve", "Primitive")
@scenario
def s05_box_volume():     return _box_volume()
def _box_volume():
    doc = GH_Document()
    brep = rg.Brep.CreateFromBox(rg.BoundingBox(0, 0, 0, 100, 60, 40))
    comp = find("Volume", "Surface", "Analysis")
    res, msgs = run(doc, comp, [[brep]])
    return {"component": "Volume", "outputs": outs(res), "errors": msgs, "expected_volume": 240000.0}
@scenario
def s06_boolean_difference():
    doc = GH_Document()
    a = rg.Brep.CreateFromBox(rg.BoundingBox(0, 0, 0, 100, 60, 40))
    b = rg.Cylinder(rg.Circle(rg.Plane(rg.Point3d(50, 30, -5), rg.Vector3d.ZAxis), 10.0), 50.0).ToBrep(True, True)
    comp = find("Solid Difference", "Intersect", "Shape")
    res, msgs = run(doc, comp, [[a], [b]])
    return {"outputs": outs(res), "errors": msgs, "expected_volume": 240000.0 - math.pi * 100 * 40}
@scenario
def s07_offset_curve():
    doc = GH_Document()
    c = rg.Polyline([rg.Point3d(0, 0, 0), rg.Point3d(100, 0, 0), rg.Point3d(100, 60, 0), rg.Point3d(0, 60, 0), rg.Point3d(0, 0, 0)]).ToPolylineCurve()
    comp = find("Offset Curve", "Curve", "Util")
    res, msgs = run(doc, comp, [[c], [num(5)], None, None, [GH_Integer(1)]])
    return {"outputs": outs(res), "errors": msgs, "input_names": [p.Name for p in comp.Params.Input], "expected_length_sharp_outward": 360.0}
@scenario
def s08_move():              return one("Move", [[rg.Brep.CreateFromBox(rg.BoundingBox(0, 0, 0, 10, 10, 10))], [GH_Point(rg.Point3d(5, 5, 5))]], "Transform", "Euclidean")
@scenario
def s09_rotate():
    return one("Rotate", [[rg.Brep.CreateFromBox(rg.BoundingBox(0, 0, 0, 100, 60, 40))], [num(math.radians(45))], None], "Transform", "Euclidean")
@scenario
def s10_scale():
    return one("Scale", [[rg.Brep.CreateFromBox(rg.BoundingBox(0, 0, 0, 100, 60, 40))], None, [num(2)]], "Transform", "Affine")
@scenario
def s11_divide_curve():
    return one("Divide Curve", [[rg.ArcCurve(rg.Circle(rg.Plane.WorldXY, 20.0))], [GH_Integer(8)], None], "Curve", "Division")
@scenario
def s12_length_area():
    d = GH_Document(); c = rg.ArcCurve(rg.Circle(rg.Plane.WorldXY, 20.0))
    r1, m1 = run(d, find("Length", "Curve", "Analysis"), [[c]])
    r2, m2 = run(d, find("Area", "Surface", "Analysis"), [[c]])
    return {"length": outs(r1), "area": outs(r2), "expected_length": 2 * math.pi * 20, "expected_area": math.pi * 400, "errors": m1 + m2}
@scenario
def s13_linear_array():
    return one("Linear Array", [[rg.Brep.CreateFromBox(rg.BoundingBox(0, 0, 0, 10, 10, 10))], [GH_Point(rg.Point3d(20, 0, 0))], [GH_Integer(5)]], "Transform", "Array")
@scenario
def s14_loft():
    c1 = rg.ArcCurve(rg.Circle(rg.Plane.WorldXY, 20.0)); c2 = rg.ArcCurve(rg.Circle(rg.Plane(rg.Point3d(0, 0, 50), rg.Vector3d.ZAxis), 10.0))
    return one("Loft", [[c1, c2], None], "Surface", "Freeform")
@scenario
def s15_extrude():
    c = rg.Polyline([rg.Point3d(0, 0, 0), rg.Point3d(50, 0, 0), rg.Point3d(50, 30, 0), rg.Point3d(0, 30, 0), rg.Point3d(0, 0, 0)]).ToPolylineCurve()
    return one("Extrude", [[c], [rg.Vector3d(0, 0, 20)]], "Surface", "Freeform")
@scenario
def s16_revolve():
    prof = rg.Polyline([rg.Point3d(10, 0, 0), rg.Point3d(30, 0, 0), rg.Point3d(30, 0, 20), rg.Point3d(10, 0, 20), rg.Point3d(10, 0, 0)]).ToPolylineCurve()
    return one("Revolve", [[prof], [rg.Line(rg.Point3d.Origin, rg.Point3d(0, 0, 1))], [num(2 * math.pi)]], "Surface", "Freeform")
@scenario
def s17_mesh_brep():
    return one("Mesh Brep", [[rg.Brep.CreateFromBox(rg.BoundingBox(0, 0, 0, 100, 60, 40))], None], "Mesh", "Triangulation")
@scenario
def s18_sort_cull_lists():
    d = GH_Document()
    r1, m1 = run(d, find("Sort List", "Sets", "Sequence"), [[num(v) for v in (5, 3, 9, 1)], [num(v) for v in (5, 3, 9, 1)]])
    r2, m2 = run(d, find("Cull Nth", "Sets", "Sequence"), [[num(v) for v in range(8)], [GH_Integer(3)]])
    return {"sort": outs(r1), "cull": outs(r2), "errors": m1 + m2}
@scenario
def s19_data_tree_path():
    d = GH_Document(); comp = find("Merge", "Sets", "Tree")
    res, msgs = run(d, comp, [[num(1), num(2)], [num(3)]])
    return {"outputs": outs(res), "errors": msgs}
@scenario
def s20_save_result_3dm():
    d = GH_Document(); c = rg.Brep.CreateFromBox(rg.BoundingBox(0, 0, 0, 100, 60, 40))
    comp = find("Volume", "Surface", "Analysis")
    res, msgs = run(d, comp, [[c]])
    f = Rhino.FileIO.File3dm(); f.Objects.AddBrep(c)
    path = safe_path("scenario20_box.3dm"); ok = f.Write(path, 8)
    return {"written": bool(ok), "bytes": os.path.getsize(path) if os.path.exists(path) else 0, "sha256": sha256_of(path) if os.path.exists(path) else None, "volume": outs(res), "errors": msgs}

def manifest():
    m = {"harness": "dino8-reference-grasshopper", "harness_version": HARNESS_VERSION, "tested_against_real_application": False,
         "utc": datetime.datetime.utcnow().isoformat() + "Z", "program": "Grasshopper (inside Rhino)",
         "os": platform.platform(), "python": sys.version, "output_dir": OUT, "settings": {"document": "in-memory GH_Document per scenario"}}
    try: m["rhino_version"] = str(Rhino.RhinoApp.Version)
    except Exception: pass
    try: m["grasshopper_version"] = str(Grasshopper.Versioning.Version.Version) if hasattr(Grasshopper, "Versioning") else str(Grasshopper.Instances.RunningVersion)
    except Exception:
        try: m["grasshopper_version"] = str(Grasshopper.Instances.Version)
        except Exception: pass
    return m

def main():
    t0 = time.time(); meas = {}
    cat = catalogue(); write_json("components.json", {"count": len(cat), "components": cat, "libraries": libraries()})
    for fn in S:
        t = time.time()
        try: meas[fn.__name__] = {"status": "ok", "values": fn(), "seconds": round(time.time() - t, 3)}
        except Exception as e: meas[fn.__name__] = {"status": "error", "error": str(e), "trace": traceback.format_exc()}
    write_json("measurements.json", meas)
    m = manifest(); m["scenario_count"] = len(S); m["scenarios_ok"] = sum(1 for v in meas.values() if v["status"] == "ok")
    m["component_count"] = len(cat); m["seconds"] = round(time.time() - t0, 2); m["files"] = sorted(os.listdir(OUT))
    write_json("manifest.json", m)
    print("Grasshopper reference run: %d/%d scenarios ok. Output: %s" % (m["scenarios_ok"], len(S), OUT))
main()
