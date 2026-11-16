#!/usr/bin/env python3
# Independent file-format checker for the RWI* sections (sections/94_rwi*.sh). It shares no code with the application: Python standard library only
# (struct, json, re, sys, math). It parses a file the application exported, measures it, and compares the measurements with values the section
# computed by hand from the construction (two 10 mm cubes, corners (0,0,0) and (20,0,0) in a millimetre document), printing "OK ..." (exit 0) or
# "BAD ..." (exit 1). A file that cannot be parsed at all prints "BAD ..." too. Tolerances are given by the caller and are never widened here.
#
#   python3 -I rwi_oracle.py FORMAT FILE [--parts N] [--min x,y,z] [--max x,y,z] [--vol V] [--vol-tol T] [--tris N] [--tris-multiple-of K]
#                                        [--closed] [--tol T] [--colors r,g,b;r,g,b] [--json]
#   FORMAT: obj | stl | ply | sat | iges | 3dm
#     obj   v / f lines (negative and polygon faces handled), "o" groups = parts, usemtl + the .mtl next to the file for Kd colours
#     stl   binary (80 byte header, count, 50 byte records; size must equal 84 + 50 n) or ascii; the stored normal of every facet must agree with
#           the winding (so the facet normals are outward for the closed solids checked here)
#     ply   ascii / binary_little_endian / binary_big_endian, vertex x y z (+ optional red green blue), face list property
#     sat   ACIS text records: body / lump / shell / face / loop / coedge / edge / vertex / point / plane-surface, pointers resolved, Euler number
#           per body, every edge used by two coedges, the volume from the face loops (face sense and plane normal cross-checked)
#     iges  80-column records, S G D P T sections, the T counts, the unit flag, entity 144 (trimmed surface) count, bounding box from the 110 line
#           end points and the 128 surface control points
#     3dm   openNURBS archive: 32 byte start string, 8 byte chunk lengths, the object table (0x10000013) records and their object type codes
#   Measurements: parts, verts, tris (triangles after fan-triangulating polygons), min, max, volume (signed, divergence theorem, summed over
#   parts), closed (every undirected edge used by exactly two faces in opposite directions, vertices welded by coordinate to 1e-5).
import json, math, os, re, struct, sys


def die(msg):
    print("BAD " + msg)
    sys.exit(1)


def weld_key(p):
    return (round(p[0] * 1e5), round(p[1] * 1e5), round(p[2] * 1e5))


def measure(parts):
    """parts: list of (verts, tris) with tris indices into verts. Returns the measurement dict."""
    allp = [v for verts, _ in parts for v in verts]
    out = {"parts": len(parts), "verts": len(allp), "tris": sum(len(t) for _, t in parts)}
    used = [verts[i] for verts, tris in parts for t in tris for i in t]
    if not used:
        die("no triangles")
    out["min"] = [min(p[k] for p in used) for k in range(3)]
    out["max"] = [max(p[k] for p in used) for k in range(3)]
    vol = 0.0
    closed = True
    for verts, tris in parts:
        for a, b, c in tris:
            pa, pb, pc = verts[a], verts[b], verts[c]
            vol += (pa[0] * (pb[1] * pc[2] - pb[2] * pc[1]) - pa[1] * (pb[0] * pc[2] - pb[2] * pc[0]) + pa[2] * (pb[0] * pc[1] - pb[1] * pc[0])) / 6.0
        edges = {}
        for a, b, c in tris:
            ka, kb, kc = weld_key(verts[a]), weld_key(verts[b]), weld_key(verts[c])
            if len({ka, kb, kc}) < 3:
                continue
            for u, v in ((ka, kb), (kb, kc), (kc, ka)):
                edges[(u, v)] = edges.get((u, v), 0) + 1
        for (u, v), n in edges.items():
            if n != 1 or edges.get((v, u), 0) != 1:
                closed = False
    out["volume"] = vol
    out["closed"] = closed
    return out


