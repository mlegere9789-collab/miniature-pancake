// Register row tests, round 3, mesh-subd-sculpt factory (second half): Blender primitives, vertex groups, sculpt brush / mask / expand / pivot,
// and UV projections and unwrap. Every expectation below is a closed form or an exact count that does not come from the code under test:
//  * Primitives: Euler characteristic V - E + F (2 for a sphere-like solid, 0 for the torus), the element counts of the n-gon constructions,
//    the area of a regular n-gon n r^2 sin(2 pi / n) / 2, the prism volume (base area x depth), the icosahedron volume (5 / 12)(3 + sqrt 5) a^3
//    with a = 4 R / sqrt(10 + 2 sqrt 5), the ico-sphere recurrences V = 10 * 4^(s-1) + 2, F = 20 * 4^(s-1), E = 30 * 4^(s-1).
//  * Vertex groups: hand-worked weight tables (the arithmetic of each operator is done by hand in the comments).
//  * Sculpt: the volume swept by a radial brush is 2 pi R^2 * integral_0^1 f(t) (1 - t) dt, for example (2/3) pi R^2 for the Sphere falloff
//    (a hemisphere); graph balls on a square lattice have 2 k^2 + 2 k + 1 points (edge steps), (2 k + 1)^2 (steps across faces) and the
//    Gauss circle counts 5, 9, 13, 21, 25, 29, 37 for squared radii 1, 2, 4, 5, 8, 9, 10.
//  * UV: spherical and cylindrical coordinates of vertices at known angles, and the unrolled size of a flat patch / cylinder strip.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/mesh_unwrap.h"
#include "dino8/kernel/polygon_mesh.h"
#include "dino8/kernel/sculpt_ops.h"
#include "dino8/kernel/vertex_groups.h"
#include "test_registry.h"

namespace {

using dino8::kernel::CapFill;
using dino8::kernel::Mesh;
using dino8::kernel::Point2d;
using dino8::kernel::Point3d;
using dino8::kernel::PolygonMesh;
using dino8::kernel::Vector3d;
using dino8::kernel::VertexGroups;
namespace prim = dino8::kernel::PolygonPrimitives;
namespace sculpt = dino8::kernel::sculpt;

constexpr double kPi = 3.14159265358979323846;

void Ck(bool ok, const std::string& what) { d8test::Check(ok, what.c_str()); }
bool Near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
std::string Num(double v) {
  char b[40];
  std::snprintf(b, sizeof b, "%.6g", v);
  return b;
}

// Regular n-gon of circumradius r: area n r^2 sin(2 pi / n) / 2.
double NGonArea(int n, double r) { return 0.5 * n * r * r * std::sin(2.0 * kPi / n); }

// (nx + 1) x (ny + 1) vertices of a flat grid with the given pitch centred on the origin of the XY plane, nx x ny quads, normal +Z.
PolygonMesh Grid(int nx, int ny, double pitch) {
  PolygonMesh m;
  for (int j = 0; j <= ny; ++j)
    for (int i = 0; i <= nx; ++i) m.vertices.emplace_back((i - nx / 2.0) * pitch, (j - ny / 2.0) * pitch, 0.0);
  auto id = [nx](int i, int j) { return j * (nx + 1) + i; };
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) m.faces.push_back({id(i, j), id(i + 1, j), id(i + 1, j + 1), id(i, j + 1)});
  return m;
}

double Dist(const Point2d& a, const Point2d& b) { return std::hypot(a.x - b.x, a.y - b.y); }

void AddTri(Mesh& m, int a, int b, int c) {
  ON_MeshFace f;
  f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = c;
  m.raw().m_F.Append(f);
}
void AddQuad(Mesh& m, int a, int b, int c, int d) {
  ON_MeshFace f;
  f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
  m.raw().m_F.Append(f);
}

}  // namespace

// ---- Add > Cube / Plane / Circle / Cylinder / Ico Sphere / Torus ----------------------------------------------------------------------------
// bpy.ops.mesh.primitive_cube_add (D8-BLENDER-MESH-0097): a cube of edge `size` has 8 vertices, 12 edges and 6 quad faces, Euler characteristic 2,
// volume size^3 and surface area 6 size^2.
D8_EXTRA_TEST(Rw3MeshBCubeAddHasEightVerticesTwelveEdgesSixQuadsAndVolumeSizeCubed) {
  for (double size : {2.0, 3.5}) {
    const PolygonMesh c = prim::MakeCube(size);
    const std::string l = "cube size " + Num(size) + ": ";
    Ck(c.VertexCount() == 8 && c.EdgeCount() == 12 && c.FaceCount() == 6, l + "8 vertices, 12 edges, 6 faces");
    Ck(c.EulerCharacteristic() == 2, l + "Euler characteristic 2");
    Ck(c.IsClosedOrientedManifold(), l + "every edge is shared by two faces traversed in opposite directions");
    Ck(Near(c.Volume(), size * size * size, 1e-12), l + "volume = size^3 (" + Num(c.Volume()) + ")");
    Ck(Near(c.Area(), 6 * size * size, 1e-12), l + "area = 6 size^2");
    Ck(std::all_of(c.faces.begin(), c.faces.end(), [](const auto& f) { return f.size() == 4; }), l + "all faces are quads");
    const Mesh m = c.ToMesh();
    Ck(m.IsClosedManifold() && Near(m.Volume(), size * size * size, 1e-4), l + "the converted quad mesh is a closed manifold of the same volume");
  }
}

// bpy.ops.mesh.primitive_plane_add (D8-BLENDER-MESH-0103): one quad face of side `size`: 4 vertices, 4 edges, 1 face, area size^2, Euler 1.
D8_EXTRA_TEST(Rw3MeshBPlaneAddIsOneQuadOfAreaSizeSquared) {
  for (double size : {2.0, 0.75}) {
    const PolygonMesh p = prim::MakePlane(size);
    const std::string l = "plane size " + Num(size) + ": ";
    Ck(p.VertexCount() == 4 && p.EdgeCount() == 4 && p.FaceCount() == 1, l + "4 vertices, 4 edges, 1 face");
    Ck(p.EulerCharacteristic() == 1 && !p.IsClosedOrientedManifold(), l + "a disc: Euler characteristic 1, boundary edges present");
    Ck(Near(p.Area(), size * size, 1e-12), l + "area = size^2");
    const Vector3d n = p.FaceNormalTimesArea(p.faces[0]);
    Ck(Near(n.x, 0, 1e-12) && Near(n.y, 0, 1e-12) && Near(n.z, size * size, 1e-12), l + "the face normal is +Z");
  }
}

// bpy.ops.mesh.primitive_circle_add (D8-BLENDER-MESH-0095): vertices n; Nothing: n vertices, n edges, no face; NGon: one face; TriangleFan: a centre
// vertex, 2n edges (n rim + n spokes) and n triangles. The filled area is the regular n-gon area n r^2 sin(2 pi / n) / 2.
D8_EXTRA_TEST(Rw3MeshBCircleAddCountsFollowTheFillType) {
  for (int n : {3, 8, 32}) {
    const double r = 1.5;
    const std::string l = "circle n=" + std::to_string(n) + ": ";
    const PolygonMesh none = prim::MakeCircle(n, r, CapFill::Nothing);
    Ck(none.VertexCount() == n && none.EdgeCount() == n && none.FaceCount() == 0, l + "no fill: n vertices, n edges, no face");
    const PolygonMesh ng = prim::MakeCircle(n, r, CapFill::NGon);
    Ck(ng.VertexCount() == n && ng.EdgeCount() == n && ng.FaceCount() == 1 && ng.faces[0].size() == static_cast<size_t>(n), l + "n-gon fill: n vertices, n edges, 1 face of n corners");
    const PolygonMesh fan = prim::MakeCircle(n, r, CapFill::TriangleFan);
    Ck(fan.VertexCount() == n + 1 && fan.EdgeCount() == 2 * n && fan.FaceCount() == n, l + "triangle fan: n + 1 vertices, 2n edges, n faces");
    Ck(ng.EulerCharacteristic() == 1 && fan.EulerCharacteristic() == 1, l + "both filled circles are discs (Euler characteristic 1)");
    Ck(Near(ng.Area(), NGonArea(n, r), 1e-12) && Near(fan.Area(), NGonArea(n, r), 1e-12), l + "filled area = n r^2 sin(2 pi / n) / 2 for both fills");
    bool onring = true;
    for (int i = 0; i < n; ++i) onring = onring && Near(Vector3d(none.vertices[i]).Length(), r, 1e-12) && Near(none.vertices[i].z, 0.0, 0.0);
    Ck(onring, l + "every ring vertex lies at radius r in the XY plane");
  }
}

