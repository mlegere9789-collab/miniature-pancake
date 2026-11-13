#!/usr/bin/env python3
"""ASM_ helper: a STEP assembly with an independent oracle.

    python3 asm_check.py gen <dir>            reads <dir>/box.stp (a 10 x 10 x 10 box at the origin exported by the application),
                                              appends a SHAPE_REPRESENTATION of its solid, a REPRESENTATION_MAP and two MAPPED_ITEMs
                                              (written here, not by the application) and writes <dir>/asm.stp
    python3 asm_check.py check <dir> <app.out>  checks the application's "Bounding box" lines for the two imported instances

Instance A: placed at (100,0,0), identity orientation            -> box (100,0,0)-(110,10,10)
Instance B: placed at (0,50,0), local x -> world +y, z unchanged  -> local (x,y) in [0,10]^2 maps to world (-y, x)+(0,50):
                                                                     x in [-10,0], y in [50,60]       -> box (-10,50,0)-(0,60,10)
"""
import re
import sys

mode, D = sys.argv[1], sys.argv[2]

if mode == "gen":
    t = open(D + "/box.stp").read()
    ids = [int(x) for x in re.findall(r"^#(\d+)=", t, re.M)]
    solid = [int(m.group(1)) for m in re.finditer(r"^#(\d+)=MANIFOLD_SOLID_BREP\(", t, re.M)][0]
    ctx = [int(m.group(1)) for m in re.finditer(r"^#(\d+)=\(GEOMETRIC_REPRESENTATION_CONTEXT", t, re.M)][0]
    n = max(ids)
    out = []

    def add(s):
        global n
        n += 1
        out.append("#%d=%s;" % (n, s))
        return n

    def axis(origin, z, x):
        p = add("CARTESIAN_POINT('',(%s))" % ",".join("%g." % v for v in origin))
        dz = add("DIRECTION('',(%s))" % ",".join("%g." % v for v in z))
        dx = add("DIRECTION('',(%s))" % ",".join("%g." % v for v in x))
        return add("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)" % (p, dz, dx))

    rep = add("SHAPE_REPRESENTATION('part',(#%d),#%d)" % (solid, ctx))
    origin = axis((0, 0, 0), (0, 0, 1), (1, 0, 0))
    rmap = add("REPRESENTATION_MAP(#%d,#%d)" % (origin, rep))
    ta = axis((100, 0, 0), (0, 0, 1), (1, 0, 0))
    tb = axis((0, 50, 0), (0, 0, 1), (0, 1, 0))
    add("MAPPED_ITEM('A',#%d,#%d)" % (rmap, ta))
    add("MAPPED_ITEM('B',#%d,#%d)" % (rmap, tb))
    k = t.rindex("ENDSEC;")
    open(D + "/asm.stp", "w").write(t[:k] + "\n".join(out) + "\n" + t[k:])
    print("wrote asm.stp")
    sys.exit(0)

failed = 0


def report(ok, name, detail=""):
    print(("ok   " if ok else "FAIL ") + "ASM_" + name + (": " + detail if detail else ""))


text = open(sys.argv[3], errors="replace").read()
boxes = []
for m in re.finditer(r"Bounding box min ([-0-9.e+,]+) max ([-0-9.e+,]+)", text):
    boxes.append(tuple(float(x) for x in m.group(1).split(",")) + tuple(float(x) for x in m.group(2).split(",")))


def near(a, b):
    return all(abs(x - y) < 1e-4 for x, y in zip(a, b))


report(len(boxes) == 2, "two_instances_are_imported", "%d Bounding box lines" % len(boxes))
if len(boxes) == 2:
    report(near(boxes[0], (100, 0, 0, 110, 10, 10)), "instance_A_translated_to_100_0_0", str(boxes[0]))
    report(near(boxes[1], (-10, 50, 0, 0, 60, 10)), "instance_B_rotated_90_degrees_about_z_and_placed_at_0_50_0", str(boxes[1]))
