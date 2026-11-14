# Dino 8 reference-output harness for Blender (bpy). Run headless:
#   blender -b --factory-startup --offline-mode --python d8_reference_blender.py -- --out /path/to/empty/folder
# (older builds without --offline-mode: omit that flag; the script itself makes no network calls.)
# STATUS: UNTESTED against a real Blender build. Written from the public Blender Python API reference.
# SAFETY: factory startup, never opens or saves a user .blend, writes only inside --out.
# EULA/licence: Blender is GPL; no licence limit on observation, but the owner decides what to upload.
import bpy, bmesh, sys, os, json, math, time, hashlib, platform, datetime, traceback
from mathutils import Vector, Matrix
HARNESS_VERSION = "0.1.0"

def parse_out():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = os.environ.get("D8_REF_OUT")
    for i, a in enumerate(argv):
        if a == "--out" and i + 1 < len(argv): out = argv[i + 1]
    if not out:
        out = os.path.join(os.path.expanduser("~"), "dino8_reference", "blender_" + datetime.datetime.utcnow().strftime("%Y%m%dT%H%M%SZ"))
    out = os.path.realpath(out); os.makedirs(out, exist_ok=True); return out
OUT = parse_out()
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

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'; sc.unit_settings.scale_length = 0.001; sc.unit_settings.length_unit = 'MILLIMETERS'

def mesh_stats(obj):
    dg = bpy.context.evaluated_depsgraph_get(); ev = obj.evaluated_get(dg); me = ev.to_mesh()
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    vol = bm.calc_volume(signed=True); area = sum(f.calc_area() for f in bm.faces)
    nonman = sum(1 for e in bm.edges if not e.is_manifold)
    mw = ev.matrix_world; ws = [mw @ v.co for v in bm.verts]
    bbmin = [min(w[i] for w in ws) for i in range(3)] if ws else None; bbmax = [max(w[i] for w in ws) for i in range(3)] if ws else None
    res = {"vertices": len(me.vertices), "edges": len(me.edges), "polygons": len(me.polygons), "triangles": len(bm.faces),
           "volume_signed": vol, "area": area, "non_manifold_edges": nonman, "bbox_min": bbmin, "bbox_max": bbmax}
    bm.free(); ev.to_mesh_clear(); return res

def add_box(name="Box", x=100.0, y=60.0, z=40.0):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(x / 2, y / 2, z / 2)); o = bpy.context.active_object; o.name = name
    o.scale = (x, y, z); bpy.ops.object.transform_apply(location=False, rotation=False, scale=True); return o
def add_cyl(name, cx, cy, z0, r, h, verts=128):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=h, location=(cx, cy, z0 + h / 2)); o = bpy.context.active_object; o.name = name; return o
def apply_mod(obj, **kw):
    m = obj.modifiers.new("d8", kw.pop("type"))
    for k, v in kw.items(): setattr(m, k, v)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=m.name); return obj

S = []
def scenario(fn): S.append(fn); return fn

@scenario
def s01_box():
    reset(); o = add_box(); return {"expected_volume": 240000.0, **mesh_stats(o)}
@scenario
def s02_cylinder():
    reset(); o = add_cyl("Cyl", 0, 0, 0, 15.0, 40.0, 256); return {"expected_volume_true_circle": math.pi * 225 * 40, "expected_volume_polygon": 0.5 * 256 * 225 * math.sin(2 * math.pi / 256) * 40, **mesh_stats(o)}
@scenario
def s03_uv_sphere():
    reset(); bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, radius=25.0); return {"expected_volume_true_sphere": 4 / 3 * math.pi * 25 ** 3, **mesh_stats(bpy.context.active_object)}
@scenario
def s04_bevel_one_edge():
    reset(); o = add_box(); bm = bmesh.new(); bm.from_mesh(o.data); bm.edges.ensure_lookup_table()
    e = [e for e in bm.edges if all(abs(v.co.x) < 1e-6 and abs(v.co.y) < 1e-6 for v in e.verts)][0]
    bmesh.ops.bevel(bm, geom=[e], offset=5.0, segments=16, profile=0.5, affect='EDGES'); bm.to_mesh(o.data); bm.free()
    return {"expected_volume_change": -(25 - math.pi * 25 / 4) * 40, **mesh_stats(o)}
