#!/usr/bin/env python3
# Dino 8 reference-output harness for Archicad (Python package "archicad" over the JSON API).
# STATUS: UNTESTED against a real Archicad installation. The JSON parameter shapes below were written from
# the public Archicad Python/JSON API documentation without the application available, so the first real
# run may need small adjustments; every scenario records its exact error and the run continues.
# SAFETY:
#  - connects to the Archicad you have already started (localhost only; no other network traffic);
#  - REFUSES to run unless the open project has no elements (use File > New, from any template);
#  - creates test elements, measures them, and deletes every element it created before finishing;
#  - never saves the project; writes only inside --out.
# RUN:   pip install archicad          (one-off; this is the only network use and is done by you, not the script)
#        python d8_reference_archicad.py --out /path/to/empty/folder [--port 19723]
# EULA: counsel must confirm that scripted observation is permitted before this is run.
import argparse, os, sys, json, math, time, hashlib, platform, datetime, traceback, re

HARNESS_VERSION = "0.1.0"
ap = argparse.ArgumentParser()
ap.add_argument("--out", default=os.environ.get("D8_REF_OUT"))
ap.add_argument("--port", type=int, default=19723)
args = ap.parse_args()
OUT = os.path.realpath(args.out or os.path.join(os.path.expanduser("~"), "dino8_reference", "archicad_" + datetime.datetime.utcnow().strftime("%Y%m%dT%H%M%SZ")))
os.makedirs(OUT, exist_ok=True)

def safe_path(n):
    p = os.path.realpath(os.path.join(OUT, n))
    if not (p == OUT or p.startswith(OUT + os.sep)): raise RuntimeError("outside output folder: " + p)
    return p
def write_json(n, o):
    with open(safe_path(n), "w", encoding="utf-8") as f: json.dump(o, f, indent=2, sort_keys=True, default=str)

from archicad import ACConnection
try:
    conn = ACConnection.connect(args.port)
except TypeError:
    conn = ACConnection.connect()
if conn is None:
    sys.exit("Could not connect to Archicad on localhost. Start Archicad, open a NEW empty project, and retry.")
acc, act, acu = conn.commands, conn.types, conn.utilities

def raw(command, params=None):
    """Raw JSON-API call; command like 'API.GetProductInfo'."""
    return conn.core.post_command(command, params or {})

def guid_list(res):
    return [e["elementId"]["guid"] if "elementId" in e else e.get("guid") for e in res.get("elements", [])]
def eid(g): return {"elementId": {"guid": g}}

CREATED = []     # guids created by this run; always deleted at the end

def count_elements():
    return len(raw("API.GetAllElements").get("elements", []))

S = []
def scenario(fn): S.append(fn); return fn

@scenario
def s01_product_info():
    return raw("API.GetProductInfo")
@scenario
def s02_project_info():
    out = {}
    for cmd in ("API.GetProjectInfo", "API.GetProjectInfoFields", "API.GetLocation", "API.GetGeoLocation"):
        try: out[cmd] = raw(cmd)
        except Exception as e: out[cmd] = {"error": str(e)}
    return out
@scenario
def s03_stories():
    return raw("API.GetStories")
ATTR_TYPES = ["Layer", "Pen", "LineType", "Fill", "Material", "BuildingMaterial", "Composite", "Profile", "Surface", "ZoneCategory", "MEPSystem", "OperationProfile", "PenTable", "LayerCombination", "MarkupStyle"]
@scenario
def s04_attribute_catalogues():
    out = {}
    for t in ATTR_TYPES:
        try:
            r = raw("API.GetAttributesByType", {"attributeType": t}); attrs = r.get("attributeIds", [])
            out[t] = {"count": len(attrs)}
            cmd = "API.Get%sAttributes" % t
            try:
                d = raw(cmd, {"attributeIds": attrs[:200]}); out[t]["details_sample"] = d
            except Exception as e: out[t]["details_error"] = str(e)
        except Exception as e: out[t] = {"error": str(e)}
    return out
@scenario
def s05_classification_systems():
    out = {}
    try:
        cs = raw("API.GetAllClassificationSystems").get("classificationSystems", []); out["systems"] = cs
        out["items"] = {}
        for s in cs:
            try: out["items"][s.get("name", str(s))] = len(raw("API.GetAllClassificationsInSystem", {"classificationSystemId": s["classificationSystemId"]}).get("classifications", []))
            except Exception as e: out["items"][s.get("name", str(s))] = "error: " + str(e)
    except Exception as e: out["error"] = str(e)
    return out