# ---------------------------------------------------------------------------------------------------------------- OBJ
def read_obj(path):
    verts, parts, cur, mats, curmat = [], [], None, {}, None
    mtl = None
    for raw in open(path, "r", encoding="utf-8", errors="strict"):
        t = raw.split("#", 1)[0].split()
        if not t:
            continue
        if t[0] == "mtllib":
            mtl = os.path.join(os.path.dirname(os.path.abspath(path)), raw.split(None, 1)[1].strip())
        elif t[0] == "o" or t[0] == "g":
            cur = {"name": " ".join(t[1:]), "tris": [], "mat": None}
            parts.append(cur)
        elif t[0] == "usemtl":
            if cur is None:
                cur = {"name": "", "tris": [], "mat": None}
                parts.append(cur)
            cur["mat"] = t[1]
        elif t[0] == "v":
            verts.append(tuple(float(x) for x in t[1:4]))
        elif t[0] == "f":
            if cur is None:
                cur = {"name": "", "tris": [], "mat": None}
                parts.append(cur)
            idx = []
            for c in t[1:]:
                i = int(c.split("/")[0])
                i = i - 1 if i > 0 else len(verts) + i
                if not 0 <= i < len(verts):
                    die("face index %s out of range (%d vertices)" % (c, len(verts)))
                idx.append(i)
            if len(idx) < 3:
                die("face with fewer than 3 corners")
            for k in range(1, len(idx) - 1):
                cur["tris"].append((idx[0], idx[k], idx[k + 1]))
    colors = {}
    if mtl and os.path.exists(mtl):
        name = None
        for raw in open(mtl):
            t = raw.split()
            if t and t[0] == "newmtl":
                name = t[1]
            elif t and t[0] == "Kd" and name:
                colors[name] = tuple(float(x) for x in t[1:4])
    return verts, parts, colors


def do_obj(path):
    verts, parts, colors = read_obj(path)
    if not parts:
        die("no faces")
    # a part uses the vertices its faces name; measure each part against the shared vertex list
    m = measure([(verts, p["tris"]) for p in parts])
    m["verts"] = len(verts)
    m["colors"] = [list(colors[p["mat"]]) if p["mat"] in colors else None for p in parts]
    m["names"] = [p["name"] for p in parts]
    return m


# ---------------------------------------------------------------------------------------------------------------- STL
def do_stl(path):
    data = open(path, "rb").read()
    tris, verts = [], []
    normals = []
    if len(data) >= 84 and 84 + 50 * struct.unpack_from("<I", data, 80)[0] == len(data):
        n = struct.unpack_from("<I", data, 80)[0]
        for i in range(n):
            rec = struct.unpack_from("<12fH", data, 84 + 50 * i)
            normals.append(rec[0:3])
            base = len(verts)
            verts += [tuple(rec[3:6]), tuple(rec[6:9]), tuple(rec[9:12])]
            tris.append((base, base + 1, base + 2))
        kind = "binary"
    else:
        text = data.decode("ascii", errors="strict")
        if not text.lstrip().startswith("solid"):
            die("neither a binary STL of the exact size 84 + 50 n nor an ascii STL")
        cur = None
        for line in text.splitlines():
            t = line.split()
            if len(t) >= 5 and t[0] == "facet" and t[1] == "normal":
                cur = tuple(float(x) for x in t[2:5])
            elif len(t) == 4 and t[0] == "vertex":
                verts.append(tuple(float(x) for x in t[1:4]))
                if len(verts) % 3 == 0:
                    base = len(verts) - 3
                    tris.append((base, base + 1, base + 2))
                    normals.append(cur)
        if len(verts) % 3:
            die("ascii STL vertex count not a multiple of 3")
        kind = "ascii"
    if not tris:
        die("no facets")
    bad = 0
    for (a, b, c), nrm in zip(tris, normals):
        pa, pb, pc = verts[a], verts[b], verts[c]
        e1 = [pb[k] - pa[k] for k in range(3)]
        e2 = [pc[k] - pa[k] for k in range(3)]
        cr = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        ln = math.sqrt(sum(x * x for x in cr))
        if ln == 0.0:
            bad += 1
            continue
        if sum(cr[k] / ln * nrm[k] for k in range(3)) < 0.999:
            bad += 1
    m = measure([(verts, tris)])
    m["stl"] = kind
    m["bad_normals"] = bad
    return m