@scenario
def s05_bevel_modifier_chamfer():
    reset(); o = add_box(); m = o.modifiers.new("b", 'BEVEL'); m.width = 5.0; m.segments = 1; m.limit_method = 'ANGLE'
    return {"all_edges_chamfered": True, **mesh_stats(o)}
def boolean(op, a_args, b_args, solver='EXACT'):
    reset(); a = add_box(); b = add_cyl("Cut", 50, 30, -5, 10.0, 50.0, 128)
    m = a.modifiers.new("bool", 'BOOLEAN'); m.operation = op; m.object = b; m.solver = solver
    bpy.context.view_layer.objects.active = a; bpy.ops.object.modifier_apply(modifier=m.name); return a
@scenario
def s06_boolean_difference():
    a = boolean('DIFFERENCE', 0, 0); return {"expected_volume_polygonal_cut": 240000.0 - 0.5 * 128 * 100 * math.sin(2 * math.pi / 128) * 40, **mesh_stats(a)}
@scenario
def s07_boolean_union():
    a = boolean('UNION', 0, 0); return mesh_stats(a)
@scenario
def s08_boolean_intersect():
    a = boolean('INTERSECT', 0, 0); return {"expected_volume_polygonal": 0.5 * 128 * 100 * math.sin(2 * math.pi / 128) * 40, **mesh_stats(a)}
@scenario
def s09_curve_offset_geometry():
    reset(); cu = bpy.data.curves.new("c", 'CURVE'); cu.dimensions = '2D'
    sp = cu.splines.new('POLY'); sp.points.add(3)
    for p, xy in zip(sp.points, [(0, 0), (100, 0), (100, 60), (0, 60)]): p.co = (xy[0], xy[1], 0, 1)
    sp.use_cyclic_u = True; cu.offset = 5.0; ob = bpy.data.objects.new("c", cu); bpy.context.scene.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    r = {"note": "curve 'offset' property is Blender's offset-curve behaviour; sign convention is observed", "expected_perimeter_sharp": 2 * (110 + 70)}
    dg = bpy.context.evaluated_depsgraph_get(); ev = ob.evaluated_get(dg); me = ev.to_mesh()
    r["evaluated_vertices"] = len(me.vertices); ev.to_mesh_clear()
    bpy.ops.object.convert(target='MESH'); r.update(mesh_stats(bpy.context.active_object)); return r
@scenario
def s10_circle_curve_to_mesh():
    reset(); bpy.ops.curve.primitive_bezier_circle_add(radius=20.0); cu = bpy.context.active_object; cu.data.resolution_u = 64
    bpy.ops.object.convert(target='MESH'); me = bpy.context.active_object.data
    pts = [v.co for v in me.vertices]; per = sum((pts[i] - pts[(i + 1) % len(pts)]).length for i in range(len(pts)))
    return {"vertices": len(pts), "polyline_length": per, "expected_length": 2 * math.pi * 20}
