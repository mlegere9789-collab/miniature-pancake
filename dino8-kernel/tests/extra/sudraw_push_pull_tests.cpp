// SketchUp-style direct modelling rows (edit-tools and drawing-tools domains), kernel part one: Push/Pull, repeated extrusions, stretching
// a gable by its ridge edge, folding a flap, and healing a face when an edge is erased. Every expectation is a closed form worked out in
// the comment of its test, or a second implementation written in this file (a polyhedron volume from the faces' own loops by signed
// tetrahedra, a planarity residual by Newell's normal, Euler's V - E + F), never a number read off the code under test.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

// A convex or concave planar polygon (points in order) swept by d into a closed prism, built face by face with the outward normal and
// the loop counter-clockwise seen from outside, exactly the shape Brep::FromPlanarFaces expects.
Brep Prism(std::vector<Point3d> base, Vector3d d) {
  // Newell normal of the base
  Vector3d n(0, 0, 0);
  for (size_t i = 0; i < base.size(); ++i) {
    const Point3d& a = base[i];
    const Point3d& b = base[(i + 1) % base.size()];
    n.x += (a.y - b.y) * (a.z + b.z);
    n.y += (a.z - b.z) * (a.x + b.x);
    n.z += (a.x - b.x) * (a.y + b.y);
  }
  if (n.x * d.x + n.y * d.y + n.z * d.z < 0) {
    std::reverse(base.begin(), base.end());
    n = Vector3d(-n.x, -n.y, -n.z);
  }
  n.Unitize();
  std::vector<Brep::PlanarFace> faces;
  Brep::PlanarFace bottom;
  bottom.plane = ON_Plane(base[0], ON_3dVector(-n.x, -n.y, -n.z));
  bottom.loop.assign(base.rbegin(), base.rend());
  faces.push_back(bottom);
  Brep::PlanarFace top;
  top.plane = ON_Plane(base[0] + d, ON_3dVector(n.x, n.y, n.z));
  for (const Point3d& p : base) top.loop.push_back(p + d);
  faces.push_back(top);
  for (size_t i = 0; i < base.size(); ++i) {
    const Point3d& a = base[i];
    const Point3d& b = base[(i + 1) % base.size()];
    Brep::PlanarFace side;
    const Vector3d e = b - a;
    Vector3d out = ON_CrossProduct(ON_3dVector(e.x, e.y, e.z), ON_3dVector(n.x, n.y, n.z));
    out.Unitize();
    side.plane = ON_Plane(a, ON_3dVector(out.x, out.y, out.z));
    side.loop = {a, b, b + d, a + d};
    faces.push_back(side);
  }
  return Brep::FromPlanarFaces(faces);
}

// Second implementation of the volume of a planar-faced closed solid: signed tetrahedra from the origin over a fan of every loop
// (the faces' loops are counter-clockwise seen from outside, so every face contributes (1/6) sum p0 . (pi x pi+1)).
double PolyhedronVolume(const Brep& b) {
  double v = 0.0;
  for (const Brep::PlanarFace& f : b.PlanarFaces()) {
    for (size_t i = 1; i + 1 < f.loop.size(); ++i) {
      const Point3d& a = f.loop[0];
      const Point3d& p = f.loop[i];
      const Point3d& q = f.loop[i + 1];
      v += (a.x * (p.y * q.z - p.z * q.y) - a.y * (p.x * q.z - p.z * q.x) + a.z * (p.x * q.y - p.y * q.x)) / 6.0;
    }
  }
  return v;
}

// Area of a planar polygon loop as half the length of the vector area (Newell), and the largest distance of its corners from the
// plane through its centroid with that normal: a planarity residual that does not use the kernel's own plane.
void LoopResidual(const std::vector<Point3d>& loop, double& area, double& planarity) {
  Vector3d n(0, 0, 0);
  Vector3d c(0, 0, 0);
  for (size_t i = 0; i < loop.size(); ++i) {
    const Point3d& a = loop[i];
    const Point3d& b = loop[(i + 1) % loop.size()];
    n.x += (a.y - b.y) * (a.z + b.z);
    n.y += (a.z - b.z) * (a.x + b.x);
    n.z += (a.x - b.x) * (a.y + b.y);
    c.x += a.x;
    c.y += a.y;
    c.z += a.z;
  }
  c = Vector3d(c.x / loop.size(), c.y / loop.size(), c.z / loop.size());
  const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
  area = 0.5 * len;
  planarity = 0.0;
  for (const Point3d& p : loop) {
    const double d = ((p.x - c.x) * n.x + (p.y - c.y) * n.y + (p.z - c.z) * n.z) / len;
    planarity = std::max(planarity, std::fabs(d));
  }
}

