#!/usr/bin/env python3
"""Independent oracle for the SUDOC_SCN1, SUDOC_SCN30 and SUDOC_PRT50 sections (scenes and printing). Python standard library only.

  scn1  APPLOG DUMP    five scenes (named views) saved, listed, one deleted: the listing order and names, and the file as read back by openNURBS
  scn30 APPLOG DUMP    a perspective view with a 40 degree field of view saved, left, restored: the restored camera equals the saved one, and the
                       file's frustum (read by openNURBS, which does not use the app's lens) spans 40 degrees
  prt50 DIR            Print at the scale 1:50 (0.02 page millimetres per millimetre of the model): a 1 m edge is 20 mm in the SVG and the PDF
  selftest             hand-authored inputs: the checks accept them and reject doctored copies

APPLOG is the application output of the script (the blocks start at `Unknown command: SUDOCBEGIN_<name>` lines the script writes), DUMP the output of
`view3dm_check dump FILE.3dm`. Prints `ok   ...` / `FAIL ...` lines, exit status 1 when any FAIL.
"""
import math
import re
import sys
import xml.etree.ElementTree as ET

NUM = r"[-+]?\d+(?:\.\d+)?(?:e[-+]?\d+)?"
CAM = re.compile(r"Camera \((?P<name>[^)]*)\): location (?P<lx>%s),(?P<ly>%s),(?P<lz>%s), target (?P<tx>%s),(?P<ty>%s),(?P<tz>%s), distance (?P<d>%s), "
                 r"(?:lens (?P<lens>%s) mm \((?P<fov>%s) deg\)|(?P<par>parallel projection), height (?P<h>%s))" % ((NUM,) * 10))
failed = 0


def report(cond, what):
    global failed
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        failed += 1


def block(out, name):
    m = re.search(r"Unknown command: SUDOCBEGIN_%s\n(.*?)(?=Unknown command: SUDOCBEGIN_|\Z)" % name, out, re.S)
    return m.group(1) if m else ""


def cam_of(text):
    """The first Camera report of the text as a dict with the printed line, or None."""
    m = CAM.search(text)
    if not m:
        return None
    g = {k: v for k, v in m.groupdict().items() if v is not None}
    c = {k: (float(v) if k not in ("name", "par") else v) for k, v in g.items()}
    c["line"] = m.group(0)
    c["eye"] = (c["lx"], c["ly"], c["lz"])
    c["tgt"] = (c["tx"], c["ty"], c["tz"])
    return c


def near(a, b, scale=None):
    """The camera lines carry 4 significant digits: 0.02 absolute plus 0.2 percent of the value is the rounding of the print and nothing more."""
    scale = abs(b) if scale is None else scale
    return abs(a - b) <= 0.02 + 0.002 * scale


def near3(p, q):
    return all(near(a, b) for a, b in zip(p, q))


def dump_views(text):
    """[{name, persp, eye, dir, up, target, angle, lens}] of the lines `view i name=... persp=... eye=(x,y,z) ...` of view3dm_check dump."""
    out = []
    for ln in text.split("\n"):
        m = re.match(r"view (\d+) name=(\S*) persp=(\d) eye=\(([^)]*)\) dir=\(([^)]*)\) up=\(([^)]*)\) target=\(([^)]*)\) frustum_h=\S+ frustum_w=\S+ near=\S+ angle=(\S+) lens=(\S+)", ln)
        if m:
            v = lambda s: tuple(float(x) for x in s.split(","))
            out.append({"name": m.group(2), "persp": int(m.group(3)), "eye": v(m.group(4)), "dir": v(m.group(5)), "up": v(m.group(6)), "target": v(m.group(7)),
                        "angle": float(m.group(8)), "lens": float(m.group(9))})
    return out


