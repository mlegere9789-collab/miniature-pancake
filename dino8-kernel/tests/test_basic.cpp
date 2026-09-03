// Minimal assert-based smoke tests for chunk 1's exit criteria. Not pulling
// in a test framework dependency for four checks.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/file_io.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++g_failures;
  } else {
    std::printf("ok: %s\n", what);
  }
}

void TestCurveDegreeElevation() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  std::vector<Point3d> pts = {
      Point3d(0, 0, 0),
      Point3d(1, 2, 0),
      Point3d(2, 0, 0),
      Point3d(3, 2, 0),
  };
  NurbsCurve curve = NurbsCurve::FromControlPoints(pts, /*degree=*/3);
  Check(curve.Degree() == 3, "curve constructed at requested degree");

  const auto result = curve.ElevateDegree(5);
  Check(result == dino8::kernel::Result::Ok, "degree elevation succeeded");
  Check(curve.Degree() == 5, "curve degree increased to 5");
}

void TestSurfaceDegreeElevation() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  std::vector<Point3d> grid;
  for (int u = 0; u < 4; ++u) {
    for (int v = 0; v < 4; ++v) {
      grid.emplace_back(u, v, 0);
    }
  }
  NurbsSurface surf =
      NurbsSurface::FromControlGrid(grid, /*u_count=*/4, /*v_count=*/4,
                                     /*u_degree=*/3, /*v_degree=*/3);
  Check(surf.DegreeU() == 3 && surf.DegreeV() == 3,
        "surface constructed at requested degree");

  const auto result = surf.ElevateDegree(/*direction=*/0, /*new_degree=*/4);
  Check(result == dino8::kernel::Result::Ok, "surface U-degree elevation succeeded");
  Check(surf.DegreeU() == 4, "surface U degree increased to 4");
}

void TestFileRoundTrip() {
  using dino8::kernel::Brep;
  using dino8::kernel::Model;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  std::vector<Point3d> grid;
  for (int u = 0; u < 3; ++u) {
    for (int v = 0; v < 3; ++v) {
      grid.emplace_back(u, v, (u == 1 && v == 1) ? 1.0 : 0.0);
    }
  }
  NurbsSurface surf = NurbsSurface::FromControlGrid(grid, 3, 3, 2, 2);
  Brep brep = Brep::FromSurface(surf);
  Check(brep.FaceCount() == 1, "brep has one face");

  Model model;
  model.AddBrep(brep);
  Check(model.ObjectCount() == 1, "model has one object before save");

  const std::string path = "dino8_kernel_roundtrip_test.3dm";
  const auto save_result = model.Save(path);
  Check(save_result == dino8::kernel::Result::Ok, ".3dm save succeeded");

  Model loaded;
  const auto load_result = Model::Load(path, loaded);
  Check(load_result == dino8::kernel::Result::Ok, ".3dm load succeeded");
  Check(loaded.ObjectCount() == 1, "round-tripped model has one object");

  std::remove(path.c_str());
}

// Builds a closed, consistently-oriented (CCW from outside) axis-aligned
// box mesh directly - not via NurbsSurface::TessellateGrid, since that
// only tessellates a single open surface, not a closed solid. Booleans
// need actual watertight input.
dino8::kernel::Mesh MakeBox(double x0, double y0, double z0, double x1,
                             double y1, double z1) {
  dino8::kernel::Mesh mesh;
  ON_Mesh& raw = mesh.raw();

  raw.m_V.Append(ON_3fPoint(x0, y0, z0));  // 0
  raw.m_V.Append(ON_3fPoint(x1, y0, z0));  // 1
  raw.m_V.Append(ON_3fPoint(x1, y1, z0));  // 2
  raw.m_V.Append(ON_3fPoint(x0, y1, z0));  // 3
  raw.m_V.Append(ON_3fPoint(x0, y0, z1));  // 4
  raw.m_V.Append(ON_3fPoint(x1, y0, z1));  // 5
  raw.m_V.Append(ON_3fPoint(x1, y1, z1));  // 6
  raw.m_V.Append(ON_3fPoint(x0, y1, z1));  // 7

  auto add_tri = [&raw](int a, int b, int c) {
    ON_MeshFace face;
    face.vi[0] = a;
    face.vi[1] = b;
    face.vi[2] = c;
    face.vi[3] = c;
    raw.m_F.Append(face);
  };

  add_tri(0, 3, 2);
  add_tri(0, 2, 1);  // bottom (-z)
  add_tri(4, 5, 6);
  add_tri(4, 6, 7);  // top (+z)
  add_tri(0, 1, 5);
  add_tri(0, 5, 4);  // front (-y)
  add_tri(3, 7, 6);
  add_tri(3, 6, 2);  // back (+y)
  add_tri(0, 4, 7);
  add_tri(0, 7, 3);  // left (-x)
  add_tri(1, 2, 6);
  add_tri(1, 6, 5);  // right (+x)

  return mesh;
}