double SurfaceArea(const Brep& b) {
  double s = 0.0;
  for (const Brep::PlanarFace& f : b.PlanarFaces()) {
    double a = 0.0, r = 0.0;
    LoopResidual(f.loop, a, r);
    s += a;
  }
  return s;
}

struct Topology {
  int vertices = 0, edges = 0, faces = 0, two_trim_edges = 0;
};

Topology CountTopology(const Brep& b) {
  Topology t;
  const ON_Brep& r = b.raw();
  for (int i = 0; i < r.m_V.Count(); ++i) t.vertices += r.m_V[i].m_vertex_index >= 0 ? 1 : 0;
  for (int i = 0; i < r.m_F.Count(); ++i) t.faces += r.m_F[i].m_face_index >= 0 ? 1 : 0;
  for (int i = 0; i < r.m_E.Count(); ++i) {
    if (r.m_E[i].m_edge_index < 0) continue;
    ++t.edges;
    if (r.m_E[i].TrimCount() == 2) ++t.two_trim_edges;
  }
  return t;
}

// A flat sheet [0,w] x [0,l] in the plane z = 0.
Brep FlatSheet(double w, double l) {
  std::vector<Point3d> grid;
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) grid.push_back(P(w * i, l * j, 0.0));
  }
  return Brep::FromSurface(NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1));
}

// Index of the planar face whose outward normal is closest to `dir`, and the planes' corner list for tests that need it.
int FaceFacing(const Brep& b, double nx, double ny, double nz) {
  const std::vector<Brep::PlanarFace> f = b.PlanarFaces();
  int best = -1;
  double bd = -2.0;
  for (size_t i = 0; i < f.size(); ++i) {
    const double d = f[i].plane.zaxis.x * nx + f[i].plane.zaxis.y * ny + f[i].plane.zaxis.z * nz;
    if (d > bd) {
      bd = d;
      best = static_cast<int>(i);
    }
  }
  return best;
}

}  // namespace