def list_names(text):
    """Scene names of one `NamedView List` report: the count line and the '  name: location ...' lines, in order."""
    m = re.search(r"(\d+) named view\(s\)", text)
    names = re.findall(r"^(?:history: )?\s+(\S+): location ", text, re.M)
    return (int(m.group(1)) if m else None), names


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-SCENE-0001
def scn1(log, dump):
    cams = [cam_of(block(log, "c%d" % i)) for i in range(1, 6)]
    report(all(c is not None and "lens" in c for c in cams), "SCN1_1: five Camera reports, one per scene, all perspective (found %d)" % sum(1 for c in cams if c))
    if not all(cams):
        return
    gaps = [math.dist(cams[i]["eye"], cams[i + 1]["eye"]) for i in range(4)]
    report(all(x > 1.0 for x in gaps), "SCN1_2: each scene has a camera of its own (the 30 degree turns move the camera by %s)" % ", ".join("%.2f" % x for x in gaps))
    n1, names1 = list_names(block(log, "list1"))
    report(n1 == 5 and names1 == ["S1", "S2", "S3", "S4", "S5"], "SCN1_3: the list reports 5 named views in the order they were saved: S1 S2 S3 S4 S5 (found %s, %s)" % (n1, names1))
    n2, names2 = list_names(block(log, "list2"))
    report(n2 == 4 and names2 == ["S1", "S2", "S4", "S5"], "SCN1_4: after deleting S3 the list reports 4 views and the remaining order S1 S2 S4 S5 (found %s, %s)" % (n2, names2))
    report(names2 != ["S1", "S2", "S3", "S4"] and names2 != ["S2", "S3", "S4", "S5"], "SCN1_4d: decoy, deleting S3 did not remove the last or the first scene instead")
    views = dump_views(dump)
    report([v["name"] for v in views] == ["S1", "S2", "S4", "S5"], "SCN1_5: the file, read by openNURBS, holds the four views S1 S2 S4 S5 in that order (found %s)" % [v["name"] for v in views])
    if [v["name"] for v in views] == ["S1", "S2", "S4", "S5"]:
        want = [cams[0], cams[1], cams[3], cams[4]]
        report(all(v["persp"] == 1 and near3(v["eye"], c["eye"]) and near3(v["target"], c["tgt"]) for v, c in zip(views, want)),
               "SCN1_6: every view of the file has the camera location and target the app reported when it saved it (to the 4 digits of the report)")
        report(not near3(views[1]["eye"], cams[2]["eye"]), "SCN1_6d: decoy, the second view of the file is not the deleted scene's camera")


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-SCENE-0030
def scn30(log, dump):
    a, b, c = cam_of(block(log, "a")), cam_of(block(log, "b")), cam_of(block(log, "c"))
    report(a is not None and "lens" in a and abs(a["fov"] - 40.0) <= 0.06, "SCN30_1: the saved view is perspective with a field of view of 40 degrees (found %s)" % (a["fov"] if a and "fov" in a else None))
    report(b is not None and "par" in b, "SCN30_2: in between, the viewport is a parallel view (Top)")
    if not (a and b and c):
        report(False, "SCN30_3: the camera reports are missing")
        return
    report(c["line"] == a["line"], "SCN30_3: after Restore the camera report is the saved one: location, target, distance, lens and angle all equal (%s)" % c["line"])
    report(a["line"] != b["line"], "SCN30_3d: decoy, the intermediate parallel view is not the saved one")
    views = dump_views(dump)
    v = next((x for x in views if x["name"] == "Fov40"), None)
    report(v is not None and v["persp"] == 1, "SCN30_4: the file holds the perspective view Fov40")
    if v is None:
        return
    lens = 12.0 / math.tan(math.radians(20.0))
    report(abs(v["angle"] - 40.0) <= 0.06 and abs(v["lens"] - lens) <= 0.05, "SCN30_5: openNURBS reads a frustum of %.3f degrees and a 35 mm lens of %.3f mm from the file; closed form 40 degrees and 12 / tan 20 = %.3f mm" % (v["angle"], v["lens"], lens))
    report(near3(v["eye"], a["eye"]) and near3(v["target"], a["tgt"]), "SCN30_6: the file's camera location and target are the ones of the saved view")
    d = tuple(t - e for t, e in zip(v["target"], v["eye"]))
    nd = math.sqrt(sum(x * x for x in d))
    dd = tuple(x / nd for x in d)
    report(all(abs(p - q) < 1e-4 for p, q in zip(dd, v["dir"])), "SCN30_7: its direction vector is the unit vector from location to target (%s)" % (v["dir"],))
    report(abs(v["angle"] - 50.0) > 1.0, "SCN30_7d: decoy, the field of view is not the default one")