void TestBoxVolume() {
  const auto box = MakeBox(0, 0, 0, 2, 2, 2);
  Check(std::abs(box.Volume() - 8.0) < 1e-9, "unit-scaled box volume is correct");
}

void TestBooleanUnion() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;

  const auto a = MakeBox(0, 0, 0, 2, 2, 2);   // volume 8
  const auto b = MakeBox(1, 1, 1, 3, 3, 3);   // volume 8, overlaps a in [1,2]^3 (volume 1)

  const auto result = BooleanCombine(a, b, BooleanOp::Union);
  Check(std::abs(result.Volume() - 15.0) < 1e-6,
        "union volume equals 8 + 8 - 1 overlap");
}

void TestBooleanIntersection() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;

  const auto a = MakeBox(0, 0, 0, 2, 2, 2);
  const auto b = MakeBox(1, 1, 1, 3, 3, 3);

  const auto result = BooleanCombine(a, b, BooleanOp::Intersection);
  Check(std::abs(result.Volume() - 1.0) < 1e-6,
        "intersection volume equals the 1x1x1 overlap");
}

void TestBooleanDifference() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;

  const auto a = MakeBox(0, 0, 0, 2, 2, 2);
  const auto b = MakeBox(1, 1, 1, 3, 3, 3);

  const auto result = BooleanCombine(a, b, BooleanOp::Difference);
  Check(std::abs(result.Volume() - 7.0) < 1e-6,
        "difference volume equals 8 - 1 overlap");
}

void TestBrepTessellation() {
  using dino8::kernel::Brep;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;

  std::vector<Point3d> grid;
  for (int u = 0; u < 4; ++u) {
    for (int v = 0; v < 4; ++v) {
      grid.emplace_back(u, v, 0.0);
    }
  }
  NurbsSurface surf = NurbsSurface::FromControlGrid(grid, 4, 4, 3, 3);
  Brep brep = Brep::FromSurface(surf);

  const auto meshes = brep.Tessellate(/*u_divisions=*/4, /*v_divisions=*/4);
  Check(meshes.size() == 1, "tessellation produced one mesh per face");
  Check(!meshes.empty() && meshes.front().VertexCount() == 5 * 5,
        "tessellated mesh has the expected (divisions+1)^2 vertex count");
  Check(!meshes.empty() && meshes.front().FaceCount() == 4 * 4 * 2,
        "tessellated mesh has the expected 2 triangles per grid cell");
}

void TestBrepBoxIsClosedAndWatertight() {
  using dino8::kernel::Brep;

  const Brep box = Brep::Box(0, 0, 0, 2, 2, 2);
  Check(box.FaceCount() == 6, "Brep::Box has six faces");

  // u_divisions = v_divisions = 1 means each face is exactly its 2
  // corner-to-corner triangles (no interior subdivision), so welding
  // should collapse the 6 faces * 4 corners = 24 raw vertices down to
  // exactly the box's 8 unique corners.
  const auto mesh = box.TessellateToClosedMesh(/*u_divisions=*/1, /*v_divisions=*/1);
  Check(mesh.VertexCount() == 8,
        "welding a tessellated Brep::Box collapses shared-edge vertices to 8 corners");
  Check(mesh.FaceCount() == 12, "welded box mesh has 12 triangles (2 per face x 6 faces)");
  Check(std::abs(mesh.Volume() - 8.0) < 1e-6,
        "Brep::Box -> Tessellate -> weld volume matches the box's true volume");
}

