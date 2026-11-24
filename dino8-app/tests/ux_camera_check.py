#!/usr/bin/env python3
"""Checks of the UXCAM section: reads the app output on stdin, evaluates closed-form camera relations and prints one `ok   ...` or `FAIL ...` line per check.

Every "Camera" line carries numbers printed with four significant digits, so each comparison allows 0.02 absolute plus 0.2 percent relative, which is the
rounding of the print and nothing more (2e-3 of a value of 100 is 0.2; of 59.11 it is 0.12 where the rounding is 0.005)."""
import math
import re
import sys

OUT = sys.stdin.read()
NUM = r"[-+]?\d+(?:\.\d+)?(?:e[-+]?\d+)?"
CAM = re.compile(r"Camera \((?P<name>[^)]*)\): location (?P<lx>%s),(?P<ly>%s),(?P<lz>%s), target (?P<tx>%s),(?P<ty>%s),(?P<tz>%s), distance (?P<d>%s), "
                 r"(?:lens (?P<lens>%s) mm \((?P<fov>%s) deg\)|(?P<par>parallel projection), height (?P<h>%s))" % ((NUM,) * 10))
failed = 0


def block(name):
    m = re.search(r"Unknown command: UXBEGIN_%s\n(.*?)(?=Unknown command: UXBEGIN_|\Z)" % name, OUT, re.S)
    return m.group(1) if m else ""


def cams(name):
    out = []
    for m in CAM.finditer(block(name)):
        g = {k: v for k, v in m.groupdict().items() if v is not None}
        c = {k: (float(v) if k not in ("name", "par") else v) for k, v in g.items()}
        c["eye"] = (c["lx"], c["ly"], c["lz"])
        c["tgt"] = (c["tx"], c["ty"], c["tz"])
        c["off"] = tuple(a - b for a, b in zip(c["eye"], c["tgt"]))
        out.append(c)
    return out


def near(a, b, scale=None):
    scale = abs(b) if scale is None else scale
    return abs(a - b) <= 0.02 + 0.002 * scale


def report(label, ok, detail=""):
    global failed
    print(("ok   " if ok else "FAIL ") + label + ("" if ok else "  [" + detail + "]"))
    if not ok:
        failed += 1


def fov_of(lens):  # horizontal field of view of a lens on the 36 mm film width, degrees
    return 2 * math.degrees(math.atan(18.0 / lens))


# ZoomLens (VIEW-0057): the lens and the field of view printed together obey fov = 2 atan(18 / lens)
c = cams("lens")
ok = len(c) == 3 and all("lens" in x for x in c)
report("UXCAM_ZoomLens: three Camera reports of the perspective viewport", ok, "got %d" % len(c))
if ok:
    for want, x in zip((None, 70.0, 24.0), c):
        if want is not None:
            report("UXCAM_ZoomLens: ZoomLens %g reports lens %g mm" % (want, want), near(x["lens"], want), "lens %g" % x["lens"])
        report("UXCAM_ZoomLens: lens %g mm gives field of view %.2f degrees (2 atan(18/lens))" % (x["lens"], fov_of(x["lens"])),
               abs(x["fov"] - fov_of(x["lens"])) <= 0.06, "printed %g" % x["fov"])

# PerspectiveAngle (VIEW-0047): a 40 degree angle needs lens 18 / tan(20 degrees) = 49.4546 mm
c = cams("angle")
ok = len(c) == 1 and "lens" in c[0]
report("UXCAM_PerspectiveAngle: one Camera report", ok, "got %d" % len(c))
if ok:
    want = 18.0 / math.tan(math.radians(20.0))
    report("UXCAM_PerspectiveAngle: 40 degrees gives lens %.4f mm" % want, abs(c[0]["lens"] - want) <= 0.02, "lens %g" % c[0]["lens"])
    report("UXCAM_PerspectiveAngle: the reported angle is 40", abs(c[0]["fov"] - 40.0) <= 0.06, "angle %g" % c[0]["fov"])

# DollyZoom (VIEW-0011): the subject keeps its size, so the distance is proportional to the lens (d1 / d0 = L1 / L0) and the camera stays on its ray
c = cams("dolly")
ok = len(c) == 2 and all("lens" in x for x in c)
report("UXCAM_DollyZoom: two Camera reports (lens 50, then after DollyZoom 100)", ok, "got %d" % len(c))
if ok:
    a, b = c
    report("UXCAM_DollyZoom: the lens before is 50 and after is 100", near(a["lens"], 50) and near(b["lens"], 100), "%g, %g" % (a["lens"], b["lens"]))
    report("UXCAM_DollyZoom: distance ratio %.4f equals lens ratio 2" % (b["d"] / a["d"]), abs(b["d"] / a["d"] - 2.0) <= 0.004, "%g / %g" % (b["d"], a["d"]))
    ua = [v / a["d"] for v in a["off"]]
    ub = [v / b["d"] for v in b["off"]]
    report("UXCAM_DollyZoom: the camera stays on the same ray through the target", all(abs(p - q) <= 0.003 for p, q in zip(ua, ub)) and b["tgt"] == a["tgt"], "%s vs %s" % (ua, ub))

