#!/usr/bin/env python3
"""EXO_ checker: decodes the .stl / .ply / .obj files written by the EXO_ section of smoke.sh with readers written here from the
public format descriptions (struct for the binary STL and binary PLY layouts, plain parsing for the text ones), never with
the application's own importers. Prints one "ok   EXO_<name>: ..." or "FAIL EXO_<name>: ..." line per check.

    python3 exo_check.py <directory the section exported into> [app output text file]
"""
import glob
import math
import os
import struct
import sys

D = sys.argv[1]
APP_OUT = open(sys.argv[2], errors="replace").read() if len(sys.argv) > 2 else ""
failed = 0


def report(ok, name, detail=""):
    global failed
    if not ok:
        failed += 1
    print(("ok   " if ok else "FAIL ") + "EXO_" + name + (": " + detail if detail else ""))


def tri_volume(tris):
    v = 0.0
    for a, b, c in tris:
        v += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0
    return v


def bbox(points):
    xs, ys, zs = zip(*points)
    return (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))


def close(a, b, rel=1e-4, absol=1e-9):
    return abs(a - b) <= max(absol, rel * max(abs(a), abs(b)))


def bbox_close(bb, ex, rel=1e-4):
    return all(close(x, y, rel, 1e-9) for x, y in zip(bb, ex))


# ------------------------------------------------------------------ STL
def read_stl_binary(path):
    data = open(path, "rb").read()
    n = struct.unpack_from("<I", data, 80)[0]
    if len(data) != 84 + 50 * n:
        return None
    tris = []
    for i in range(n):
        f = struct.unpack_from("<12fH", data, 84 + 50 * i)
        tris.append(((f[3], f[4], f[5]), (f[6], f[7], f[8]), (f[9], f[10], f[11])))
    return tris


def read_stl_ascii(path):
    data = open(path, "rb").read()
    if not data.lstrip().startswith(b"solid"):
        return None
    pts = []
    for line in data.decode("ascii", "replace").splitlines():
        t = line.split()
        if t and t[0] == "vertex":
            pts.append(tuple(float(x) for x in t[1:4]))
    if not pts or len(pts) % 3:
        return None
    return [tuple(pts[i:i + 3]) for i in range(0, len(pts), 3)]


def stl_check(name, tris, volume, ex_bbox, rel=1e-4):
    if not tris:
        report(False, name, "could not decode the file with the independent STL reader")
        return
    pts = [p for t in tris for p in t]
    vol = tri_volume(tris)
    bb = bbox(pts)
    report(close(vol, volume, rel * 10) and bbox_close(bb, ex_bbox, rel),
           name, "%d triangles, volume %.6g (expected %.6g), bbox %s" % (len(tris), vol, volume, tuple(round(x, 6) for x in bb)))


def exists(name):
    return os.path.exists(os.path.join(D, name))


# 10 x 20 x 30 box: volume 6000, bbox (0,0,0)-(10,20,30)
BOX_BB = (0, 0, 0, 10, 20, 30)
if exists("bin.stl"):
    t = read_stl_binary(os.path.join(D, "bin.stl"))
    stl_check("stl_default_is_binary", t, 6000.0, BOX_BB)
else:
    report(False, "stl_default_is_binary", "bin.stl was not written")
if exists("asc.stl"):
    raw = open(os.path.join(D, "asc.stl"), "rb").read(5)
    report(raw == b"solid", "stl_format_ascii_starts_with_solid", "first bytes %r" % raw)
    stl_check("stl_format_ascii_geometry", read_stl_ascii(os.path.join(D, "asc.stl")), 6000.0, BOX_BB)
else:
    report(False, "stl_format_ascii", "asc.stl was not written")
if exists("sc.stl"):
    stl_check("stl_scale_0_001_is_metres_from_millimetres", read_stl_binary(os.path.join(D, "sc.stl")), 6000.0e-9, tuple(x * 0.001 for x in BOX_BB))
else:
    report(False, "stl_scale", "sc.stl was not written")