// DRAW EDIT-0019, "Push/Pull extrudes or cuts a face along its normal with typed distance...". Acceptance sketch: "Pull a 1 m by 1 m face
// 2 m; resulting solid volume 2.0 m3 exact, 6 faces." The face is a unit sheet in z = 0, pulled by (0,0,2). Closed forms: volume 1 * 1 * 2 = 2,
// surface 2 * 1 + 4 * (1 * 2) = 10, Euler's formula for a closed cuboid shell 8 - 12 + 6 = 2, every edge in exactly two faces. A second pull
// of the same face the other way (-2) gives the same volume, and a pull on a face of a solid (box top, +2 then a pull of the new top by
// -1.5) shows the sign convention: positive grows the solid along the outward normal, negative cuts it (1 x 1 x 1 box: 3, then 1.5).
D8_EXTRA_TEST(SuPushPullPullsAUnitFaceIntoASixFaceSolidOfVolumeTwo) {
  const Brep solid = Brep::ExtrudeFaceWelded(FlatSheet(1.0, 1.0), 0, Vector3d(0, 0, 2.0));
  const Topology t = CountTopology(solid);
  d8test::Check(t.vertices == 8 && t.edges == 12 && t.faces == 6, "the pulled unit face has 8 vertices, 12 edges and 6 faces");
  d8test::Check(t.vertices - t.edges + t.faces == 2, "Euler's formula V - E + F = 2 holds for the pulled solid");
  d8test::Check(t.two_trim_edges == 12, "every one of the 12 edges is shared by exactly two faces");
  d8test::Check(solid.raw().IsSolid() && solid.raw().IsValid(), "the pulled face is a valid closed solid");
  d8test::Check(std::fabs(solid.VolumeOfSealedSolid() - 2.0) < 1e-12, Msg("volume of the pulled 1 x 1 face by 2 is 2", solid.VolumeOfSealedSolid()).c_str());
  d8test::Check(std::fabs(solid.Area() - 10.0) < 1e-9, Msg("surface of the 1 x 1 x 2 prism is 2 + 8 = 10", solid.Area()).c_str());

  const Brep down = Brep::ExtrudeFaceWelded(FlatSheet(1.0, 1.0), 0, Vector3d(0, 0, -2.0));
  d8test::Check(std::fabs(down.VolumeOfSealedSolid() - 2.0) < 1e-12 && down.raw().IsSolid(), "pulling the face the other way is the same solid mirrored (volume 2)");

  const Brep box = Brep::Box(0, 0, 0, 1, 1, 1);
  const int top = FaceFacing(box, 0, 0, 1);
  const Brep grown = dino8::kernel::PushPullFace(box, top, 2.0);
  d8test::Check(std::fabs(PolyhedronVolume(grown) - 3.0) < 1e-12, Msg("push the top of the unit box by +2: volume 1 + 2 = 3", PolyhedronVolume(grown)).c_str());
  d8test::Check(std::fabs(SurfaceArea(grown) - 14.0) < 1e-9, Msg("its surface is 2 + 4 * 3 = 14", SurfaceArea(grown)).c_str());
  const int newtop = FaceFacing(grown, 0, 0, 1);
  const Brep cut = dino8::kernel::PushPullFace(grown, newtop, -1.5);
  d8test::Check(std::fabs(PolyhedronVolume(cut) - 1.5) < 1e-12, Msg("pull the new top by -1.5 into the solid: height 1.5, volume 1.5", PolyhedronVolume(cut)).c_str());
}

// DRAW EDIT-0022, "Double-click another face to apply the previous distance; Ctrl double-click stacks an identical extrusion." Acceptance
// sketch: "Repeat extrusions on 3 faces; each extrudes by the same distance exactly." Three solids with a face of area 1, 6 and 12 each
// get the same distance d = 0.75 on that face: the volume grows by d times the face area exactly (0.75, 4.5, 9), and stacking the same push
// a second time on the new face (the "identical extrusion") grows it by the same amount again (twice d times the area).
D8_EXTRA_TEST(SuPushPullRepeatsTheSameDistanceOnThreeFacesAndStacks) {
  struct Case { double w, l, h; double area; const char* what; };
  const Case cases[] = {{1, 1, 1, 1.0, "1 x 1 top"}, {2, 3, 1, 6.0, "2 x 3 top"}, {3, 4, 5, 12.0, "3 x 4 top"}};
  const double d = 0.75;
  for (const Case& c : cases) {
    const Brep b = Brep::Box(0, 0, 0, c.w, c.l, c.h);
    const double v0 = c.w * c.l * c.h;
    const Brep one = dino8::kernel::PushPullFace(b, FaceFacing(b, 0, 0, 1), d);
    d8test::Check(std::fabs(PolyhedronVolume(one) - (v0 + d * c.area)) < 1e-12, Msg(std::string("one push of ") + c.what + " by 0.75 adds d times the face area", PolyhedronVolume(one) - v0).c_str());
    const Brep two = dino8::kernel::PushPullFace(one, FaceFacing(one, 0, 0, 1), d);
    d8test::Check(std::fabs(PolyhedronVolume(two) - (v0 + 2 * d * c.area)) < 1e-12, Msg(std::string("the identical push stacked on ") + c.what + " adds the same amount again", PolyhedronVolume(two) - v0).c_str());
  }
}