// bpy.ops.mesh.primitive_cylinder_add (D8-BLENDER-MESH-0099): n-gon caps: V = 2n, E = 3n, F = n + 2; triangle fans: V = 2n + 2, E = 5n, F = 3n; no caps:
// V = 2n, E = 3n, F = n. Euler 2 for the closed fills. Volume = n-gon area x depth; area = caps + n * (2 r sin(pi / n)) * depth.
D8_EXTRA_TEST(Rw3MeshBCylinderAddCountsVolumeAndAreaFollowTheNGonPrism) {
  for (int n : {3, 8, 32}) {
    const double r = 1.25, depth = 3.0;
    const std::string l = "cylinder n=" + std::to_string(n) + ": ";
    const PolygonMesh ng = prim::MakeCylinder(n, r, depth, CapFill::NGon);
    Ck(ng.VertexCount() == 2 * n && ng.EdgeCount() == 3 * n && ng.FaceCount() == n + 2 && ng.EulerCharacteristic() == 2, l + "n-gon caps: 2n / 3n / n + 2, Euler 2");
    Ck(ng.IsClosedOrientedManifold(), l + "n-gon caps: closed and consistently oriented");
    const double base = NGonArea(n, r);
    Ck(Near(ng.Volume(), base * depth, 1e-10), l + "volume = n-gon area x depth (" + Num(ng.Volume()) + " vs " + Num(base * depth) + ")");
    const double side = n * (2.0 * r * std::sin(kPi / n)) * depth;
    Ck(Near(ng.Area(), 2 * base + side, 1e-10), l + "area = 2 caps + n chord x depth");
    const PolygonMesh fan = prim::MakeCylinder(n, r, depth, CapFill::TriangleFan);
    Ck(fan.VertexCount() == 2 * n + 2 && fan.EdgeCount() == 5 * n && fan.FaceCount() == 3 * n && fan.EulerCharacteristic() == 2, l + "triangle-fan caps: 2n + 2 / 5n / 3n, Euler 2");
    Ck(fan.IsClosedOrientedManifold() && Near(fan.Volume(), base * depth, 1e-10), l + "triangle-fan caps: closed, same volume");
    const PolygonMesh open = prim::MakeCylinder(n, r, depth, CapFill::Nothing);
    Ck(open.VertexCount() == 2 * n && open.EdgeCount() == 3 * n && open.FaceCount() == n && open.EulerCharacteristic() == 0, l + "no caps: 2n / 3n / n, an annulus (Euler 0)");
    Ck(Near(open.Area(), side, 1e-10), l + "no caps: area = side wall");
  }
}

// bpy.ops.mesh.primitive_ico_sphere_add (D8-BLENDER-MESH-0101): subdivision s gives V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1) (s = 1 is the
// icosahedron: 12 / 30 / 20; s = 2, the default: 42 / 120 / 80), every vertex at the radius, Euler 2. The icosahedron of circumradius R has edge
// a = 4 R / sqrt(10 + 2 sqrt 5) and volume (5 / 12)(3 + sqrt 5) a^3.
D8_EXTRA_TEST(Rw3MeshBIcoSphereAddFollowsTheIcosahedronSubdivisionRecurrences) {
  const double R = 2.0;
  int pow4 = 1;
  double prev_vol = 0.0;
  for (int s = 1; s <= 4; ++s, pow4 *= 4) {
    const PolygonMesh m = prim::MakeIcoSphere(s, R);
    const std::string l = "ico sphere s=" + std::to_string(s) + ": ";
    Ck(m.VertexCount() == 10 * pow4 + 2 && m.EdgeCount() == 30 * pow4 && m.FaceCount() == 20 * pow4, l + "counts " + std::to_string(m.VertexCount()) + "/" + std::to_string(m.EdgeCount()) + "/" + std::to_string(m.FaceCount()));
    Ck(m.EulerCharacteristic() == 2 && m.IsClosedOrientedManifold(), l + "Euler 2, closed and oriented");
    double worst = 0.0;
    for (const auto& p : m.vertices) worst = std::max(worst, std::abs(Vector3d(p).Length() - R));
    Ck(worst < 1e-12, l + "every vertex lies on the sphere of radius R (deviation " + Num(worst) + ")");
    const double vol = m.Volume();
    const double ball = 4.0 / 3.0 * kPi * R * R * R;
    Ck(vol > 0.0 && vol < ball && vol > prev_vol, l + "volume " + Num(vol) + " is positive, below the ball " + Num(ball) + " and grows with the subdivision");
    prev_vol = vol;
    if (s == 1) {
      const double a = 4.0 * R / std::sqrt(10.0 + 2.0 * std::sqrt(5.0));
      Ck(Near(vol, 5.0 / 12.0 * (3.0 + std::sqrt(5.0)) * a * a * a, 1e-9), l + "icosahedron volume (5/12)(3 + sqrt 5) a^3 = " + Num(5.0 / 12.0 * (3.0 + std::sqrt(5.0)) * a * a * a));
      Ck(Near(m.Area(), 5.0 * std::sqrt(3.0) * a * a, 1e-9), l + "icosahedron area 5 sqrt(3) a^2");
    }
    if (s == 4) Ck(vol > 0.98 * ball, l + "four subdivisions fill 98 percent of the ball");
  }
}

// bpy.ops.mesh.primitive_torus_add (D8-BLENDER-MESH-0105): M x N vertices, M N quads, 2 M N edges, Euler characteristic 0, closed. Every vertex satisfies
// (sqrt(x^2 + y^2) - R)^2 + z^2 = r^2. The solid of revolution of the regular N-gon section has volume 2 pi R (N r^2 sin(2 pi / N) / 2) (Pappus); the faceted
// torus lies inside it, with the chord sagitta of the M-gon, so its volume is within the factor cos(pi / M)^2 below it (bounds 1 and cos^2).
D8_EXTRA_TEST(Rw3MeshBTorusAddCountsAndPappusVolumeBracket) {
  const int M = 48, N = 12;
  const double R = 1.0, r = 0.25;
  const PolygonMesh t = prim::MakeTorus(M, N, R, r);
  Ck(t.VertexCount() == M * N && t.FaceCount() == M * N && t.EdgeCount() == 2 * M * N, "torus 48 x 12: V = F = 576, E = 1152");
  Ck(t.EulerCharacteristic() == 0 && t.IsClosedOrientedManifold(), "torus: Euler characteristic 0, closed and oriented");
  double worst = 0.0;
  for (const auto& p : t.vertices) {
    const double rho = std::sqrt(p.x * p.x + p.y * p.y);
    worst = std::max(worst, std::abs((rho - R) * (rho - R) + p.z * p.z - r * r));
  }
  Ck(worst < 1e-12, "torus: every vertex satisfies (rho - R)^2 + z^2 = r^2 (residual " + Num(worst) + ")");
  const double pappus = 2.0 * kPi * R * NGonArea(N, r);
  const double c = std::cos(kPi / M);
  const double vol = t.Volume();
  Ck(vol <= pappus * (1.0 + 1e-12) && vol >= pappus * c * c, "torus: volume " + Num(vol) + " lies between Pappus x cos^2(pi/M) = " + Num(pappus * c * c) + " and Pappus " + Num(pappus));
  Ck(vol < 2.0 * kPi * kPi * R * r * r, "torus: below the smooth torus 2 pi^2 R r^2");
  const PolygonMesh small = prim::MakeTorus(3, 3, 2.0, 0.5);
  Ck(small.VertexCount() == 9 && small.FaceCount() == 9 && small.EdgeCount() == 18 && small.EulerCharacteristic() == 0, "torus 3 x 3: 9 / 18 / 9, Euler 0");
}