if exists("in.stl"):
    f = 1.0 / 25.4
    stl_check("stl_units_in_from_millimetre_document", read_stl_binary(os.path.join(D, "in.stl")), 6000.0 * f ** 3, tuple(x * f for x in BOX_BB), 1e-3)
else:
    report(False, "stl_units", "in.stl was not written")


# ------------------------------------------------------------------ PLY
PLY_SIZE = {"char": 1, "uchar": 1, "int8": 1, "uint8": 1, "short": 2, "ushort": 2, "int16": 2, "uint16": 2, "int": 4, "uint": 4,
            "int32": 4, "uint32": 4, "float": 4, "float32": 4, "double": 8, "float64": 8}
PLY_FMT = {"char": "b", "uchar": "B", "int8": "b", "uint8": "B", "short": "h", "ushort": "H", "int16": "h", "uint16": "H",
           "int": "i", "uint": "I", "int32": "i", "uint32": "I", "float": "f", "float32": "f", "double": "d", "float64": "d"}


def read_ply(path):
    data = open(path, "rb").read()
    end = data.find(b"end_header\n")
    if end < 0 or not data.startswith(b"ply\n"):
        return None
    header = data[:end].decode("ascii").splitlines()
    body = data[end + len(b"end_header\n"):]
    fmt = [l.split()[1] for l in header if l.startswith("format ")][0]
    elems = []
    for l in header:
        t = l.split()
        if t[0] == "element":
            elems.append({"name": t[1], "count": int(t[2]), "props": []})
        elif t[0] == "property":
            elems[-1]["props"].append({"list": True, "count": t[2], "type": t[3], "name": t[4]} if t[1] == "list" else {"list": False, "type": t[1], "name": t[2]})
    verts, faces = [], []
    if fmt == "ascii":
        lines = body.decode("ascii").splitlines()
        li = 0
        for e in elems:
            for _ in range(e["count"]):
                t = lines[li].split()
                li += 1
                if e["name"] == "vertex":
                    verts.append(tuple(float(x) for x in t[:3]))
                elif e["name"] == "face":
                    n = int(t[0])
                    faces.append([int(x) for x in t[1:1 + n]])
    else:
        endian = "<" if fmt == "binary_little_endian" else ">"
        pos = 0
        for e in elems:
            for _ in range(e["count"]):
                row = {}
                idx = []
                for p in e["props"]:
                    if p["list"]:
                        n = struct.unpack_from(endian + PLY_FMT[p["count"]], body, pos)[0]
                        pos += PLY_SIZE[p["count"]]
                        vals = struct.unpack_from(endian + PLY_FMT[p["type"]] * n, body, pos)
                        pos += PLY_SIZE[p["type"]] * n
                        idx = list(vals)
                    else:
                        row[p["name"]] = struct.unpack_from(endian + PLY_FMT[p["type"]], body, pos)[0]
                        pos += PLY_SIZE[p["type"]]
                if e["name"] == "vertex":
                    verts.append((row["x"], row["y"], row["z"]))
                elif e["name"] == "face":
                    faces.append(idx)
        if pos != len(body):
            return None
    return fmt, verts, faces


def ply_check(name, path, fmt_expect):
    if not exists(path):
        report(False, name, path + " was not written")
        return
    r = read_ply(os.path.join(D, path))
    if r is None:
        report(False, name, "could not decode the file with the independent PLY reader (payload length / header)")
        return
    fmt, verts, faces = r
    tris = []
    for f in faces:
        for k in range(1, len(f) - 1):
            tris.append((verts[f[0]], verts[f[k]], verts[f[k + 1]]))
    vol = tri_volume(tris)
    bb = bbox(verts)
    report(fmt == fmt_expect and close(vol, 6000.0, 1e-3) and bbox_close(bb, BOX_BB, 1e-4) and len(faces) >= 6,
           name, "format %s, %d vertices, %d faces, volume %.6g, bbox %s" % (fmt, len(verts), len(faces), vol, tuple(round(x, 5) for x in bb)))