# ---------------------------------------------------------------------------------------------------------------- SKETCHUP-SCENE-0028 / LAYOUT-0041
def svg_paths(path):
    out = []
    root = ET.parse(path).getroot()
    for p in root.iter("{http://www.w3.org/2000/svg}path"):
        toks = re.findall(r"[MLZ]|-?[0-9.]+(?:e[-+]?[0-9]+)?", p.get("d"))
        pts, i = [], 0
        while i < len(toks):
            if toks[i] in "ML":
                pts.append((float(toks[i + 1]), float(toks[i + 2])))
                i += 3
            else:
                i += 1
        out.append(pts)
    return root, out


def pdf_paths(path):
    data = open(path, "rb").read()
    m = re.search(rb"stream\r?\n(.*?)endstream", data, re.S)
    text = m.group(1).decode("latin-1")
    paths, cur = [], []
    for line in text.split("\n"):
        line = line.strip()
        mm = re.fullmatch(r"(-?[0-9.]+) (-?[0-9.]+) ([ml])", line)
        if mm:
            if mm.group(3) == "m" and cur:
                paths.append(cur)
                cur = []
            cur.append((float(mm.group(1)), float(mm.group(2))))
        elif line == "S" and cur:
            paths.append(cur)
            cur = []
    if cur:
        paths.append(cur)
    return paths


def length(pts):
    return sum(math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1))


def prt50(d):
    pt = 72.0 / 25.4
    try:
        root, sv = svg_paths(d + "/p50.svg")
        pv = pdf_paths(d + "/p50.pdf")
        root1, sv1 = svg_paths(d + "/p100.svg")
    except (OSError, ET.ParseError, AttributeError) as e:
        report(False, "PRT50_0: the printed files could not be read (%s)" % e)
        return
    ls = sorted(length(p) for p in sv)
    report(len(sv) == 2 and abs(ls[1] - 20.0) < 2e-3 and abs(ls[0] - 10.0) < 2e-3,
           "PRT50_1: at 1:50 the 1 m edge (1000 mm) is 1000 / 50 = 20 mm long in the SVG and the 0.5 m edge 10 mm (found %s mm)" % ", ".join("%.3f" % x for x in ls))
    lp = sorted(length(p) / pt for p in pv)
    report(len(pv) == 2 and abs(lp[1] - 20.0) < 0.02 and abs(lp[0] - 10.0) < 0.02,
           "PRT50_2: and 20 mm and 10 mm in the PDF (1 mm = 72 / 25.4 pt): found %s mm" % ", ".join("%.4f" % x for x in lp))
    l1 = sorted(length(p) for p in sv1)
    report(len(sv1) == 2 and abs(l1[1] - 10.0) < 2e-3, "PRT50_3: at 1:100 the 1 m edge is 10 mm (found %s mm)" % ", ".join("%.3f" % x for x in l1))
    report(abs(ls[1] - l1[1]) > 5.0, "PRT50_3d: decoy, the 1:50 and 1:100 prints differ (20 mm against 10 mm), so the scale is read from the file")
    w = root.get("width")
    report(w == "297mm", "PRT50_4: the page stays A4 landscape (297 mm wide) (found %s)" % w)