@scenario
def s06_property_catalogue():
    out = {}
    try:
        r = raw("API.GetAllPropertyIds", {"properties": [{"type": "BuiltIn"}]}); ids = r.get("properties", [])
        out["builtin_count"] = len(ids)
        names = raw("API.GetPropertyDefinitions", {"properties": ids}).get("propertyDefinitions", []) if ids else []
        out["builtin_names_sample"] = [(d.get("propertyDefinition", d)).get("name") for d in names][:400]
    except Exception as e: out["error"] = str(e)
    return out
@scenario
def s07_library_parts():
    out = {}
    for cmd in ("API.GetLibraries", "API.GetLibraryParts"):
        try: out[cmd] = raw(cmd)
        except Exception as e: out[cmd] = {"error": str(e)}
    return out
@scenario
def s08_element_type_support():
    out = {}
    for cmd in ("API.GetTypesOfElements", "API.GetElementTypes"):
        try: out[cmd] = raw(cmd, {"elements": []})
        except Exception as e: out[cmd] = {"error": str(e)}
    return out
@scenario
def s09_baseline_empty_project():
    return {"element_count": count_elements(), "walls": len(raw("API.GetElementsByType", {"elementType": "Wall"}).get("elements", []))}
def create(cmd, key, items):
    r = raw(cmd, {key: items}); gs = guid_list(r) if "elements" in r else [x["elementId"]["guid"] for x in r.get("elementIds", [])]
    CREATED.extend(gs); return gs, r
@scenario
def s10_create_column():
    gs, r = create("API.CreateColumns", "columnsData", [{"coordinates": {"x": 0.0, "y": 0.0, "z": 0.0}}]); return {"guids": gs, "raw": r}
RECT = lambda w, h, ox=0.0, oy=0.0: [{"x": ox, "y": oy}, {"x": ox + w, "y": oy}, {"x": ox + w, "y": oy + h}, {"x": ox, "y": oy + h}, {"x": ox, "y": oy}]
@scenario
def s11_create_slab_5x4():
    gs, r = create("API.CreateSlabs", "slabsData", [{"level": 0.0, "polygonCoordinates": RECT(5.0, 4.0, 10.0, 0.0)}]); return {"guids": gs, "raw": r, "expected_area_m2": 20.0}
@scenario
def s12_slab_bbox():
    gs = [g for g in CREATED]; r = raw("API.Get3DBoundingBoxes", {"elements": [eid(g) for g in gs]}); return {"boxes": r, "note": "units are metres in the JSON API"}
@scenario
def s13_create_zone():
    gs, r = create("API.CreateZones", "zonesData", [{"floorIndex": 0, "polygonCoordinates": RECT(5.0, 4.0, 20.0, 0.0)}]); return {"guids": gs, "raw": r}
@scenario
def s14_create_polyline():
    gs, r = create("API.CreatePolylines", "polylinesData", [{"floorIndex": 0, "coordinates": RECT(3.0, 2.0, 30.0, 0.0)}]); return {"guids": gs, "raw": r}
@scenario
def s15_create_object():
    out = {}
    try: parts = raw("API.GetLibraryParts", {})
    except Exception as e: return {"error": "cannot list library parts: " + str(e)}
    out["library_parts_listing"] = str(parts)[:2000]; return out
@scenario
def s16_element_details():
    out = {}
    for cmd in ("API.GetDetailsOfElements", "API.GetSubelementsOfHierarchicalElements"):
        try: out[cmd] = raw(cmd, {"elements": [eid(g) for g in CREATED]})
        except Exception as e: out[cmd] = {"error": str(e)}
    return out
@scenario
def s17_quantity_properties():
    out = {}
    ids = raw("API.GetAllPropertyIds", {"properties": [{"type": "BuiltIn"}]}).get("properties", [])
    defs = raw("API.GetPropertyDefinitions", {"properties": ids}).get("propertyDefinitions", [])
    wanted = []
    for pid, d in zip(ids, defs):
        nm = (d.get("propertyDefinition", d)).get("name", "")
        if re.search(r"(area|volume|length|perimeter|height|width|thickness)", nm, re.I): wanted.append((pid, nm))
    out["property_count_queried"] = len(wanted)
    if CREATED and wanted:
        vals = raw("API.GetPropertyValuesOfElements", {"elements": [eid(g) for g in CREATED], "properties": [w[0] for w in wanted[:80]]})
        out["names"] = [w[1] for w in wanted[:80]]; out["values"] = vals
    return out