void TestBrepBooleanEndToEnd() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;

  // Same scenario as TestBooleanUnion/Intersection/Difference above, but
  // built through the real Brep -> Tessellate -> weld pipeline instead
  // of MakeBox()'s hand-authored mesh - this is the gap the previous
  // chunk's README flagged: "Brep only constructs untrimmed, open
  // surfaces... can't feed BooleanCombine() yet." Box() + TessellateToClosedMesh()
  // close it.
  const auto a = Brep::Box(0, 0, 0, 2, 2, 2).TessellateToClosedMesh(1, 1);
  const auto b = Brep::Box(1, 1, 1, 3, 3, 3).TessellateToClosedMesh(1, 1);

  const auto union_result = BooleanCombine(a, b, BooleanOp::Union);
  Check(std::abs(union_result.Volume() - 15.0) < 1e-6,
        "Brep-built union volume equals 8 + 8 - 1 overlap");

  const auto intersection_result = BooleanCombine(a, b, BooleanOp::Intersection);
  Check(std::abs(intersection_result.Volume() - 1.0) < 1e-6,
        "Brep-built intersection volume equals the 1x1x1 overlap");

  const auto difference_result = BooleanCombine(a, b, BooleanOp::Difference);
  Check(std::abs(difference_result.Volume() - 7.0) < 1e-6,
        "Brep-built difference volume equals 8 - 1 overlap");
}

void TestBrepSphereIsClosedAndWatertight() {
  using dino8::kernel::Brep;
  using dino8::kernel::Point3d;

  const double radius = 2.0;
  const Brep sphere = Brep::Sphere(Point3d(0, 0, 0), radius);
  Check(sphere.FaceCount() == 1, "Brep::Sphere is a single curved face");

  // A genuinely curved case, unlike Box(): the sphere's own u-seam
  // (u=0 and u=2*pi are the same meridian) and its two poles (every u
  // value at v_min/v_max collapses to one physical point) both have to
  // be welded shut against *themselves*, not just against a neighboring
  // face - exactly what the previous chunk's README flagged as
  // unvalidated ("not yet validated against curved surfaces").
  const int divisions = 32;
  const auto mesh = sphere.TessellateToClosedMesh(divisions, divisions);

  const int raw_vertex_count = (divisions + 1) * (divisions + 1);
  Check(mesh.VertexCount() < raw_vertex_count,
        "welding the sphere's own seam and poles reduces its vertex count");

  const double exact_volume = (4.0 / 3.0) * M_PI * radius * radius * radius;
  const double relative_error = std::abs(mesh.Volume() - exact_volume) / exact_volume;
  Check(relative_error < 0.01,
        "tessellated+welded sphere volume is within 1% of the exact 4/3*pi*r^3");
}

void TestBrepSphereBooleanEndToEnd() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Point3d;

  // Proves the weld pipeline's output is actually usable by Manifold, not
  // just internally self-consistent: two overlapping spheres, same radius,
  // centers offset by the radius along X. Exact spherical-cap overlap
  // volume for two radius-r spheres with center distance d = r is a closed
  // form (from the standard sphere-sphere intersection formula), so this
  // checks against real geometry, not just "didn't crash."
  const double r = 2.0;
  const double d = r;
  const auto a = Brep::Sphere(Point3d(0, 0, 0), r).TessellateToClosedMesh(32, 32);
  const auto b = Brep::Sphere(Point3d(d, 0, 0), r).TessellateToClosedMesh(32, 32);

  // Standard two-equal-sphere lens-volume formula:
  // V = (pi * (4r + d) * (2r - d)^2) / 12
  const double exact_lens = (M_PI * (4 * r + d) * (2 * r - d) * (2 * r - d)) / 12.0;

  const auto intersection_result = BooleanCombine(a, b, BooleanOp::Intersection);
  const double relative_error =
      std::abs(intersection_result.Volume() - exact_lens) / exact_lens;
  Check(relative_error < 0.03,
        "sphere-sphere boolean intersection volume is within 3% of the exact lens formula");
}