# ---------------------------------------------------------------------------------------------------------------- PLY
def do_ply(path):
    data = open(path, "rb").read()
    end = data.find(b"end_header")
    if not data.startswith(b"ply") or end < 0:
        die("not a PLY file")
    nl = data.find(b"\n", end)
    head = data[:nl].decode("ascii").splitlines()
    body = data[nl + 1:]
    fmt, elems = None, []
    for line in head:
        t = line.split()
        if not t:
            continue
        if t[0] == "format":
            fmt = t[1]
        elif t[0] == "element":
            elems.append({"name": t[1], "count": int(t[2]), "props": []})
        elif t[0] == "property":
            elems[-1]["props"].append(t[1:])
    sizes = {"char": "b", "uchar": "B", "short": "h", "ushort": "H", "int": "i", "uint": "I", "float": "f", "double": "d",
             "int8": "b", "uint8": "B", "int16": "h", "uint16": "H", "int32": "i", "uint32": "I", "float32": "f", "float64": "d"}
    verts, faces, cols = [], [], []
    if fmt == "ascii":
        toks = body.decode("ascii").split()
        p = 0
        for e in elems:
            for _ in range(e["count"]):
                row = {}
                for pr in e["props"]:
                    if pr[0] == "list":
                        n = int(toks[p]); p += 1
                        row[pr[3]] = [int(x) for x in toks[p:p + n]]; p += n
                    else:
                        row[pr[1]] = float(toks[p]); p += 1
                if e["name"] == "vertex":
                    verts.append((row["x"], row["y"], row["z"]))
                    if "red" in row:
                        cols.append((row["red"], row["green"], row["blue"]))
                elif e["name"] == "face":
                    faces.append(row.get("vertex_indices") or row.get("vertex_index"))
        if p != len(toks):
            die("ascii PLY has %d unread tokens" % (len(toks) - p))
    elif fmt in ("binary_little_endian", "binary_big_endian"):
        en = "<" if fmt == "binary_little_endian" else ">"
        p = 0
        for e in elems:
            for _ in range(e["count"]):
                row = {}
                for pr in e["props"]:
                    if pr[0] == "list":
                        cs, vs = sizes[pr[1]], sizes[pr[2]]
                        n, = struct.unpack_from(en + cs, body, p); p += struct.calcsize(cs)
                        row[pr[3]] = list(struct.unpack_from(en + vs * n, body, p)); p += struct.calcsize(vs) * n
                    else:
                        v, = struct.unpack_from(en + sizes[pr[0]], body, p); p += struct.calcsize(sizes[pr[0]])
                        row[pr[1]] = v
                if e["name"] == "vertex":
                    verts.append((row["x"], row["y"], row["z"]))
                    if "red" in row:
                        cols.append((row["red"], row["green"], row["blue"]))
                elif e["name"] == "face":
                    faces.append(row.get("vertex_indices") or row.get("vertex_index"))
        if p != len(body):
            die("binary PLY has %d unread bytes" % (len(body) - p))
    else:
        die("unknown PLY format %r" % fmt)
    tris = []
    for f in faces:
        if len(f) < 3:
            die("face with fewer than 3 corners")
        for i in f:
            if not 0 <= i < len(verts):
                die("face index %d out of range" % i)
        for k in range(1, len(f) - 1):
            tris.append((f[0], f[k], f[k + 1]))
    m = measure([(verts, tris)])
    m["verts"] = len(verts)
    m["has_colors"] = bool(cols)
    return m