// ---- Vertex groups ---------------------------------------------------------------------------------------------------------------------------
// bpy.ops.object.vertex_group_assign (D8-BLENDER-OBJECT-0218) and vertex_group_remove_from (D8-BLENDER-OBJECT-0234): assigning writes exactly the given weight
// (replacing an earlier one), only to the listed vertices of the ACTIVE group; removing drops the membership (a member with weight 0 is still a member).
D8_EXTRA_TEST(Rw3MeshBVertexGroupAssignAndRemoveFromChangeOnlyTheListedMemberships) {
  VertexGroups vg(6);
  Ck(vg.AssignActive({0, 1}, 1.0) == 0, "no active group: assign does nothing");
  const int a = vg.AddGroup("A"), b = vg.AddGroup("B");
  Ck(a == 0 && b == 1 && vg.ActiveGroup() == 1, "the new group is the active one");
  Ck(vg.AssignActive({0, 2, 4}, 0.75) == 3, "assign returns the number of vertices written");
  Ck(vg.Contains(1, 2) && vg.Weight(1, 2) == 0.75 && !vg.Contains(1, 1) && !vg.Contains(0, 2), "only the listed vertices of the active group became members");
  vg.AssignActive({2}, 0.0);
  Ck(vg.Contains(1, 2) && vg.Weight(1, 2) == 0.0, "weight 0 replaces the old weight and the vertex stays a member");
  vg.SetActiveGroup(0);
  vg.AssignActive({0, 1}, 0.5);
  Ck(vg.RemoveFrom({0, 3}) == 1 && !vg.Contains(0, 0) && vg.Contains(0, 1) && vg.Contains(1, 0), "remove_from the active group only: vertex 0 left group A, group B untouched; vertex 3 was no member");
  Ck(vg.RemoveFrom({0, 1}, true) == 2, "use_all_groups removes every remaining membership of the listed vertices (B:0 and A:1 = 2)");
  Ck(vg.MemberCount(0) == 0 && vg.MemberCount(1) == 2, "group A is empty, group B keeps vertices 2 and 4");
  bool threw = false;
  try { vg.AssignActive({6}, 1.0); } catch (const std::out_of_range&) { threw = true; }
  Ck(threw, "a vertex index outside the mesh is refused");
}

// bpy.ops.object.vertex_group_set_active (D8-BLENDER-OBJECT-0236): the active group becomes the one with the given index; an index outside the list is refused and
// leaves the active group alone.
D8_EXTRA_TEST(Rw3MeshBVertexGroupSetActiveSelectsByIndexAndRefusesOutOfRange) {
  VertexGroups vg(2);
  Ck(vg.ActiveGroup() == -1 && !vg.SetActiveGroup(0), "no groups: no active group, index 0 refused");
  vg.AddGroup("a"); vg.AddGroup("b"); vg.AddGroup("c");
  Ck(vg.SetActiveGroup(0) && vg.ActiveGroup() == 0, "set_active(0)");
  Ck(!vg.SetActiveGroup(3) && !vg.SetActiveGroup(-1) && vg.ActiveGroup() == 0, "indices 3 and -1 are refused and the active group stays 0");
  vg.SetActiveGroup(2);
  vg.AssignActive({1}, 0.4);
  Ck(vg.Contains(2, 1) && !vg.Contains(0, 1), "a following assign goes to the new active group");
}

// bpy.ops.object.vertex_group_normalize (D8-BLENDER-OBJECT-0230): weights {0.2, 0.5, 0.1} divided by the largest (0.5) become {0.4, 1, 0.2}. Closed form per
// vertex for normalize_all: weights (0.2, 0.6) sum to 0.8, so they become (0.25, 0.75); with the first group locked, the second takes 1 - 0.2 = 0.8.
D8_EXTRA_TEST(Rw3MeshBVertexGroupNormalizeScalesByTheMaximumAndAllGroupsSumToOne) {
  VertexGroups vg(3);
  vg.AddGroup("g");
  vg.AssignActive({0}, 0.2); vg.AssignActive({1}, 0.5); vg.AssignActive({2}, 0.1);
  Ck(vg.NormalizeActive(), "normalize reports success");
  Ck(Near(vg.Weight(0, 0), 0.4, 1e-15) && Near(vg.Weight(0, 1), 1.0, 1e-15) && Near(vg.Weight(0, 2), 0.2, 1e-15), "{0.2, 0.5, 0.1} / 0.5 = {0.4, 1, 0.2}");
  Ck(Near(vg.Weight(0, 0) / vg.Weight(0, 2), 2.0, 1e-12), "the ratio between weights is unchanged (0.2 : 0.1 = 2)");
  VertexGroups empty(2);
  empty.AddGroup("e");
  Ck(!empty.NormalizeActive(), "an empty group has nothing to normalize");
  VertexGroups all(2);
  all.AddGroup("a"); all.AddGroup("b");
  all.SetActiveGroup(0); all.AssignActive({0}, 0.2);
  all.SetActiveGroup(1); all.AssignActive({0}, 0.6); all.AssignActive({1}, 3.0);
  VertexGroups locked = all;
  all.NormalizeAll(false);
  Ck(Near(all.Weight(0, 0), 0.25, 1e-15) && Near(all.Weight(1, 0), 0.75, 1e-15), "normalize_all: (0.2, 0.6) -> (0.25, 0.75)");
  Ck(Near(all.Weight(1, 1), 1.0, 1e-15), "normalize_all: a vertex in a single group gets weight 1");
  locked.SetActiveGroup(0);
  locked.NormalizeAll(true);
  Ck(Near(locked.Weight(0, 0), 0.2, 1e-15) && Near(locked.Weight(1, 0), 0.8, 1e-15), "normalize_all with the active group locked: it keeps 0.2, the other takes 0.8");
}

// bpy.ops.object.vertex_group_invert (D8-BLENDER-OBJECT-0224): w -> 1 - w. Members {0: 0.25, 1: 1.0}, vertices 2 and 3 not members. With auto_assign and
// auto_remove: vertex 0 -> 0.75, vertex 1 -> 0 and removed, vertices 2 and 3 -> 1. Without auto_assign they stay out; without auto_remove vertex 1 stays at 0.
// Inverting twice with both off restores the members exactly.
D8_EXTRA_TEST(Rw3MeshBVertexGroupInvertFlipsMembersAndHonoursAutoAssignAndRemove) {
  auto make = [] {
    VertexGroups v(4);
    v.AddGroup("g");
    v.AssignActive({0}, 0.25);
    v.AssignActive({1}, 1.0);
    return v;
  };
  VertexGroups a = make();
  a.InvertActive(true, true);
  Ck(a.Weight(0, 0) == 0.75 && !a.Contains(0, 1) && a.Weight(0, 2) == 1.0 && a.Weight(0, 3) == 1.0 && a.MemberCount(0) == 3, "auto_assign + auto_remove: {0: 0.75, 2: 1, 3: 1}, vertex 1 removed");
  VertexGroups b = make();
  b.InvertActive(false, false);
  Ck(b.Weight(0, 0) == 0.75 && b.Contains(0, 1) && b.Weight(0, 1) == 0.0 && !b.Contains(0, 2) && b.MemberCount(0) == 2, "no auto flags: only the members flip, vertex 1 stays a member with weight 0");
  VertexGroups c = make();
  c.InvertActive(false, false);
  c.InvertActive(false, false);
  Ck(c.Weight(0, 0) == 0.25 && c.Weight(0, 1) == 1.0 && c.MemberCount(0) == 2, "inverting twice restores the weights (1 - (1 - w) = w for these dyadic values)");
}