void TestBrepTrimmedPlanarFace() {
  using dino8::kernel::Brep;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;

  // A 10x10 physical square, built the same bilinear way Box()'s faces
  // are (FromControlGrid always gives a [0,1]x[0,1] parameter domain),
  // trimmed to the inner square [0.15,0.85]^2 in UV. That boundary is
  // deliberately off the grid lines (grid lines land on multiples of
  // 0.1) so no grid point sits exactly on the trim edge - point-in-polygon
  // is well-defined here, not dependent on floating-point tie-breaking at
  // a boundary.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 10, 0),
      Point3d(10, 0, 0),
      Point3d(10, 10, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, /*u_count=*/2, /*v_count=*/2,
                                     /*u_degree=*/1, /*v_degree=*/1);

  const std::vector<Point2d> trim_loop = {
      Point2d(0.15, 0.15),
      Point2d(0.85, 0.15),
      Point2d(0.85, 0.85),
      Point2d(0.15, 0.85),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, trim_loop);
  Check(face.FaceCount() == 1, "TrimmedPlanarFace is a single face");

  // Grid points strictly inside (0.15, 0.85) at divisions=10 are
  // u,v in {0.2, 0.3, ..., 0.8} - 7 values per axis, so 7x7=49 vertices
  // and a 6x6 grid of fully-inside cells (12 divisions -> 72 triangles),
  // hand-derived, not measured after the fact.
  const auto meshes = face.Tessellate(/*u_divisions=*/10, /*v_divisions=*/10);
  Check(meshes.size() == 1, "trimmed face tessellates to one mesh");
  Check(meshes.front().VertexCount() == 49,
        "trimming excludes vertices outside the trim loop, keeping exactly the interior grid");
  Check(meshes.front().FaceCount() == 72,
        "trimming keeps exactly the fully-inside grid cells (6x6x2 triangles)");

  // Physical area: the bilinear map scales the unit param square to a
  // 10x10 physical one uniformly, so trimmed param area 0.6x0.6=0.36
  // maps to physical area 0.36*100=36 exactly.
  Check(std::abs(meshes.front().Area() - 36.0) < 1e-9,
        "trimmed face's physical area matches the exact scaled trim-loop area");
}

