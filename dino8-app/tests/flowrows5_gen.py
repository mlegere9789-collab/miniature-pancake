#!/usr/bin/env python3
"""Third batch of Blender Geometry Nodes Register-row tests (src/flow/FlowNodesBlender3.cpp): one .dflow graph, one player script
and one tests/sections/98_<name>.sh section per row (the section name, upper-cased, is the row's acceptance test id).

Machinery and conventions are those of flowrows3_gen.py (read its header). New here: the oracle meshes are AUTHORED FILES.
Each section writes small Wavefront OBJ / PLY / STL / text files into the test's temp directory with printf-style heredocs
(FILES below), and the graph reads them through the import nodes. So the vertices, the faces and the corner numbers the
topology nodes must report are fixed by the text of the file, and every expected number is derived by hand from that text
(the corner of a face is the running count of the corners before it, a vertex's valence is read off the face lists, ...).
The path in a graph is the literal @TMPW@/<file>; the section replaces @TMPW@ with the temp directory.
Run from dino8-app/tests:  python3 flowrows5_gen.py
"""
import math
import os

from flowrows_gen import SECTION
from flowrows3_gen import T, PI, SQ5, ngon_area, poly_len, dist

HERE = os.path.dirname(os.path.abspath(__file__))

# A unit cube: 8 vertices, 6 outward quads (counter-clockwise seen from outside).
CUBE_OBJ = """v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
v 0 0 1
v 1 0 1
v 1 1 1
v 0 1 1
f 1 4 3 2
f 5 6 7 8
f 1 2 6 5
f 4 8 7 3
f 1 5 8 4
f 2 3 7 6
"""
# Two quads and a triangle sharing edges, plus one separate triangle far away (10 vertices, 4 faces, 2 pieces).
STRIP_OBJ = """v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
v 2 0 0
v 2 1 0
v 0.5 2 0
v 5 5 5
v 6 5 5
v 5 6 5
f 1 2 3 4
f 2 5 6 3
f 4 3 7
f 8 9 10
"""
CUBE_PLY = """ply
format ascii 1.0
element vertex 8
property float x
property float y
property float z
element face 6
property list uchar int vertex_indices
end_header
0 0 0
1 0 0
1 1 0
0 1 0
0 0 1
1 0 1
1 1 1
0 1 1
4 0 3 2 1
4 4 5 6 7
4 0 1 5 4
4 3 7 6 2
4 0 4 7 3
4 1 2 6 5
"""
# a "house": the 2 x 1 rectangle with a triangular roof of height 1: area 2 + 1 = 3, as one pentagon
HOUSE_PLY = """ply
format ascii 1.0
element vertex 5
property float x
property float y
property float z
element face 1
property list uchar int vertex_indices
end_header
0 0 0
2 0 0
2 1 0
1 2 0
0 1 0
5 0 1 2 3 4
"""
# the cube as 12 triangles (each quad a b c d split into a b c and a c d), 36 vertices as stored
def _stl():
    quads = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (3, 7, 6, 2), (0, 4, 7, 3), (1, 2, 6, 5)]
    v = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]
    out = ["solid cube"]
    for a, b, c, d in quads:
        for tri in ((a, b, c), (a, c, d)):
            out.append("facet normal 0 0 0\n outer loop")
            for i in tri:
                out.append("  vertex %d %d %d" % v[i])
            out.append(" endloop\nendfacet")
    out.append("endsolid cube")
    return "\n".join(out) + "\n"


CUBE_STL = _stl()
FILES = {"cube.obj": CUBE_OBJ, "strip.obj": STRIP_OBJ, "cube.ply": CUBE_PLY, "house.ply": HOUSE_PLY, "cube.stl": CUBE_STL,
         "two.txt": "alpha\nbeta\n", "tabs.txt": "x\ty\n1\t2\n"}


def f(name):
    return ("t", "@TMPW@/" + name)


def vol(t, geo, c, msg, scale=1e5):
    n = t.n("Volume"); t.w(geo, 0, n, 0); t.exact(n, 0, c, msg, scale)


def area(t, geo, c, msg, scale=1e5):
    n = t.n("Area"); t.w(geo, 0, n, 0); t.exact(n, 0, c, msg, scale)


def numlist(t, values):
    """A list of number literals through a chain of Merge nodes (a node input takes one wire)."""
    cur = t.n("Merge", float(values[0]), float(values[1]))
    for v in values[2:]:
        m = t.n("Merge", None, float(v)); t.w(cur, 0, m, 0)
        cur = m
    return cur


def move_cube(t, geo):
    x = t.n("Transform Geometry"); t.w(geo, 0, x, 0); t.w(t.vec(3, 0, 0), 0, x, 1)
    return x


def cc_cube_volume():
    """Volume of the Catmull-Clark level-1 solid of the unit cube, from hand-derived coordinates (see the BFSUBS row text): about the
    centre of the cube of side 2, corners at (+-5/9)^3, edge points at +-0.75 on two axes and 0 on the third, face points on the
    axes at +-1; 24 quads (corner, edge point, face point, edge point), one per cube face and quadrant."""
    def vec(a, s, u, su, v, sv, ca, cu, cv):
        p = [0.0, 0.0, 0.0]
        p[a] = s * ca; p[(a + 1) % 3] = su * cu; p[(a + 2) % 3] = sv * cv
        return tuple(p)
    total = 0.0
    for a in range(3):
        for s in (1, -1):
            for su in (1, -1):
                for sv in (1, -1):
                    c = vec(a, s, 0, su, 0, sv, 5 / 9, 5 / 9, 5 / 9)
                    e1 = vec(a, s, 0, su, 0, 0, 0.75, 0.75, 0.0)
                    fp = vec(a, s, 0, 0, 0, 0, 1.0, 0.0, 0.0)
                    e2 = vec(a, s, 0, 0, 0, sv, 0.75, 0.0, 0.75)
                    for tri in ((c, e1, fp), (c, fp, e2)):
                        p, q, r = tri
                        det = (p[0] * (q[1] * r[2] - q[2] * r[1]) - p[1] * (q[0] * r[2] - q[2] * r[0]) + p[2] * (q[0] * r[1] - q[1] * r[0])) / 6.0
                        total += abs(det)   # the origin is inside the star-shaped solid: every tetrahedron from it adds
    return total / 8.0   # side 2 -> side 1