// bpy.ops.object.vertex_group_clean (D8-BLENDER-OBJECT-0220): memberships with weight <= limit are removed. Weights {0, 0.05, 0.1, 0.11, 0.9} with limit 0.1
// leave {0.11, 0.9}; limit 0 removes only the zero; keep_single keeps a vertex's only membership.
D8_EXTRA_TEST(Rw3MeshBVertexGroupCleanRemovesWeightsAtOrBelowTheLimit) {
  auto make = [] {
    VertexGroups v(5);
    v.AddGroup("g");
    const double w[5] = {0.0, 0.05, 0.1, 0.11, 0.9};
    for (int i = 0; i < 5; ++i) v.AssignActive({i}, w[i]);
    return v;
  };
  VertexGroups a = make();
  Ck(a.CleanActive(0.1) == 3 && a.MemberCount(0) == 2 && a.Contains(0, 3) && a.Contains(0, 4) && !a.Contains(0, 2), "limit 0.1 removes 0, 0.05 and 0.1 (<= limit), keeps 0.11 and 0.9");
  VertexGroups b = make();
  Ck(b.CleanActive(0.0) == 1 && !b.Contains(0, 0) && b.MemberCount(0) == 4, "limit 0 removes only the zero weight");
  VertexGroups c = make();
  Ck(c.CleanActive(0.1, true) == 0 && c.MemberCount(0) == 5, "keep_single: every vertex is in only this group, so nothing is removed");
  VertexGroups d(2);
  d.AddGroup("x"); d.AddGroup("y");
  d.AssignActive({0, 1}, 0.0);          // y: both zero
  d.SetActiveGroup(0);
  d.AssignActive({1}, 0.5);             // x: vertex 1 only
  d.SetActiveGroup(1);
  Ck(d.CleanActive(0.0, true) == 1 && d.Contains(1, 0) && !d.Contains(1, 1), "keep_single: vertex 0 keeps its only (zero) membership, vertex 1 has another group and loses it");
}

// bpy.ops.object.vertex_group_limit_total (D8-BLENDER-OBJECT-0226): each vertex keeps its `limit` largest weights. Vertex weights (g0..g3) = (0.1, 0.7, 0.3, 0.5)
// with limit 2 keep g1 (0.7) and g3 (0.5); a vertex with two groups is untouched by limit 2; limit 0 clears everything.
D8_EXTRA_TEST(Rw3MeshBVertexGroupLimitTotalKeepsTheLargestWeightsPerVertex) {
  VertexGroups vg(2);
  for (const char* n : {"g0", "g1", "g2", "g3"}) vg.AddGroup(n);
  const double w[4] = {0.1, 0.7, 0.3, 0.5};
  for (int g = 0; g < 4; ++g) { vg.SetActiveGroup(g); vg.AssignActive({0}, w[g]); }
  vg.SetActiveGroup(0); vg.AssignActive({1}, 0.2);
  vg.SetActiveGroup(1); vg.AssignActive({1}, 0.9);
  VertexGroups copy = vg;
  Ck(vg.LimitTotal(2) == 2, "two memberships removed (vertex 0 had four)");
  Ck(!vg.Contains(0, 0) && vg.Contains(1, 0) && !vg.Contains(2, 0) && vg.Contains(3, 0), "vertex 0 keeps g1 (0.7) and g3 (0.5)");
  Ck(vg.GroupsOfVertex(1) == 2 && vg.Weight(0, 1) == 0.2 && vg.Weight(1, 1) == 0.9, "vertex 1 (two groups) is untouched");
  Ck(Near(vg.Weight(1, 0), 0.7, 0) && Near(vg.Weight(3, 0), 0.5, 0), "the kept weights are not changed");
  Ck(copy.LimitTotal(0) == 6 && copy.GroupsOfVertex(0) == 0 && copy.GroupsOfVertex(1) == 0, "limit 0 removes all six memberships");
}

// bpy.ops.object.vertex_group_quantize (D8-BLENDER-OBJECT-0232): w -> round(w * steps) / steps. steps = 4: 0.37 -> 1.48 -> 1 -> 0.25; 0.38 -> 1.52 -> 2 -> 0.5;
// 0.9 -> 3.6 -> 4 -> 1; 0.1 -> 0.4 -> 0 -> 0 (still a member); steps = 2: 0.76 -> 1.52 -> 2 -> 1.
D8_EXTRA_TEST(Rw3MeshBVertexGroupQuantizeSnapsWeightsToTheStepGrid) {
  VertexGroups vg(4);
  vg.AddGroup("g");
  const double in[4] = {0.37, 0.38, 0.9, 0.1};
  for (int i = 0; i < 4; ++i) vg.AssignActive({i}, in[i]);
  VertexGroups two = vg;
  vg.QuantizeActive(4);
  Ck(vg.Weight(0, 0) == 0.25 && vg.Weight(0, 1) == 0.5 && vg.Weight(0, 2) == 1.0 && vg.Weight(0, 3) == 0.0, "steps 4: {0.37, 0.38, 0.9, 0.1} -> {0.25, 0.5, 1, 0}");
  Ck(vg.Contains(0, 3), "a weight that rounds to 0 stays a member");
  two.QuantizeActive(2);
  Ck(two.Weight(0, 0) == 0.5 && two.Weight(0, 1) == 0.5 && two.Weight(0, 2) == 1.0 && two.Weight(0, 3) == 0.0, "steps 2: {0.37, 0.38, 0.9, 0.1} -> {0.5, 0.5, 1, 0}");
}

// bpy.ops.object.vertex_group_sort (D8-BLENDER-OBJECT-0238): sorted by name, case-insensitively with digit runs compared as numbers: {"Leg.10", "bone", "Leg.2",
// "Arm"} -> Arm, bone, Leg.2, Leg.10. The weights and the active group follow their group.
D8_EXTRA_TEST(Rw3MeshBVertexGroupSortByNameIsCaseInsensitiveNaturalAndKeepsWeights) {
  VertexGroups vg(4);
  const char* names[4] = {"Leg.10", "bone", "Leg.2", "Arm"};
  for (int i = 0; i < 4; ++i) { vg.AddGroup(names[i]); vg.AssignActive({i}, 0.1 * (i + 1)); }
  vg.SetActiveGroup(2);  // "Leg.2"
  vg.SortByName();
  const char* want[4] = {"Arm", "bone", "Leg.2", "Leg.10"};
  bool ok = true;
  for (int i = 0; i < 4; ++i) ok = ok && vg.Name(i) == want[i];
  Ck(ok, "order Arm, bone, Leg.2, Leg.10");
  Ck(vg.Name(vg.ActiveGroup()) == "Leg.2", "the active group is still Leg.2");
  Ck(Near(vg.Weight(0, 3), 0.4, 1e-12) && Near(vg.Weight(1, 1), 0.2, 1e-12) && Near(vg.Weight(2, 2), 0.3, 1e-12) && Near(vg.Weight(3, 0), 0.1, 1e-12), "each group kept its own weights (Arm: v3 0.4, bone: v1 0.2, Leg.2: v2 0.3, Leg.10: v0 0.1)");
}