void TestWeldAcrossIndependentlyParameterizedSurfaces() {
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;
  using dino8::kernel::Result;

  // Every weld test so far (Box, Sphere) welds vertices that come from
  // literally the same double-precision Point3d values, or from one
  // surface's own self-intersection with itself - not the general case
  // this README flags as still open: two *independently constructed*
  // surfaces whose shared boundary is only geometrically coincident, not
  // parametrically identical. This gets closer: two adjacent unit
  // squares sharing the edge x=1, built as separate single-face Breps
  // (so Tessellate() and MergeAndWeld() see them exactly as if they'd
  // come from unrelated parts of a model), where the second square's
  // surface is degree-elevated (bilinear -> bicubic) after construction.
  // Degree elevation is mathematically shape-preserving but re-derives
  // the control points/knot vector through real floating-point
  // arithmetic, so evaluating its shared edge no longer goes through the
  // same computation path as the first square's - a real, not
  // artificial, test of whether MergeAndWeld's tolerance is doing its
  // job rather than merely matching identical bit patterns.
  const std::vector<Point3d> grid_a = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const Brep face_a = Brep::FromSurface(
      NurbsSurface::FromControlGrid(grid_a, 2, 2, /*u_degree=*/1, /*v_degree=*/1));

  const std::vector<Point3d> grid_b = {
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
      Point3d(2, 0, 0),
      Point3d(2, 1, 0),
  };
  NurbsSurface surface_b =
      NurbsSurface::FromControlGrid(grid_b, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const Result elevate_result = surface_b.ElevateDegree(/*direction=*/0, /*new_degree=*/3);
  Check(elevate_result == Result::Ok, "surface B's degree elevation succeeded");
  const Brep face_b = Brep::FromSurface(surface_b);

  auto meshes_a = face_a.Tessellate(/*u_divisions=*/4, /*v_divisions=*/4);
  auto meshes_b = face_b.Tessellate(/*u_divisions=*/4, /*v_divisions=*/4);
  std::vector<Mesh> combined;
  combined.insert(combined.end(), meshes_a.begin(), meshes_a.end());
  combined.insert(combined.end(), meshes_b.begin(), meshes_b.end());

  const auto welded = Mesh::MergeAndWeld(combined);

  // Each face's own 4x4 grid has 25 vertices; the shared edge (5 points)
  // is duplicated between them before welding (50 raw), so a correct
  // weld collapses exactly those 5 shared points, leaving 45.
  Check(welded.VertexCount() == 45,
        "welding two independently-parameterized adjacent faces "
        "(one degree-elevated after construction) collapses exactly the shared edge");
  Check(std::abs(welded.Area() - 2.0) < 1e-9,
        "welded two-square area is exactly 2.0 despite the degree elevation");
}

void TestExtrudeUntrimmedFaceIntoSolid() {
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // A 2x2 square in the z=0 plane, built the same way TrimmedPlanarFace's
  // test built its base face - u_dir x v_dir gives an outward +Z normal
  // (see the corner-order derivation comment in Brep::Box).
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 2, 0),
      Point3d(2, 0, 0),
      Point3d(2, 2, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const Brep face = Brep::FromSurface(surface);
  const auto cap = face.Tessellate(/*u_divisions=*/3, /*v_divisions=*/3).front();

  // Extrude downward (into -Z, away from the cap's own +Z normal) by 3 -
  // the solid should occupy z in [-3, 0], volume 2*2*3 = 12.
  const auto solid = Mesh::ExtrudeCappedSolid(cap, Vector3d(0, 0, -3));

  Check(solid.VertexCount() == 2 * cap.VertexCount(),
        "extrusion doubles the cap's vertex count exactly (no welding needed - "
        "near/far ends and walls all reuse the cap's own vertex positions)");
  Check(std::abs(solid.Volume() - 12.0) < 1e-9,
        "extruded solid's volume matches base area (4) x height (3) exactly");
}

void TestExtrudeTrimmedFaceFeedsBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // Same trimmed face as TestBrepTrimmedPlanarFace (exact area 36), now
  // extruded into an actual closed solid - proving Brep::TrimmedPlanarFace()
  // can feed BooleanCombine() after all, closing the gap the previous
  // chunk's README flagged ("a trimmed face can't feed BooleanCombine()
  // yet"). ExtrudeCappedSolid()'s boundary-edge extraction has to cope
  // with the trim's jagged/staircased boundary here, not a clean polygon -
  // this is the real test of it, not the flat-untrimmed-square case above.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 10, 0),
      Point3d(10, 0, 0),
      Point3d(10, 10, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const std::vector<Point2d> trim_loop = {
      Point2d(0.15, 0.15),
      Point2d(0.85, 0.15),
      Point2d(0.85, 0.85),
      Point2d(0.15, 0.85),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, trim_loop);
  const auto cap = face.Tessellate(/*u_divisions=*/10, /*v_divisions=*/10).front();

  const auto solid = Mesh::ExtrudeCappedSolid(cap, Vector3d(0, 0, -1));
  Check(std::abs(solid.Volume() - 36.0) < 1e-9,
        "extruded trimmed-face solid's volume matches trim area (36) x height (1) exactly");

  // Union with a disjoint box far away: if the extruded solid weren't
  // genuinely closed/watertight, Manifold::Status() would reject it and
  // BooleanCombine() would throw rather than return a result.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(solid, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - 37.0) < 1e-9,
        "union of the extruded trimmed solid with a disjoint unit box equals 36 + 1");
}

void TestCylinderVolumeAndBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const double radius = 2.0;
  const double height = 5.0;
  // Measured, not guessed, and directly comparable to the whole-cell
  // numbers this replaced: Cylinder() now tessellates its disk cap via
  // real boundary clipping (TessellateGridClippedConvex), not whole-cell
  // in/out. At the SAME 48/48 divisions that measured a 7% volume error
  // with whole-cell trimming, exact clipping measures well under 1%; at
  // just 32/32 it measures ~0.64%. 1% here is a real, tight check on
  // that improvement, not a rubber stamp.
  const auto cylinder =
      Mesh::Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), radius, height,
                     /*circle_segments=*/32, /*grid_divisions=*/32);

  const double exact_volume = ON_PI * radius * radius * height;
  const double relative_error = std::abs(cylinder.Volume() - exact_volume) / exact_volume;
  Check(relative_error < 0.01,
        "cylinder volume (now via exact boundary clipping) is within 1% of pi*r^2*h");

  // Real proof of watertightness, same as the trimmed-face extrusion test:
  // Manifold would reject a non-manifold mesh outright rather than return
  // a plausible-looking wrong answer.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(cylinder, box, BooleanOp::Union);
  const double expected_union = cylinder.Volume() + 1.0;
  Check(std::abs(result.Volume() - expected_union) < 1e-6,
        "union of the cylinder with a disjoint unit box equals cylinder volume + 1");
}