@scenario
def s11_extrude_profile():
    reset(); me = bpy.data.meshes.new("p"); bm = bmesh.new()
    vs = [bm.verts.new(v) for v in [(0, 0, 0), (50, 0, 0), (50, 30, 0), (0, 30, 0)]]; f = bm.faces.new(vs)
    r = bmesh.ops.extrude_face_region(bm, geom=[f]); mv = [e for e in r["geom"] if isinstance(e, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, vec=(0, 0, 20), verts=mv); bm.normal_update(); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new("e", me); bpy.context.scene.collection.objects.link(o); return {"expected_volume": 30000.0, **mesh_stats(o)}
@scenario
def s12_screw_revolve():
    reset(); me = bpy.data.meshes.new("p"); me.from_pydata([(10, 0, 0), (30, 0, 0), (30, 0, 20), (10, 0, 20)], [(0, 1), (1, 2), (2, 3), (3, 0)], [])
    o = bpy.data.objects.new("r", me); bpy.context.scene.collection.objects.link(o)
    m = o.modifiers.new("s", 'SCREW'); m.angle = 2 * math.pi; m.steps = 128; m.render_steps = 128; m.axis = 'Z'; m.use_merge_vertices = True
    return {"expected_volume_true": math.pi * 800 * 20, **mesh_stats(o)}
@scenario
def s13_bridge_loft_frustum():
    reset(); bm = bmesh.new()
    n = 128
    v1 = [bm.verts.new((20 * math.cos(2 * math.pi * i / n), 20 * math.sin(2 * math.pi * i / n), 0)) for i in range(n)]
    v2 = [bm.verts.new((10 * math.cos(2 * math.pi * i / n), 10 * math.sin(2 * math.pi * i / n), 50)) for i in range(n)]
    edges = [bm.edges.new((v[i], v[(i + 1) % n])) for v in (v1, v2) for i in range(n)]
    bmesh.ops.bridge_loops(bm, edges=edges)
    bmesh.ops.contextual_create(bm, geom=v1); bmesh.ops.contextual_create(bm, geom=v2); bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new("l"); bm.to_mesh(me); bm.free(); o = bpy.data.objects.new("l", me); bpy.context.scene.collection.objects.link(o)
    return {"expected_volume_true": math.pi * 50 / 3 * 700, **mesh_stats(o)}
@scenario
def s14_transform_rotate_scale():
    reset(); o = add_box(); o.rotation_euler = (0, 0, math.radians(45)); o.scale = (2, 1, 0.5); return {"expected_volume": 240000.0, **mesh_stats(o)}
@scenario
def s15_array_modifier():
    reset(); o = add_box(10, 10, 10); m = o.modifiers.new("a", 'ARRAY'); m.count = 5; m.relative_offset_displace = (2.0, 0, 0); return {"expected_volume": 5000.0, **mesh_stats(o)}
@scenario
def s16_solidify_shell():
    reset(); o = add_box(); bm = bmesh.new(); bm.from_mesh(o.data); top = [f for f in bm.faces if f.normal.z > 0.99]; bmesh.ops.delete(bm, geom=top, context='FACES_ONLY'); bm.to_mesh(o.data); bm.free()
    m = o.modifiers.new("s", 'SOLIDIFY'); m.thickness = -2.0; m.offset = -1.0
    return {"note": "open-top box shell 2 mm; volume of material expected 240000-96*56*38", "expected_material_volume": 240000.0 - 96 * 56 * 38, **mesh_stats(o)}
@scenario
def s17_subsurf_and_mirror():
    reset(); o = add_box(); m = o.modifiers.new("sub", 'SUBSURF'); m.levels = 2; m.render_levels = 2; r = {"subsurf2": mesh_stats(o)}
    o.modifiers.remove(m); mm = o.modifiers.new("mir", 'MIRROR'); mm.use_axis[0] = True; r["mirror_x_bbox"] = mesh_stats(o)["bbox_min"]; return r
@scenario
def s18_decimate_remesh():
    reset(); bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, radius=25.0); o = bpy.context.active_object
    m = o.modifiers.new("d", 'DECIMATE'); m.ratio = 0.25; r = {"decimate_0.25": mesh_stats(o)}; o.modifiers.remove(m)
    m = o.modifiers.new("r", 'REMESH'); m.mode = 'VOXEL'; m.voxel_size = 2.0; r["voxel_remesh_2mm"] = mesh_stats(o); return r
@scenario
def s19_ray_cast_and_closest():
    reset(); o = add_box(); dg = bpy.context.evaluated_depsgraph_get()
    hit, loc, nor, idx = o.ray_cast(Vector((-10, 30, 20)), Vector((1, 0, 0)))
    ok, cl, cn, ci = o.closest_point_on_mesh(Vector((150, 30, 20)))
    return {"ray_hit": hit, "ray_location": list(loc), "expected_hit": [0, 30, 20], "closest": list(cl), "expected_closest": [100, 30, 20]}