// DRAW EDIT-0021, "Push/Pulling a curved face creates a surface entity made of many smaller faces". Acceptance sketch: "Extrude a 24-gon
// face; resulting prism has 24 side faces and smooth normals." The 24-gon of circumradius 3 is a trimmed planar face pulled by 2. Closed
// forms: the polygon area is (n/2) R^2 sin(2 pi / n) = 12 * 9 * sin(15 deg) and the volume is that times 2; every side face is a rectangle
// of the chord 2 R sin(pi / n) by 2; Euler for the prism shell: V = 48, E = 72, F = 26 (24 sides and two caps). Smooth normals (the soft
// edge display) are not part of this test.
D8_EXTRA_TEST(SuPushPullExtrudesAn24GonIntoTwentyFourSideFaces) {
  const int n = 24;
  const double r = 3.0, h = 2.0;
  std::vector<Point3d> ring;
  for (int i = 0; i < n; ++i) ring.push_back(P(r * std::cos(2 * kPi * i / n), r * std::sin(2 * kPi * i / n), 0.0));
  Brep::PlanarFace face;
  face.plane = ON_Plane(P(0, 0, 0), ON_3dVector(0, 0, -1));
  face.loop.assign(ring.rbegin(), ring.rend());  // outward normal -z: the loop runs counter-clockwise seen from below
  const Brep sheet = Brep::FromPlanarFaces({face});
  const Brep solid = Brep::ExtrudeFace(sheet, 0, Vector3d(0, 0, h));
  const double area = 0.5 * n * r * r * std::sin(2 * kPi / n);
  const Topology t = CountTopology(solid);
  d8test::Check(t.faces == n + 2, Msg("26 faces: 24 sides and two caps", t.faces).c_str());
  d8test::Check(t.vertices == 2 * n && t.edges == 3 * n && t.vertices - t.edges + t.faces == 2, "V = 48, E = 72 and Euler's V - E + F = 2");
  d8test::Check(solid.raw().IsSolid(), "the extruded 24-gon is a closed solid");
  d8test::Check(std::fabs(PolyhedronVolume(solid) - area * h) < 1e-9, Msg("volume equals polygon area times height", PolyhedronVolume(solid) - area * h).c_str());
  const double chord = 2 * r * std::sin(kPi / n);
  int rectangles = 0;
  for (const Brep::PlanarFace& f : solid.PlanarFaces()) {
    double a = 0, res = 0;
    LoopResidual(f.loop, a, res);
    if (f.loop.size() == 4 && std::fabs(a - chord * h) < 1e-9 && res < 1e-9) ++rectangles;
  }
  d8test::Check(rectangles == n, Msg("24 side faces are planar rectangles of area chord * height", rectangles).c_str());
}

// DRAW EDIT-0003, "Dragging a face, edge or vertex of attached geometry stretches adjacent geometry and keeps faces planar". Acceptance
// sketch: "Drag a roof ridge vertex up by 1 m; adjacent faces remain planar within 1e-9." The house is the pentagon (0,0),(4,0),(4,3),(2,4),
// (0,3) in the xz plane swept 6 along y. The ridge edge (both ends, so the two roof quads stay planar) is raised by 1: the gable pentagon area
// grows from 4 * 3 + 4 * (4 - 3) / 2 = 14 to 4 * 3 + 4 * (5 - 3) / 2 = 16, so the volume goes from 84 to 96. Every face of the result is checked
// planar with Newell's normal (not the kernel's plane), and the side walls and floor keep their area.
D8_EXTRA_TEST(SuStretchRaisingTheRidgeEdgeKeepsEveryFacePlanarWithTheClosedFormVolume) {
  const Brep house = Prism({P(0, 0, 0), P(4, 0, 0), P(4, 0, 3), P(2, 0, 4), P(0, 0, 3)}, Vector3d(0, 6, 0));
  d8test::Check(std::fabs(PolyhedronVolume(house) - 84.0) < 1e-9, Msg("the house is 14 * 6 = 84", PolyhedronVolume(house)).c_str());
  const Brep raised = dino8::kernel::MoveEdgeConvexPlanar(house, P(2, 0, 4), P(2, 6, 4), P(2, 0, 5), P(2, 6, 5));
  d8test::Check(raised.FaceCount() == house.FaceCount(), "the face count is unchanged by the stretch");
  d8test::Check(std::fabs(PolyhedronVolume(raised) - 96.0) < 1e-9, Msg("raised house volume is 16 * 6 = 96", PolyhedronVolume(raised)).c_str());
  double worst = 0.0;
  for (const Brep::PlanarFace& f : raised.PlanarFaces()) {
    double a = 0, res = 0;
    LoopResidual(f.loop, a, res);
    worst = std::max(worst, res);
  }
  d8test::Check(worst < 1e-9, Msg("every face is planar within 1e-9 after the ridge moves", worst).c_str());
  // the area of the two roof planes: each is 6 long by the slant of (2, 2) = 2 sqrt 2 after the move, the gable faces 16 each, floor 24, walls 18 + 18
  const double expected_area = 2 * 16.0 + 24.0 + 2 * (6 * 3.0) + 2 * (6 * std::sqrt(8.0));
  d8test::Check(std::fabs(SurfaceArea(raised) - expected_area) < 1e-9, Msg("surface area is gables + floor + walls + two 6 x 2 sqrt2 roof planes", SurfaceArea(raised) - expected_area).c_str());
}