# ---------------------------------------------------------------------------------------------------------------- SAT
def do_sat(path):
    lines = open(path, "r", encoding="ascii").read().splitlines()
    if len(lines) < 4:
        die("too short for a SAT file")
    hdr = lines[0].split()
    if len(hdr) != 4:
        die("bad SAT header line %r" % lines[0])
    version = int(hdr[0])
    rec = {}
    for ln in lines[3:]:
        if not ln.strip():
            continue
        if ln.startswith("End-of-ACIS-data"):
            break
        m = re.match(r"^-(\d+)\s+(\S+)\s+(.*?)\s*#\s*$", ln)
        if not m:
            die("record not of the form '-N type ... #': %r" % ln[:60])
        rec[int(m.group(1))] = (m.group(2), m.group(3).split())

    def ptr(tok):
        if not tok.startswith("$"):
            die("expected a pointer, got %r" % tok)
        return int(tok[1:])

    def of(i, typ):
        if i not in rec or rec[i][0] != typ:
            die("pointer $%d does not name a %s record" % (i, typ))
        return rec[i][1]

    counts = {}
    for i, (typ, _) in rec.items():
        counts[typ] = counts.get(typ, 0) + 1
    bodies = [i for i, (t, _) in rec.items() if t == "body"]
    out = {"version": version, "counts": counts, "parts": len(bodies)}
    # edges: [-n edge $-1 -1 $-1 $v0 t0 $v1 t1 $coedge $curve sense ...] after the three attribute / id fields
    def fields(i, typ):
        f = of(i, typ)
        return f[3:] if f[0] == "$-1" and f[1] == "-1" and f[2] == "$-1" else f

    use = {}
    pts = {}
    for i, (t, f) in rec.items():
        if t == "point":
            pts[i] = tuple(float(x) for x in f[3:6])
    vpt = {i: pts[ptr(fields(i, "vertex")[1])] for i, (t, _) in rec.items() if t == "vertex"}
    edge_ends = {}
    for i, (t, _) in rec.items():
        if t == "edge":
            f = fields(i, "edge")
            edge_ends[i] = (ptr(f[0]), ptr(f[2]))
    for i, (t, _) in rec.items():
        if t == "coedge":
            f = fields(i, "coedge")
            e = ptr(f[3])
            if e not in edge_ends:
                die("coedge %d names a non-edge" % i)
            use[e] = use.get(e, 0) + 1
    if any(use.get(e, 0) != 2 for e in edge_ends):
        die("an edge is not used by exactly two coedges")
    euler = {}
    vol = 0.0
    allpts = []
    for b in bodies:
        faces = []
        lump = ptr(fields(b, "body")[1])
        while lump != -1:
            shell = ptr(fields(lump, "lump")[1])
            while shell != -1:
                fp = ptr(fields(shell, "shell")[2])
                while fp != -1:
                    faces.append(fp)
                    fp = ptr(fields(fp, "face")[0])
                shell = ptr(fields(shell, "shell")[0])
            lump = ptr(fields(lump, "lump")[0])
        vs, es = set(), set()
        for fc in faces:
            ff = fields(fc, "face")
            loop = ptr(ff[1])
            sense_rev = ff[-2] == "reversed"
            surf = ptr(ff[4])
            first = ptr(fields(loop, "loop")[1])
            seq, ce = [], first
            while True:
                cf = fields(ce, "coedge")
                e = ptr(cf[3])
                v0, v1 = edge_ends[e]
                es.add(e)
                if cf[4] == "reversed":
                    v0, v1 = v1, v0
                seq.append(v0)
                vs.add(v0)
                vs.add(v1)
                ce = ptr(cf[0])
                if ce == first:
                    break
                if len(seq) > 10000:
                    die("loop does not close")
            poly = [vpt[v] for v in seq]
            allpts += poly
            # plane normal check: the loop runs counter-clockwise about the face normal, which is the surface normal for a forward face
            # and its opposite for a reversed one
            sf = rec[surf]
            if sf[0] == "plane-surface":
                sn = [float(x) for x in sf[1][3 + 3:3 + 6]]
                nx = ny = nz = 0.0
                for k in range(len(poly)):
                    a, c = poly[k], poly[(k + 1) % len(poly)]
                    nx += (a[1] - c[1]) * (a[2] + c[2]); ny += (a[2] - c[2]) * (a[0] + c[0]); nz += (a[0] - c[0]) * (a[1] + c[1])
                ln = math.sqrt(nx * nx + ny * ny + nz * nz)
                sgn = -1.0 if sense_rev else 1.0
                if ln == 0 or sgn * (nx * sn[0] + ny * sn[1] + nz * sn[2]) / ln / math.sqrt(sum(x * x for x in sn)) < 0.999:
                    die("face %d: the loop's normal does not agree with the plane-surface normal (face sense %s)" % (fc, "reversed" if sense_rev else "forward"))
            for k in range(1, len(poly) - 1):
                a, bb, c = poly[0], poly[k], poly[k + 1]
                vol += (a[0] * (bb[1] * c[2] - bb[2] * c[1]) - a[1] * (bb[0] * c[2] - bb[2] * c[0]) + a[2] * (bb[0] * c[1] - bb[1] * c[0])) / 6.0
        euler[b] = len(vs) - len(es) + len(faces)
    out["euler"] = list(euler.values())
    out["faces"] = counts.get("face", 0)
    out["volume"] = vol
    out["closed"] = True  # every edge used exactly twice (checked above)
    out["min"] = [min(p[k] for p in pts.values()) for k in range(3)]
    out["max"] = [max(p[k] for p in pts.values()) for k in range(3)]
    return out