@scenario
def s20_export_formats():
    reset(); a = boolean('DIFFERENCE', 0, 0); bpy.ops.object.select_all(action='DESELECT')
    for n in [o.name for o in bpy.data.objects if o is not a]: bpy.data.objects.remove(bpy.data.objects[n], do_unlink=True)
    a.select_set(True); bpy.context.view_layer.objects.active = a
    res = {}
    def tryexp(tag, fn, name):
        try:
            p = safe_path(name); fn(p); ex = os.path.exists(p)
            res[tag] = {"ok": ex, "bytes": os.path.getsize(p) if ex else 0, "sha256": sha256_of(p) if ex else None}
        except Exception as e: res[tag] = {"ok": False, "error": str(e)}
    tryexp("obj", lambda p: bpy.ops.wm.obj_export(filepath=p, export_selected_objects=True), "scenario20_result.obj")
    tryexp("stl", lambda p: bpy.ops.wm.stl_export(filepath=p, export_selected_objects=True), "scenario20_result.stl")
    tryexp("ply", lambda p: bpy.ops.wm.ply_export(filepath=p, export_selected_objects=True), "scenario20_result.ply")
    tryexp("glb", lambda p: bpy.ops.export_scene.gltf(filepath=p, export_format='GLB', use_selection=True), "scenario20_result.glb")
    tryexp("fbx", lambda p: bpy.ops.export_scene.fbx(filepath=p, use_selection=True), "scenario20_result.fbx")
    tryexp("usd", lambda p: bpy.ops.wm.usd_export(filepath=p, selected_objects_only=True), "scenario20_result.usd")
    tryexp("blend", lambda p: bpy.ops.wm.save_as_mainfile(filepath=p, copy=True), "scenario20_result.blend")
    return res

def enumerate_operators():
    out = {}
    for mod in sorted(dir(bpy.ops)):
        if mod.startswith("_"): continue
        grp = getattr(bpy.ops, mod); ops = {}
        for name in sorted(dir(grp)):
            if name.startswith("_"): continue
            try:
                r = getattr(grp, name).get_rna_type()
                ops[name] = {"idname": mod + "." + name, "label": r.name, "description": r.description,
                             "properties": [{"id": p.identifier, "type": p.type, "default": str(getattr(p, "default", None))} for p in r.properties if p.identifier != "rna_type"]}
            except Exception as e:
                ops[name] = {"error": str(e)}
        out[mod] = ops
    return out
def enumerate_types():
    r = {}
    for nm, base in (("modifiers", bpy.types.Modifier), ("constraints", bpy.types.Constraint), ("nodes", bpy.types.Node), ("shader_fx", bpy.types.ShaderFx),
                     ("editors_space_types", bpy.types.Space), ("operators_class_count", bpy.types.Operator)):
        try: r[nm] = sorted(c.__name__ for c in base.__subclasses__())
        except Exception as e: r[nm] = "error: " + str(e)
    try: r["modifier_enum"] = [i.identifier for i in bpy.types.Modifier.bl_rna.properties["type"].enum_items]
    except Exception as e: r["modifier_enum"] = "error: " + str(e)
    try: r["addons_enabled"] = sorted(bpy.context.preferences.addons.keys())
    except Exception: pass
    return r

def manifest():
    return {"harness": "dino8-reference-blender", "harness_version": HARNESS_VERSION, "tested_against_real_application": False,
            "utc": datetime.datetime.utcnow().isoformat() + "Z", "program": "Blender", "blender_version": bpy.app.version_string,
            "blender_build_hash": str(bpy.app.build_hash), "blender_build_date": str(bpy.app.build_date), "os": platform.platform(), "python": sys.version,
            "output_dir": OUT, "settings": {"startup": "factory", "units": "metric, scale_length 0.001, millimeters"}, "script_sha256": sha256_of(os.path.realpath(__file__))}
def main():
    t0 = time.time(); meas = {}
    for fn in S:
        t = time.time()
        try: meas[fn.__name__] = {"status": "ok", "values": fn(), "seconds": round(time.time() - t, 3)}
        except Exception as e: meas[fn.__name__] = {"status": "error", "error": str(e), "trace": traceback.format_exc()}
    write_json("measurements.json", meas)
    try: write_json("commands.json", {"operators": enumerate_operators(), "types": enumerate_types()})
    except Exception as e: write_json("commands.json", {"error": str(e)})
    m = manifest(); m["scenario_count"] = len(S); m["scenarios_ok"] = sum(1 for v in meas.values() if v["status"] == "ok")
    m["seconds"] = round(time.time() - t0, 2); m["files"] = sorted(os.listdir(OUT)); write_json("manifest.json", m)
    print("Blender reference run: %d/%d scenarios ok. Output: %s" % (m["scenarios_ok"], len(S), OUT))
main()