def selftest():
    import contextlib
    import io
    import os
    import tempfile
    global failed
    sbad = [0]

    def sreport(cond, what):
        print(('ok   ' if cond else 'FAIL ') + what)
        if not cond:
            sbad[0] += 1

    def fails(fn, *args):
        """The number of FAIL lines the check `fn` prints on these inputs (its own output is swallowed)."""
        global failed
        saved, failed = failed, 0
        with contextlib.redirect_stdout(io.StringIO()):
            fn(*args)
        n, failed = failed, saved
        return n

    pt = 72.0 / 25.4
    with tempfile.TemporaryDirectory() as d:
        def svg(name, scale, extra=0.0):
            body = '<?xml version="1.0"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="297mm" height="210mm" viewBox="0 0 297 210"><g>'
            body += '<path d="M10 100 L%.3f 100"/><path d="M10 100 L10 %.3f"/></g></svg>' % (10 + 1000 * scale + extra, 100 - 500 * scale)
            open(os.path.join(d, name), 'w').write(body)
        svg('p50.svg', 0.02)
        svg('p100.svg', 0.01)
        open(os.path.join(d, 'p50.pdf'), 'w').write("%%PDF-1.4\nstream\n%.4f %.4f m\n%.4f %.4f l\nS\n%.4f %.4f m\n%.4f %.4f l\nS\nendstream\n" % (10 * pt, 100 * pt, 30 * pt, 100 * pt, 10 * pt, 100 * pt, 10 * pt, 110 * pt))
        sreport(fails(prt50, d) == 0, "SELF_prt50: the hand-authored 1:50 and 1:100 files are accepted")
        svg('p50.svg', 0.02, extra=1.0)
        sreport(fails(prt50, d) > 0, "SELF_prt50: the checker rejects an SVG whose 1 m edge is 21 mm")
    cam = lambda i: "history: Camera (Perspective): location %g,%g,%g, target 0,0,0, distance 50, lens 33 mm (40 deg)\n" % (30 - 5 * i, 10 * i, 5)
    log = ""
    for i in range(1, 6):
        log += "Unknown command: SUDOCBEGIN_c%d\n%s" % (i, cam(i))
    log += "Unknown command: SUDOCBEGIN_list1\nhistory: 5 named view(s)\n" + "".join("history:   S%d: location (1, 2, 3), target (0, 0, 0)\n" % i for i in range(1, 6))
    log += "Unknown command: SUDOCBEGIN_list2\nhistory: 4 named view(s)\n" + "".join("history:   S%d: location (1, 2, 3), target (0, 0, 0)\n" % i for i in (1, 2, 4, 5))
    dump = "views: 4\n"
    for n, i in zip(("S1", "S2", "S4", "S5"), (1, 2, 4, 5)):
        dump += "view 0 name=%s persp=1 eye=(%g,%g,%g) dir=(0,0,-1) up=(0,1,0) target=(0,0,0) frustum_h=1 frustum_w=1 near=1 angle=40 lens=33\n" % (n, 30 - 5 * i, 10 * i, 5)
    sreport(fails(scn1, log, dump) == 0, "SELF_scn1: the hand-authored scene list and file are accepted")
    sreport(fails(scn1, log, dump.replace("name=S4", "name=S3")) > 0, "SELF_scn1: the checker rejects a file that kept S3 and lost S4")
    bad_list = log.replace("history:   S5: location (1, 2, 3), target (0, 0, 0)\n", "", 1)
    sreport(fails(scn1, bad_list, dump) > 0, "SELF_scn1: the checker rejects a first list that lost S5")
    a = "history: Camera (Perspective): location 30,-30,25, target 0,0,0, distance 46.9, lens 32.97 mm (40 deg)\n"
    b = "history: Camera (Perspective): location 0,0,100, target 0,0,0, distance 100, parallel projection, height 80\n"
    log30 = "Unknown command: SUDOCBEGIN_a\n" + a + "Unknown command: SUDOCBEGIN_b\n" + b + "Unknown command: SUDOCBEGIN_c\n" + a
    n = math.sqrt(30 * 30 + 30 * 30 + 25 * 25)
    dump30 = "view 0 name=Fov40 persp=1 eye=(30,-30,25) dir=(%.6f,%.6f,%.6f) up=(0,0,1) target=(0,0,0) frustum_h=1 frustum_w=1 near=1 angle=40.0 lens=32.9697\n" % (-30 / n, 30 / n, -25 / n)
    sreport(fails(scn30, log30, dump30) == 0, "SELF_scn30: the hand-authored restore and file are accepted")
    sreport(fails(scn30, log30, dump30.replace("angle=40.0", "angle=42.0")) > 0, "SELF_scn30: the checker rejects a file whose frustum spans 42 degrees")
    moved = log30.replace("Unknown command: SUDOCBEGIN_c\n" + a, "Unknown command: SUDOCBEGIN_c\n" + a.replace("30,-30,25", "30,-30,26"))
    sreport(fails(scn30, moved, dump30) > 0, "SELF_scn30: the checker rejects a restore that moved the camera by 1")
    print("selftest: %d failure(s)" % sbad[0])
    failed = sbad[0]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    mode = sys.argv[1]
    if mode == "selftest":
        selftest()
        return 1 if failed else 0
    if mode == "prt50":
        prt50(sys.argv[2])
    elif mode in ("scn1", "scn30"):
        log = open(sys.argv[2], encoding="latin-1").read()
        try:
            dump = open(sys.argv[3], encoding="latin-1").read()
        except OSError:
            dump = ""
        (scn1 if mode == "scn1" else scn30)(log, dump)
    else:
        print("FAIL sudoc_view: unknown mode %s" % mode)
        return 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