void TestExactClippingMatchesAreaButNotCellCounts() {
  using dino8::kernel::Brep;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;

  // Same 10x10 surface and same [0.15,0.85]^2 trim as
  // TestBrepTrimmedPlanarFace, but with exact_clip=true. The TRUE trim
  // area is (0.85-0.15)^2 * 100 = 49 - that's what exact clipping should
  // measure. Whole-cell trimming's exact area of 36 (see the other test)
  // is a different, smaller number: it only ever keeps cells fully
  // inside the nominal boundary, so its output is really the retained
  // *grid-snapped* sub-square [0.2,0.8]^2, not the true [0.15,0.85]^2
  // trim - 36 was that algorithm's systematic under-count landing on a
  // clean number by construction, not the actual trim area. Getting 49
  // here (not 36) is exact clipping's whole point.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 10, 0),
      Point3d(10, 0, 0),
      Point3d(10, 10, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const std::vector<Point2d> trim_loop = {
      Point2d(0.15, 0.15),
      Point2d(0.85, 0.15),
      Point2d(0.85, 0.85),
      Point2d(0.15, 0.85),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, trim_loop, /*exact_clip=*/true);
  const auto mesh = face.Tessellate(/*u_divisions=*/10, /*v_divisions=*/10).front();

  Check(std::abs(mesh.Area() - 49.0) < 1e-9,
        "exact-clipped area matches the true trim area (49), not whole-cell's 36");
  Check(mesh.VertexCount() != 49 || mesh.FaceCount() != 72,
        "exact clipping's vertex/triangle counts differ from whole-cell's "
        "(boundary cells are clipped, not dropped or kept whole)");
}

void TestExactClippingRejectsNonConvexTrim() {
  using dino8::kernel::Brep;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;

  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);

  // A dart/arrowhead shape - concave at (0.5, 0.3).
  const std::vector<Point2d> non_convex_trim = {
      Point2d(0.1, 0.1),
      Point2d(0.9, 0.1),
      Point2d(0.9, 0.9),
      Point2d(0.5, 0.3),
      Point2d(0.1, 0.9),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, non_convex_trim, /*exact_clip=*/true);

  bool threw = false;
  try {
    face.Tessellate(8, 8);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw,
        "exact clipping throws on a non-convex trim polygon instead of "
        "silently producing wrong geometry");
}

void TestAnnulusFaceExtrudesToWatertightTube() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // Same 10x10 surface and outer [0.15,0.85]^2 trim as
  // TestBrepTrimmedPlanarFace (whole-cell area 36 there), now with a
  // hole at [0.35,0.65]^2 - also deliberately off grid lines (0.35/0.65
  // aren't multiples of 0.1) so there's no boundary-point ambiguity at
  // the hole either.
  //
  // Hand-derived (not measured after the fact): outer-inside grid values
  // are u,v in {0.2,...,0.8} (7 each, as before); hole-inside grid values
  // are u,v in {0.4,0.5,0.6} (3 each). A cell is dropped if EITHER its
  // outer-corner test fails OR any one of its 4 corners falls inside the
  // hole - which happens for exactly the (i,j) in {3,4,5,6}^2 cells (16
  // of them), since every such cell has a corner landing on a hole-inside
  // grid point in both u and v. Of the 36 outer-retained cells (i,j in
  // {2..7}), that leaves 36-16=20 retained cells (40 triangles), and
  // 49-9=40 retained vertices (7x7 outer grid points minus the 3x3 that
  // are also inside the hole). Physical area = 20 cells x (0.1*10)^2 = 20.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 10, 0),
      Point3d(10, 0, 0),
      Point3d(10, 10, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  const std::vector<Point2d> outer_loop = {
      Point2d(0.15, 0.15),
      Point2d(0.85, 0.15),
      Point2d(0.85, 0.85),
      Point2d(0.15, 0.85),
  };
  const std::vector<Point2d> hole_loop = {
      Point2d(0.35, 0.35),
      Point2d(0.65, 0.35),
      Point2d(0.65, 0.65),
      Point2d(0.35, 0.65),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, outer_loop, /*exact_clip=*/false,
                                             {hole_loop});
  const auto cap = face.Tessellate(/*u_divisions=*/10, /*v_divisions=*/10).front();

  Check(cap.VertexCount() == 40, "annulus face keeps exactly the 40 outer-grid-minus-hole vertices");
  Check(cap.FaceCount() == 40, "annulus face keeps exactly the 20 retained cells (40 triangles)");
  Check(std::abs(cap.Area() - 20.0) < 1e-9, "annulus face's area matches the hand-derived 20 exactly");

  // ExtrudeCappedSolid()'s boundary-edge extraction was documented as
  // working on "any cap shape" via triangle adjacency alone, without
  // ever having been tried on a cap with TWO independent boundary loops
  // (outer + hole) - this is that test. If it silently only walled one
  // loop, the result wouldn't be closed and BooleanCombine() would throw.
  const double height = 2.0;
  const auto tube = Mesh::ExtrudeCappedSolid(cap, Vector3d(0, 0, -height));
  Check(std::abs(tube.Volume() - cap.Area() * height) < 1e-9,
        "extruded annulus tube's volume matches area x height exactly");

  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(tube, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (tube.Volume() + 1.0)) < 1e-9,
        "union of the extruded annulus tube with a disjoint unit box equals tube volume + 1 "
        "(both the outer and inner walls were genuinely closed)");
}