// bpy.ops.object.vertex_group_mirror (D8-BLENDER-OBJECT-0228): vertices at (+-1, 0, 0), (+-2, 1, 0) and (0, 0, 3) on the plane. The active group has weights
// {x=+1: 0.2, x=-1: 0.9, x=+2: 0.5 (partner without weight), the centre vertex 0.7}: after the mirror the two pairs are swapped (x=+1: 0.9, x=-1: 0.2;
// x=+2 loses its weight and x=-2 gets 0.5) and the centre vertex is unchanged. "Arm.L" is renamed "Arm.R" with flip_group_names.
D8_EXTRA_TEST(Rw3MeshBVertexGroupMirrorSwapsWeightsAcrossXAndFlipsTheName) {
  VertexGroups vg(6);
  const std::vector<Point3d> pos = {Point3d(1, 0, 0), Point3d(-1, 0, 0), Point3d(2, 1, 0), Point3d(-2, 1, 0), Point3d(0, 0, 3), Point3d(5, 5, 5)};
  vg.AddGroup("Arm.L");
  vg.AssignActive({0}, 0.2); vg.AssignActive({1}, 0.9); vg.AssignActive({2}, 0.5); vg.AssignActive({4}, 0.7); vg.AssignActive({5}, 0.3);
  const int pairs = vg.MirrorActiveX(pos, 1e-6, true);
  Ck(pairs == 2, "two vertex pairs were swapped");
  Ck(vg.Weight(0, 0) == 0.9 && vg.Weight(0, 1) == 0.2, "x = +-1: 0.2 and 0.9 swapped");
  Ck(!vg.Contains(0, 2) && vg.Weight(0, 3) == 0.5, "x = +-2: the single weight moved to the other side");
  Ck(vg.Weight(0, 4) == 0.7 && vg.Weight(0, 5) == 0.3, "the vertex on the plane and the vertex without a partner keep their weights");
  Ck(vg.Name(0) == "Arm.R", "flip_group_names renamed Arm.L to Arm.R");
  Ck(VertexGroups::FlipSideName("Arm_R") == "Arm_L" && VertexGroups::FlipSideName("hip-l") == "hip-r" && VertexGroups::FlipSideName("Spine") == "Spine", "the name flip handles _R, -l and leaves a neutral name alone");
}

// bpy.ops.object.vertex_weight_delete (D8-BLENDER-OBJECT-0241): deletes the weight of one vertex in one group; deleting a missing weight does nothing.
D8_EXTRA_TEST(Rw3MeshBVertexWeightDeleteRemovesOneMembershipOnly) {
  VertexGroups vg(3);
  vg.AddGroup("a"); vg.AddGroup("b");
  vg.SetActiveGroup(0); vg.AssignActive({0, 1}, 0.5);
  vg.SetActiveGroup(1); vg.AssignActive({0}, 0.25);
  Ck(vg.DeleteVertexWeight(0, 0) && !vg.Contains(0, 0), "vertex 0 left group a");
  Ck(vg.Contains(0, 1) && vg.Contains(1, 0) && vg.Weight(1, 0) == 0.25, "vertex 1 in a and vertex 0 in b are untouched");
  Ck(!vg.DeleteVertexWeight(0, 0) && !vg.DeleteVertexWeight(1, 2), "deleting a weight that does not exist reports false and changes nothing");
  Ck(vg.MemberCount(0) == 1 && vg.MemberCount(1) == 1, "member counts 1 and 1");
}

// mesh.select_ungrouped (D8-BLENDER-MESH-0141) and paint.vert_select_ungrouped (D8-BLENDER-PAINT-0036): the vertices that belong to no group, i.e. exactly the
// complement of the union of the group memberships (a member with weight 0 is grouped).
D8_EXTRA_TEST(Rw3MeshBSelectUngroupedIsTheComplementOfTheUnionOfMemberships) {
  VertexGroups none(5);
  Ck(none.UngroupedVertices().size() == 5, "no groups: every vertex is ungrouped");
  VertexGroups vg(6);
  vg.AddGroup("a"); vg.AddGroup("b");
  vg.SetActiveGroup(0); vg.AssignActive({0, 2}, 1.0);
  vg.SetActiveGroup(1); vg.AssignActive({2, 3}, 0.0);
  const std::vector<int> u = vg.UngroupedVertices();
  Ck((u == std::vector<int>{1, 4, 5}), "groups {0, 2} and {2, 3 (weight 0)} leave {1, 4, 5} ungrouped");
  vg.RemoveFrom({2}, true);
  Ck((vg.UngroupedVertices() == std::vector<int>{1, 2, 4, 5}), "after removing vertex 2 from all groups it is ungrouped too");
  VertexGroups big(100);
  big.AddGroup("g");
  std::vector<int> evens;
  for (int i = 0; i < 100; i += 2) evens.push_back(i);
  big.AssignActive(evens, 0.5);
  Ck(big.UngroupedVertices().size() == 50 && big.UngroupedVertices()[0] == 1, "100 vertices, the 50 even ones grouped: 50 ungrouped, the first is 1");
}