def make_rows():
    rows = []

    def row(name, label, title, closed, files, build):
        t = T()
        build(t)
        t.control()
        rows.append((name, label, title, closed, files, t))

    # ---------------------------------------------------------------- imports
    def b_impo(t):
        c = t.n("Import OBJ", f("cube.obj"))
        t.dsize(c, pt=8, ed=12, fa=6, co=24, msg="the cube file has 8 v, 6 quad f:")
        vol(t, c, 1.0, "volume of the unit cube 1")
        area(t, c, 6.0, "area 6")
        t.bbox(c, (0, 0, 0), (1, 1, 1), "box [0,1]^3 in the file's own coordinates", 1e5)
        s = t.n("Import OBJ", f("strip.obj"))
        t.dsize(s, pt=10, ed=12, fa=4, co=14, msg="the strip file (2 quads, 2 triangles, 12 distinct edges):")
        area(t, s, 3.0, "area 1 + 1 + 0.5 + 0.5")
        t.bbox(s, (0, 0, 0), (6, 6, 5), "box of the 10 vertices: x 0..6, y 0..6, z 0..5", 1e5)
    row("bfimpo", "Import OBJ", "Import OBJ",
        "the unit cube file (8 vertices, 6 outward quads) reads back as V 8, E 12, F 6, corners 24, volume 1, area 6, box [0,1]^3; the strip file (7 + 3 vertices; quads 1-2-3-4 and 2-5-6-3, triangles 4-3-7 and 8-9-10) has 10 vertices, 12 distinct edges, 4 faces, 14 corners, area 1 + 1 + 0.5 + 0.5 = 3, box x 0..6, y 0..6, z 0..5. No axis conversion is applied: the Blender importer's default axis settings are recalled, not checked",
        ["cube.obj", "strip.obj"], b_impo)

    def b_impp(t):
        c = t.n("Import PLY", f("cube.ply"))
        t.dsize(c, pt=8, ed=12, fa=6, co=24, msg="the cube file (ascii, 8 vertices, 6 quads):")
        vol(t, c, 1.0, "volume 1")
        t.bbox(c, (0, 0, 0), (1, 1, 1), "box [0,1]^3", 1e5)
        h = t.n("Import PLY", f("house.ply"))
        t.dsize(h, pt=5, ed=7, fa=3, co=9, msg="a pentagon is split into n - 2 = 3 triangles (7 edges: 5 + 2 diagonals):")
        area(t, h, 3.0, "house area 2 * 1 + (2 * 1) / 2 = 3")
    row("bfimpp", "Import PLY", "Import PLY",
        "the ascii cube file reads back as V 8, E 12, F 6, volume 1, box [0,1]^3; the pentagon (0,0) (2,0) (2,1) (1,2) (0,1) has area 2 * 1 + 2 * 1 / 2 = 3 and is split into 5 - 2 = 3 triangles with 5 + 2 = 7 edges",
        ["cube.ply", "house.ply"], b_impp)

    def b_imps(t):
        c = t.n("Import STL", f("cube.stl"))
        t.dsize(c, pt=8, ed=18, fa=12, co=36, msg="12 facets welded into 8 vertices, 12 + 6 diagonals = 18 edges:")
        vol(t, c, 1.0, "closed after welding: volume 1")
        area(t, c, 6.0, "area 6")
        t.bbox(c, (0, 0, 0), (1, 1, 1), "box [0,1]^3", 1e5)
    row("bfimps", "Import STL", "Import STL",
        "the ascii file lists the 12 triangles of the unit cube (36 vertex lines); welded it is the closed solid with 8 vertices, 18 edges (12 cube edges + 6 face diagonals), 12 faces, volume 1, area 6, box [0,1]^3",
        ["cube.stl"], b_imps)

    def b_impt(t):
        a = t.n("Import Text", f("two.txt"))
        t.textis(a, "alpha\nbeta\n", "a two-line file comes back byte for byte (with its final newline)")
        w = t.n("Text Distance", None, ("t", "alpha beta")); t.w(a, 0, w, 0)
        t.count(w, 0, 2, "control: against 'alpha beta' (a space for the first newline, the last one missing) the distance is 2")
        b = t.n("Import Text", f("tabs.txt"))
        t.textis(b, "x\ty\n1\t2\n", "tabs and the trailing newline are kept")
    row("bfimpt", "Import Text", "Import Text",
        "the file 'alpha<newline>beta<newline>' (11 bytes) reads back as exactly that string (Text Distance 0; against 'alpha beta' the distance is 2); a tab-separated two-line file with a trailing newline reads back unchanged",
        ["two.txt", "tabs.txt"], b_impt)

    # ---------------------------------------------------------------- topology (authored files)
    def mesh(t, name):
        return t.n("Import OBJ", f(name))

    def b_cfac(t):
        s = mesh(t, "strip.obj"); c = mesh(t, "cube.obj")
        for mesh_, face, want, msg in ((s, 0, "0 1 2 3", "strip face 0 (quad): corners 0..3"), (s, 1, "4 5 6 7", "face 1 (quad): corners 4..7"),
                                       (s, 2, "8 9 10", "face 2 (triangle) starts after 4 + 4 corners"), (s, 3, "11 12 13", "face 3 (triangle)"),
                                       (c, 4, "16 17 18 19", "cube face 4 starts at 4 * 4 = 16"), (c, 0, "0 1 2 3", "cube face 0")):
            n = t.n("Corners of Face", None, ("i", face)); t.w(mesh_, 0, n, 0)
            t.tag += 1
            t.g.probe(n, 0, t.tag); t.expect.append((t.tag, want, msg))
            t.count(n, 1, len(want.split()), msg + " (Total)")
    row("bfcfac", "Corners of Face", "Corners of Face",
        "corners are numbered face by face: strip file faces have 4, 4, 3, 3 corners so face 0 owns corners 0-3, face 1 4-7, face 2 8-10, face 3 11-13 (14 in all); cube file face f owns 4f .. 4f + 3 (face 4: 16-19)",
        ["cube.obj", "strip.obj"], b_cfac)

    def b_fcor(t):
        s = mesh(t, "strip.obj"); c = mesh(t, "cube.obj")
        for mesh_, k, face, pos in ((s, 0, 0, 0), (s, 3, 0, 3), (s, 5, 1, 1), (s, 7, 1, 3), (s, 9, 2, 1), (s, 10, 2, 2), (s, 11, 3, 0), (s, 13, 3, 2), (c, 23, 5, 3), (c, 8, 2, 0)):
            n = t.n("Face of Corner", None, ("i", k)); t.w(mesh_, 0, n, 0)
            t.count(n, 0, face, "corner %d lies in face %d" % (k, face))
            t.count(n, 1, pos, "corner %d is corner %d of its face" % (k, pos))
    row("bffcor", "Face of Corner", "Face of Corner",
        "the inverse of the corner numbering of the strip file (faces of 4, 4, 3, 3 corners) and the cube file (4 each): corner 5 = face 1 position 1, corner 9 = face 2 position 1, corner 13 = face 3 position 2; cube corner 23 = face 5 position 3, corner 8 = face 2 position 0",
        ["cube.obj", "strip.obj"], b_fcor)

    def b_vcor(t):
        s = mesh(t, "strip.obj"); c = mesh(t, "cube.obj")
        # strip faces (0-based vertices): f0 (0 1 2 3), f1 (1 4 5 2), f2 (3 2 6), f3 (7 8 9)
        for mesh_, k, v in ((s, 0, 0), (s, 4, 1), (s, 6, 5), (s, 7, 2), (s, 8, 3), (s, 10, 6), (s, 11, 7), (s, 13, 9),
                            # cube faces (0-based): f0 (0 3 2 1), f1 (4 5 6 7), f2 (0 1 5 4), f3 (3 7 6 2), f4 (0 4 7 3), f5 (1 2 6 5)
                            (c, 0, 0), (c, 5, 5), (c, 10, 5), (c, 14, 6), (c, 19, 3), (c, 23, 5)):
            n = t.n("Vertex of Corner", None, ("i", k)); t.w(mesh_, 0, n, 0)
            t.count(n, 0, v, "corner %d sits on vertex %d" % (k, v))
    row("bfvcor", "Vertex of Corner", "Vertex of Corner",
        "read off the face lists of the files: strip f0 (0 1 2 3), f1 (1 4 5 2), f2 (3 2 6), f3 (7 8 9) give corner 4 = vertex 1, 6 = 5, 7 = 2, 8 = 3, 10 = 6, 11 = 7, 13 = 9; cube f0 (0 3 2 1), f1 (4 5 6 7), f2 (0 1 5 4), f3 (3 7 6 2), f4 (0 4 7 3), f5 (1 2 6 5) give corner 5 = 5, 10 = 5, 14 = 6, 19 = 3, 23 = 5",
        ["cube.obj", "strip.obj"], b_vcor)

    def b_cver(t):
        s = mesh(t, "strip.obj"); c = mesh(t, "cube.obj")
        for mesh_, v, want, msg in ((s, 2, "2 7 9", "strip vertex 2 is in f0 (position 2), f1 (position 3), f2 (position 1)"), (s, 0, "0", "vertex 0 is only in f0"),
                                    (s, 6, "10", "vertex 6 is only in f2, position 2"), (s, 3, "3 8", "vertex 3 is in f0 (position 3) and f2 (position 0)"),
                                    (c, 0, "0 8 16", "cube vertex 0 is in f0, f2, f4 at position 0"), (c, 6, "6 14 22", "cube vertex 6 is in f1, f3, f5 at position 2")):
            n = t.n("Corners of Vertex", None, ("i", v)); t.w(mesh_, 0, n, 0)
            t.tag += 1
            t.g.probe(n, 0, t.tag); t.expect.append((t.tag, want, msg))
            t.count(n, 1, len(want.split()), msg + " (Total)")
    row("bfcver", "Corners of Vertex", "Corners of Vertex",
        "from the corner numbering and the face lists: strip vertex 2 owns corners 2 (f0), 7 (f1), 9 (f2); vertex 3 owns 3 and 8; vertex 0 owns 0; vertex 6 owns 10; cube vertex 0 owns 0, 8, 16 and vertex 6 owns 6, 14, 22 (always one corner per face around the vertex)",
        ["cube.obj", "strip.obj"], b_cver)

    def b_eov(t):
        s = mesh(t, "strip.obj"); c = mesh(t, "cube.obj")
        for mesh_, v, tot in ((s, 2, 4), (s, 0, 2), (s, 1, 3), (s, 6, 2), (s, 7, 2), (c, 0, 3), (c, 6, 3)):
            n = t.n("Edges of Vertex", None, ("i", v)); t.w(mesh_, 0, n, 0)
            t.count(n, 1, tot, "vertex %d has %d edges" % (v, tot))
        n = t.n("Edges of Vertex", None, ("i", 2)); t.w(s, 0, n, 0)
        t.tag += 1
        t.g.probe(n, 0, t.tag); t.expect.append((t.tag, "2 4 5 6", "vertex 2 of the strip: its edges (1,2) (2,3) (2,5) (2,6) are entries 2, 4, 5, 6 of the sorted pair list (this program's own numbering)"))
        ml = t.n("Mesh Line", ("i", 5))
        n2 = t.n("Edges of Vertex", None, ("i", 0)); t.w(ml, 0, n2, 0)
        t.count(n2, 1, 1, "the end of a 5-vertex line has 1 edge")
        n3 = t.n("Edges of Vertex", None, ("i", 2)); t.w(ml, 0, n3, 0)
        t.count(n3, 1, 2, "a middle vertex has 2")
    row("bfeovx", "Edges of Vertex", "Edges of Vertex",
        "valence read off the strip file: vertex 2 touches 1, 3, 5, 6 (4 edges), vertex 0 touches 1, 3 (2), vertex 1 touches 0, 2, 4 (3), vertex 6 touches 2, 3 (2), vertex 7 touches 8, 9 (2); every cube vertex has 3; a 5-vertex line has 1 edge at its ends and 2 inside. Edge numbers are Dino's own (position in the sorted pair list), not Blender's",
        ["cube.obj", "strip.obj"], b_eov)

    def b_vnb(t):
        s = mesh(t, "strip.obj"); c = mesh(t, "cube.obj")
        for mesh_, v, e, fcount in ((s, 2, 4, 3), (s, 1, 3, 2), (s, 0, 2, 1), (s, 3, 3, 2), (s, 6, 2, 1), (s, 7, 2, 1), (c, 0, 3, 3), (c, 7, 3, 3)):
            n = t.n("Vertex Neighbors", None, ("i", v)); t.w(mesh_, 0, n, 0)
            t.count(n, 0, e, "vertex %d: %d edges" % (v, e)); t.count(n, 1, fcount, "vertex %d: %d faces" % (v, fcount))
        cy = t.n("Cylinder"); n = t.n("Vertex Neighbors", None, ("i", 0)); t.w(cy, 0, n, 0)
        t.count(n, 0, 3, "a cylinder rim vertex has 3 edges"); t.count(n, 1, 3, "and 3 faces: two quads and the cap n-gon, counted once")
        ic = t.n("Ico Sphere", 1.0, ("i", 1)); n2 = t.n("Vertex Neighbors", None, ("i", 0)); t.w(ic, 0, n2, 0)
        t.count(n2, 0, 5, "an icosahedron vertex has 5 edges"); t.count(n2, 1, 5, "and 5 faces")
        g = t.n("Grid", 1.0, 1.0, ("i", 3), ("i", 3))
        nc = t.n("Vertex Neighbors", None, ("i", 4)); t.w(g, 0, nc, 0)
        t.count(nc, 0, 4, "the centre vertex of a 3 x 3 grid has 4 edges"); t.count(nc, 1, 4, "and 4 faces")
        nk = t.n("Vertex Neighbors", None, ("i", 0)); t.w(g, 0, nk, 0)
        t.count(nk, 0, 2, "a grid corner has 2 edges"); t.count(nk, 1, 1, "and 1 face")
    row("bfvnbr", "Vertex Neighbors", "Vertex Neighbors",
        "from the strip file: vertex 2 has edges to 1, 3, 5, 6 and lies in 3 faces; vertex 1 3 edges 2 faces; vertex 0 2 edges 1 face; vertex 3 3 edges 2 faces; cube 3 and 3; a default cylinder rim vertex 3 edges, 3 faces (the cap n-gon counts once); icosahedron vertex 5 and 5; 3 x 3 grid centre 4 and 4, corner 2 and 1",
        ["cube.obj", "strip.obj"], b_vnb)

    def b_far(t):
        s = mesh(t, "strip.obj"); c = mesh(t, "cube.obj")
        for mesh_, face, want, msg in ((s, 0, 1.0, "strip f0 is a unit square"), (s, 1, 1.0, "f1 is a unit square"), (s, 2, 0.5, "f2: base (1,1)-(0,1) of length 1, apex (0.5,2) at height 1"),
                                       (s, 3, 0.5, "the far triangle has legs 1 and 1"), (c, 0, 1.0, "a cube face"), (c, 5, 1.0, "another cube face")):
            n = t.n("Face Area", None, ("i", face)); t.w(mesh_, 0, n, 0)
            t.exact(n, 0, want, msg, 1e5)
        cy = t.n("Cylinder")
        q = t.n("Face Area", None, ("i", 0)); t.w(cy, 0, q, 0)
        t.exact(q, 0, 2 * math.sin(PI / 32) * 2.0, "default cylinder side quad: chord 2 sin(pi/32) times depth 2", 1e5)
        cap = t.n("Face Area", None, ("i", 32)); t.w(cy, 0, cap, 0)
        t.exact(cap, 0, ngon_area(32, 1.0), "the cap is one 32-gon: (32/2) sin(2 pi/32)", 1e5)
        gd = t.n("Grid", 4.0, 6.0, ("i", 3), ("i", 3))
        ga = t.n("Face Area", None, ("i", 1)); t.w(gd, 0, ga, 0)
        t.exact(ga, 0, 6.0, "a 3 x 3 grid of 4 x 6 has quads of 2 x 3 = 6", 1e5)
    row("bffar", "Face Area", "Face Area",
        "strip file: f0 and f1 are unit squares (1), f2 is the triangle (0,1) (1,1) (0.5,2) of base 1 and height 1 (0.5), f3 the triangle with legs 1 (0.5); cube faces 1; default cylinder: side quad 2 sin(pi/32) * 2 = 0.3925..., each cap one 32-gon of area 16 sin(2 pi/32); a 3 x 3 grid of 4 x 6 has quads of 2 x 3 = 6. Single precision vertices: within 5e-6",
        ["cube.obj", "strip.obj"], b_far)

    def b_isl(t):
        s = mesh(t, "strip.obj"); c = mesh(t, "cube.obj")
        n = t.n("Mesh Island", None, ("i", 0)); t.w(s, 0, n, 0)
        t.count(n, 1, 2, "the strip file has 2 pieces")
        for v, idx in ((0, 0), (3, 0), (6, 0), (7, 1), (9, 1)):
            m = t.n("Mesh Island", None, ("i", v)); t.w(s, 0, m, 0)
            t.count(m, 0, idx, "strip vertex %d is in piece %d (pieces numbered by their lowest vertex)" % (v, idx))
        nc = t.n("Mesh Island", None, ("i", 5)); t.w(c, 0, nc, 0)
        t.count(nc, 1, 1, "a cube is one piece")
        j = t.n("Merge"); t.w(c, 0, j, 0); t.w(move_cube(t, c), 0, j, 1)
        jj = t.n("Join Geometry"); t.w(j, 0, jj, 0)
        n2 = t.n("Mesh Island", None, ("i", 8)); t.w(jj, 0, n2, 0)
        t.count(n2, 1, 2, "two joined cubes are 2 pieces")
        t.count(n2, 0, 1, "vertex 8 is the first vertex of the second cube")
        p = t.n("Points", ("i", 4)); n3 = t.n("Mesh Island", None, ("i", 3)); t.w(p, 0, n3, 0)
        t.count(n3, 1, 4, "4 loose points are 4 pieces")
        t.count(n3, 0, 3, "the point 3 is piece 3")
        g = t.n("Grid", 1.0, 1.0, ("i", 3), ("i", 3)); n4 = t.n("Mesh Island", None, ("i", 8)); t.w(g, 0, n4, 0)
        t.count(n4, 1, 1, "a grid is one piece")
    row("bfisl", "Mesh Island", "Mesh Island",
        "strip file: vertices 0-6 are joined by faces (piece 0), vertices 7-9 form the separate triangle (piece 1): 2 pieces; a cube is 1; two cubes side by side are 2 with vertex 8 first in the second; 4 coincident loose points are 4 pieces; a grid is 1. Pieces are numbered by their lowest vertex (a convention that is not checked against Blender's numbering)",
        ["cube.obj", "strip.obj"], b_isl)

    def b_sep(t):
        s = mesh(t, "strip.obj")
        # strip edges: 0-1 0-3 1-2 1-4 2-3 2-5 2-6 3-6 4-5 7-8 7-9 8-9
        for v, cost, nxt, msg in ((4, 1, 5, "4 is next to the end 5"), (6, 2, 2, "6 -> 2 -> 5 (6 -> 3 -> 2 -> 5 is longer)"), (3, 2, 2, "3 -> 2 -> 5"), (0, 3, None, "0 needs 3 edges to reach 5"), (1, 2, None, "1 -> 2 -> 5 or 1 -> 4 -> 5")):
            n = t.n("Shortest Edge Paths", None, ("i", 5), 1.0, ("i", v)); t.w(s, 0, n, 0)
            t.exact(n, 1, cost, "strip, end vertex 5: vertex %d costs %g (%s)" % (v, cost, msg), 1e12)
            if nxt is not None:
                t.count(n, 0, nxt, "its next vertex is %d" % nxt)
        n = t.n("Shortest Edge Paths", None, ("i", 5), 2.5, ("i", 0)); t.w(s, 0, n, 0)
        t.exact(n, 1, 7.5, "with every edge costing 2.5 the 3 edges from vertex 0 cost 7.5", 1e12)
        n = t.n("Shortest Edge Paths", None, ("i", 5), 1.0, ("i", 5)); t.w(s, 0, n, 0)
        t.exact(n, 1, 0.0, "the end vertex itself costs 0", 1e12)
        t.count(n, 0, 5, "and is its own next vertex")
        ml = t.n("Mesh Line", ("i", 5))
        n = t.n("Shortest Edge Paths", None, ("i", 0), 0.5, ("i", 4)); t.w(ml, 0, n, 0)
        t.exact(n, 1, 2.0, "line of 5, end vertex 0, 0.5 per edge: vertex 4 is 4 edges away = 2", 1e12)
        t.count(n, 0, 3, "its next vertex is 3")
        ic = t.n("Ico Sphere", 1.0, ("i", 1))
        n = t.n("Shortest Edge Paths", None, ("i", 0), 1.0, ("i", 11)); t.w(ic, 0, n, 0)
        t.exact(n, 1, 3.0, "icosahedron: pole to pole is 3 edges", 1e12)
        n = t.n("Shortest Edge Paths", None, ("i", 0), 1.0, ("i", 8)); t.w(ic, 0, n, 0)
        t.exact(n, 1, 2.0, "pole to a vertex of the lower ring is 2 edges", 1e12)
    row("bfsep", "Shortest Edge Paths", "Shortest Edge Paths",
        "graph distances on the strip file (edges 0-1 0-3 1-2 1-4 2-3 2-5 2-6 3-6 4-5 | 7-8 7-9 8-9) to End Vertex 5: vertex 4 is 1 edge (next 5), 6 and 3 are 2 edges (next 2), 1 is 2, 0 is 3 (7.5 at 2.5 per edge); the end vertex is 0 and its own next vertex; a 5-vertex line at 0.5 per edge: vertex 4 costs 2, next 3; icosahedron: pole to the opposite pole 3, pole to a lower-ring vertex 2. Ties between equal paths go to the lowest vertex number (own rule, untested against Blender)",
        ["strip.obj"], b_sep)

    def b_nrm(t):
        c = mesh(t, "cube.obj"); s = mesh(t, "strip.obj")
        for face, nv in ((0, (0, 0, -1)), (1, (0, 0, 1)), (2, (0, -1, 0)), (3, (0, 1, 0)), (4, (-1, 0, 0)), (5, (1, 0, 0))):
            n = t.n("Normal", None, ("i", 1), ("i", face)); t.w(c, 0, n, 0)
            t.point_exact(n, 0, nv, "cube face %d faces %s" % (face, nv), 1e5)
        r3 = 1 / math.sqrt(3.0)
        for v, nv in ((0, (-r3, -r3, -r3)), (6, (r3, r3, r3)), (1, (r3, -r3, -r3)), (3, (-r3, r3, -r3))):
            n = t.n("Normal", None, ("i", 0), ("i", v)); t.w(c, 0, n, 0)
            t.point_exact(n, 0, nv, "cube corner vertex %d points along its diagonal (the 3 faces meet at equal angles)" % v, 1e5)
        n = t.n("Normal", None, ("i", 1), ("i", 2)); t.w(s, 0, n, 0)
        t.point_exact(n, 0, (0, 0, 1), "strip triangle f2 is counter-clockwise: +Z", 1e5)
        n = t.n("Normal", None, ("i", 0), ("i", 2)); t.w(s, 0, n, 0)
        t.point_exact(n, 0, (0, 0, 1), "strip vertex 2 lies in three flat faces: +Z", 1e5)
        ic = t.n("Ico Sphere", 1.0, ("i", 1))
        n = t.n("Normal", None, ("i", 0), ("i", 0)); t.w(ic, 0, n, 0)
        t.point_exact(n, 0, (0, 0, 1), "icosahedron top vertex (0,0,1) points along +Z by symmetry", 1e5)
        n = t.n("Normal", None, ("i", 0), ("i", 11)); t.w(ic, 0, n, 0)
        t.point_exact(n, 0, (0, 0, -1), "and the bottom one along -Z", 1e5)
    row("bfnorm", "Normal", "Normal",
        "cube file faces: f0 (z = 0) -Z, f1 (z = 1) +Z, f2 (y = 0) -Y, f3 (y = 1) +Y, f4 (x = 0) -X, f5 (x = 1) +X; a cube vertex normal is the diagonal outward (+-1,+-1,+-1)/sqrt 3 (three faces at equal angles, so any angle weights give it); strip vertex 2 and triangle f2 are flat and counter-clockwise: +Z; icosahedron poles (0,0,+-1) by symmetry. Single precision vertices: within 5e-6",
        ["cube.obj", "strip.obj"], b_nrm)

    # ---------------------------------------------------------------- mesh operations
    def b_split(t):
        c = mesh(t, "cube.obj")
        sp = t.n("Split Edges"); t.w(c, 0, sp, 0)
        t.dsize(sp, pt=24, ed=24, fa=6, co=24, msg="6 loose quads: 6 * 4 vertices, 6 * 4 edges:")
        area(t, sp, 6.0, "area unchanged: 6")
        isl = t.n("Mesh Island", None, ("i", 0)); t.w(sp, 0, isl, 0)
        t.count(isl, 1, 6, "6 separate pieces")
        s = mesh(t, "strip.obj")
        ss = t.n("Split Edges"); t.w(s, 0, ss, 0)
        t.dsize(ss, pt=4 + 4 + 3 + 3, ed=14, fa=4, co=14, msg="strip: 14 corners = 14 vertices = 14 edges:")
        cy = t.n("Cylinder")
        sc = t.n("Split Edges"); t.w(cy, 0, sc, 0)
        t.dsize(sc, pt=32 * 4 + 2 * 32, ed=32 * 4 + 2 * 32, fa=34, co=192, msg="default cylinder: 32 quads + two 32-gon caps stay faces, each with its own vertices:")
        area(t, sc, 2 * 32 * math.sin(PI / 32) * 2 + 2 * ngon_area(32, 1.0), "area unchanged: sides 32 * 2 sin(pi/32) * 2 + 2 caps", 1e5)
        no = t.n("Split Edges", None, ("b", False)); t.w(c, 0, no, 0)
        t.dsize(no, pt=8, ed=12, fa=6, msg="Selection false changes nothing:")
        ml = t.n("Mesh Line", ("i", 4)); sl = t.n("Split Edges"); t.w(ml, 0, sl, 0)
        t.dsize(sl, pt=6, ed=3, fa=0, msg="3 loose edges of a 4-vertex line: 6 vertices:")
    row("bfsplt", "Split Edges", "Split Edges",
        "every face gets private vertices: the cube file's 6 quads become 24 vertices, 24 edges, 6 faces and 6 separate pieces with area 6; the strip file's 4 faces (14 corners) become 14 vertices, 14 edges; the default cylinder keeps 34 faces (its 32-gon caps stay one face each) with 32 * 4 + 2 * 32 = 192 vertices and 192 edges and the same area; a 3-edge line becomes 6 vertices; Selection false changes nothing",
        ["cube.obj", "strip.obj"], b_split)

    def b_subs(t):
        c = mesh(t, "cube.obj")
        s1 = t.n("Subdivision Surface"); t.w(c, 0, s1, 0)
        t.dsize(s1, pt=8 + 12 + 6, ed=24 + 24, fa=24, co=96, msg="cube level 1: V + E + F, 2E + corners, corners:")
        vol(t, s1, cc_cube_volume(), "volume of the hand-derived level-1 solid (corners at 5/9, edge points at 3/4, face points at 1 of the half side)", 1e5)
        t.bbox(s1, (0, 0, 0), (1, 1, 1), "face points stay on the faces: the box is the original [0,1]^3", 1e5)
        s2 = t.n("Subdivision Surface", None, ("i", 2)); t.w(c, 0, s2, 0)
        t.dsize(s2, pt=26 + 48 + 24, ed=2 * 48 + 96, fa=96, co=384, msg="level 2: 26 + 48 + 24 = 98, 2 * 48 + 96 = 192, 96:")
        ic = t.n("Ico Sphere", 1.0, ("i", 1))
        si = t.n("Subdivision Surface"); t.w(ic, 0, si, 0)
        t.dsize(si, pt=12 + 30 + 20, ed=2 * 30 + 60, fa=60, co=240, msg="icosahedron: each triangle becomes 3 quads, so 62 / 120 / 60 (Euler 2):")
        s0 = t.n("Subdivision Surface", None, ("i", 0)); t.w(c, 0, s0, 0)
        t.dsize(s0, pt=8, ed=12, fa=6, msg="Level 0 changes nothing:")
        cy = t.n("Cylinder", ("i", 8), ("i", 1), 1.0, 2.0, ("i", 1))
        sc = t.n("Subdivision Surface"); t.w(cy, 0, sc, 0)
        t.dsize(sc, pt=16 + 24 + 10, ed=2 * 24 + 8 * 4 + 2 * 8, fa=48, co=192, msg="8-gon prism (16 vertices, 24 edges, 10 faces with 48 corners): V + E + F = 50, 2E + corners = 96, corners = 48:")
    row("bfsubs", "Subdivision Surface", "Subdivision Surface",
        "Catmull-Clark: V' = V + E + F, E' = 2E + corners, F' = corners (every face of k corners gives k quads): cube 8/12/6 -> 26/48/24 -> 98/192/96; icosahedron 12/30/20 -> 62/120/60; the 8-gon prism 16/24/10 -> 50/96/48; level 1 of the unit cube (hand-derived: about the centre, corners move to 5/9 of the half side, edge points to 3/4 on two axes, face points stay on the faces) has its volume summed by the independent routine cc_cube_volume() in flowrows5_gen.py, box [0,1]^3. Open meshes (boundary rules) are untested",
        ["cube.obj"], b_subs)

    def b_setp(t):
        c = mesh(t, "cube.obj")
        o = t.n("Set Position"); t.w(c, 0, o, 0); t.w(t.vec(1, 2, 3), 0, o, 2)
        t.bbox(o, (1, 2, 3), (2, 3, 4), "Offset (1,2,3) moves the unit cube", 1e5)
        vol(t, o, 1.0, "and keeps its volume")
        p = t.n("Set Position"); t.w(c, 0, p, 0); t.w(t.pt(5, 5, 5), 0, p, 1); t.w(t.vec(0, 0, 1), 0, p, 2)
        t.bbox(p, (5, 5, 6), (5, 5, 6), "Position (5,5,5) puts every vertex there, then Offset (0,0,1): one point (5,5,6)", 1e5)
        ln = t.n("Curve Line", ("i", 0)); t.w(t.pt(0, 0, 0), 0, ln, 1); t.w(t.pt(0, 0, 3), 0, ln, 2)
        lo = t.n("Set Position"); t.w(ln, 0, lo, 0); t.w(t.vec(2, 0, -1), 0, lo, 2)
        t.bbox(lo, (2, 0, -1), (2, 0, 2), "curve (0,0,0)-(0,0,3) offset (2,0,-1)")
        pt = t.n("Set Position"); t.w(t.pt(1, 2, 3), 0, pt, 0); t.w(t.vec(1, 1, 1), 0, pt, 2)
        t.bbox(pt, (2, 3, 4), (2, 3, 4), "a point (1,2,3) offset (1,1,1)")
        z = t.n("Set Position"); t.w(c, 0, z, 0)
        t.bbox(z, (0, 0, 0), (1, 1, 1), "no Position and a zero Offset change nothing", 1e5)
    row("bfsetp", "Set Position", "Set Position",
        "new position = (Position when connected, else the old one) + Offset: unit cube + (1,2,3) spans (1,2,3)-(2,3,4) with volume 1; Position (5,5,5) + Offset (0,0,1) collapses all 8 vertices to (5,5,6); the line (0,0,0)-(0,0,3) + (2,0,-1) spans z -1..2 at x = 2; the point (1,2,3) + (1,1,1) = (2,3,4); no inputs change nothing. Position and Offset are constants, not fields (untested as fields)",
        ["cube.obj"], b_setp)

    def b_p2c(t):
        ml = t.n("Mesh Line", ("i", 5))
        pc = t.n("Points to Curves"); t.w(ml, 0, pc, 0)
        t.dsize(pc, pt=5, sp=1, msg="5 points in a line:")
        t.clen(pc, 4.0, "length 4 (4 edges of 1)")
        c = mesh(t, "cube.obj")
        pts = t.n("Mesh to Points"); t.w(c, 0, pts, 0)
        cp = t.n("Points to Curves"); t.w(pts, 0, cp, 0)
        order = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]
        t.clen(cp, poly_len(order, False), "the 8 cube-file vertices in file order: 1 + 1 + 1 + sqrt 2 + 1 + 1 + 1", 1e5)
        t.dsize(cp, pt=8, sp=1, msg="8 points on one spline:")
        t.bbox(cp, (0, 0, 0), (1, 1, 1), "box of the cube", 1e5)
    row("bfp2c", "Points to Curves", "Points to Curves",
        "one poly spline through the points in vertex order: a 5-vertex line has length 4; the 8 vertices of the cube file in file order (0,0,0) (1,0,0) (1,1,0) (0,1,0) (0,0,1) (1,0,1) (1,1,1) (0,1,1) are joined by edges of 1, 1, 1, sqrt 2, 1, 1, 1: length 6 + sqrt 2. Curve Group ID and Weight are untested",
        ["cube.obj"], b_p2c)

    # ---------------------------------------------------------------- curves
    def b_slen(t):
        ln = t.n("Curve Line", ("i", 0)); t.w(t.pt(0, 0, 0), 0, ln, 1); t.w(t.pt(0, 0, 10), 0, ln, 2)
        for geo, length, count, msg in ((ln, 10.0, 2, "line (0,0,0)-(0,0,10)"), (t.n("Quadrilateral", 6.0, 4.0), 20.0, 4, "6 x 4 rectangle (closing edge included)"),
                                        (t.n("Curve Circle", ("i", 8), 2.0), 16 * 2 * math.sin(PI / 8), 8, "8-gon of radius 2"), (t.n("Star", ("i", 5), 1.0, 2.0, 0.0), 10 * math.sqrt(5 - 4 * math.cos(PI / 5)), 10, "5-point star")):
            n = t.n("Spline Length"); t.w(geo, 0, n, 0)
            t.exact(n, 0, length, msg + ": length")
            t.count(n, 1, count, msg + ": %d points" % count)
        b = t.n("Bezier Segment", ("i", 1)); n = t.n("Spline Length"); t.w(b, 0, n, 0)
        t.exact(n, 0, 2.0, "one Bezier edge is the chord of length 2")
        t.count(n, 1, 2, "with 2 points")
    row("bfslen", "Spline Length", "Spline Length",
        "total length and point count: line 10 / 2; rectangle 6 x 4 = 20 / 4 (closing edge included); 8-gon of radius 2 = 8 * 2 * 2 sin(pi/8) / 8 points; 5-point star ro 2, ri 1 = 10 sqrt(5 - 4 cos 36 degrees) / 10 points; the 1-edge Bezier chord is 2 / 2 points",
        [], b_slen)

    def b_iscy(t):
        ln = t.n("Curve Line", ("i", 0)); t.w(t.pt(0, 0, 0), 0, ln, 1); t.w(t.pt(0, 0, 1), 0, ln, 2)
        for geo, want, msg in ((ln, 0, "an open line"), (t.n("Quadrilateral"), 1, "a rectangle"), (t.n("Curve Circle", ("i", 6), 1.0), 1, "a circle"),
                               (t.n("Spiral"), 0, "a spiral"), (t.n("Star"), 1, "a star"), (t.n("Arc (Blender)"), 0, "an arc"), (t.n("Bezier Segment"), 0, "a Bezier segment")):
            n = t.n("Is Spline Cyclic"); t.w(geo, 0, n, 0)
            t.count(n, 0, want, "%s: cyclic = %d" % (msg, want))
        c = t.n("Set Spline Cyclic", None, ("b", True)); t.w(ln, 0, c, 0)
        n = t.n("Is Spline Cyclic"); t.w(c, 0, n, 0)
        t.count(n, 0, 1, "the line made cyclic")
        o = t.n("Set Spline Cyclic", None, ("b", False)); t.w(t.n("Quadrilateral"), 0, o, 0)
        n2 = t.n("Is Spline Cyclic"); t.w(o, 0, n2, 0)
        t.count(n2, 0, 0, "the rectangle made open")
        rv = t.n("Reverse Curve"); t.w(t.n("Quadrilateral"), 0, rv, 0)
        n3 = t.n("Is Spline Cyclic"); t.w(rv, 0, n3, 0)
        t.count(n3, 0, 1, "reversing keeps it cyclic")
    row("bfiscy", "Is Spline Cyclic", "Is Spline Cyclic",
        "closed shapes are cyclic (rectangle, circle, star), open ones are not (line, spiral, arc, Bezier segment); Set Spline Cyclic true makes the line cyclic, false opens the rectangle; reversing keeps cyclic",
        [], b_iscy)

    def b_spar(t):
        ln = t.n("Curve Line", ("i", 0)); t.w(t.pt(0, 0, 0), 0, ln, 1); t.w(t.pt(0, 0, 10), 0, ln, 2)
        sd = t.n("Subdivide Curve", None, ("i", 3)); t.w(ln, 0, sd, 0)
        for k, fac, length in ((0, 0.0, 0.0), (1, 0.25, 2.5), (2, 0.5, 5.0), (4, 1.0, 10.0)):
            n = t.n("Spline Parameter", None, ("i", k)); t.w(sd, 0, n, 0)
            t.exact(n, 0, fac, "point %d of the 5-point line: factor %g" % (k, fac))
            t.exact(n, 1, length, "point %d: length %g" % (k, length))
            t.count(n, 2, k, "point %d: index %d" % (k, k))
        q = t.n("Quadrilateral", 6.0, 4.0); o = t.n("Set Spline Cyclic", None, ("b", False)); t.w(q, 0, o, 0)
        for k, length in ((1, 6.0), (2, 10.0), (3, 16.0)):
            n = t.n("Spline Parameter", None, ("i", k)); t.w(o, 0, n, 0)
            t.exact(n, 1, length, "open rectangle, point %d: length %g" % (k, length))
            t.exact(n, 0, length / 16.0, "open rectangle, point %d: factor %g / 16" % (k, length))
    row("bfspar", "Spline Parameter", "Spline Parameter",
        "distance along the spline: the 5 points of the line z 0..10 (cuts of 2.5) have lengths 0, 2.5, 5, 7.5, 10 and factors 0, 0.25, 0.5, 0.75, 1 and index = position; the open rectangle (3,2) (-3,2) (-3,-2) (3,-2) of total length 16 has lengths 6, 10, 16 at points 1, 2, 3 (factors 0.375, 0.625, 1). The factor of a cyclic spline (closing edge in the total) is untested",
        [], b_spar)

    # ---------------------------------------------------------------- statistics
    def b_stat(t):
        vals = [2, 5, 8, 11, 14]
        l = numlist(t, vals)
        n = t.n("Attribute Statistic"); t.w(l, 0, n, 0)
        for port, want, msg in ((0, 8.0, "mean 40 / 5"), (1, 8.0, "median of 5 values is the middle one"), (2, 40.0, "sum"), (3, 2.0, "min"), (4, 14.0, "max"), (5, 12.0, "range 14 - 2"),
                                (7, 18.0, "variance (36 + 9 + 0 + 9 + 36) / 5 (over n)"), (6, math.sqrt(18.0), "standard deviation sqrt 18")):
            t.exact(n, port, want, msg)
        e = numlist(t, [1, 2, 3, 4])
        n2 = t.n("Attribute Statistic"); t.w(e, 0, n2, 0)
        t.exact(n2, 0, 2.5, "1 2 3 4: mean 2.5"); t.exact(n2, 2, 10.0, "sum 10"); t.exact(n2, 5, 3.0, "range 3")
        t.exact(n2, 1, 3.0, "median of an even count: the upper middle value (recalled, needs owner check)")
        neg = numlist(t, [-3, 7, -1, 5])
        n3 = t.n("Attribute Statistic"); t.w(neg, 0, n3, 0)
        t.exact(n3, 3, -3.0, "negative min"); t.exact(n3, 4, 7.0, "max"); t.exact(n3, 2, 8.0, "sum"); t.exact(n3, 0, 2.0, "mean"); t.exact(n3, 5, 10.0, "range")
        one = t.n("Attribute Statistic", 4.5)
        t.exact(one, 0, 4.5, "one value: mean"); t.exact(one, 7, 0.0, "variance 0"); t.exact(one, 5, 0.0, "range 0")
    row("bfstat", "Attribute Statistic", "Attribute Statistic",
        "values 2 5 8 11 14: sum 40, mean 8, median 8 (the middle of 5), min 2, max 14, range 12, variance (36 + 9 + 0 + 9 + 36) / 5 = 18 and standard deviation sqrt 18; values 1 2 3 4: mean 2.5, sum 10, range 3; values -3 7 -1 5: min -3, max 7, sum 8, mean 2, range 10; one value has variance 0. RECALLED, needs owner check (not independent): the upper-middle median of an even count (3 for 1 2 3 4) and that the variance divides by n rather than n - 1",
        [], b_stat)

    def b_favg(t):
        l = numlist(t, [1, 2, 3, 4, 5])
        n = t.n("Field Average"); t.w(l, 0, n, 0)
        t.exact(n, 0, 3.0, "mean of 1..5"); t.exact(n, 1, 3.0, "median of 1..5")
        u = numlist(t, [9, 1, 5, 7, 3])
        n2 = t.n("Field Average"); t.w(u, 0, n2, 0)
        t.exact(n2, 0, 5.0, "mean of the unsorted 9 1 5 7 3"); t.exact(n2, 1, 5.0, "median sorts first: 1 3 5 7 9")
        e = numlist(t, [10, 20, 30, 40])
        n3 = t.n("Field Average"); t.w(e, 0, n3, 0)
        t.exact(n3, 0, 25.0, "mean of 10 20 30 40")
        t.exact(n3, 1, 30.0, "median of an even count: the upper middle value (recalled, needs owner check)")
        s = t.n("Series", 0.0, 0.25, ("i", 9)); n4 = t.n("Field Average"); t.w(s, 0, n4, 0)
        t.exact(n4, 0, 1.0, "mean of 0, 0.25 ... 2"); t.exact(n4, 1, 1.0, "median 1")
    row("bffavg", "Field Average", "Field Average",
        "mean and median of one group: 1..5 -> 3 and 3; 9 1 5 7 3 -> 5 and 5 (the median sorts first); the 9 values 0, 0.25 ... 2 -> 1 and 1; 10 20 30 40 -> mean 25. RECALLED, needs owner check (not independent): the median of an even count is the upper middle value (30). Group ID is untested",
        [], b_favg)

    def b_mm(t):
        for vals, lo, hi in (([4, -2, 9, 0.5], -2.0, 9.0), ([3, 3, 3], 3.0, 3.0), ([-8, -1, -4], -8.0, -1.0), ([1e6, 2e6, 1.5e6], 1e6, 2e6)):
            l = numlist(t, vals)
            n = t.n("Field Min & Max"); t.w(l, 0, n, 0)
            t.exact(n, 0, lo, "min of %s" % vals); t.exact(n, 1, hi, "max of %s" % vals)
        one = t.n("Field Min & Max", 7.0)
        t.exact(one, 0, 7.0, "one value: min"); t.exact(one, 1, 7.0, "max")
        s = t.n("Series", -5.0, 2.0, ("i", 20)); n = t.n("Field Min & Max"); t.w(s, 0, n, 0)
        t.exact(n, 0, -5.0, "series -5, -3 ... 33: min -5"); t.exact(n, 1, 33.0, "max -5 + 19 * 2 = 33")
    row("bfmmax", "Field Min & Max", "Field Min & Max",
        "smallest and largest value: 4 -2 9 0.5 -> -2, 9; 3 3 3 -> 3, 3; -8 -1 -4 -> -8, -1; 1e6 2e6 1.5e6 -> 1e6, 2e6; one value is both; the series -5, -3 ... 33 -> -5 and 33",
        [], b_mm)

    def b_fvar(t):
        l = numlist(t, [2, 4, 4, 4, 5, 5, 7, 9])
        n = t.n("Field Variance"); t.w(l, 0, n, 0)
        t.exact(n, 1, 4.0, "the textbook data 2 4 4 4 5 5 7 9 (mean 5, squared distances 9 1 1 1 0 0 4 16 = 32): variance 32 / 8 = 4")
        t.exact(n, 0, 2.0, "standard deviation 2")
        c = numlist(t, [6, 6, 6])
        n2 = t.n("Field Variance"); t.w(c, 0, n2, 0)
        t.exact(n2, 1, 0.0, "equal values: variance 0"); t.exact(n2, 0, 0.0, "deviation 0")
        two = numlist(t, [1, 3])
        n3 = t.n("Field Variance"); t.w(two, 0, n3, 0)
        t.exact(n3, 1, 1.0, "1 and 3: mean 2, variance (1 + 1) / 2 = 1"); t.exact(n3, 0, 1.0, "deviation 1")
        s = t.n("Series", 1.0, 1.0, ("i", 5)); n4 = t.n("Field Variance"); t.w(s, 0, n4, 0)
        t.exact(n4, 1, 2.0, "1..5: variance (4 + 1 + 0 + 1 + 4) / 5 = 2"); t.exact(n4, 0, math.sqrt(2.0), "deviation sqrt 2")
    row("bffvar", "Field Variance", "Field Variance",
        "the sum of squared distances from the mean over n: 2 4 4 4 5 5 7 9 (mean 5) -> 32 / 8 = 4 and deviation 2; 1 3 -> 1 and 1; 1..5 -> 2 and sqrt 2; equal values -> 0. RECALLED, needs owner check (not independent): that Blender divides by n (population variance) and not by n - 1; Group ID is untested",
        [], b_fvar)
    return rows