def yaw_between(u, v):  # the angle in degrees, in 0..180, between the horizontal parts of two vectors
    a = math.degrees(math.atan2(u[1], u[0]))
    b = math.degrees(math.atan2(v[1], v[0]))
    return abs((b - a + 180.0) % 360.0 - 180.0)


# RotateView (VIEW-0076): the camera turns around the target, so the target and the distance are kept and the camera moves
c = cams("rotview")
ok = len(c) == 2
report("UXCAM_RotateView: two Camera reports around RotateView 30", ok, "got %d" % len(c))
if ok:
    a, b = c
    moved = math.dist(a["eye"], b["eye"])
    report("UXCAM_RotateView: target unchanged, distance unchanged, camera moved by %.2f" % moved,
           all(near(p, q) for p, q in zip(a["tgt"], b["tgt"])) and near(a["d"], b["d"]) and moved > 1.0, "%s / %s" % (a, b))
    # a turn of 30 degrees about the vertical axis through the target: the height and the horizontal radius are kept and the azimuth changes by 30
    ra, rb = math.hypot(a["off"][0], a["off"][1]), math.hypot(b["off"][0], b["off"][1])
    turn = yaw_between(a["off"], b["off"])
    report("UXCAM_RotateView: 30 degrees turns the azimuth by %.2f degrees, keeping height %.2f and horizontal radius %.2f" % (turn, a["off"][2], ra),
           abs(turn - 30.0) <= 0.4 and near(a["off"][2], b["off"][2]) and near(ra, rb), "turn %g, radii %g %g, heights %g %g" % (turn, ra, rb, a["off"][2], b["off"][2]))

# RotateCamera (VIEW-0075): the camera stays and the target swings around it
c = cams("rotcam")
ok = len(c) == 2
report("UXCAM_RotateCamera: two Camera reports around RotateCamera 30", ok, "got %d" % len(c))
if ok:
    a, b = c
    moved = math.dist(a["tgt"], b["tgt"])
    report("UXCAM_RotateCamera: camera unchanged, distance unchanged, target moved by %.2f" % moved,
           all(near(p, q) for p, q in zip(a["eye"], b["eye"])) and near(a["d"], b["d"]) and moved > 1.0, "%s / %s" % (a, b))
    fa = tuple(-v for v in a["off"])  # look direction: target - eye
    fb = tuple(-v for v in b["off"])
    ra, rb = math.hypot(fa[0], fa[1]), math.hypot(fb[0], fb[1])
    turn = yaw_between(fa, fb)
    report("UXCAM_RotateCamera: 30 degrees turns the look direction's azimuth by %.2f degrees, keeping its height %.2f" % (turn, fa[2]),
           abs(turn - 30.0) <= 0.4 and near(fa[2], fb[2]) and near(ra, rb), "turn %g, radii %g %g, heights %g %g" % (turn, ra, rb, fa[2], fb[2]))

# SetView (VIEW-0096): the standard Z-up views look at the target from +Z (Top), -Y (Front) and +X (Right), in parallel projection
c = cams("views")
ok = len(c) == 3 and all(x.get("par") for x in c)
report("UXCAM_SetView: three parallel Camera reports (Top, Front, Right)", ok, "got %d" % len(c))
if ok:
    for label, x, axis, sign in (("Top", c[0], 2, 1), ("Front", c[1], 1, -1), ("Right", c[2], 0, 1)):
        off = x["off"]
        others = [abs(off[i]) for i in range(3) if i != axis]
        report("UXCAM_SetView: %s looks from %s%s along the target's axis" % (label, "+-"[sign < 0], "XYZ"[axis]),
               sign * off[axis] > 1.0 and max(others) <= 0.02 + 0.002 * x["d"] and near(abs(off[axis]), x["d"]), str(off))

# Isometric (VIEW-0027): equal foreshortening of the three axes means the view direction has |dx| = |dy| = |dz|
c = cams("iso")
ok = len(c) == 1 and c[0].get("par")
report("UXCAM_Isometric: one parallel Camera report", bool(ok), "got %d" % len(c))
if ok:
    o = [abs(v) for v in c[0]["off"]]
    report("UXCAM_Isometric: offset components %s have equal magnitude" % (o,), max(o) - min(o) <= 0.02 + 0.004 * c[0]["d"], str(o))

# Plan (VIEW-0049): straight down onto the plan, so x and y offsets are 0 and the camera is above the target
c = cams("plan")
ok = len(c) == 1 and c[0].get("par")
report("UXCAM_Plan: one parallel Camera report", bool(ok), "got %d" % len(c))
if ok:
    o = c[0]["off"]
    report("UXCAM_Plan: camera straight above the target", abs(o[0]) <= 0.02 and abs(o[1]) <= 0.02 and o[2] > 1.0, str(o))

sys.exit(1 if failed else 0)