// ---- Sculpt ----------------------------------------------------------------------------------------------------------------------------------
// bpy.ops.sculpt.brush_stroke (D8-BLENDER-SCULPT-0001): one dab of the Draw brush on a flat grid raises it by strength * falloff(d). The volume swept by a radial
// profile is 2 pi R^2 * integral_0^1 f(t) (1 - t) dt: Smooth 3t^2 - 2t^3 gives 0.15 * 2 pi R^2 = 0.3 pi R^2, Linear pi R^2 / 3, Sharp pi R^2 / 6, Sphere (a
// hemisphere of radius R scaled to height 1) 2 pi R^2 / 3. The falloff curves also have their own closed-form properties: f(0 distance) = 1, f(radius) = 0,
// Smooth f(1/2) = 1/2 with zero slope at both ends, Linear f(1/2) = 1/2, Sharp f(1/2) = 1/4, Sphere f(1/2) = sqrt(3)/2.
D8_EXTRA_TEST(Rw3MeshBDrawBrushDabSweepsTheClosedFormVolumeOfItsFalloff) {
  using sculpt::Falloff;
  const double R = 2.0;
  Ck(sculpt::FalloffStrength(Falloff::Smooth, 0.0, R) == 1.0 && sculpt::FalloffStrength(Falloff::Smooth, R, R) == 0.0 && sculpt::FalloffStrength(Falloff::Smooth, 3.0, R) == 0.0, "smooth: 1 at the centre, 0 on and beyond the rim");
  Ck(Near(sculpt::FalloffStrength(Falloff::Smooth, R / 2, R), 0.5, 1e-15) && Near(sculpt::FalloffStrength(Falloff::Linear, R / 2, R), 0.5, 1e-15), "smooth and linear are 1/2 at half radius");
  Ck(Near(sculpt::FalloffStrength(Falloff::Sharp, R / 2, R), 0.25, 1e-15) && Near(sculpt::FalloffStrength(Falloff::Sphere, R / 2, R), std::sqrt(3.0) / 2.0, 1e-15), "sharp 1/4 and sphere sqrt(3)/2 at half radius");
  const double h = 1e-6;
  Ck(Near(sculpt::FalloffStrength(Falloff::Smooth, h * R, R), 1.0, 1e-9) && Near(sculpt::FalloffStrength(Falloff::Smooth, R * (1 - h), R), 0.0, 1e-11), "smooth has zero slope at both ends");
  bool mono = true;
  for (Falloff f : {Falloff::Smooth, Falloff::Sphere, Falloff::Root, Falloff::Sharp, Falloff::Linear}) {
    double prev = 2.0;
    for (int i = 0; i <= 40; ++i) {
      const double v = sculpt::FalloffStrength(f, R * i / 40.0, R);
      mono = mono && v <= prev + 1e-15 && v >= 0.0 && v <= 1.0;
      prev = v;
    }
  }
  Ck(mono, "every falloff is non-increasing from 1 to 0 over the radius");
  struct Case { Falloff f; double coeff; const char* name; };  // volume = coeff * pi R^2 * strength
  const Case cases[] = {{Falloff::Smooth, 0.3, "smooth"}, {Falloff::Linear, 1.0 / 3.0, "linear"}, {Falloff::Sharp, 1.0 / 6.0, "sharp"}, {Falloff::Sphere, 2.0 / 3.0, "sphere (hemisphere)"}};
  for (const Case& c : cases) {
    PolygonMesh g = Grid(160, 160, 0.05);  // 8 x 8 units, pitch 0.05
    const double strength = 1.5;
    const int moved = sculpt::DrawDab(g, Point3d(0, 0, 0), R, strength, c.f);
    double vol = 0.0;
    for (const auto& p : g.vertices) vol += p.z * 0.05 * 0.05;
    const double want = c.coeff * kPi * R * R * strength;
    Ck(std::abs(vol - want) <= 0.004 * want, std::string(c.name) + ": swept volume " + Num(vol) + " matches " + Num(want) + " within 0.4 percent (lattice sum)");
    Ck(moved > 0 && moved < g.VertexCount(), std::string(c.name) + ": only the vertices inside the brush moved");
    double outside = 0.0, ringspread = 0.0;
    double ring_min = 1e9, ring_max = -1e9;
    for (const auto& p : g.vertices) {
      const double d = std::sqrt(p.x * p.x + p.y * p.y);
      if (d >= R) outside = std::max(outside, std::abs(p.z));
      if (std::abs(d - 1.0) < 1e-9) { ring_min = std::min(ring_min, p.z); ring_max = std::max(ring_max, p.z); }
    }
    ringspread = ring_max - ring_min;
    Ck(outside == 0.0, std::string(c.name) + ": no vertex at or beyond the radius moved");
    Ck(ring_max > 0 && ringspread < 1e-12, std::string(c.name) + ": vertices at equal distance (the on-axis lattice points at distance 1) rise equally");
    Ck(std::abs(g.vertices[static_cast<size_t>(80 * 161 + 80)].z - strength) < 1e-12, std::string(c.name) + ": the centre vertex rises by exactly the strength");
  }
  // lattice count with the Constant falloff at pitch 1, R = 2 (strict inequality d < R): the 9 points (0,0), (+-1,0), (0,+-1), (+-1,+-1)
  PolygonMesh g = Grid(8, 8, 1.0);
  Ck(sculpt::DrawDab(g, Point3d(0, 0, 0), 2.0, 1.0, Falloff::Constant) == 9, "constant falloff, R = 2 on a unit lattice: 9 vertices inside");
  // a mask value m scales the move by 1 - m; a masked-out vertex does not move
  PolygonMesh g2 = Grid(4, 4, 1.0);
  std::vector<double> mask(static_cast<size_t>(g2.VertexCount()), 0.0);
  mask[12] = 1.0; mask[13] = 0.25;  // vertex 12 is the centre (index 2 * 5 + 2), 13 is its +x neighbour
  sculpt::DrawDab(g2, Point3d(0, 0, 0), 5.0, 2.0, Falloff::Constant, Vector3d(0, 0, 1), &mask);
  Ck(g2.vertices[12].z == 0.0 && Near(g2.vertices[13].z, 1.5, 1e-15) && Near(g2.vertices[11].z, 2.0, 1e-15), "mask 1 blocks the move, mask 0.25 leaves 0.75 of it (1.5), mask 0 the full 2.0");
  // with a zero direction the area normal of a +Z grid is +Z; a -Z grid moves down
  PolygonMesh g3 = Grid(2, 2, 1.0);
  sculpt::DrawDab(g3, Point3d(0, 0, 0), 5.0, 1.0, Falloff::Constant);
  Ck(Near(g3.vertices[4].z, 1.0, 1e-12), "the automatic direction is the surface normal (+Z)");
}

// bpy.ops.sculpt.mask_filter (D8-BLENDER-SCULPT-0019): on a square lattice one Grow step turns the mask of a single vertex into the diamond of vertices within
// k edges, which has 2 k^2 + 2 k + 1 points (5, 13, 25, 41 for k = 1..4); Shrink is the inverse: a diamond of radius R shrinks to radius R - k, and a diamond
// of radius k shrunk k times leaves a single vertex... which then disappears after one more shrink.
D8_EXTRA_TEST(Rw3MeshBMaskFilterGrowAndShrinkWalkTheEdgeGraphByDiamondCounts) {
  const PolygonMesh g = Grid(30, 30, 1.0);  // 31 x 31 vertices, centre (15, 15)
  const int centre = 15 * 31 + 15;
  std::vector<double> mask(static_cast<size_t>(g.VertexCount()), 0.0);
  mask[static_cast<size_t>(centre)] = 1.0;
  auto count = [](const std::vector<double>& m) { return static_cast<int>(std::count_if(m.begin(), m.end(), [](double v) { return v > 0.5; })); };
  for (int k = 1; k <= 4; ++k) {
    const std::vector<double> grown = sculpt::FilterMask(g, mask, sculpt::MaskFilter::Grow, k);
    Ck(count(grown) == 2 * k * k + 2 * k + 1, "grow x" + std::to_string(k) + ": " + std::to_string(count(grown)) + " vertices = 2k^2 + 2k + 1");
  }
  // iterating one step at a time equals one call with the same iteration count
  std::vector<double> step = mask;
  for (int i = 0; i < 3; ++i) step = sculpt::FilterMask(g, step, sculpt::MaskFilter::Grow, 1);
  Ck(step == sculpt::FilterMask(g, mask, sculpt::MaskFilter::Grow, 3), "grow x3 equals three single grows");
  const std::vector<double> d5 = sculpt::FilterMask(g, mask, sculpt::MaskFilter::Grow, 5);  // diamond of radius 5: 61 points
  Ck(count(d5) == 61, "grow x5: 61 vertices");
  for (int k = 1; k <= 5; ++k) {
    const std::vector<double> sh = sculpt::FilterMask(g, d5, sculpt::MaskFilter::Shrink, k);
    const int r = 5 - k;
    Ck(count(sh) == 2 * r * r + 2 * r + 1, "shrink x" + std::to_string(k) + " of the radius-5 diamond: radius " + std::to_string(r) + " has " + std::to_string(2 * r * r + 2 * r + 1) + " vertices");
  }
  Ck(count(sculpt::FilterMask(g, d5, sculpt::MaskFilter::Shrink, 6)) == 0, "shrink x6 removes the last vertex");
  // grey values: grow takes the maximum of the neighbours, shrink the minimum
  std::vector<double> grey(static_cast<size_t>(g.VertexCount()), 0.0);
  grey[static_cast<size_t>(centre)] = 0.6;
  grey[static_cast<size_t>(centre + 1)] = 0.3;
  const std::vector<double> gg = sculpt::FilterMask(g, grey, sculpt::MaskFilter::Grow, 1);
  Ck(gg[static_cast<size_t>(centre + 1)] == 0.6 && gg[static_cast<size_t>(centre - 1)] == 0.6 && gg[static_cast<size_t>(centre + 2)] == 0.3 && gg[static_cast<size_t>(centre + 32)] == 0.3 && gg[static_cast<size_t>(centre + 33)] == 0.0, "grow copies the larger neighbour value: 0.6 reaches the +x neighbour (which held 0.3), 0.3 reaches +2x and the vertex above +x, the vertex above +2x stays 0");
  const std::vector<double> gs = sculpt::FilterMask(g, grey, sculpt::MaskFilter::Shrink, 1);
  Ck(gs[static_cast<size_t>(centre)] == 0.0 && gs[static_cast<size_t>(centre + 1)] == 0.0, "shrink takes the smallest neighbour value: both grey vertices drop to 0");
  bool threw = false;
  try { sculpt::FilterMask(g, std::vector<double>(3, 0.0), sculpt::MaskFilter::Grow, 1); } catch (const std::invalid_argument&) { threw = true; }
  Ck(threw, "a mask of the wrong size is refused");
}