ply_check("ply_default_is_ascii", "a.ply", "ascii")
ply_check("ply_format_binary_is_little_endian", "b_le.ply", "binary_little_endian")
ply_check("ply_format_binarybigendian", "b_be.ply", "binary_big_endian")
if exists("b_le.ply") and exists("b_be.ply"):
    a, b = read_ply(os.path.join(D, "b_le.ply")), read_ply(os.path.join(D, "b_be.ply"))
    report(a and b and a[1] == b[1] and a[2] == b[2], "ply_both_byte_orders_carry_the_same_mesh")

# ------------------------------------------------------------------ OBJ
def read_obj(path):
    v, f = [], []
    for line in open(path, errors="replace"):
        t = line.split()
        if not t:
            continue
        if t[0] == "v":
            v.append(tuple(float(x) for x in t[1:4]))
        elif t[0] == "f":
            f.append([int(x.split("/")[0]) - 1 for x in t[1:]])
    return v, f


if exists("s.obj"):
    v, f = read_obj(os.path.join(D, "s.obj"))
    tris = [(v[q[0]], v[q[k]], v[q[k + 1]]) for q in f for k in range(1, len(q) - 1)]
    report(bbox_close(bbox(v), tuple(x * 2 for x in BOX_BB)) and close(tri_volume(tris), 48000.0, 1e-3), "obj_scale_2",
           "bbox %s, volume %.6g (expected 48000)" % (tuple(round(x, 5) for x in bbox(v)), tri_volume(tris)))
else:
    report(False, "obj_scale_2", "s.obj was not written")
if exists("big.obj"):
    v, f = read_obj(os.path.join(D, "big.obj"))
    mx = max(p[0] for p in v)
    # the box is 1234.56789 wide: the default 6 significant digits wrote 1234.57 (error 2e-3); 9 digits write the float exactly
    report(abs(mx - 1234.56789) < 5e-4, "obj_keeps_float_precision", "max x %.7f (expected 1234.56789 within 5e-4)" % mx)
else:
    report(False, "obj_keeps_float_precision", "big.obj was not written")

# ------------------------------------------------------------------ tolerance
if exists("coarse.stl") and exists("fine.stl"):
    c, f = read_stl_binary(os.path.join(D, "coarse.stl")), read_stl_binary(os.path.join(D, "fine.stl"))
    exact = math.pi * 25.0 * 20.0
    if c and f:
        vc, vf = tri_volume(c), tri_volume(f)
        report(len(f) > 2 * len(c), "tolerance_finer_makes_more_triangles", "%d (Tolerance=1) vs %d (Tolerance=0.005)" % (len(c), len(f)))
        report(abs(vf - exact) < 0.002 * exact and abs(vc - exact) > abs(vf - exact), "tolerance_finer_is_closer_to_the_exact_volume",
               "coarse %.2f, fine %.2f, exact %.2f" % (vc, vf, exact))
    else:
        report(False, "tolerance", "could not decode the cylinder STL files")
else:
    report(False, "tolerance", "coarse.stl / fine.stl were not written")

# ------------------------------------------------------------------ per object
po = sorted(glob.glob(os.path.join(D, "po_*.stl")))
report(len(po) == 2 and not exists("po.stl"), "perobject_writes_one_file_per_object_and_no_merged_file", "files %s" % [os.path.basename(p) for p in po])
if len(po) == 2:
    for i, p in enumerate(po):
        t = read_stl_binary(p)
        stl_check("perobject_file_%d" % (i + 1), t, 1000.0, (0, 0, 0, 10, 10, 10) if i == 0 else (20, 0, 0, 30, 10, 10))

# ------------------------------------------------------------------ refusals
report(not exists("bad.stl") and "Format=Bogus is not" in APP_OUT, "bad_format_value_is_refused_and_writes_nothing")
report(not exists("badscale.stl") and "Scale=-2 is not a positive number" in APP_OUT, "bad_scale_value_is_refused_and_writes_nothing")
report(not exists("badpo.dxf") and "PerObject=Yes applies to .stl, .obj and .ply files only" in APP_OUT, "perobject_on_another_format_is_refused")

sys.exit(0)