// DRAW EDIT-0010, "Rotate can fold a face or group of faces along a drag-defined hinge edge, bending adjacent geometry". Acceptance sketch:
// "Fold a flap by 90 degrees about an edge; folded face normal equals the rotated normal." A 90 degree fold of a wall of a convex solid
// flattens it, which is outside the convex precondition of this function, so the fold here is 30 degrees: the front wall (normal -y) of a
// 10 x 10 x 10 cube hinged at its bottom edge (the x axis) turns by theta about +x: Rodrigues gives the new normal (0, -cos theta, sin theta)
// (or the mirrored sign when the face loop winds the other way, so both are accepted and the sign of the hinge axis decides). The bottom
// face must not move and the volume follows 1000 +- 500 tan theta.
D8_EXTRA_TEST(SuRotateFoldsAWallAboutItsHingeEdgeToTheRotatedNormal) {
  const Brep cube = Brep::Box(0, 0, 0, 10, 10, 10);
  const int front = FaceFacing(cube, 0, -1, 0);
  const std::vector<Brep::PlanarFace> faces = cube.PlanarFaces();
  const std::vector<Point3d>& loop = faces[front].loop;
  int hinge = -1;
  for (size_t i = 0; i < loop.size(); ++i) {
    const Point3d& a = loop[i];
    const Point3d& b = loop[(i + 1) % loop.size()];
    if (std::fabs(a.z) < 1e-9 && std::fabs(b.z) < 1e-9) hinge = static_cast<int>(i);
  }
  d8test::Check(hinge >= 0, "the front wall has an edge on the floor to hinge at");
  if (hinge < 0) return;
  const Point3d a = loop[static_cast<size_t>(hinge)];
  const Point3d b = loop[static_cast<size_t>((hinge + 1) % static_cast<int>(loop.size()))];
  const double sign = b.x > a.x ? 1.0 : -1.0;  // the hinge axis as walked is sign * (+x)
  const double theta = 30.0 * kPi / 180.0;
  const Brep folded = dino8::kernel::FoldFaceConvexPlanar(cube, front, hinge, theta);
  // Rodrigues: n' = n cos t + (k x n) sin t + k (k . n)(1 - cos t) with k = sign * x, n = (0, -1, 0): k x n = sign * (x cross -y) = sign * (-z)
  const Vector3d expected(0.0, -std::cos(theta), -sign * std::sin(theta));
  const int moved = FaceFacing(folded, expected.x, expected.y, expected.z);
  const std::vector<Brep::PlanarFace> ff = folded.PlanarFaces();
  const ON_3dVector got = ff[moved].plane.zaxis;
  const double err = std::sqrt((got.x - expected.x) * (got.x - expected.x) + (got.y - expected.y) * (got.y - expected.y) + (got.z - expected.z) * (got.z - expected.z));
  d8test::Check(err < 1e-9, Msg("the folded wall's normal is the Rodrigues rotation of the original normal", err).c_str());
  const double volume = 1000.0 + sign * 500.0 * std::tan(theta);
  d8test::Check(std::fabs(PolyhedronVolume(folded) - volume) < 1e-9, Msg("volume 1000 +- 500 tan(30 deg) by the closed-form slab integral", PolyhedronVolume(folded) - volume).c_str());
  const int bottom = FaceFacing(folded, 0, 0, -1);
  double zmax = 0.0;
  for (const Point3d& p : ff[bottom].loop) zmax = std::max(zmax, std::fabs(p.z));
  d8test::Check(zmax < 1e-9, "the hinge side (the floor) stays in z = 0");
}

