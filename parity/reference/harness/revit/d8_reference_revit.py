# -*- coding: utf-8 -*-
# Dino 8 reference-output harness for Revit (pyRevit script; Revit .NET API).
# Compatible with the pyRevit IronPython 2.7 engine and the CPython 3 engine (no f-strings).
# STATUS: UNTESTED against a real Revit installation. Written from the public Revit API documentation.
# SAFETY: read-only with respect to the owner's work.
#  - The script NEVER touches an open project. It creates its own new in-memory project document with
#    Application.NewProjectDocument, builds elements in it, measures them and closes it WITHOUT saving.
#  - Writes only inside the output folder. No network calls. No settings changes.
# RUN: pyRevit > (put this file in a pyRevit extension script folder, or run with "Run Script").
#      Output folder: environment variable D8_REF_OUT, else ~/dino8_reference/revit_<timestamp>.
#      Optional: D8_REVIT_TEMPLATE = full path to a .rte template (default: Application default template).
# EULA: counsel must confirm that scripted observation is permitted before this is run.
from __future__ import print_function
import os, sys, json, math, time, hashlib, platform, datetime, traceback
import clr
clr.AddReference('RevitAPI'); clr.AddReference('RevitAPIUI')
import Autodesk.Revit.DB as DB
import Autodesk.Revit.UI as UI
from System.Collections.Generic import List

HARNESS_VERSION = "0.1.0"
FT = 304.8  # millimetres per international foot (Revit internal length unit)
def ft(mm): return mm / FT
def mm(f): return None if f is None else f * FT
def sq_mm(f): return None if f is None else f * FT * FT
def cu_mm(f): return None if f is None else f * FT * FT * FT

def choose_out():
    out = os.environ.get("D8_REF_OUT")
    if not out:
        out = os.path.join(os.path.expanduser("~"), "dino8_reference", "revit_" + datetime.datetime.utcnow().strftime("%Y%m%dT%H%M%SZ"))
    out = os.path.realpath(out)
    if not os.path.isdir(out): os.makedirs(out)
    return out
OUT = choose_out()
def safe_path(name):
    p = os.path.realpath(os.path.join(OUT, name))
    if not (p == OUT or p.startswith(OUT + os.sep)): raise Exception("refusing to write outside the output folder: " + p)
    return p
def write_json(name, obj):
    with open(safe_path(name), "w") as f: f.write(json.dumps(obj, indent=2, sort_keys=True, default=str))