@scenario
def s18_element_counts_by_type():
    out = {}
    for t in ("Column", "Slab", "Zone", "PolyLine", "Wall", "Beam", "Roof", "Object", "Window", "Door", "Mesh", "Morph", "Stair", "Railing"):
        try: out[t] = len(raw("API.GetElementsByType", {"elementType": t}).get("elements", []))
        except Exception as e: out[t] = "error: " + str(e)
    return out
@scenario
def s19_cleanup_created_elements():
    before = count_elements()
    if CREATED: raw("API.DeleteElements", {"elements": [eid(g) for g in CREATED]})
    after = count_elements(); CREATED.clear()
    return {"count_before_delete": before, "count_after_delete": after, "project_back_to_empty": after == 0}
@scenario
def s20_export_attempts():
    out = {}
    for cmd, p in (("API.GetPublisherSetNames", {}),):
        try: out[cmd] = raw(cmd, p)
        except Exception as e: out[cmd] = {"error": str(e)}
    out["note"] = "The JSON API cannot run File > Save As. See MANUAL_EXPORT_STEPS.txt for the owner's manual exports (IFC, DXF/DWG, 3D formats)."
    with open(safe_path("MANUAL_EXPORT_STEPS.txt"), "w") as f:
        f.write("Manual exports for Archicad. The harness deletes its test elements, so do this in a SEPARATE scratch project: draw one 5 x 4 m slab and one column by hand, then:\n"
                "1. File > Save As > IFC (name scenario20_result.ifc), translator 'General Export'.\n"
                "2. File > Save As > DXF-DWG (scenario20_result.dxf and .dwg).\n"
                "3. File > Save As > 3D formats available in your edition (OBJ, 3DS, SKP, FBX) from the 3D window.\n"
                "4. Record the Archicad version and build shown in Help > About, then place the files in this folder.\n")
    return out

def probe_commands():
    cands = ["API.GetProductInfo", "API.GetProjectInfo", "API.GetAllElements", "API.GetElementsByType", "API.GetAttributesByType", "API.GetAllClassificationSystems",
             "API.GetAllPropertyIds", "API.GetPropertyDefinitions", "API.GetStories", "API.GetLibraries", "API.Get3DBoundingBoxes", "API.GetSelectedElements",
             "API.GetAllPropertyGroupIds", "API.GetPublisherSetNames", "API.GetLocation", "API.GetGeoLocation"]
    r = {}
    for c in cands:
        try: raw(c, {}); r[c] = "ok"
        except Exception as e: r[c] = "error: " + str(e)[:200]
    r["note"] = ("The JSON API has no call that lists Archicad's own menu commands or tools. This file records which read-only API commands answered. "
                 "The owner records the tool box and menu tree manually (README).")
    return r

def manifest(pi):
    return {"harness": "dino8-reference-archicad", "harness_version": HARNESS_VERSION, "tested_against_real_application": False,
            "utc": datetime.datetime.utcnow().isoformat() + "Z", "program": "Archicad", "product_info": pi, "os": platform.platform(), "python": sys.version,
            "archicad_python_package": getattr(sys.modules.get("archicad"), "__version__", None), "output_dir": OUT, "settings": {"project": "empty project required; elements deleted after the run; never saved"}}

def main():
    t0 = time.time(); meas = {}
    n = count_elements()
    if n != 0:
        sys.exit("The open project has %d elements. Use File > New (empty project) and run again. Nothing was changed." % n)
    pi = {}
    try: pi = raw("API.GetProductInfo")
    except Exception: pass
    try:
        for fn in S:
            t = time.time()
            try: meas[fn.__name__] = {"status": "ok", "values": fn(), "seconds": round(time.time() - t, 3)}
            except Exception as e: meas[fn.__name__] = {"status": "error", "error": str(e), "trace": traceback.format_exc()}
    finally:
        try:
            if CREATED: raw("API.DeleteElements", {"elements": [eid(g) for g in CREATED]})
        except Exception: pass
    write_json("measurements.json", meas); write_json("commands.json", probe_commands())
    m = manifest(pi); m["scenario_count"] = len(S); m["scenarios_ok"] = sum(1 for v in meas.values() if v["status"] == "ok")
    m["seconds"] = round(time.time() - t0, 2); m["files"] = sorted(os.listdir(OUT)); write_json("manifest.json", m)
    print("Archicad reference run: %d/%d scenarios ok. Output: %s" % (m["scenarios_ok"], len(S), OUT))
main()