// DRAW DRAW-0045, "Drawing across or erasing edges can heal deleted faces; coplanar faces merge when the shared edge is erased". Acceptance
// sketch: "Erase shared edge between coplanar faces: one merged face; area equals sum." The fixture is a 2 x 1 x 1 cuboid whose top has an
// edge drawn across it at x = 1 (the top is two coplanar unit squares sharing that edge; the front and back walls carry the extra vertex on
// the top rim): 7 faces. Erasing the edge is the merge of coplanar faces: one top face of area 1 + 1 = 2 and the 6 faces of a cuboid, surface
// 2 * 2 + 2 * 2 + 2 * 1 = 10 (top and floor 2 each, front and back 2 each, the two ends 1 each) and volume 2.
D8_EXTRA_TEST(SuEraseTheSharedEdgeMergesCoplanarFacesIntoOneWithTheSummedArea) {
  std::vector<Brep::PlanarFace> faces;
  auto face = [&](double nx, double ny, double nz, std::vector<Point3d> loop) {
    Brep::PlanarFace f;
    f.plane = ON_Plane(loop[0], ON_3dVector(nx, ny, nz));
    f.loop = std::move(loop);
    faces.push_back(f);
  };
  face(0, 0, -1, {P(0, 0, 0), P(0, 1, 0), P(2, 1, 0), P(2, 0, 0)});                       // floor
  face(0, 0, 1, {P(0, 0, 1), P(1, 0, 1), P(1, 1, 1), P(0, 1, 1)});                         // top, left half
  face(0, 0, 1, {P(1, 0, 1), P(2, 0, 1), P(2, 1, 1), P(1, 1, 1)});                         // top, right half
  face(0, -1, 0, {P(0, 0, 0), P(2, 0, 0), P(2, 0, 1), P(1, 0, 1), P(0, 0, 1)});           // front, with the vertex (1,0,1)
  face(0, 1, 0, {P(0, 1, 0), P(0, 1, 1), P(1, 1, 1), P(2, 1, 1), P(2, 1, 0)});             // back, with the vertex (1,1,1)
  face(-1, 0, 0, {P(0, 0, 0), P(0, 0, 1), P(0, 1, 1), P(0, 1, 0)});                       // left end
  face(1, 0, 0, {P(2, 0, 0), P(2, 1, 0), P(2, 1, 1), P(2, 0, 1)});                         // right end
  Brep body = Brep::FromPlanarFaces(faces);
  d8test::Check(body.FaceCount() == 7 && body.raw().IsSolid(), "the cuboid with an edge drawn across its top is a closed solid of 7 faces");
  d8test::Check(std::fabs(PolyhedronVolume(body) - 2.0) < 1e-12 && std::fabs(SurfaceArea(body) - 10.0) < 1e-9, "before the erase: volume 2, surface 10");
  const int merged = body.MergeCoplanarFaces();
  d8test::Check(merged == 1, Msg("exactly one pair of coplanar faces merges", merged).c_str());
  d8test::Check(body.FaceCount() == 6, Msg("after the edge is erased the body has the 6 faces of a cuboid", body.FaceCount()).c_str());
  int top_faces = 0;
  double top_area = 0.0;
  for (const Brep::PlanarFace& f : body.PlanarFaces()) {
    if (f.plane.zaxis.z > 0.99) {
      ++top_faces;
      double ar = 0, res = 0;
      LoopResidual(f.loop, ar, res);
      top_area += ar;
    }
  }
  d8test::Check(top_faces == 1 && std::fabs(top_area - 2.0) < 1e-12, Msg("the two top faces became one face of area 1 + 1 = 2", top_area).c_str());
  d8test::Check(std::fabs(SurfaceArea(body) - 10.0) < 1e-9 && std::fabs(PolyhedronVolume(body) - 2.0) < 1e-12, "the cuboid keeps surface 10 and volume 2");
  d8test::Check(body.raw().IsSolid() && body.raw().IsValid(), "the merged body is still a valid closed solid");
}