void TestExtrudeRejectsAlreadyClosedCap() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Vector3d;

  // MakeBox() (defined above) is already a closed, boundary-free mesh -
  // ExtrudeCappedSolid() has nothing to sweep into walls and should say
  // so rather than silently producing two disconnected shells.
  const auto box = MakeBox(0, 0, 0, 1, 1, 1);

  bool threw = false;
  try {
    Mesh::ExtrudeCappedSolid(box, Vector3d(0, 0, 1));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw, "ExtrudeCappedSolid throws on a cap with no boundary (already closed)");
}

void TestExtrudeRejectsBowtieBoundary() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Vector3d;

  // Two triangles sharing exactly one vertex (index 0) and no edges - a
  // "bowtie": two boundary loops that touch at a single point rather than
  // being disjoint. Vertex 0 ends up with two outgoing and two incoming
  // boundary edges, which ExtrudeCappedSolid's validation should reject
  // rather than emit overlapping wall geometry through that shared point.
  Mesh bowtie;
  ON_Mesh& raw = bowtie.raw();
  raw.m_V.Append(ON_3fPoint(0, 0, 0));   // 0: shared vertex
  raw.m_V.Append(ON_3fPoint(1, 0, 0));   // 1
  raw.m_V.Append(ON_3fPoint(0, 1, 0));   // 2
  raw.m_V.Append(ON_3fPoint(-1, 0, 0));  // 3
  raw.m_V.Append(ON_3fPoint(0, -1, 0));  // 4

  auto add_tri = [&raw](int a, int b, int c) {
    ON_MeshFace face;
    face.vi[0] = a;
    face.vi[1] = b;
    face.vi[2] = c;
    face.vi[3] = c;
    raw.m_F.Append(face);
  };
  add_tri(0, 1, 2);
  add_tri(0, 3, 4);

  bool threw = false;
  try {
    Mesh::ExtrudeCappedSolid(bowtie, Vector3d(0, 0, 1));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw,
        "ExtrudeCappedSolid throws on a bowtie boundary (a vertex with more "
        "than one boundary edge) instead of emitting broken wall geometry");
}

}  // namespace

int main() {
  ON::Begin();

  TestCurveDegreeElevation();
  TestSurfaceDegreeElevation();
  TestFileRoundTrip();
  TestBrepTessellation();
  TestBoxVolume();
  TestBooleanUnion();
  TestBooleanIntersection();
  TestBooleanDifference();
  TestBrepBoxIsClosedAndWatertight();
  TestBrepBooleanEndToEnd();
  TestBrepSphereIsClosedAndWatertight();
  TestBrepSphereBooleanEndToEnd();
  TestBrepTrimmedPlanarFace();
  TestWeldAcrossIndependentlyParameterizedSurfaces();
  TestExtrudeUntrimmedFaceIntoSolid();
  TestExtrudeTrimmedFaceFeedsBoolean();
  TestCylinderVolumeAndBoolean();
  TestExactClippingMatchesAreaButNotCellCounts();
  TestExactClippingRejectsNonConvexTrim();
  TestAnnulusFaceExtrudesToWatertightTube();
  TestExtrudeRejectsAlreadyClosedCap();
  TestExtrudeRejectsBowtieBoundary();

  ON::End();

  if (g_failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}