// bpy.ops.sculpt.expand (D8-BLENDER-SCULPT-0007): topology falloff selects the vertices within k edges of the start (diamond, 2 k^2 + 2 k + 1); topology with
// diagonals also crosses faces, selecting the (2 k + 1)^2 square; spherical falloff selects the vertices within a straight-line radius, i.e. the Gauss circle
// counts 5, 9, 13, 21, 25, 29, 37 for squared radius 1, 2, 4, 5, 8, 9, 10 on a unit lattice. A start vertex at the lattice border is clipped by the border.
D8_EXTRA_TEST(Rw3MeshBExpandSelectsDiamondSquareAndGaussCircleCounts) {
  const PolygonMesh g = Grid(20, 20, 1.0);  // 21 x 21, centre (10, 10)
  const int centre = 10 * 21 + 10;
  for (int k = 0; k <= 5; ++k) {
    Ck(static_cast<int>(sculpt::ExpandTopology(g, centre, k).size()) == 2 * k * k + 2 * k + 1, "topology, " + std::to_string(k) + " steps: 2k^2 + 2k + 1");
    Ck(static_cast<int>(sculpt::ExpandTopology(g, centre, k, true).size()) == (2 * k + 1) * (2 * k + 1), "topology with diagonals, " + std::to_string(k) + " steps: (2k + 1)^2");
  }
  const int sq[] = {1, 2, 4, 5, 8, 9, 10};
  const int want[] = {5, 9, 13, 21, 25, 29, 37};
  for (int i = 0; i < 7; ++i)
    Ck(static_cast<int>(sculpt::ExpandSphere(g, centre, std::sqrt(static_cast<double>(sq[i]))).size()) == want[i], "sphere of squared radius " + std::to_string(sq[i]) + ": " + std::to_string(want[i]) + " lattice points");
  Ck(sculpt::ExpandSphere(g, centre, 0.5).size() == 1, "radius 0.5: only the start");
  // corner start: the diamond of 2 edges keeps only the in-bounds quarter: (0,0), (1,0), (0,1), (2,0), (1,1), (0,2) = 6
  Ck(sculpt::ExpandTopology(g, 0, 2).size() == 6, "from a corner, 2 steps reach only 6 vertices (the quarter diamond 1 + 2 + 3)");
  const auto sel = sculpt::ExpandTopology(g, centre, 3);
  Ck(std::is_sorted(sel.begin(), sel.end()) && sel.front() == centre - 3 * 21 && sel.back() == centre + 3 * 21, "indices come back in ascending order, from the vertex 3 rows below to the one 3 rows above");
}

// bpy.ops.sculpt.set_pivot_position (D8-BLENDER-SCULPT-0031): Origin is (0, 0, 0); Active is the active vertex position; Unmasked is the mean position of the vertices
// that are not masked (binary mask: the plain mean). Grid 4 x 4 (5 x 5 vertices, x and y in -2..2) with the whole right half x >= 1 masked leaves x in {-2, -1, 0},
// mean x = -1, mean y = 0; masking everything is refused.
D8_EXTRA_TEST(Rw3MeshBPivotPositionOriginActiveAndUnmaskedAverage) {
  PolygonMesh g = Grid(4, 4, 1.0);
  for (auto& p : g.vertices) p.z = 3.0;
  std::vector<double> mask(static_cast<size_t>(g.VertexCount()), 0.0);
  for (int v = 0; v < g.VertexCount(); ++v)
    if (g.vertices[static_cast<size_t>(v)].x >= 1.0) mask[static_cast<size_t>(v)] = 1.0;
  const Point3d o = sculpt::PivotPosition(g, mask, sculpt::PivotMode::Origin);
  Ck(o.x == 0 && o.y == 0 && o.z == 0, "origin pivot");
  const Point3d a = sculpt::PivotPosition(g, mask, sculpt::PivotMode::Active, 7);
  Ck(a == g.vertices[7], "active pivot is the active vertex");
  const Point3d u = sculpt::PivotPosition(g, mask, sculpt::PivotMode::Unmasked);
  Ck(Near(u.x, -1.0, 1e-12) && Near(u.y, 0.0, 1e-12) && Near(u.z, 3.0, 1e-12), "unmasked average (-1, 0, 3) = " + Num(u.x) + ", " + Num(u.y) + ", " + Num(u.z));
  std::vector<double> none(mask.size(), 0.0);
  const Point3d all = sculpt::PivotPosition(g, none, sculpt::PivotMode::Unmasked);
  Ck(Near(all.x, 0.0, 1e-12) && Near(all.y, 0.0, 1e-12) && Near(all.z, 3.0, 1e-12), "nothing masked: the centroid of the grid (0, 0, 3)");
  std::vector<double> full(mask.size(), 1.0);
  bool threw = false;
  try { sculpt::PivotPosition(g, full, sculpt::PivotMode::Unmasked); } catch (const std::invalid_argument&) { threw = true; }
  Ck(threw, "a fully masked mesh has no unmasked average and is refused");
}

// ---- UV --------------------------------------------------------------------------------------------------------------------------------------
// bpy.ops.uv.sphere_project (D8-BLENDER-UV-0052): the spherical projection of an octahedron with vertices on the axes at radius 2 around the origin: the four
// equator vertices have v = 1/2 and consecutive quarter turns in azimuth differ by exactly 1/4 in u (mod 1); the pole +z has v = 1, -z has v = 0; opposite
// equator vertices differ by half a turn. On a generic ico sphere of radius 3, v = 1/2 + asin(z / 3) / pi (the latitude) at every vertex and
// u(P) - u(Q) = (azimuth(P) - azimuth(Q)) / (2 pi) modulo 1 for every vertex P against a reference vertex Q. (Where u = 0 lies is a convention of this
// mapping, not tested; Blender's own seam position is not compared.)
D8_EXTRA_TEST(Rw3MeshBSphereProjectUVsMatchAzimuthAndLatitudeOfKnownVertices) {
  auto wrap = [](double d) { d -= std::floor(d); return std::min(d, 1.0 - d); };  // distance to 0 on the circle of circumference 1
  Mesh m;
  const double r = 2.0;
  const double pts[6][3] = {{r, 0, 0}, {0, r, 0}, {-r, 0, 0}, {0, -r, 0}, {0, 0, r}, {0, 0, -r}};
  for (const auto& p : pts) m.raw().m_V.Append(ON_3fPoint(float(p[0]), float(p[1]), float(p[2])));
  AddTri(m, 0, 1, 4); AddTri(m, 1, 2, 4); AddTri(m, 2, 3, 4); AddTri(m, 3, 0, 4);
  AddTri(m, 1, 0, 5); AddTri(m, 2, 1, 5); AddTri(m, 3, 2, 5); AddTri(m, 0, 3, 5);
  const std::vector<Point2d> uv = m.ComputeSphericalMappingUVs();
  Ck(uv.size() == 6, "one UV per vertex");
  bool eq = true;
  for (int i = 0; i < 4; ++i) eq = eq && Near(uv[static_cast<size_t>(i)].y, 0.5, 1e-6) && wrap(uv[static_cast<size_t>(i)].x - uv[0].x - 0.25 * i) < 1e-6;
  Ck(eq, "equator vertices: v = 1/2 and u steps by 1/4 per quarter turn");
  Ck(Near(uv[4].y, 1.0, 1e-6) && Near(uv[5].y, 0.0, 1e-6), "poles: +z has v = 1, -z has v = 0");
  Ck(wrap(uv[2].x - uv[0].x - 0.5) < 1e-6 && wrap(uv[3].x - uv[1].x - 0.5) < 1e-6, "opposite equator vertices differ by half a turn in u");
  const PolygonMesh ico = prim::MakeIcoSphere(2, 3.0);
  const std::vector<Point2d> iuv = ico.ToMesh().ComputeSphericalMappingUVs();
  auto az = [&](int i) { return std::atan2(ico.vertices[static_cast<size_t>(i)].y, ico.vertices[static_cast<size_t>(i)].x) / (2.0 * kPi); };
  double worst_v = 0.0, worst_u = 0.0;
  for (int i = 0; i < ico.VertexCount(); ++i) {
    worst_v = std::max(worst_v, std::abs(0.5 + std::asin(ico.vertices[static_cast<size_t>(i)].z / 3.0) / kPi - iuv[static_cast<size_t>(i)].y));
    worst_u = std::max(worst_u, wrap((iuv[static_cast<size_t>(i)].x - iuv[0].x) - (az(i) - az(0))));
  }
  Ck(worst_v < 2e-6, "ico sphere: v = 1/2 + latitude / pi at every vertex (worst " + Num(worst_v) + ")");
  Ck(worst_u < 2e-6, "ico sphere: u differences equal azimuth differences / 2 pi (worst " + Num(worst_u) + ")");
}

