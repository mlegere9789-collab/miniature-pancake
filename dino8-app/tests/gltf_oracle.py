#!/usr/bin/env python3
# Independent glTF 2.0 / GLB checker used by sections/89_gltf.sh. It shares no code with the application: it parses the container with
# struct + json + base64, resolves every accessor itself, and prints one JSON object of measurements the shell compares with closed forms.
#   usage: python3 -I gltf_oracle.py <file.glb|file.gltf>
# Checks (any violation exits 2 with a message on stderr): header / chunk lengths, buffer and bufferView ranges, 4-byte alignment of
# component data, accessor count / min / max against the decoded values (POSITION), unit-length NORMALs with the same count as POSITION,
# indices in range, no node transform (the application writes none), a material index in range.
import base64, json, math, os, struct, sys

def die(msg):
    sys.stderr.write("gltf_oracle: " + msg + "\n")
    sys.exit(2)

def load(path):
    data = open(path, "rb").read()
    bins = []
    if data[:4] == b"glTF":
        magic, ver, total = struct.unpack_from("<4sII", data, 0)
        if ver != 2 or total != len(data):
            die("bad GLB header (version %d, length %d vs file %d)" % (ver, total, len(data)))
        off, js, bn = 12, None, None
        while off < total:
            ln, typ = struct.unpack_from("<II", data, off)
            if ln % 4:
                die("chunk length not a multiple of 4")
            body = data[off + 8: off + 8 + ln]
            if len(body) != ln:
                die("chunk runs past the file")
            if typ == 0x4E4F534A and js is None:
                js = body
            elif typ == 0x004E4942 and bn is None:
                bn = body
            off += 8 + ln
        if js is None or bn is None:
            die("GLB without a JSON or BIN chunk")
        doc = json.loads(js.decode("utf-8"))
        bins = [bn]
    else:
        doc = json.loads(data.decode("utf-8"))
        base = os.path.dirname(os.path.abspath(path))
        for b in doc.get("buffers", []):
            uri = b.get("uri", "")
            if uri.startswith("data:"):
                head, payload = uri.split(",", 1)
                if not head.endswith(";base64"):
                    die("non-base64 data uri")
                bins.append(base64.b64decode(payload, validate=True))
            else:
                bins.append(open(os.path.join(base, uri), "rb").read())
    if doc.get("asset", {}).get("version") != "2.0":
        die("asset.version is not 2.0")
    for i, b in enumerate(doc.get("buffers", [])):
        if i >= len(bins) or b["byteLength"] > len(bins[i]):
            die("buffer %d shorter than its byteLength" % i)
    return doc, bins

CT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}

def accessor(doc, bins, ai):
    a = doc["accessors"][ai]
    if "sparse" in a:
        die("sparse accessor")
    bv = doc["bufferViews"][a["bufferView"]]
    fmt, size = CT[a["componentType"]]
    n = NC[a["type"]]
    stride = bv.get("byteStride", size * n)
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    if base % size:
        die("accessor %d component data not aligned" % ai)
    end = base + stride * (a["count"] - 1) + size * n if a["count"] else base
    if end > bv.get("byteOffset", 0) + bv["byteLength"]:
        die("accessor %d runs past its bufferView" % ai)
    buf = bins[bv["buffer"]]
    out = []
    for i in range(a["count"]):
        out.append(struct.unpack_from("<" + fmt * n, buf, base + i * stride))
    return a, out

def main():
    doc, bins = load(sys.argv[1])
    for nd in doc.get("nodes", []):
        for k in ("matrix", "translation", "rotation", "scale"):
            if k in nd:
                die("node carries a transform (%s)" % k)
    parts, verts, tris = 0, 0, 0
    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    vol = 0.0
    colors = []
    for m in doc.get("meshes", []):
        for p in m["primitives"]:
            if p.get("mode", 4) != 4:
                die("primitive is not TRIANGLES")
            pa, pos = accessor(doc, bins, p["attributes"]["POSITION"])
            if pa["type"] != "VEC3" or pa["componentType"] != 5126:
                die("POSITION is not float VEC3")
            for k in range(3):
                if pa.get("min", [None] * 3)[k] is None or abs(min(v[k] for v in pos) - pa["min"][k]) > 1e-6 * max(1.0, abs(pa["min"][k])) \
                        or abs(max(v[k] for v in pos) - pa["max"][k]) > 1e-6 * max(1.0, abs(pa["max"][k])):
                    die("POSITION min / max do not match the data")
            if "NORMAL" in p["attributes"]:
                na, nrm = accessor(doc, bins, p["attributes"]["NORMAL"])
                if na["count"] != pa["count"]:
                    die("NORMAL count differs from POSITION count")
                for v in nrm:
                    if abs(math.sqrt(sum(c * c for c in v)) - 1.0) > 1e-4:
                        die("a NORMAL is not unit length")
            _, idx = accessor(doc, bins, p["indices"])
            flat = [i[0] for i in idx]
            if len(flat) % 3 or any(i >= pa["count"] for i in flat):
                die("indices not whole triangles or out of range")
            for t in range(0, len(flat), 3):
                a, b, c = (pos[flat[t]], pos[flat[t + 1]], pos[flat[t + 2]])
                vol += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0
            for v in pos:
                for k in range(3):
                    lo[k] = min(lo[k], v[k])
                    hi[k] = max(hi[k], v[k])
            parts += 1
            verts += pa["count"]
            tris += len(flat) // 3
            if "material" in p:
                mats = doc.get("materials", [])
                if p["material"] >= len(mats):
                    die("material index out of range")
                pbr = mats[p["material"]].get("pbrMetallicRoughness", {})
                colors.append(pbr.get("baseColorFactor", [1, 1, 1, 1]))
            else:
                colors.append(None)
    json.dump({"parts": parts, "verts": verts, "tris": tris, "min": lo, "max": hi, "volume": vol, "colors": colors}, sys.stdout)
    sys.stdout.write("\n")

main()