def sha256_of(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        while True:
            c = f.read(1 << 20)
            if not c: break
            h.update(c)
    return h.hexdigest()

uiapp = __revit__          # provided by pyRevit
app = uiapp.Application

def new_doc():
    tpl = os.environ.get("D8_REVIT_TEMPLATE")
    if tpl: return app.NewProjectDocument(tpl)
    return app.NewProjectDocument(DB.UnitSystem.Metric)

def pt(x, y, z=0.0): return DB.XYZ(ft(x), ft(y), ft(z))
def rect_loop(x, y, ox=0.0, oy=0.0, z=0.0):
    cl = DB.CurveLoop()
    p = [pt(ox, oy, z), pt(ox + x, oy, z), pt(ox + x, oy + y, z), pt(ox, oy + y, z)]
    for i in range(4): cl.Append(DB.Line.CreateBound(p[i], p[(i + 1) % 4]))
    return cl
def circle_loop(cx, cy, z, r):
    c = pt(cx, cy, z); a1 = DB.Arc.Create(c + DB.XYZ(ft(r), 0, 0), c - DB.XYZ(ft(r), 0, 0), c + DB.XYZ(0, ft(r), 0))
    a2 = DB.Arc.Create(c - DB.XYZ(ft(r), 0, 0), c + DB.XYZ(ft(r), 0, 0), c - DB.XYZ(0, ft(r), 0))
    cl = DB.CurveLoop(); cl.Append(a1); cl.Append(a2); return cl
def extrude(loop, h):
    return DB.GeometryCreationUtilities.CreateExtrusionGeometry(List[DB.CurveLoop]([loop]), DB.XYZ.BasisZ, ft(h))
def solid_stats(s):
    if s is None: return {"error": "no solid"}
    bb = s.GetBoundingBox()
    r = {"volume_mm3": cu_mm(s.Volume), "surface_area_mm2": sq_mm(s.SurfaceArea), "faces": s.Faces.Size, "edges": s.Edges.Size}
    try:
        c = s.ComputeCentroid(); r["centroid_mm"] = [mm(c.X), mm(c.Y), mm(c.Z)]
    except Exception: pass
    if bb is not None:
        mn, mx = bb.Transform.OfPoint(bb.Min), bb.Transform.OfPoint(bb.Max)
        r["bbox_mm"] = {"min": [mm(mn.X), mm(mn.Y), mm(mn.Z)], "max": [mm(mx.X), mm(mx.Y), mm(mx.Z)]}
    return r
def bip(el, name):
    p = el.get_Parameter(getattr(DB.BuiltInParameter, name))
    if p is None: return None
    return p.AsDouble() if p.StorageType == DB.StorageType.Double else (p.AsInteger() if p.StorageType == DB.StorageType.Integer else p.AsString())

class Ctx(object):
    def __init__(self, doc): self.doc = doc; self.level = None; self.cur = None
def first_type(doc, cls):
    return DB.FilteredElementCollector(doc).OfClass(cls).FirstElementId()

S = []
def scenario(fn): S.append(fn); return fn
def tx(ctx, name):
    t = DB.Transaction(ctx.doc, name); t.Start(); ctx.cur = t; return t

@scenario
def s01_levels(c):
    t = tx(c, "levels"); l0 = DB.Level.Create(c.doc, ft(0)); l1 = DB.Level.Create(c.doc, ft(3000)); t.Commit(); c.level = l0
    return {"elevations_mm": [mm(l0.Elevation), mm(l1.Elevation)], "names": [l0.Name, l1.Name]}
@scenario
def s02_grids(c):
    t = tx(c, "grids"); g = DB.Grid.Create(c.doc, DB.Line.CreateBound(pt(0, -1000), pt(0, 6000))); t.Commit()
    return {"name": g.Name, "length_mm": mm(g.Curve.Length)}
@scenario
def s03_walls_room(c):
    lvl = c.level or DB.FilteredElementCollector(c.doc).OfClass(DB.Level).FirstElement()
    wt = first_type(c.doc, DB.WallType); t = tx(c, "walls"); ws = []
    p = [pt(0, 0), pt(5000, 0), pt(5000, 4000), pt(0, 4000)]
    for i in range(4):
        w = DB.Wall.Create(c.doc, DB.Line.CreateBound(p[i], p[(i + 1) % 4]), wt, lvl.Id, ft(3000), 0.0, False, False); ws.append(w)
    c.doc.Regenerate(); t.Commit()
    return {"walls": [{"length_mm": mm(bip(w, "CURVE_ELEM_LENGTH")), "area_mm2": sq_mm(bip(w, "HOST_AREA_COMPUTED")), "volume_mm3": cu_mm(bip(w, "HOST_VOLUME_COMPUTED")), "width_mm": mm(w.Width), "type": w.WallType.Name} for w in ws]}
@scenario
def s04_wall_height_offset(c):
    ws = list(DB.FilteredElementCollector(c.doc).OfClass(DB.Wall)); t = tx(c, "h")
    ws[0].get_Parameter(DB.BuiltInParameter.WALL_USER_HEIGHT_PARAM).Set(ft(4500)); c.doc.Regenerate(); t.Commit()
    return {"area_mm2": sq_mm(bip(ws[0], "HOST_AREA_COMPUTED")), "expected_area_mm2": 5000.0 * 4500.0}
@scenario
def s05_floor(c):
    lvl = c.level; ftid = first_type(c.doc, DB.FloorType); t = tx(c, "floor")
    try:
        f = DB.Floor.Create(c.doc, List[DB.CurveLoop]([rect_loop(5000, 4000)]), ftid, lvl.Id)
    except Exception:
        ca = DB.CurveArray()
        for cv in rect_loop(5000, 4000): ca.Append(cv)
        f = c.doc.Create.NewFloor(ca, c.doc.GetElement(ftid), lvl, False)
    c.doc.Regenerate(); t.Commit()
    return {"area_mm2": sq_mm(bip(f, "HOST_AREA_COMPUTED")), "volume_mm3": cu_mm(bip(f, "HOST_VOLUME_COMPUTED")), "expected_area_mm2": 20000000.0}
@scenario
def s06_footprint_roof(c):
    t = tx(c, "roof"); ca = DB.CurveArray()
    for cv in rect_loop(5000, 4000, 0, 0, 3000): ca.Append(cv)
    lvl = list(DB.FilteredElementCollector(c.doc).OfClass(DB.Level))[-1]
    mca = clr.Reference[DB.ModelCurveArray]()
    r = c.doc.Create.NewFootPrintRoof(ca, lvl, c.doc.GetElement(first_type(c.doc, DB.RoofType)), mca); c.doc.Regenerate(); t.Commit()
    return {"area_mm2": sq_mm(bip(r, "HOST_AREA_COMPUTED")), "volume_mm3": cu_mm(bip(r, "HOST_VOLUME_COMPUTED"))}
@scenario
def s07_room_area(c):
    t = tx(c, "room"); lvl = c.level; room = c.doc.Create.NewRoom(lvl, DB.UV(ft(2500), ft(2000))); c.doc.Regenerate(); t.Commit()
    return {"area_mm2": sq_mm(room.Area), "perimeter_mm": mm(room.Perimeter), "volume_mm3": cu_mm(room.Volume), "note": "walls from s03 enclose a 5000x4000 centreline rectangle; boundary uses wall faces"}
def direct_shape(c, solid, name):
    ds = DB.DirectShape.CreateElement(c.doc, DB.ElementId(DB.BuiltInCategory.OST_GenericModel)); ds.SetShape(List[DB.GeometryObject]([solid])); ds.Name = name; return ds
@scenario
def s08_box_solid(c):
    s = extrude(rect_loop(100, 60), 40); t = tx(c, "box"); direct_shape(c, s, "d8box"); t.Commit(); r = solid_stats(s); r["expected_volume_mm3"] = 240000.0; return r
@scenario
def s09_cylinder_solid(c):
    s = extrude(circle_loop(0, 0, 0, 15.0), 40); r = solid_stats(s); r["expected_volume_mm3"] = math.pi * 225 * 40; return r
def bool_op(kind):
    a = extrude(rect_loop(100, 60), 40); b = extrude(circle_loop(50, 30, -5, 10.0), 50)
    return DB.BooleanOperationsUtils.ExecuteBooleanOperation(a, b, kind)
@scenario
def s10_boolean_difference(c):
    r = solid_stats(bool_op(DB.BooleanOperationsType.Difference)); r["expected_volume_mm3"] = 240000.0 - math.pi * 100 * 40; return r
@scenario
def s11_boolean_union(c): return solid_stats(bool_op(DB.BooleanOperationsType.Union))
@scenario
def s12_boolean_intersect(c):
    r = solid_stats(bool_op(DB.BooleanOperationsType.Intersect)); r["expected_volume_mm3"] = math.pi * 100 * 40; return r
@scenario
def s13_offset_curve_loop(c):
    cl = rect_loop(100, 60); out = {}
    for tag, d in (("inward_candidate", 5.0), ("outward_candidate", -5.0)):
        try:
            o = DB.CurveLoop.CreateViaOffset(cl, ft(d), DB.XYZ.BasisZ); out[tag] = {"length_mm": mm(o.GetExactLength()), "curves": o.NumberOfCurves()}
        except Exception as e: out[tag] = {"error": str(e)}
    out["note"] = "sign convention observed; expected sharp-corner perimeters 360 (outward) and 280 (inward)"; return out
@scenario
def s14_revolve(c):
    prof = DB.CurveLoop()
    p = [pt(10, 0, 0), pt(30, 0, 0), pt(30, 0, 20), pt(10, 0, 20)]
    for i in range(4): prof.Append(DB.Line.CreateBound(p[i], p[(i + 1) % 4]))
    fr = DB.Frame(DB.XYZ(0, 0, 0), DB.XYZ.BasisX, DB.XYZ.BasisY, DB.XYZ.BasisZ)
    s = DB.GeometryCreationUtilities.CreateRevolvedGeometry(fr, List[DB.CurveLoop]([prof]), 0.0, 2 * math.pi)
    r = solid_stats(s); r["expected_volume_mm3"] = math.pi * 800 * 20; return r
@scenario
def s15_blend_frustum(c):
    b = circle_loop(0, 0, 0, 20.0); u = circle_loop(0, 0, 50, 10.0)
    s = DB.GeometryCreationUtilities.CreateBlendGeometry(b, u, None)
    r = solid_stats(s); r["expected_volume_mm3"] = math.pi * 50 / 3.0 * 700; return r
@scenario
def s16_transform_solid(c):
    s = extrude(rect_loop(100, 60), 40); tr = DB.Transform.CreateRotation(DB.XYZ.BasisZ, math.radians(45))
    return {"rotated": solid_stats(DB.SolidUtils.CreateTransformed(s, tr)), "expected_volume_mm3": 240000.0}
@scenario
def s17_tessellate(c):
    s = extrude(circle_loop(0, 0, 0, 15.0), 40); out = {}
    for tag, lod in (("coarse", 0.1), ("default", None), ("fine", 1.0)):
        ctl = DB.SolidOrShellTessellationControls()
        if lod is not None:
            try: ctl.LevelOfDetail = lod
            except Exception: pass
        tess = DB.SolidUtils.TessellateSolidOrShell(s, ctl)
        out[tag] = {"triangles": tess.GetTriangleCount() if hasattr(tess, "GetTriangleCount") else None, "vertices": tess.GetVertexCount() if hasattr(tess, "GetVertexCount") else None}
    return out
@scenario
def s18_split_volumes(c):
    a = extrude(rect_loop(100, 60), 40); b = extrude(rect_loop(100, 60, 200, 0), 40)
    u = DB.BooleanOperationsUtils.ExecuteBooleanOperation(a, b, DB.BooleanOperationsType.Union)
    parts = DB.SolidUtils.SplitVolumes(u); return {"parts": parts.Count, "expected_parts": 2, "volumes_mm3": [cu_mm(p.Volume) for p in parts]}
@scenario
def s19_wall_schedule(c):
    t = tx(c, "sched"); vs = DB.ViewSchedule.CreateSchedule(c.doc, DB.ElementId(DB.BuiltInCategory.OST_Walls)); d = vs.Definition
    for f in d.GetSchedulableFields():
        n = f.GetName(c.doc)
        if n in ("Length", "Area", "Volume", "Type", "Width"): d.AddField(f)
    c.doc.Regenerate(); t.Commit()
    td = vs.GetTableData(); sec = td.GetSectionData(DB.SectionType.Body)
    rows = []
    for r in range(sec.NumberOfRows):
        rows.append([vs.GetCellText(DB.SectionType.Body, r, k) for k in range(sec.NumberOfColumns)])
    try:
        o = DB.ViewScheduleExportOptions(); vs.Export(OUT, "scenario19_wall_schedule.csv", o)
    except Exception as e: rows.append(["export error", str(e)])
    return {"rows": rows}
@scenario
def s20_export_formats(c):
    res = {}; t = tx(c, "view3d")
    vft = [x for x in DB.FilteredElementCollector(c.doc).OfClass(DB.ViewFamilyType) if x.ViewFamily == DB.ViewFamily.ThreeDimensional][0]
    v = DB.View3D.CreateIsometric(c.doc, vft.Id); c.doc.Regenerate(); t.Commit()
    ids = List[DB.ElementId](); ids.Add(v.Id)
    def attempt(tag, fn, fname):
        try:
            fn(); path = safe_path(fname); ex = os.path.exists(path)
            res[tag] = {"ok": True, "exists": ex, "bytes": os.path.getsize(path) if ex else 0, "sha256": sha256_of(path) if ex else None}
        except Exception as e: res[tag] = {"ok": False, "error": str(e)}
    attempt("dwg", lambda: c.doc.Export(OUT, "scenario20_result", ids, DB.DWGExportOptions()), "scenario20_result.dwg")
    attempt("dxf", lambda: c.doc.Export(OUT, "scenario20_result_dxf", ids, DB.DXFExportOptions()) if hasattr(DB, "DXFExportOptions") else (_ for _ in ()).throw(Exception("no DXFExportOptions")), "scenario20_result_dxf.dxf")
    attempt("ifc", lambda: c.doc.Export(OUT, "scenario20_result.ifc", DB.IFCExportOptions()), "scenario20_result.ifc")
    attempt("fbx", lambda: c.doc.Export(OUT, "scenario20_result", ids, DB.FBXExportOptions()), "scenario20_result.fbx")
    attempt("sat", lambda: c.doc.Export(OUT, "scenario20_result", ids, DB.SATExportOptions()) if hasattr(DB, "SATExportOptions") else (_ for _ in ()).throw(Exception("no SATExportOptions")), "scenario20_result.sat")
    attempt("gbxml", lambda: c.doc.Export(OUT, "scenario20_result", DB.GBXMLExportOptions()), "scenario20_result.xml")
    return res

def enumerate_commands():
    r = {}
    try:
        import System
        r["postable_commands"] = sorted(System.Enum.GetNames(UI.PostableCommand))
    except Exception as e: r["postable_error"] = str(e)
    try:
        import System
        r["builtin_categories"] = sorted(System.Enum.GetNames(DB.BuiltInCategory))
        r["view_families"] = sorted(System.Enum.GetNames(DB.ViewFamily))
    except Exception as e: r["enum_error"] = str(e)
    r["note"] = "Ribbon tabs and panels are not enumerable through the public API; see the manual walk-through in the README."
    return r

def manifest():
    m = {"harness": "dino8-reference-revit", "harness_version": HARNESS_VERSION, "tested_against_real_application": False,
         "utc": datetime.datetime.utcnow().isoformat() + "Z", "program": "Revit", "os": platform.platform(), "python": sys.version, "output_dir": OUT,
         "settings": {"project": "new in-memory project document, never saved", "template": os.environ.get("D8_REVIT_TEMPLATE") or "application default (UnitSystem.Metric)", "internal_unit": "feet"}}
    for k in ("VersionName", "VersionNumber", "VersionBuild", "SubVersionNumber", "Language", "Product"):
        try: m["revit_" + k] = str(getattr(app, k))
        except Exception: pass
    return m

def main():
    t0 = time.time(); meas = {}; doc = new_doc(); c = Ctx(doc)
    try:
        for fn in S:
            t = time.time()
            try: meas[fn.__name__] = {"status": "ok", "values": fn(c), "seconds": round(time.time() - t, 3)}
            except Exception as e:
                meas[fn.__name__] = {"status": "error", "error": str(e), "trace": traceback.format_exc(), "seconds": round(time.time() - t, 3)}
                try:
                    if c.cur is not None and c.cur.GetStatus() == DB.TransactionStatus.Started: c.cur.RollBack()
                except Exception: pass
    finally:
        doc.Close(False)    # never saved
    write_json("measurements.json", meas); write_json("commands.json", enumerate_commands())
    m = manifest(); m["scenario_count"] = len(S); m["scenarios_ok"] = len([1 for v in meas.values() if v["status"] == "ok"])
    m["seconds"] = round(time.time() - t0, 2); m["files"] = sorted(os.listdir(OUT)); write_json("manifest.json", m)
    print("Revit reference run: %d/%d scenarios ok. Output: %s" % (m["scenarios_ok"], len(S), OUT))
main()