# ---------------------------------------------------------------------------------------------------------------- IGES
def do_iges(path):
    raw = open(path, "r", encoding="ascii").read().splitlines()
    sec = {"S": [], "G": [], "D": [], "P": [], "T": []}
    last = {}
    for ln in raw:
        if len(ln) != 80:
            die("record is %d columns, not 80: %r" % (len(ln), ln[:30]))
        s, n = ln[72], int(ln[73:80])
        if s not in sec:
            die("unknown section letter %r" % s)
        last[s] = last.get(s, 0) + 1
        if n != last[s]:
            die("section %s sequence number %d where %d was expected" % (s, n, last[s]))
        sec[s].append(ln[:72])
    t = sec["T"][0]
    want = {"S": int(t[1:8]), "G": int(t[9:16]), "D": int(t[17:24]), "P": int(t[25:32])}
    for k, v in want.items():
        if len(sec[k]) != v:
            die("T record says %d %s records, the file has %d" % (v, k, len(sec[k])))
    g = "".join(sec["G"])
    # global parameters: the first two are the delimiters, declared as Hollerith strings ("1H,,1H;" is the default pair)
    if not g.startswith("1H,,1H;,"):
        die("global section does not start with the default delimiters")
    rest = g[len("1H,,1H;,"):]
    params, i, cur = [], 0, ""
    while i < len(rest):
        m = re.match(r"(\d+)H", rest[i:]) if cur.strip() == "" else None
        if m:
            n = int(m.group(1)); i += len(m.group(0)); cur = rest[i:i + n]; i += n  # (leading blanks of a continued line are skipped by the match)
            continue
        c = rest[i]
        if c == "," or c == ";":
            params.append(cur); cur = ""
            if c == ";":
                break
        else:
            cur += c
        i += 1
    unit_flag = int(params[11])  # global parameter 14 (params[0] is parameter 3)
    unit_name = params[12]       # global parameter 15
    des = []
    for k in range(0, len(sec["D"]), 2):
        a, b = sec["D"][k], sec["D"][k + 1]
        des.append({"type": int(a[0:8]), "pd": int(a[8:16]), "seq": k + 1, "lines": int(b[24:32]), "level": int(a[32:40])})
    praw = []
    for line in raw:
        if line[72] == "P":
            praw.append((int(line[64:72]), line[:64]))
    byde = {}
    for de, text in praw:
        byde.setdefault(de, "")
        byde[de] += text
    ents = {}
    for d in des:
        if d["seq"] not in byde:
            die("directory entry %d has no parameter data" % d["seq"])
        txt = byde[d["seq"]].rstrip()
        if not txt.endswith(";"):
            die("parameter data of entry %d is not terminated by ';'" % d["seq"])
        vals = [x.strip() for x in txt[:-1].split(",")]
        if int(vals[0]) != d["type"]:
            die("entry %d: directory says type %d, the parameter data says %s" % (d["seq"], d["type"], vals[0]))
        ents[d["seq"]] = (d["type"], vals[1:])
    counts = {}
    for t_, _ in ents.values():
        counts[t_] = counts.get(t_, 0) + 1
    pts = []
    def num(x):
        return float(x.replace("D", "E").replace("d", "e"))
    for typ, v in ents.values():
        if typ == 110:
            pts.append([num(x) for x in v[0:3]]); pts.append([num(x) for x in v[3:6]])
        elif typ == 128:
            k1, k2, m1, m2 = (int(v[0]), int(v[1]), int(v[2]), int(v[3]))
            n1, n2 = k1 + 1, k2 + 1
            off = 4 + 5 + (k1 + m1 + 2) + (k2 + m2 + 2) + n1 * n2
            for q in range(n1 * n2):
                pts.append([num(x) for x in v[off + 3 * q: off + 3 * q + 3]])
    out = {"counts": counts, "faces": counts.get(144, 0), "parts": counts.get(144, 0), "unit_flag": unit_flag, "unit_name": unit_name}
    # levels: the level field (columns 33-40) of every trimmed surface, and the level definitions (406 form 3: "2,<level>,<name>")
    level_counts, level_names = {}, {}
    for d in des:
        if d["type"] == 144:
            level_counts[d["level"]] = level_counts.get(d["level"], 0) + 1
        if d["type"] == 406:
            v = ents[d["seq"]][1]
            mh = re.match(r"(\d+)H(.*)", v[2]) if len(v) > 2 else None
            if mh and v[0] == "2":
                level_names[int(v[1])] = mh.group(2)[:int(mh.group(1))] if mh.group(2) else ""
    out["level_counts"] = level_counts
    out["level_names"] = level_names
    out["named_counts"] = {}
    for lv, n in level_counts.items():
        nm = level_names.get(lv, "?%d" % lv)
        out["named_counts"][nm] = out["named_counts"].get(nm, 0) + n
    if pts:
        out["min"] = [min(p[k] for p in pts) for k in range(3)]
        out["max"] = [max(p[k] for p in pts) for k in range(3)]
    return out