PRELUDE_EOF = "EOF_BF"


def main():
    for name, label, title, closed, files, t in make_rows():
        nobj = sum(len(vals.split()) if vals else 0 for _tag, vals, _msg in t.expect)
        with open(os.path.join(HERE, "flowrows_%s.dflow" % name), "w") as fh:
            fh.write(t.g.text())
        with open(os.path.join(HERE, "flowrows_%s_script.txt" % name), "w") as fh:
            fh.write("# Flow Register row %s (Blender Geometry Nodes): player + bake, read back by sections/98_%s.sh (see flowrows5_gen.py)\n"
                     "Grasshopper\nGrasshopperPlayer @GRAPH@ Bake=Yes\n@expect_objects %d\nSelAll\nList\n" % (title, name, nobj))
        checks = "\n".join('fr_chk %d "%s" "%s"' % (tag, vals, msg.replace('"', "'")) for tag, vals, msg in t.expect)
        text = SECTION.format(up=name.upper(), rid="geometry_nodes/" + label, title=title, name=name, closed=closed,
                              nn=len(t.g.nodes), nobj=nobj, checks=checks)
        pre = ""
        for fn in files:
            pre += "cat > \"$TMPW/%s\" <<'%s'\n%s%s\n" % (fn, PRELUDE_EOF, FILES[fn], PRELUDE_EOF)
        old = 'sed "s|@GRAPH@|$HEREW/flowrows_%s.dflow|g" "$HERE/flowrows_%s_script.txt" > "$TMPW/flowrows_%s_script.txt"' % (name, name, name)
        assert old in text
        new = (pre + 'sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_%s.dflow" > "$TMPW/flowrows_%s.dflow"\n' % (name, name) +
               'sed "s|@GRAPH@|$TMPW/flowrows_%s.dflow|g" "$HERE/flowrows_%s_script.txt" > "$TMPW/flowrows_%s_script.txt"' % (name, name, name))
        text = text.replace(old, new)
        with open(os.path.join(HERE, "sections", "98_%s.sh" % name), "w") as fh:
            fh.write(text)
        print(name, label, "nodes", len(t.g.nodes), "objects", nobj)


if __name__ == "__main__":
    main()
