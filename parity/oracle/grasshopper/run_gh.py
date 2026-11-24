# Grasshopper oracle runner (Phase B). Run inside Rhino 8 (Python 3). Public Grasshopper SDK types only.
#   D8_CASES  path to cases.jsonl   D8_OUT  path to write golden jsonl   D8_LIMIT optional max cases   D8_ONLY optional id prefixes
# Records numbers, flags and data-tree structure only. Component names are public identifiers; no descriptions or help text are read.
import json, math, os, time, traceback, System

import Rhino

Rhino.PlugIns.PlugIn.LoadPlugIn(System.Guid("b45a29b1-4343-4035-989e-044e8580d9cf"))
import Grasshopper as gh
from Grasshopper.Kernel import GH_Document, GH_RuntimeMessageLevel
from Grasshopper.Kernel.Data import GH_Path
from Grasshopper.Kernel.Types import GH_Number, GH_Integer, GH_String, GH_Boolean

PROXIES = [x for x in gh.Instances.ComponentServer.ObjectProxies if not x.Obsolete]


def make(name, sub):
    c = [x for x in PROXIES if x.Desc.Name == name and (not sub or str(x.Desc.SubCategory) == sub)]
    if not c:
        raise LookupError("component")
    return c[0].CreateInstance()


def parse_path(key):
    return GH_Path(*[int(s) for s in key.split(";")]) if key else GH_Path(0)


def append(param, v, path):
    d = param.PersistentData
    if isinstance(v, bool):
        order = (GH_Boolean, GH_Integer, GH_Number)
    elif isinstance(v, str):
        order = (GH_String,)
    elif isinstance(v, int):
        order = (GH_Integer, GH_Number)
    else:
        order = (GH_Number, GH_Integer)
    last = None
    for ctor in order:
        try:
            d.Append(ctor(v) if ctor is not GH_Integer else ctor(int(v)), path)
            return
        except TypeError as e:
            last = e
    raise last


def fill(param, spec):
    param.PersistentData.Clear()
    branches = {"0": spec} if isinstance(spec, list) else spec
    for key, vals in branches.items():
        path = parse_path(key)
        if not vals:
            param.PersistentData.EnsurePath(path)
        for v in vals:
            append(param, v, path)


def set_input(param, spec, doc):
    if spec is None:
        return
    if hasattr(param, "PersistentData"):
        fill(param, spec)
        return
    # Some inputs (for example the expression component's) are not persistent parameters: feed them from a standard upstream parameter.
    flat = [v for vals in ([spec] if isinstance(spec, list) else spec.values()) for v in (vals if isinstance(vals, list) else [vals])]
    helper = make("Text" if any(isinstance(v, str) for v in flat) else "Number", "Primitive")
    doc.AddObject(helper, False)
    fill(helper, spec)
    param.AddSource(helper)


def num(x):
    if x != x:
        return "nan"
    if x in (float("inf"), float("-inf")):
        return "inf" if x > 0 else "-inf"
    return float("%.12g" % x)


def conv(goo):
    if goo is None:
        return "null"
    v = getattr(goo, "Value", goo)
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)):
        return num(float(v))
    if isinstance(v, str):
        return v[:200]
    n = type(v).__name__
    if n in ("Point3d", "Vector3d"):
        return [num(v.X), num(v.Y), num(v.Z)]
    if n == "Interval":
        return [num(v.T0), num(v.T1)]
    return "T:" + n


def run_case(c):
    a = c["args"]
    doc = GH_Document()
    doc.Enabled = True
    inst = {}
    for nid, spec in a["nodes"].items():
        o = make(spec["c"], spec.get("sub"))
        doc.AddObject(o, False)
        inst[nid] = o
    for nid, spec in a["nodes"].items():
        for i, s in enumerate(spec.get("in", [])):
            if s is not None:
                set_input(inst[nid].Params.Input[i], s, doc)
    for src, so, dst, di in a["wires"]:
        inst[dst].Params.Input[di].AddSource(inst[src].Params.Output[so])
    doc.NewSolution(True)
    trees = {}
    for nid, oi in a["outs"]:
        d = inst[nid].Params.Output[oi].VolatileData
        trees["%s.%d" % (nid, oi)] = [[str(p), [conv(g) for g in d.get_Branch(p)]] for p in d.Paths]
    counts = {"warnings": 0, "errors": 0, "remarks": 0}
    for o in inst.values():
        for lvl, key in ((GH_RuntimeMessageLevel.Warning, "warnings"), (GH_RuntimeMessageLevel.Error, "errors"), (GH_RuntimeMessageLevel.Blank, "remarks")):
            counts[key] += len(list(o.RuntimeMessages(lvl)))
    out = {"trees": trees}
    out.update(counts)
    return out


def main():
    cases_path, out_path = os.environ["D8_CASES"], os.environ["D8_OUT"]
    limit = int(os.environ.get("D8_LIMIT", "0") or 0)
    only = [s for s in os.environ.get("D8_ONLY", "").split(",") if s]
    build = "rhino-%s" % str(Rhino.RhinoApp.Version)
    try:
        build += "+gh-%s" % str(gh.Instances.RunningVersion)
    except Exception:
        pass
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
            rec = {"id": c["id"], "build": build}
            try:
                rec["out"] = run_case(c)
                rec["ok"] = True
            except Exception as e:
                rec["ok"] = False
                rec["error"] = type(e).__name__
                rec["out"] = {}
            rec["ms"] = int((time.time() - t0) * 1000)
            fout.write(json.dumps(rec, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n")
    print("wrote %d golden records (%s)" % (n, build))


main()