# ---------------------------------------------------------------------------------------------------------------- 3dm
def do_3dm(path):
    d = open(path, "rb").read()
    if d[:24] != b"3D Geometry File Format ":
        die("no openNURBS start string")
    ver = int(d[24:32].decode("ascii").strip())
    if ver < 50:
        die("archive version %d: the oracle reads 8 byte chunk lengths (version 50 and later)" % ver)
    off, objs, types, tables, eof = 32, 0, [], [], False
    while off + 12 <= len(d):
        t, v = struct.unpack_from("<Iq", d, off)
        short = bool(t & 0x80000000)
        if not short:
            if v < 0 or off + 12 + v > len(d):
                die("top level chunk %08x runs past the end of the file" % t)
            tables.append(t)
            if t == 0x10000013:  # object table
                p, end = off + 12, off + 12 + v
                while p + 12 <= end:
                    rt, rv = struct.unpack_from("<Iq", d, p)
                    if rt & 0x80000000:
                        p += 12
                        continue
                    if rt == 0x20008070:
                        objs += 1
                        q, rend = p + 12, p + 12 + rv
                        while q + 12 <= rend:
                            ct, cv = struct.unpack_from("<Iq", d, q)
                            if ct == 0x82000071:
                                types.append(cv)
                            q += 12 if ct & 0x80000000 else 12 + cv
                    p += 12 + rv
            off += 12 + v
        else:
            off += 12
        if t == 0x7FFF:
            eof = True
            break
    if not eof:
        die("no end-of-file chunk")
    return {"version": ver, "parts": objs, "types": types, "tables": ["%08x" % t for t in tables]}