// bpy.ops.uv.cylinder_project (D8-BLENDER-UV-0010): a prism with n = 8 sides, radius 1.5, depth 3 (z from -1.5 to 1.5): ring vertex i sits at azimuth 2 pi i / 8,
// so u(i) - u(0) = i / 8 (mod 1); v = (z - z_min) / scale: 0 on the bottom ring and 3 / scale on the top ring. u lies in [0, 1] (the seam vertex may take
// either end). Where u = 0 lies is a convention of this mapping, not tested.
D8_EXTRA_TEST(Rw3MeshBCylinderProjectUVsGiveAzimuthFractionAndHeightOverScale) {
  const int n = 8;
  const PolygonMesh cyl = prim::MakeCylinder(n, 1.5, 3.0, CapFill::NGon);
  const Mesh m = cyl.ToMesh();
  auto wrap = [](double d) { d -= std::floor(d); return std::min(d, 1.0 - d); };
  for (double scale : {1.0, 2.0}) {
    const std::vector<Point2d> uv = m.ComputeCylindricalMappingUVs(scale);
    double worst = 0.0;
    bool in_range = true;
    for (int ring = 0; ring < 2; ++ring)
      for (int i = 0; i < n; ++i) {
        const Point2d& q = uv[static_cast<size_t>(ring * n + i)];
        in_range = in_range && q.x >= 0.0 && q.x <= 1.0;
        worst = std::max({worst, wrap(q.x - uv[0].x - double(i) / n), std::abs(q.y - (ring ? 3.0 / scale : 0.0))});
      }
    Ck(worst < 1e-6, "scale " + Num(scale) + ": u steps by 1/8 per side and v = 0 / 3 / scale on the two rings (worst " + Num(worst) + ")");
    Ck(in_range, "scale " + Num(scale) + ": u stays in [0, 1]");
  }
}

// bpy.ops.uv.unwrap (D8-BLENDER-UV-0054), conformal method: a flat regular hexagon fan (centre + 6 rim vertices, 6 triangles, edge length 1) in a tilted plane
// unwraps to itself: all 12 edges keep length 1 (the pinned pair is the opposite rim vertices, 2 apart, so the scale is exactly 1), every triangle has the
// equilateral area sqrt(3) / 4 and the total is 3 sqrt(3) / 2. A cylinder strip of r = 1, h = 2 over 180 degrees in 12 chord facets unrolls to a rectangle of
// width 12 * 2 sin(pi / 24) and height 2: the corner-to-corner UV distances are w, h and sqrt(w^2 + h^2).
D8_EXTRA_TEST(Rw3MeshBConformalUnwrapKeepsAHexagonFanAndUnrollsACylinderStrip) {
  {
    Mesh m;
    const double c = std::cos(kPi / 6.0), s = std::sin(kPi / 6.0);  // plane tilted 30 degrees about x, offset
    auto put = [&](double x, double y) { m.raw().m_V.Append(ON_3fPoint(float(4.0 + x), float(-1.0 + y * c), float(2.0 + y * s))); };
    put(0, 0);
    for (int i = 0; i < 6; ++i) put(std::cos(kPi / 3.0 * i), std::sin(kPi / 3.0 * i));
    for (int i = 0; i < 6; ++i) AddTri(m, 0, 1 + i, 1 + (i + 1) % 6);
    const std::vector<Point2d> uv = dino8::kernel::ComputeLscmUVs(m);
    double worst_edge = 0.0, worst_area = 0.0, total = 0.0;
    for (int i = 0; i < 6; ++i) {
      const int a = 1 + i, b = 1 + (i + 1) % 6;
      worst_edge = std::max({worst_edge, std::abs(Dist(uv[0], uv[a]) - 1.0), std::abs(Dist(uv[a], uv[b]) - 1.0)});
      const double area = 0.5 * ((uv[a].x - uv[0].x) * (uv[b].y - uv[0].y) - (uv[b].x - uv[0].x) * (uv[a].y - uv[0].y));
      worst_area = std::max(worst_area, std::abs(area - std::sqrt(3.0) / 4.0));
      total += area;
    }
    Ck(worst_edge < 2e-6, "hexagon fan: every edge keeps length 1 (worst deviation " + Num(worst_edge) + ")");
    Ck(worst_area < 2e-6 && Near(total, 3.0 * std::sqrt(3.0) / 2.0, 1e-5), "hexagon fan: triangles keep their area sqrt(3)/4 and the same winding (total " + Num(total) + ")");
  }
  {
    const int N = 12;
    Mesh m;
    for (int row = 0; row < 2; ++row)
      for (int i = 0; i <= N; ++i) {
        const double th = kPi * i / N;
        m.raw().m_V.Append(ON_3fPoint(float(std::cos(th)), float(std::sin(th)), float(2.0 * row)));
      }
    for (int i = 0; i < N; ++i) AddQuad(m, i, i + 1, N + 1 + i + 1, N + 1 + i);
    const std::vector<Point2d> uv = dino8::kernel::ComputeLscmUVs(m);
    // The unwrap is a similarity of the unrolled strip (one global scale s, fixed by pinning the two farthest boundary vertices at their 3D distance), so only
    // ratios of distances are compared: width : height = w : 2 and diagonal : height = sqrt(w^2 + 4) : 2.
    const double w = N * 2.0 * std::sin(kPi / (2.0 * N)), h = 2.0;
    const double uh = Dist(uv[0], uv[N + 1]);
    Ck(uh > 0.0, "strip: the end rule has a positive UV length");
    Ck(Near(Dist(uv[0], uv[N]) / uh, w / h, 1e-5), "strip: bottom edge : end rule = w : h = 24 sin(pi / 24) : 2 = " + Num(w / h) + " (got " + Num(Dist(uv[0], uv[N]) / uh) + ")");
    Ck(Near(Dist(uv[0], uv[2 * N + 1]) / uh, std::sqrt(w * w + h * h) / h, 1e-5), "strip: diagonal : end rule = sqrt(w^2 + h^2) : h");
    Ck(Near(Dist(uv[N], uv[N + 1]) / uh, std::sqrt(w * w + h * h) / h, 1e-5), "strip: the other diagonal has the same ratio (the unrolled strip is a rectangle)");
    Ck(Near(Dist(uv[N + 1], uv[2 * N + 1]) / uh, w / h, 1e-5) && Near(Dist(uv[N], uv[2 * N + 1]) / uh, 1.0, 1e-5), "strip: top edge : end rule = w : h and the far end rule has the same length as the near one");
  }
}