# ---------------------------------------------------------------------------------------------------------------- compare
def vec(s):
    return [float(x) for x in s.split(",")]


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        die("usage: rwi_oracle.py FORMAT FILE [options]")
    fmt, path = a[0], a[1]
    opts = {}
    flags = set()
    i = 2
    while i < len(a):
        if a[i] in ("--closed", "--json", "--open"):
            flags.add(a[i]); i += 1
        else:
            opts[a[i]] = a[i + 1]; i += 2
    try:
        fn = {"obj": do_obj, "stl": do_stl, "ply": do_ply, "sat": do_sat, "iges": do_iges, "3dm": do_3dm}[fmt]
        m = fn(path)
    except SystemExit:
        raise
    except Exception as e:  # any parse failure is a failure of the file, not of the oracle's expectations
        die("%s: %s: %s" % (fmt, type(e).__name__, e))
    if "--json" in flags:
        print(json.dumps(m, sort_keys=True))
    tol = float(opts.get("--tol", "1e-6"))
    errs = []
    if "--parts" in opts and m["parts"] != int(opts["--parts"]):
        errs.append("parts %d != %s" % (m["parts"], opts["--parts"]))
    for key, name in (("--min", "min"), ("--max", "max")):
        if key in opts:
            w = vec(opts[key])
            for k in range(3):
                if abs(m[name][k] - w[k]) > tol:
                    errs.append("%s[%d] %.9g != %.9g (tolerance %g)" % (name, k, m[name][k], w[k], tol))
    if "--vol" in opts:
        w = float(opts["--vol"]); vt = float(opts.get("--vol-tol", "1e-6")) * abs(w)
        if abs(m["volume"] - w) > vt:
            errs.append("volume %.9g != %.9g (tolerance %g)" % (m["volume"], w, vt))
    if "--tris" in opts and m["tris"] != int(opts["--tris"]):
        errs.append("triangles %d != %s" % (m["tris"], opts["--tris"]))
    if "--verts" in opts and m["verts"] != int(opts["--verts"]):
        errs.append("vertices %d != %s" % (m["verts"], opts["--verts"]))
    if "--tris-multiple-of" in opts and m["tris"] % int(opts["--tris-multiple-of"]):
        errs.append("triangles %d not a multiple of %s" % (m["tris"], opts["--tris-multiple-of"]))
    if "--open" in flags and m.get("closed"):
        errs.append("closed, but an open mesh was expected")
    if "--closed" in flags and not m.get("closed"):
        errs.append("not closed (an edge is not shared by exactly two oppositely directed faces)")
    if "--same-as" in opts:
        try:
            o = fn(opts["--same-as"])
        except SystemExit:
            raise
        except Exception as e:
            die("--same-as file: %s: %s" % (type(e).__name__, e))
        for key in ("parts", "tris", "faces"):
            if key in m and key in o and m[key] != o[key]:
                errs.append("%s %s != %s of the reference" % (key, m[key], o[key]))
        for k in range(3):
            if abs(m["min"][k] - o["min"][k]) > tol or abs(m["max"][k] - o["max"][k]) > tol:
                errs.append("bounding box differs from the reference in axis %d" % k)
        if "volume" in m and "volume" in o and abs(m["volume"] - o["volume"]) > 1e-6 * abs(o["volume"]):
            errs.append("volume %.9g != reference %.9g" % (m["volume"], o["volume"]))
    if "--faces" in opts and m.get("faces") != int(opts["--faces"]):
        errs.append("faces %s != %s" % (m.get("faces"), opts["--faces"]))
    if "--euler" in opts and any(e != int(opts["--euler"]) for e in m.get("euler", [])):
        errs.append("Euler numbers %s != %s" % (m.get("euler"), opts["--euler"]))
    if "--unit-flag" in opts and m.get("unit_flag") != int(opts["--unit-flag"]):
        errs.append("unit flag %s != %s" % (m.get("unit_flag"), opts["--unit-flag"]))
    if "--level-counts" in opts:
        w = {int(x.split(":")[0]): int(x.split(":")[1]) for x in opts["--level-counts"].split(",")}
        if m.get("level_counts") != w:
            errs.append("trimmed surfaces per level %s != %s" % (m.get("level_counts"), w))
    if "--level-names" in opts:
        w = {int(x.split("=")[0]): x.split("=")[1] for x in opts["--level-names"].split(",")}
        if m.get("level_names") != w:
            errs.append("level definitions %s != %s" % (m.get("level_names"), w))
    if "--named-counts" in opts:
        w = {x.split(":")[0]: int(x.split(":")[1]) for x in opts["--named-counts"].split(",")}
        if m.get("named_counts") != w:
            errs.append("trimmed surfaces per named level %s != %s" % (m.get("named_counts"), w))
    if "--types" in opts:
        w = [int(x) for x in opts["--types"].split(",")]
        if m.get("types") != w:
            errs.append("object types %s != %s" % (m.get("types"), w))
    if "--colors" in opts:
        w = [vec(x) for x in opts["--colors"].split(";")]
        got = m.get("colors", [])
        if len(got) != len(w) or any(g is None or any(abs(g[k] - ww[k]) > 5e-7 for k in range(3)) for g, ww in zip(got, w)):
            errs.append("colours %s != %s" % (got, w))
    if "--stl-normals" in flags or fmt == "stl":
        if m.get("bad_normals"):
            errs.append("%d facet normals disagree with the winding" % m["bad_normals"])
    if errs:
        print("BAD " + "; ".join(errs))
        sys.exit(1)
    print("OK " + " ".join("%s=%s" % (k, ("%.9g" % v) if isinstance(v, float) else v) for k, v in sorted(m.items()) if k in ("parts", "verts", "tris", "faces", "volume", "closed", "version")))


if __name__ == "__main__":
    main()
