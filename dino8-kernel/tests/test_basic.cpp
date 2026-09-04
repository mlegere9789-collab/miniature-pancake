// Minimal assert-based smoke tests for chunk 1's exit criteria. Not pulling
// in a test framework dependency for four checks.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/file_io.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
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

void TestCurveLength() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // A degree-1 (straight-line) curve has no curvature for polyline
  // sampling to approximate away - Length() should be exact (the true
  // 3-4-5 distance, 5.0) at any sample count, not just a large one.
  const std::vector<Point3d> line_pts = {Point3d(0, 0, 0), Point3d(3, 4, 0)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(line_pts, /*degree=*/1);
  Check(std::abs(line.Length(4) - 5.0) < 1e-9,
        "a straight-line curve's length is exact (5.0, the 3-4-5 "
        "distance) even at a small sample count");
  Check(std::abs(line.Length(1000) - 5.0) < 1e-9,
        "...and stays exact at a large sample count too");

  // A genuinely curved case: measure convergence directly rather than
  // assuming it, the same discipline used for SubD's volume-shrink
  // measurements. A polyline's chords always understate a smooth curve's
  // true length, so Length() should increase monotonically (not
  // decrease, not oscillate) as sample count grows, and the increments
  // should shrink (approaching some limit), not diverge.
  const std::vector<Point3d> curved_pts = {
      Point3d(0, 0, 0),
      Point3d(1, 3, 0),
      Point3d(2, -3, 0),
      Point3d(3, 0, 0),
  };
  const NurbsCurve curved = NurbsCurve::FromControlPoints(curved_pts, /*degree=*/3);
  const double length_10 = curved.Length(10);
  const double length_100 = curved.Length(100);
  const double length_1000 = curved.Length(1000);
  Check(length_10 <= length_100 + 1e-12 && length_100 <= length_1000 + 1e-12,
        "a curved curve's approximated length increases monotonically "
        "with sample count (chords underestimate the true arc length)");
  Check((length_1000 - length_100) < (length_100 - length_10),
        "the increase per 10x more samples shrinks - converging toward a "
        "limit, not diverging");
}

void TestCurveTangentAt() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // A straight-line curve's tangent is exactly the line's own unit
  // direction at every parameter value - no curvature to introduce any
  // variation, so this is hand-derivable exact rather than approximate.
  const std::vector<Point3d> line_pts = {Point3d(0, 0, 0), Point3d(3, 4, 0)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(line_pts, /*degree=*/1);
  const ON_Interval line_domain = line.raw().Domain();
  const Vector3d expected_direction(3.0 / 5.0, 4.0 / 5.0, 0.0);
  for (const double normalized_t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    const double t = line_domain.ParameterAt(normalized_t);
    const Vector3d tangent = line.TangentAt(t);
    Check(std::abs(tangent.x - expected_direction.x) < 1e-9 &&
              std::abs(tangent.y - expected_direction.y) < 1e-9 &&
              std::abs(tangent.z - expected_direction.z) < 1e-9,
          "a straight-line curve's tangent is exactly its own unit "
          "direction (3/5, 4/5, 0) at every parameter value");
  }

  // A genuinely curved case: TangentAt() should point the same way as a
  // central-finite-difference approximation of the derivative at the
  // same parameter - measured agreement, not just "it returns a unit
  // vector."
  const std::vector<Point3d> curved_pts = {
      Point3d(0, 0, 0),
      Point3d(1, 3, 0),
      Point3d(2, -3, 0),
      Point3d(3, 0, 0),
  };
  const NurbsCurve curved = NurbsCurve::FromControlPoints(curved_pts, /*degree=*/3);
  const ON_Interval curved_domain = curved.raw().Domain();
  constexpr double kFiniteDifferenceStep = 1e-5;
  for (const double normalized_t : {0.2, 0.4, 0.6, 0.8}) {
    const double t = curved_domain.ParameterAt(normalized_t);
    const Vector3d tangent = curved.TangentAt(t);
    Check(std::abs(tangent.Length() - 1.0) < 1e-9, "TangentAt() returns a unit vector");

    const Point3d before = curved.PointAt(t - kFiniteDifferenceStep);
    const Point3d after = curved.PointAt(t + kFiniteDifferenceStep);
    Vector3d finite_difference = after - before;
    finite_difference.Unitize();
    const double alignment = ON_DotProduct(tangent, finite_difference);
    Check(alignment > 1.0 - 1e-6,
          "TangentAt() points the same way as a central-finite-difference "
          "approximation of the curve's own derivative");
  }
}

void TestCurveGetTightBoundingBox() {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;

  // A straight-line curve's tight bounding box is exactly its two
  // endpoints' min/max - hand-derivable exact, no curvature involved.
  const std::vector<Point3d> line_pts = {Point3d(-1, 5, 2), Point3d(3, -2, 7)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(line_pts, /*degree=*/1);
  const auto line_bounds = line.GetTightBoundingBox();
  Check(line_bounds.min.x == -1.0 && line_bounds.min.y == -2.0 && line_bounds.min.z == 2.0,
        "a straight line's tight bounding box min corner is exactly its "
        "own low endpoint coordinates");
  Check(line_bounds.max.x == 3.0 && line_bounds.max.y == 5.0 && line_bounds.max.z == 7.0,
        "a straight line's tight bounding box max corner is exactly its "
        "own high endpoint coordinates");

  // A genuinely curved case, and a real discovery: a quadratic
  // Bezier-equivalent NURBS curve through (0,0,0), (1,1,0), (2,0,0) has
  // P(t) = (1-t)^2*P0 + 2t(1-t)*P1 + t^2*P2, so its *true* y-extent is
  // exactly [0, 0.5] (dy/dt = 2-4t = 0 at t=0.5, y(0.5) = 0.5 -
  // confirmed directly via PointAt() below, not just algebra). Despite
  // its name, `ON_Curve::GetTightBoundingBox`'s public-build
  // implementation does *not* compute that: reading the source
  // (opennurbs_bezier.cpp) shows `ON_BezierCurve::GetTightBoundingBox`
  // literally calls `ON_GetPointListBoundingBox` - its own comment says
  // "good enough for file IO needs in the public source code version" -
  // i.e. the *control-point* bounding box, not a real extremum search.
  // So this returns y_max = 1.0 (the middle control point's own y),
  // exactly double the curve's true 0.5 - the same "declared for Rhino,
  // degraded in the public build" pattern this codebase has found
  // before (`ON_Brep::CreateMesh`, `ON_SubD::BrepForm`), just less
  // total than those: still a real, valid (if not minimal) bound, never
  // wrong in the sense of excluding part of the curve, just measurably
  // not "tight" for a curve whose extremum isn't a control point.
  const std::vector<Point3d> bulge_pts = {Point3d(0, 0, 0), Point3d(1, 1, 0), Point3d(2, 0, 0)};
  const NurbsCurve bulge = NurbsCurve::FromControlPoints(bulge_pts, /*degree=*/2);
  const Point3d true_midpoint = bulge.PointAt(bulge.raw().Domain().ParameterAt(0.5));
  Check(std::abs(true_midpoint.y - 0.5) < 1e-9,
        "the quadratic curve's own true midpoint y-coordinate is exactly "
        "0.5, confirmed directly via PointAt() (not just the algebra)");
  const auto bulge_bounds = bulge.GetTightBoundingBox();
  Check(std::abs(bulge_bounds.max.y - 1.0) < 1e-9,
        "GetTightBoundingBox()'s public-build implementation returns the "
        "*control-point* bound (y=1.0, the middle control point's own "
        "y), not the curve's true tight extremum (0.5) - a real, "
        "documented degradation in the public OpenNURBS build, verified "
        "by testing rather than assumed from the method's name");
  Check(std::abs(bulge_bounds.min.x - 0.0) < 1e-9 && std::abs(bulge_bounds.max.x - 2.0) < 1e-9,
        "the same curve's x-extent (2t, monotonic) is still exactly "
        "[0, 2] either way, since the endpoints already bound it exactly");
}

void TestSurfaceNormalAt() {
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // A flat unit-square surface with P(u, v) = (u, v, 0) exactly (bilinear
  // identity for these control points, same construction
  // TestExactClippingHandlesNonConvexTrim already relies on): d/du =
  // (1,0,0), d/dv = (0,1,0), so the normal is exactly (1,0,0)x(0,1,0) =
  // (0,0,1) everywhere - a hand-derivable exact case, not approximate.
  const std::vector<Point3d> flat_grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface flat =
      NurbsSurface::FromControlGrid(flat_grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);
  for (const auto& uv : {std::pair(0.0, 0.0), std::pair(0.5, 0.5), std::pair(1.0, 0.0),
                          std::pair(0.25, 0.9)}) {
    const Vector3d normal = flat.NormalAt(uv.first, uv.second);
    Check(std::abs(normal.x) < 1e-9 && std::abs(normal.y) < 1e-9 &&
              std::abs(normal.z - 1.0) < 1e-9,
          "a flat P(u,v)=(u,v,0) surface's normal is exactly (0,0,1) at "
          "every (u,v) tested");
  }

  // A genuinely curved surface (z varies with both u and v): NormalAt()
  // should agree with a finite-difference cross product of the surface's
  // own partial derivatives - measured agreement, not just "returns a
  // unit vector."
  std::vector<Point3d> curved_grid;
  for (int u = 0; u < 4; ++u) {
    for (int v = 0; v < 4; ++v) {
      const double x = u;
      const double y = v;
      const double z = std::sin(0.7 * u) * std::cos(0.5 * v);
      curved_grid.emplace_back(x, y, z);
    }
  }
  const NurbsSurface curved =
      NurbsSurface::FromControlGrid(curved_grid, 4, 4, /*u_degree=*/3, /*v_degree=*/3);
  const ON_Interval u_domain = curved.raw().Domain(0);
  const ON_Interval v_domain = curved.raw().Domain(1);
  constexpr double kFiniteDifferenceStep = 1e-5;
  for (const auto& normalized_uv :
       {std::pair(0.3, 0.3), std::pair(0.6, 0.4), std::pair(0.5, 0.8)}) {
    const double u = u_domain.ParameterAt(normalized_uv.first);
    const double v = v_domain.ParameterAt(normalized_uv.second);
    const Vector3d normal = curved.NormalAt(u, v);
    Check(std::abs(normal.Length() - 1.0) < 1e-9, "NormalAt() returns a unit vector");

    Vector3d du = curved.PointAt(u + kFiniteDifferenceStep, v) -
                  curved.PointAt(u - kFiniteDifferenceStep, v);
    Vector3d dv = curved.PointAt(u, v + kFiniteDifferenceStep) -
                  curved.PointAt(u, v - kFiniteDifferenceStep);
    Vector3d finite_difference_normal = ON_CrossProduct(du, dv);
    finite_difference_normal.Unitize();
    const double alignment = std::abs(ON_DotProduct(normal, finite_difference_normal));
    Check(alignment > 1.0 - 1e-6,
          "NormalAt() agrees (up to sign) with a finite-difference cross "
          "product of the surface's own partial derivatives");
  }
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

// Same box as MakeBox(), but as 6 genuine quad faces rather than 12
// triangles - each quad below is the same pair of MakeBox() triangles
// merged along their shared diagonal (e.g. bottom's (0,3,2)+(0,2,1)
// becomes the quad 0,3,2,1), so it has the identical outward-normal
// winding, just needed for SubD tests: Catmull-Clark subdivision's
// vertex/face-count growth has a clean, hand-derivable formula on an
// all-quad control net (V_new = V+E+F, F_new = 4x once every face is a
// quad), which a triangulated box wouldn't give.
dino8::kernel::Mesh MakeQuadBoxMesh(double x0, double y0, double z0, double x1, double y1,
                                     double z1) {
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

  auto add_quad = [&raw](int a, int b, int c, int d) {
    ON_MeshFace face;
    face.vi[0] = a;
    face.vi[1] = b;
    face.vi[2] = c;
    face.vi[3] = d;
    raw.m_F.Append(face);
  };

  add_quad(0, 3, 2, 1);  // bottom (-z)
  add_quad(4, 5, 6, 7);  // top (+z)
  add_quad(0, 1, 5, 4);  // front (-y)
  add_quad(3, 7, 6, 2);  // back (+y)
  add_quad(0, 4, 7, 3);  // left (-x)
  add_quad(1, 2, 6, 5);  // right (+x)

  return mesh;
}

void TestModelAddMeshRoundTrips() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Model;
  using dino8::kernel::Result;

  // AddMesh() is the missing counterpart to AddCurve()/AddBrep(): every
  // closed-solid primitive/boolean result here is a Mesh, but until now
  // there was no way to put one into a .3dm at all.
  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 3, 4);
  Model model;
  model.AddMesh(box);
  Check(model.ObjectCount() == 1, "model has one object after AddMesh()");

  const std::string path = "dino8_kernel_mesh_roundtrip_test.3dm";
  Check(model.Save(path) == Result::Ok, ".3dm save with a mesh object succeeded");

  Model loaded;
  Check(Model::Load(path, loaded) == Result::Ok, ".3dm load succeeded");
  Check(loaded.ObjectCount() == 1, "round-tripped model has one object");

  // Not just "an object exists" - dig out the actual mesh geometry and
  // check its vertex/face counts and volume genuinely survived the
  // round trip, not just some object of some type.
  ONX_ModelComponentIterator iterator(loaded.raw(), ON_ModelComponent::Type::ModelGeometry);
  bool found_mesh = false;
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const auto* mesh_geometry = dynamic_cast<const ON_Mesh*>(geometry_component->Geometry(nullptr));
    if (mesh_geometry == nullptr) {
      continue;
    }
    found_mesh = true;
    Check(mesh_geometry->m_V.Count() == box.VertexCount(),
          "the round-tripped mesh object has the original's vertex count (8)");
    Check(mesh_geometry->m_F.Count() == box.FaceCount(),
          "the round-tripped mesh object has the original's face count (6)");
    Mesh reloaded_mesh;
    reloaded_mesh.raw() = *mesh_geometry;
    Check(std::abs(reloaded_mesh.Volume() - box.Volume()) < 1e-9,
          "the round-tripped mesh's volume exactly matches the original "
          "(quad faces preserved, not reinterpreted)");
  }
  Check(found_mesh, "the .3dm file's model geometry actually contains a mesh object");

  std::remove(path.c_str());
}

void TestModelAddSubDRoundTrips() {
  using dino8::kernel::Model;
  using dino8::kernel::Result;
  using dino8::kernel::SubD;

  // AddSubD() closes the same "no way to put this into a .3dm" gap
  // AddMesh() closed, for SubD instead of Mesh.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 3, 4);
  const auto subd = SubD::FromControlMesh(quad_box);
  Model model;
  model.AddSubD(subd);
  Check(model.ObjectCount() == 1, "model has one object after AddSubD()");

  const std::string path = "dino8_kernel_subd_roundtrip_test.3dm";
  Check(model.Save(path) == Result::Ok, ".3dm save with a SubD object succeeded");

  Model loaded;
  Check(Model::Load(path, loaded) == Result::Ok, ".3dm load succeeded");
  Check(loaded.ObjectCount() == 1, "round-tripped model has one object");

  ONX_ModelComponentIterator iterator(loaded.raw(), ON_ModelComponent::Type::ModelGeometry);
  bool found_subd = false;
  for (const ON_ModelComponent* component = iterator.FirstComponent(); component != nullptr;
       component = iterator.NextComponent()) {
    const auto* geometry_component = static_cast<const ON_ModelGeometryComponent*>(component);
    const auto* subd_geometry = dynamic_cast<const ON_SubD*>(geometry_component->Geometry(nullptr));
    if (subd_geometry == nullptr) {
      continue;
    }
    found_subd = true;
    Check(static_cast<int>(subd_geometry->VertexCount()) == subd.VertexCount(),
          "the round-tripped SubD object has the original's vertex count");
    Check(static_cast<int>(subd_geometry->FaceCount()) == subd.FaceCount(),
          "the round-tripped SubD object has the original's face count (6)");
  }
  Check(found_subd, "the .3dm file's model geometry actually contains a SubD object");

  std::remove(path.c_str());
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

void TestBooleanSymmetricDifference() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;

  // Same two boxes as the other boolean tests (volume 8 each, 1x1x1 = 1
  // overlap). Manifold has no direct XOR op, so SymmetricDifference is
  // computed as Union - Intersection (two extra Manifold calls) - checked
  // against the equally-valid alternate formula (A-B)+(B-A) = 7+7=14, not
  // just internal self-consistency with the same implementation this test
  // is verifying.
  const auto a = MakeBox(0, 0, 0, 2, 2, 2);
  const auto b = MakeBox(1, 1, 1, 3, 3, 3);

  const auto result = BooleanCombine(a, b, BooleanOp::SymmetricDifference);
  Check(std::abs(result.Volume() - 14.0) < 1e-6,
        "symmetric difference volume equals (8-1) + (8-1) = 14, matching "
        "the union-minus-intersection formula against the independent "
        "(A-B)+(B-A) one");
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

void TestBrepGetTightBoundingBox() {
  using dino8::kernel::Brep;
  using dino8::kernel::Point3d;

  // Box(): six flat faces, so the tight bounding box is exactly the box's
  // own corners - hand-derivable exact, no tessellation involved at all.
  const Brep box = Brep::Box(1, -2, 0.5, 4, 3, 7.5);
  const auto box_bounds = box.GetTightBoundingBox();
  Check(box_bounds.min.x == 1.0 && box_bounds.min.y == -2.0 && box_bounds.min.z == 0.5,
        "Brep::Box's tight bounding box min corner matches its known low corner exactly");
  Check(box_bounds.max.x == 4.0 && box_bounds.max.y == 3.0 && box_bounds.max.z == 7.5,
        "Brep::Box's tight bounding box max corner matches its known high corner exactly");

  // Sphere(): a curved surface, so this actually exercises the "tight",
  // not just control-point, bounding box - a sphere's own control net
  // (the NURBS control polygon) extends well outside the true surface
  // (it has to, to represent a circle with a rational NURBS curve), so a
  // naive control-point bbox would overshoot. The true tight bbox is
  // exactly [-r, r] on every axis around the center, since a full sphere
  // touches its own bounding box on every face.
  const double radius = 3.0;
  const Point3d center(10, -5, 2);
  const Brep sphere = Brep::Sphere(center, radius);
  const auto sphere_bounds = sphere.GetTightBoundingBox();
  Check(std::abs(sphere_bounds.min.x - (center.x - radius)) < 1e-9 &&
            std::abs(sphere_bounds.min.y - (center.y - radius)) < 1e-9 &&
            std::abs(sphere_bounds.min.z - (center.z - radius)) < 1e-9,
        "Brep::Sphere's tight bounding box min corner is exactly center - radius "
        "on every axis, not overshot by the NURBS control net");
  Check(std::abs(sphere_bounds.max.x - (center.x + radius)) < 1e-9 &&
            std::abs(sphere_bounds.max.y - (center.y + radius)) < 1e-9 &&
            std::abs(sphere_bounds.max.z - (center.z + radius)) < 1e-9,
        "Brep::Sphere's tight bounding box max corner is exactly center + radius "
        "on every axis");

  // A genuine discovery, not assumed from the method's name: a
  // doubly-curved bicubic surface whose true peak lies at its own
  // interior center - not on any boundary or Greville-abscissa isocurve
  // GetTightBoundingBox() actually samples - comes back overshot rather
  // than exact. Tensor-product quadratic bump: z(u,v) = [2u(1-u)] *
  // [2v(1-v)] * peak_height (each direction independently contributes
  // its own 1D quadratic-Bezier bump, same shape as
  // TestCurveGetTightBoundingBox's curve). True max at u=v=0.5:
  // 0.5 * 0.5 * peak_height = 0.25 * peak_height - confirmed directly
  // via NurbsSurface::PointAt(), not just algebra.
  const double peak_height = 5.0;
  std::vector<Point3d> bulge_grid;
  for (int u = 0; u < 3; ++u) {
    for (int v = 0; v < 3; ++v) {
      bulge_grid.emplace_back(u, v, (u == 1 && v == 1) ? peak_height : 0.0);
    }
  }
  const auto bulge_surface =
      dino8::kernel::NurbsSurface::FromControlGrid(bulge_grid, 3, 3, 2, 2);
  const Point3d true_peak = bulge_surface.PointAt(0.5, 0.5);
  Check(std::abs(true_peak.z - 0.25 * peak_height) < 1e-9,
        "the bicubic bulge surface's own true interior peak z-coordinate "
        "is exactly 0.25*peak_height, confirmed directly via PointAt()");
  const auto bulge_brep = Brep::FromSurface(bulge_surface);
  const auto bulge_bounds = bulge_brep.GetTightBoundingBox();
  Check(std::abs(bulge_bounds.max.z - 0.5 * peak_height) < 1e-6,
        "GetTightBoundingBox() overshoots this bulge's true peak "
        "(0.25*peak_height) to 0.5*peak_height instead - it only samples "
        "boundary/Greville isocurves, never the genuine 2D interior "
        "extremum, the same real public-build limitation "
        "TestCurveGetTightBoundingBox found for a curve");
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
  // real boundary clipping (TessellateGridClippedExact), not whole-cell
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

void TestConeVolumeAndBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const double radius = 2.0;
  const double height = 5.0;
  // Same shape/resolution as TestCylinderVolumeAndBoolean, so the 1%
  // tolerance is directly comparable: ConeToApex() collapses
  // ExtrudeCappedSolid()'s wall geometry to a single triangle per
  // boundary edge instead of two, sharing the exact same disk-cap
  // construction (BuildCircularDiskCap) and boundary-edge validation
  // (ExtractValidatedBoundaryEdges) as Cylinder().
  const auto cone = Mesh::Cone(Point3d(0, 0, 0), Vector3d(0, 0, 1), radius, height,
                                /*circle_segments=*/32, /*grid_divisions=*/32);

  const double exact_volume = ON_PI * radius * radius * height / 3.0;
  const double relative_error = std::abs(cone.Volume() - exact_volume) / exact_volume;
  Check(relative_error < 0.01, "cone volume is within 1% of (1/3)*pi*r^2*h");

  // Real proof of watertightness, same as the cylinder test: Manifold
  // would reject a non-manifold mesh outright rather than return a
  // plausible-looking wrong answer.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(cone, box, BooleanOp::Union);
  const double expected_union = cone.Volume() + 1.0;
  Check(std::abs(result.Volume() - expected_union) < 1e-6,
        "union of the cone with a disjoint unit box equals cone volume + 1");
}

void TestRevolveProfileBiconeVolumeAndBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const double radius = 2.0;
  const double half_height = 5.0;
  // A "bicone"/football: on-axis apex, out to max radius at the
  // mid-height, back to an on-axis apex - i.e. two cones glued base to
  // base. Exact volume is exactly twice one cone's (1/3)*pi*r^2*h, a
  // closed form independent of RevolveProfile()'s own implementation
  // (unlike Cone(), which this doesn't reuse - RevolveProfile() builds
  // its bands and end fans directly from the profile).
  const std::vector<Point2d> profile = {
      Point2d(0.0, -half_height),
      Point2d(radius, 0.0),
      Point2d(0.0, half_height),
  };
  const auto bicone = Mesh::RevolveProfile(profile, Point3d(0, 0, 0), Vector3d(0, 0, 1),
                                            /*revolve_segments=*/32);

  const double exact_volume = 2.0 * (ON_PI * radius * radius * half_height / 3.0);
  const double relative_error = std::abs(bicone.Volume() - exact_volume) / exact_volume;
  Check(relative_error < 0.01,
        "revolved bicone volume is within 1% of 2*(1/3)*pi*r^2*half_height");

  // Real proof of watertightness, same as Cylinder()/Cone(): Manifold
  // would reject a non-manifold mesh outright rather than return a
  // plausible-looking wrong answer.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(bicone, box, BooleanOp::Union);
  const double expected_union = bicone.Volume() + 1.0;
  Check(std::abs(result.Volume() - expected_union) < 1e-6,
        "union of the revolved bicone with a disjoint unit box equals bicone volume + 1");
}

void TestRevolveProfileRejectsTooShortProfile() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  bool threw_too_short = false;
  try {
    const std::vector<Point2d> too_short = {Point2d(0.0, -1.0)};
    Mesh::RevolveProfile(too_short, Point3d(0, 0, 0), Vector3d(0, 0, 1), 16);
  } catch (const std::invalid_argument&) {
    threw_too_short = true;
  }
  Check(threw_too_short,
        "RevolveProfile throws on a 1-point profile (nothing to revolve into "
        "a solid)");
}

// An off-axis profile end used to be rejected outright; RevolveProfile()
// now closes it with a flat disc cap instead (see the header comment).
// This checks that capability against three independent closed-form
// volumes, using the smallest possible profile (m=2, no interior rings)
// so each test isolates exactly the new end-cap code path plus one band.
void TestRevolveProfileFlatEndCaps() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const Point3d origin(0, 0, 0);
  const Vector3d up(0, 0, 1);

  // Cone built base-first (off-axis flat-capped base tapering to an
  // on-axis apex) - the reverse construction order from Mesh::Cone(), so
  // this is a genuine independent check of the new cap's orientation, not
  // just a call-through. Exact volume: (1/3)*pi*r^2*h.
  {
    const double radius = 3.0;
    const double height = 5.0;
    const std::vector<Point2d> profile = {Point2d(radius, 0.0), Point2d(0.0, height)};
    double previous_error = 1e9;
    for (const int segments : {8, 32, 128}) {
      const auto cone = Mesh::RevolveProfile(profile, origin, up, segments);
      const double exact_volume = ON_PI * radius * radius * height / 3.0;
      const double error = std::abs(cone.Volume() - exact_volume);
      Check(error < previous_error || error < 1e-6,
            "base-first flat-capped cone volume error shrinks as "
            "revolve_segments increases");
      previous_error = error;
    }
    const auto cone = Mesh::RevolveProfile(profile, origin, up, 64);
    const double exact_volume = ON_PI * radius * radius * height / 3.0;
    Check(std::abs(cone.Volume() - exact_volume) / exact_volume < 0.01,
          "base-first flat-capped cone volume is within 1% of (1/3)*pi*r^2*h");

    // Watertightness proof, same pattern as every other primitive here:
    // Manifold rejects a non-manifold mesh (an unclosed cap would leave a
    // hole) rather than silently returning a wrong-but-plausible answer.
    const auto box =
        Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
    const auto result = BooleanCombine(cone, box, BooleanOp::Union);
    Check(std::abs(result.Volume() - (cone.Volume() + 1.0)) < 1e-6,
          "union of the base-first flat-capped cone with a disjoint unit box "
          "equals cone volume + 1");
  }

  // Frustum: both ends off-axis and at different radii, so both get flat
  // disc caps. Exact volume: (pi*h/3)*(r1^2 + r1*r2 + r2^2).
  {
    const double r1 = 2.0;
    const double r2 = 5.0;
    const double height = 4.0;
    const std::vector<Point2d> profile = {Point2d(r1, 0.0), Point2d(r2, height)};
    const auto frustum = Mesh::RevolveProfile(profile, origin, up, 64);
    const double exact_volume =
        ON_PI * height * (r1 * r1 + r1 * r2 + r2 * r2) / 3.0;
    Check(std::abs(frustum.Volume() - exact_volume) / exact_volume < 0.01,
          "flat-double-capped frustum volume is within 1% of "
          "(pi*h/3)*(r1^2+r1*r2+r2^2)");
  }

  // Degenerate frustum with r1 == r2 is just a cylinder: cross-check
  // against Mesh::Cylinder()'s own (independently implemented) volume,
  // not just a closed form, since the two build caps completely
  // differently (ExtrudeCappedSolid()'s NURBS-surface-trimmed disc vs.
  // this end's plain center-vertex fan).
  {
    const double radius = 2.5;
    const double height = 6.0;
    const std::vector<Point2d> profile = {Point2d(radius, 0.0), Point2d(radius, height)};
    const auto via_revolve = Mesh::RevolveProfile(profile, origin, up, 64);
    const auto via_cylinder = Mesh::Cylinder(origin, up, radius, height, 64, 8);
    const double relative_diff =
        std::abs(via_revolve.Volume() - via_cylinder.Volume()) / via_cylinder.Volume();
    Check(relative_diff < 1e-3,
          "flat-double-capped cylinder-shaped revolve matches Mesh::Cylinder()'s "
          "volume to within 0.1%");
  }
}

void TestLoftClosedRingsSquareFrustumExactVolumeAndBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // A frustum between a small square (half-side 1, side 2, area 4) at
  // z=0 and a larger square (half-side 3, side 6, area 36) at z=3,
  // centered on and scaled uniformly about the same (0,0,z) axis - the
  // straight-line connection between corresponding vertices is then
  // exactly a frustum of a real pyramid (every lateral edge, extended,
  // meets at a single apex below z=0), not an approximation the way
  // Cylinder()/Cone()'s circular trims are. Both rings list vertices in
  // the same CCW-as-seen-from-ahead order (matching this file's
  // u_dir x v_dir = outward normal convention): (s,-s),(s,s),(-s,s),
  // (-s,-s) has positive standard-2D signed area for s > 0, i.e. is CCW
  // when viewed from +z looking down -z, per LoftClosedRings()'s own
  // documented convention.
  const std::vector<Point3d> bottom = {
      Point3d(1, -1, 0),
      Point3d(1, 1, 0),
      Point3d(-1, 1, 0),
      Point3d(-1, -1, 0),
  };
  const std::vector<Point3d> top = {
      Point3d(3, -3, 3),
      Point3d(3, 3, 3),
      Point3d(-3, 3, 3),
      Point3d(-3, -3, 3),
  };
  const auto frustum = Mesh::LoftClosedRings({bottom, top});

  // Exact frustum-of-a-pyramid volume formula: (h/3)*(A1+A2+sqrt(A1*A2)).
  const double height = 3.0;
  const double area1 = 4.0;
  const double area2 = 36.0;
  const double exact_volume = (height / 3.0) * (area1 + area2 + std::sqrt(area1 * area2));
  Check(std::abs(exact_volume - 52.0) < 1e-9,
        "sanity: the hand-derived frustum formula itself evaluates to 52");
  Check(std::abs(frustum.Volume() - exact_volume) < 1e-9,
        "lofted square frustum's volume exactly matches the closed-form "
        "pyramid-frustum formula (straight edges between only 2 rings - no "
        "circular approximation involved, unlike Cylinder()/Cone())");

  // Real proof of watertightness, same as every other solid here: Manifold
  // would reject a non-manifold mesh outright rather than return a
  // plausible-looking wrong answer.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(frustum, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (frustum.Volume() + 1.0)) < 1e-9,
        "union of the lofted frustum with a disjoint unit box equals frustum "
        "volume + 1");
}

void TestLoftClosedRingsRejectsTooFewRingsAndMismatchedCounts() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  bool threw_too_few = false;
  try {
    const std::vector<Point3d> only_ring = {Point3d(0, 0, 0), Point3d(1, 0, 0),
                                             Point3d(0, 1, 0)};
    Mesh::LoftClosedRings({only_ring});
  } catch (const std::invalid_argument&) {
    threw_too_few = true;
  }
  Check(threw_too_few, "LoftClosedRings throws with fewer than 2 rings");

  bool threw_mismatched = false;
  try {
    const std::vector<Point3d> triangle = {Point3d(0, 0, 0), Point3d(1, 0, 0),
                                            Point3d(0, 1, 0)};
    const std::vector<Point3d> square = {Point3d(0, 0, 1), Point3d(1, 0, 1), Point3d(1, 1, 1),
                                          Point3d(0, 1, 1)};
    Mesh::LoftClosedRings({triangle, square});
  } catch (const std::invalid_argument&) {
    threw_mismatched = true;
  }
  Check(threw_mismatched,
        "LoftClosedRings throws when rings have different vertex counts "
        "rather than silently misaligning bands");

  bool threw_self_intersecting = false;
  try {
    // A bowtie quadrilateral (corners in crossed order), planar in z=0 -
    // same self-intersection shape as
    // TestExactClippingRejectsSelfIntersectingTrim, just as a 3D ring.
    const std::vector<Point3d> bowtie = {
        Point3d(0, 0, 0),
        Point3d(1, 1, 0),
        Point3d(1, 0, 0),
        Point3d(0, 1, 0),
    };
    const std::vector<Point3d> square = {Point3d(0, 0, 1), Point3d(1, 0, 1), Point3d(1, 1, 1),
                                          Point3d(0, 1, 1)};
    Mesh::LoftClosedRings({bowtie, square});
  } catch (const std::invalid_argument&) {
    threw_self_intersecting = true;
  }
  Check(threw_self_intersecting,
        "LoftClosedRings throws when the first ring is self-intersecting "
        "(a bowtie), since it can't be closed into a well-defined end cap");

  bool threw_non_planar = false;
  try {
    // Same square as above, but with one corner pulled well out of the
    // z=0 plane - relative to the ring's own ~1.4-unit diagonal, 0.3 is
    // far past the check's 1e-6-relative tolerance, not a borderline case.
    const std::vector<Point3d> warped_square = {Point3d(0, 0, 0), Point3d(1, 0, 0),
                                                 Point3d(1, 1, 0.3), Point3d(0, 1, 0)};
    const std::vector<Point3d> square = {Point3d(0, 0, 1), Point3d(1, 0, 1), Point3d(1, 1, 1),
                                          Point3d(0, 1, 1)};
    Mesh::LoftClosedRings({warped_square, square});
  } catch (const std::invalid_argument&) {
    threw_non_planar = true;
  }
  Check(threw_non_planar,
        "LoftClosedRings throws when the first ring is non-planar, since its "
        "cap triangulation (projected onto a single plane) isn't well-defined");
}

void TestLoftClosedRingsConcaveEndCapsExactPrismVolume() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // Concave dart cross-section (same shape/coordinates as the exact-clip
  // dart test - shoelace area 0.404), extruded straight up by 1 as two
  // identical rings 1 apart. Since both rings are congruent and simply
  // translated (not rotated or scaled), this is an exact prism regardless
  // of the cross-section's shape - convex or concave - so its volume must
  // equal area x height exactly. A real, hand-derivable check on the new
  // ear-clipping end caps' correctness on a concave ring, not just proof
  // that Manifold didn't reject the result (the earlier frustum test only
  // exercised a convex ring).
  const std::vector<Point3d> bottom = {
      Point3d(0.1, 0.1, 0), Point3d(0.9, 0.1, 0), Point3d(0.9, 0.9, 0),
      Point3d(0.52, 0.31, 0), Point3d(0.1, 0.9, 0),
  };
  std::vector<Point3d> top;
  for (const auto& p : bottom) {
    top.emplace_back(p.x, p.y, p.z + 1.0);
  }

  const auto prism = Mesh::LoftClosedRings({bottom, top});
  Check(std::abs(prism.Volume() - 0.404) < 1e-6,
        "lofting two identical concave dart rings 1 apart gives an exact "
        "prism whose volume matches the dart's shoelace area (0.404) times "
        "height (1)");

  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(prism, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (prism.Volume() + 1.0)) < 1e-9,
        "union of the concave-cross-section lofted prism with a disjoint "
        "unit box equals prism volume + 1");
}

void TestTorusVolumeAndBoolean() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const double major_radius = 3.0;
  const double minor_radius = 1.0;
  // A shape neither RevolveProfile() (profile must touch the axis) nor
  // any earlier primitive can build - a genuinely new case, not a
  // reparameterization of one already tested. Its winding was derived
  // independently (see Torus()'s own header comment), so this needs its
  // own real verification, not a "should be fine, it's similar to X."
  const auto torus = Mesh::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), major_radius, minor_radius,
                                  /*major_segments=*/48, /*minor_segments=*/32);

  const double exact_volume = 2.0 * ON_PI * ON_PI * major_radius * minor_radius * minor_radius;
  const double relative_error = std::abs(torus.Volume() - exact_volume) / exact_volume;
  Check(relative_error < 0.01,
        "torus volume is within 1% of the exact 2*pi^2*major_radius*minor_radius^2");

  // Real proof of watertightness, same standard as every other solid
  // here: Manifold would reject a non-manifold mesh outright rather than
  // return a plausible-looking wrong answer - a meaningful check for a
  // grid that wraps in both directions with no explicit end caps at all.
  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(torus, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (torus.Volume() + 1.0)) < 1e-6,
        "union of the torus with a disjoint unit box equals torus volume + 1");
}

void TestMeshGetBoundingBox() {
  using dino8::kernel::Mesh;

  // MakeQuadBoxMesh's 8 corners span exactly [x0,x1]x[y0,y1]x[z0,z1] - an
  // asymmetric box (different extents per axis, not a cube) so a bug
  // that mixed up which axis fed which component would be caught.
  const auto box = MakeQuadBoxMesh(1, -2, 0.5, 4, 3, 7.5);
  const auto bounds = box.GetBoundingBox();
  Check(bounds.min.x == 1.0 && bounds.min.y == -2.0 && bounds.min.z == 0.5,
        "GetBoundingBox's min corner matches the box's known low corner exactly");
  Check(bounds.max.x == 4.0 && bounds.max.y == 3.0 && bounds.max.z == 7.5,
        "GetBoundingBox's max corner matches the box's known high corner exactly");

  bool threw = false;
  try {
    const Mesh empty;
    empty.GetBoundingBox();
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw, "GetBoundingBox throws on a mesh with no vertices rather than "
               "returning a misleading all-zero box");
}

void TestMeshGetCentroid() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // Same asymmetric box as TestMeshGetBoundingBox (different extents per
  // axis) - its centroid is exactly the midpoint of each axis's extent,
  // by symmetry, giving a clean hand-derivable exact check.
  const auto box = MakeQuadBoxMesh(1, -2, 0.5, 4, 3, 7.5);
  const Point3d centroid = box.GetCentroid();
  Check(std::abs(centroid.x - 2.5) < 1e-9 && std::abs(centroid.y - 0.5) < 1e-9 &&
            std::abs(centroid.z - 4.0) < 1e-9,
        "GetCentroid of an asymmetric box is exactly its per-axis midpoint "
        "(2.5, 0.5, 4.0)");

  bool threw = false;
  try {
    const Mesh empty;
    empty.GetCentroid();
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw, "GetCentroid throws on a mesh with (near) zero volume rather "
               "than dividing by it");
}

void TestMeshTransform() {
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);

  // Translation: the bounding box should shift by exactly the offset,
  // volume unchanged.
  const auto translated =
      box.Transform(ON_Xform::TranslationTransformation(Vector3d(5, -3, 10)));
  const auto translated_bounds = translated.GetBoundingBox();
  Check(translated_bounds.min.x == 5.0 && translated_bounds.min.y == -3.0 &&
            translated_bounds.min.z == 10.0 && translated_bounds.max.x == 7.0 &&
            translated_bounds.max.y == -1.0 && translated_bounds.max.z == 12.0,
        "translating the box shifts its bounding box by exactly the offset");
  Check(std::abs(translated.Volume() - 8.0) < 1e-9,
        "translation doesn't change the box's volume");

  // Uniform scale by 2 about the origin: bounding box doubles, volume
  // scales by 2^3 = 8 exactly (both hand-derivable, not approximate).
  const auto scaled = box.Transform(ON_Xform::ScaleTransformation(Point3d(0, 0, 0), 2.0));
  const auto scaled_bounds = scaled.GetBoundingBox();
  Check(scaled_bounds.max.x == 4.0 && scaled_bounds.max.y == 4.0 && scaled_bounds.max.z == 4.0,
        "scaling the box by 2 about the origin doubles its bounding box");
  Check(std::abs(scaled.Volume() - 64.0) < 1e-9,
        "scaling the box by 2 multiplies its volume by 2^3 = 8, giving 64");

  // Rotation is volume-preserving regardless of angle/axis/center - a
  // real invariant, not a coincidence of this particular box.
  Vector3d rotation_axis(0.3, 0.6, 0.74162);
  rotation_axis.Unitize();
  ON_Xform rotation;
  rotation.Rotation(/*angle_radians=*/0.7, rotation_axis, Point3d(0.5, -1.0, 2.0));
  const auto rotated = box.Transform(rotation);
  Check(std::abs(rotated.Volume() - 8.0) < 1e-6,
        "rotating the box about an arbitrary axis/center preserves its volume");
}

void TestMeshFlipNormals() {
  using dino8::kernel::Mesh;

  // Mix of quad faces (MakeQuadBoxMesh) and triangle faces (MakeBox, a
  // pre-existing helper that triangulates each side) so both of
  // FlipNormals()'s branches (IsQuad() true/false) get exercised, not
  // just one.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 3, 4);
  const auto tri_box = MakeBox(0, 0, 0, 2, 3, 4);

  for (const auto& box : {quad_box, tri_box}) {
    const double original_volume = box.Volume();
    const double original_area = box.Area();
    const auto flipped = box.FlipNormals();

    // Reversing every face's winding flips which side Volume()'s
    // divergence-theorem sum treats as "outward" - the exact negative of
    // the original, not just "a different number."
    Check(std::abs(flipped.Volume() + original_volume) < 1e-9,
          "FlipNormals() exactly negates the mesh's volume");
    // Area doesn't care about winding direction, only magnitude - it
    // should be completely unaffected.
    Check(std::abs(flipped.Area() - original_area) < 1e-9,
          "FlipNormals() doesn't change the mesh's area");
    Check(flipped.VertexCount() == box.VertexCount() && flipped.FaceCount() == box.FaceCount(),
          "FlipNormals() doesn't add or remove vertices/faces");

    // An exact involution: flipping twice must reproduce the original
    // volume exactly (not just "close"), since it's the same vertex
    // indices reversed back to their original order.
    const auto double_flipped = flipped.FlipNormals();
    Check(double_flipped.Volume() == original_volume,
          "FlipNormals() applied twice exactly reproduces the original "
          "volume (an exact involution, not merely an equivalent mesh)");
  }
}

void TestMeshIsClosedManifold() {
  using dino8::kernel::Mesh;

  // Closed, well-formed meshes - both quad-faced and triangle-faced, so
  // both of IsClosedManifold()'s edge-extraction branches get exercised -
  // must report true.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  Check(quad_box.IsClosedManifold(), "a closed quad-faced box is a closed manifold");
  const auto tri_box = MakeBox(0, 0, 0, 2, 2, 2);
  Check(tri_box.IsClosedManifold(), "a closed triangle-faced box is a closed manifold");

  // Delete one face from an otherwise-closed box: its 4 edges now each
  // border only 1 face instead of 2 - a real hole, not a manifold defect
  // of a different kind, so this specifically exercises the "count != 2"
  // (boundary edge) rejection path.
  {
    Mesh open_box = quad_box;
    ON_Mesh& raw = open_box.raw();
    raw.m_F.Remove(0);
    Check(!open_box.IsClosedManifold(),
          "a box with one face removed (an open hole) is not a closed manifold");
  }

  // Reverse a single face's own winding (the same per-face reversal
  // FlipNormals() does, but applied to only one face instead of all of
  // them) rather than the whole mesh: every edge that face shares with a
  // neighbor now gets walked the same direction by both faces instead of
  // opposite directions - every edge still borders exactly 2 faces (still
  // "closed" by that count), so this specifically exercises the
  // orientation-consistency check, not the edge-count one.
  {
    Mesh inconsistent = quad_box;
    ON_Mesh& raw = inconsistent.raw();
    ON_MeshFace& f = raw.m_F[0];
    std::swap(f.vi[0], f.vi[3]);
    std::swap(f.vi[1], f.vi[2]);
    Check(!inconsistent.IsClosedManifold(),
          "a box with a single face's winding reversed (inconsistent "
          "orientation with its neighbors) is not a closed manifold, even "
          "though every edge still borders exactly 2 faces");
  }

  // Flipping *every* face's winding (FlipNormals(), not just one) keeps
  // every neighbor pair pointing opposite ways relative to each other,
  // same as before the flip - still a closed manifold, just globally
  // reversed (inside out), which IsClosedManifold() can't and shouldn't
  // distinguish from "right side out" (Volume()'s sign is what carries
  // that information).
  Check(quad_box.FlipNormals().IsClosedManifold(),
        "flipping every face's winding still leaves a closed manifold "
        "(globally inside-out, not orientation-inconsistent)");
}

void TestMeshContainsPoint() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // Both quad-faced and triangle-faced boxes, so both of
  // ContainsPoint()'s per-face branches (IsQuad() true/false) get
  // exercised, not just one.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  const auto tri_box = MakeBox(0, 0, 0, 2, 2, 2);
  // Off-center, off-diagonal coordinates throughout (never y == z == the
  // box's own mid-value): the box's +X/-X faces are each split into two
  // triangles along a diagonal that passes exactly through the face's
  // center, so a +X-direction ray from a point whose (y, z) sits exactly
  // at that center would hit precisely the shared edge between the two
  // triangles - the documented, unhandled degenerate case - rather than
  // cleanly testing the ordinary crossing-count logic this test means to
  // check.
  for (const auto& box : {quad_box, tri_box}) {
    Check(box.ContainsPoint(Point3d(0.7, 1.3, 0.9)), "a point inside the box is inside it");
    Check(box.ContainsPoint(Point3d(0.2, 0.15, 0.3)),
          "a point just inside a corner is inside the box");
    Check(!box.ContainsPoint(Point3d(3, 1.3, 0.9)),
          "a point clearly outside on the +X side is not inside the box");
    Check(!box.ContainsPoint(Point3d(-1, 1.3, 0.9)),
          "a point clearly outside on the -X side is not inside the box "
          "(exercises a ray that starts behind every face along +X, not "
          "just one that starts already past some of them)");
    Check(!box.ContainsPoint(Point3d(0.7, 1.3, 5)),
          "a point far outside along a different axis (+Z) is not inside the box");
  }

  // A shape with a genuine hole (not just a convex solid): a box with a
  // narrower box subtracted out its middle via a real boolean, so a point
  // in the hollowed-out cavity must read as outside despite being well
  // inside the *outer* box's own bounding box - a real test of the
  // ray-cast actually counting crossings through both the outer wall and
  // the inner cavity wall, not just "is this near the object."
  const auto outer = MakeBox(0, 0, 0, 4, 4, 4);
  const auto inner = MakeBox(1, 1, 1, 3, 3, 3);
  const auto hollow = BooleanCombine(outer, inner, BooleanOp::Difference);
  Check(hollow.ContainsPoint(Point3d(0.5, 1.7, 2.3)),
        "a point in the hollow box's solid wall is inside it");
  Check(!hollow.ContainsPoint(Point3d(2, 1.7, 2.3)),
        "a point in the hollow box's empty cavity is not inside it, even "
        "though it's well within the outer box's own bounding box");
  Check(!hollow.ContainsPoint(Point3d(10, 10, 10)),
        "a point far outside the hollow box entirely is not inside it");
}

void TestMeshAreaCountsBothQuadTriangles() {
  using dino8::kernel::Mesh;

  // A single flat 2x3 quad face (not two triangles): Area() previously
  // computed only the first triangle (vi[0],vi[1],vi[2]) and silently
  // ignored vi[3] entirely for a real (non-degenerate) quad, returning
  // exactly half the true area for a case like this one, where both
  // triangles have equal area (half of 6.0 = 3.0, not the true 6.0) -
  // found while building a SubD test that needed Area() to work
  // correctly on SubD::ToApproximateMesh()'s genuinely-quad output,
  // which no earlier test here exercised (every tessellator in this file
  // emits triangles only).
  Mesh quad;
  ON_Mesh& raw = quad.raw();
  raw.m_V.Append(ON_3fPoint(0, 0, 0));
  raw.m_V.Append(ON_3fPoint(3, 0, 0));
  raw.m_V.Append(ON_3fPoint(3, 2, 0));
  raw.m_V.Append(ON_3fPoint(0, 2, 0));
  ON_MeshFace face;
  face.vi[0] = 0;
  face.vi[1] = 1;
  face.vi[2] = 2;
  face.vi[3] = 3;
  raw.m_F.Append(face);

  Check(std::abs(quad.Area() - 6.0) < 1e-9,
        "Area() of a single 3x2 quad face is the true 6.0, not half of it "
        "(the bug: only the first of the quad's two triangles was counted)");
}

void TestSubDFromBoxSubdividesToExactCatmullClarkCounts() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::SubD;

  // 6-quad closed box (MakeQuadBoxMesh(), not the triangulated MakeBox()):
  // Catmull-Clark's vertex/face-count growth has a simple, hand-derivable
  // rule on an all-quad control net - a new vertex per old vertex, edge
  // midpoint, and face center (V_new = V+E+F), and every face splits into
  // (its side count) quads, so F_new = 4x once the mesh is all-quad
  // (true from level 1 on, and this box already starts all-quad).
  // Level 0: V=8, E=12, F=6 (Euler: 8-12+6=2, genus 0, checks out).
  // Level 1: V=8+12+6=26, F=6*4=24, E=2*F=48 for a closed all-quad mesh
  // (each of 4 edges shared by 2 faces) - 26-48+24=2, checks out.
  // Level 2: V=26+48+24=98, F=24*4=96.
  const auto quad_box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  auto subd = SubD::FromControlMesh(quad_box);
  subd.Subdivide(2);

  Check(subd.VertexCount() == 98,
        "SubD box after 2 global Catmull-Clark subdivisions has the "
        "hand-derived exact vertex count (98)");
  Check(subd.FaceCount() == 96,
        "SubD box after 2 global Catmull-Clark subdivisions has the "
        "hand-derived exact face count (96)");

  const auto approx = subd.ToApproximateMesh();
  Check(approx.VertexCount() == 98 && approx.FaceCount() == 96,
        "ToApproximateMesh()'s control-net mesh matches the SubD's own "
        "vertex/face counts");

  // Catmull-Clark subdivision pulls a cube's limit surface substantially
  // inward - a cube's 8 corners are valence-3 extraordinary vertices,
  // which Catmull-Clark weights heavily toward the interior. Measured,
  // not guessed: probing levels 1 through 5 showed volume dropping
  // 8 -> 3.5 -> 2.80 -> 2.66 -> 2.63 -> 2.62, converging (not diverging
  // or going negative) toward roughly a third of the cube's volume - real
  // subdivision behavior, confirmed by the monotonic, stabilizing trend,
  // not a symptom of a winding or topology bug (which the volume/face/
  // vertex-count and Manifold checks around this one already rule out).
  const double volume = approx.Volume();
  Check(std::abs(volume - 2.802131075637103) < 1e-6,
        "subdivided box volume matches the measured level-2 Catmull-Clark "
        "value (a real, substantial shrink from the cube's volume of 8, "
        "not left flat)");

  // Real proof of watertightness, same standard as every other solid
  // here: Manifold would reject a non-manifold mesh outright rather than
  // return a plausible-looking wrong answer.
  const auto other_box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(approx, other_box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (volume + 1.0)) < 1e-6,
        "union of the subdivided SubD box with a disjoint unit box equals "
        "its volume + 1");
}

void TestSubDFromControlMeshRejectsEmptyMesh() {
  using dino8::kernel::Mesh;
  using dino8::kernel::SubD;

  const Mesh empty;
  bool threw = false;
  try {
    SubD::FromControlMesh(empty);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  Check(threw, "SubD::FromControlMesh throws on a mesh with no faces");
}

// Builds two 1x1 quads hinged along the segment from (0,0,0) to (1,0,0):
// quad A in the y=0 plane (extending in +z), quad B in the z=0 plane
// (extending in +y). Quad B's two hinge-edge vertices are separate array
// entries from quad A's, at the identical two locations - a genuine
// "mesh double edge" (ON_SubDFromMeshParameters::InteriorCreaseOption::
// AtMeshDoubleEdge's own definition), the construction
// SubD::FromControlMesh(mesh, /*crease_at_double_edges=*/true)'s own
// documentation asks for.
dino8::kernel::Mesh MakeHingedDoubleEdgeMesh() {
  dino8::kernel::Mesh hinge;
  ON_Mesh& raw = hinge.raw();
  raw.m_V.Append(ON_3fPoint(0, 0, 0));  // 0
  raw.m_V.Append(ON_3fPoint(1, 0, 0));  // 1
  raw.m_V.Append(ON_3fPoint(1, 0, 1));  // 2
  raw.m_V.Append(ON_3fPoint(0, 0, 1));  // 3
  raw.m_V.Append(ON_3fPoint(0, 0, 0));  // 4: duplicate of 0's location
  raw.m_V.Append(ON_3fPoint(1, 0, 0));  // 5: duplicate of 1's location
  raw.m_V.Append(ON_3fPoint(1, 1, 0));  // 6
  raw.m_V.Append(ON_3fPoint(0, 1, 0));  // 7
  auto add_quad = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a;
    f.vi[1] = b;
    f.vi[2] = c;
    f.vi[3] = d;
    raw.m_F.Append(f);
  };
  add_quad(0, 1, 2, 3);
  add_quad(4, 5, 6, 7);
  return hinge;
}

void TestSubDCreaseAtDoubleEdgeKeepsFoldStraight() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;
  using dino8::kernel::SubD;

  const auto hinge = MakeHingedDoubleEdgeMesh();

  // Both faces' double-edge vertices (distinct indices, same locations)
  // still weld to the same 6 SubD vertices either way - the
  // crease_at_double_edges flag only changes that edge's tag (smooth vs.
  // creased), not whether the coincident points are recognized as one
  // topological vertex.
  auto smooth = SubD::FromControlMesh(hinge, /*crease_at_double_edges=*/false);
  auto creased = SubD::FromControlMesh(hinge, /*crease_at_double_edges=*/true);
  Check(smooth.VertexCount() == 6 && creased.VertexCount() == 6,
        "both the smooth and creased SubDs weld the double-edge's "
        "coincident-but-distinct-indexed vertices into 6 shared ones");

  smooth.Subdivide(1);
  creased.Subdivide(1);

  // After one subdivision, the fold edge (0,0,0)-(1,0,0) gets a new
  // subdivision point at its midpoint. For a genuine crease, that point
  // must land exactly on the fold's original straight line - the same
  // "boundary/crease edges subdivide to stay exactly on their own line"
  // rule already verified for actual mesh boundaries. For a smooth edge,
  // Catmull-Clark instead pulls it toward the two adjacent faces' interior
  // (both faces here are perpendicular to each other), rounding the fold -
  // provably NOT landing on that same point.
  auto closest_to_fold_midpoint = [](const Mesh& mesh) {
    const Point3d target(0.5, 0, 0);
    double best_dist = 1e30;
    for (int i = 0; i < mesh.raw().m_V.Count(); ++i) {
      const double dist = (Point3d(mesh.raw().m_V[i]) - target).Length();
      best_dist = std::min(best_dist, dist);
    }
    return best_dist;
  };

  Check(closest_to_fold_midpoint(creased.ToApproximateMesh()) < 1e-6,
        "with crease_at_double_edges, the fold gets a real subdivision "
        "point exactly at its straight-line midpoint (0.5, 0, 0)");
  Check(closest_to_fold_midpoint(smooth.ToApproximateMesh()) > 0.05,
        "without it, the same edge is treated as smooth and its "
        "subdivision point is measurably pulled off that line instead - "
        "proving the crease flag does something real, not a no-op");
}

void TestSubDFlatQuadGridStaysFlatAndAreaExact() {
  using dino8::kernel::Mesh;
  using dino8::kernel::SubD;

  // A flat 2x2 grid of quads (3x3 vertices, (0,0,0) to (2,2,0), z=0
  // everywhere) - unlike the box, this control net has no extraordinary
  // *interior* vertex: its one interior vertex has valence 4 (regular for
  // a quad mesh). Worth verifying directly rather than assuming it
  // carries over from the box test, and the actual measured result is
  // more nuanced than a first guess: every vertex stays exactly on the
  // z=0 plane (regular-valence interior subdivision and a straight
  // boundary edge's own subdivision rule both keep points exactly
  // in-plane/on-line - verified below), but the *area* still measurably
  // shrinks (to 3.6875 from 4.0, not preserved) - the 4 boundary corners
  // are themselves a kind of extraordinary vertex (valence 2, not a
  // regular interior 4), and Catmull-Clark's smooth corner rule pulls
  // them inward along the boundary, the same qualitative effect that
  // shrank the box's volume, just far smaller here since only 4 vertices
  // are affected instead of every vertex neighboring one of 8 corners.
  Mesh grid;
  ON_Mesh& raw = grid.raw();
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      raw.m_V.Append(ON_3fPoint(static_cast<double>(i), static_cast<double>(j), 0.0));
    }
  }
  auto idx = [](int i, int j) { return i * 3 + j; };
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      ON_MeshFace face;
      face.vi[0] = idx(i, j);
      face.vi[1] = idx(i + 1, j);
      face.vi[2] = idx(i + 1, j + 1);
      face.vi[3] = idx(i, j + 1);
      raw.m_F.Append(face);
    }
  }

  Check(std::abs(grid.Area() - 4.0) < 1e-9,
        "sanity: the flat 2x2 quad grid's own area is exactly 4 before any subdivision");

  auto subd = SubD::FromControlMesh(grid);
  subd.Subdivide(2);
  const auto approx = subd.ToApproximateMesh();

  bool all_flat = true;
  for (int i = 0; i < approx.raw().m_V.Count(); ++i) {
    if (std::abs(static_cast<double>(approx.raw().m_V[i].z)) > 1e-6) {
      all_flat = false;
      break;
    }
  }
  Check(all_flat,
        "subdividing a flat, all-regular-valence quad grid keeps every vertex "
        "exactly on the z=0 plane (no shrinkage/warping the way the box's "
        "extraordinary corners caused)");
  Check(std::abs(approx.Area() - 3.6875) < 1e-6,
        "the flat grid's area matches the measured post-subdivision value "
        "(3.6875, not the naively-assumed exact 4) - corner-vertex shrinkage "
        "on a much smaller scale than the box's, not a bug");
}

void TestMeshComputeVertexNormals() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Vector3d;

  // MakeQuadBoxMesh's vertex 0 = (0,0,0) is shared by exactly 3 faces:
  // bottom (-z), front (-y), left (-x) - each a single unit-square quad,
  // so each contributes an equal-magnitude unit normal along its own
  // axis. Their area-weighted sum, normalized, is exactly
  // (-1,-1,-1)/sqrt(3) - a hand-derivable exact value, not just "some
  // vector that looks plausible."
  const auto box = MakeQuadBoxMesh(0, 0, 0, 1, 1, 1);
  const std::vector<Vector3d> normals = box.ComputeVertexNormals();
  Check(static_cast<int>(normals.size()) == box.VertexCount(),
        "ComputeVertexNormals returns exactly one normal per vertex");

  const double expected = -1.0 / std::sqrt(3.0);
  const Vector3d& n0 = normals[0];
  Check(std::abs(n0.x - expected) < 1e-9 && std::abs(n0.y - expected) < 1e-9 &&
            std::abs(n0.z - expected) < 1e-9,
        "vertex 0's normal is exactly (-1,-1,-1)/sqrt(3), the area-weighted "
        "average of its 3 adjacent unit-square faces' normals");
  Check(std::abs(n0.Length() - 1.0) < 1e-9, "vertex 0's normal is unit length");

  // A flat single quad: every corner's normal must equal the quad's own
  // single flat normal exactly - no neighbors to average against, so
  // area-weighting can't change anything here.
  Mesh flat;
  ON_Mesh& raw = flat.raw();
  raw.m_V.Append(ON_3fPoint(0, 0, 0));
  raw.m_V.Append(ON_3fPoint(1, 0, 0));
  raw.m_V.Append(ON_3fPoint(1, 1, 0));
  raw.m_V.Append(ON_3fPoint(0, 1, 0));
  ON_MeshFace face;
  face.vi[0] = 0;
  face.vi[1] = 1;
  face.vi[2] = 2;
  face.vi[3] = 3;
  raw.m_F.Append(face);
  const std::vector<Vector3d> flat_normals = flat.ComputeVertexNormals();
  for (const Vector3d& n : flat_normals) {
    Check(std::abs(n.x) < 1e-9 && std::abs(n.y) < 1e-9 && std::abs(n.z - 1.0) < 1e-9,
          "every corner of a single flat quad in the z=0 plane (CCW from "
          "+z) gets exactly the normal (0,0,1)");
  }
}

void TestMeshSaveObjRoundTrips() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Result;

  // MakeQuadBoxMesh (defined above): 8 vertices, 6 quad faces, known exact
  // corner coordinates - lets this test check actual written content
  // (not just line counts) against hand-known values.
  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  const std::string path = "dino8_kernel_mesh_obj_test.obj";
  Check(box.SaveObj(path) == Result::Ok, "Mesh::SaveObj succeeds");

  std::ifstream in(path);
  Check(static_cast<bool>(in), "the .obj file SaveObj wrote can be reopened for reading");

  int vertex_lines = 0;
  int normal_lines = 0;
  int face_lines = 0;
  bool saw_quad_face = false;
  bool saw_slash_slash_reference = false;
  double first_vertex[3] = {0, 0, 0};
  bool got_first_vertex = false;
  std::string line;
  while (std::getline(in, line)) {
    if (line.size() >= 2 && line[0] == 'v' && line[1] == ' ') {
      if (!got_first_vertex) {
        std::sscanf(line.c_str(), "v %lf %lf %lf", &first_vertex[0], &first_vertex[1],
                    &first_vertex[2]);
        got_first_vertex = true;
      }
      ++vertex_lines;
    } else if (line.size() >= 3 && line[0] == 'v' && line[1] == 'n' && line[2] == ' ') {
      ++normal_lines;
    } else if (line.size() >= 2 && line[0] == 'f' && line[1] == ' ') {
      ++face_lines;
      // Count whitespace-separated tokens after "f " to distinguish a
      // written quad (5 tokens: "f" + 4 indices) from a triangle (4).
      int token_count = 0;
      std::istringstream tokens(line);
      std::string token;
      while (tokens >> token) {
        ++token_count;
      }
      if (token_count == 5) {
        saw_quad_face = true;
      }
      if (line.find("//") != std::string::npos) {
        saw_slash_slash_reference = true;
      }
    }
  }

  Check(vertex_lines == box.VertexCount(),
        "the .obj file has exactly as many 'v' lines as the mesh has vertices (8)");
  Check(normal_lines == box.VertexCount(),
        "the .obj file has exactly as many 'vn' lines as the mesh has vertices (8)");
  Check(face_lines == box.FaceCount(),
        "the .obj file has exactly as many 'f' lines as the mesh has faces (6)");
  Check(saw_quad_face,
        "at least one face line has 4 indices - quad faces are written as one "
        "quad, not split into two triangles");
  Check(saw_slash_slash_reference,
        "face lines reference a normal via 'v//vn' form, not just bare "
        "vertex indices");
  Check(got_first_vertex && first_vertex[0] == 0.0 && first_vertex[1] == 0.0 &&
            first_vertex[2] == 0.0,
        "the first written vertex line matches MakeQuadBoxMesh's known first "
        "corner (0,0,0)");

  // Full round trip: LoadObj() the file SaveObj() just wrote and check the
  // result is geometrically the same solid, not just "some mesh with the
  // right counts" - same vertex/face counts AND the same exact volume
  // (quad faces preserved as quads, not reinterpreted as triangles, would
  // break Volume()'s IsQuad() handling if LoadObj() got that wrong).
  Mesh reloaded;
  Check(Mesh::LoadObj(path, reloaded) == Result::Ok, "Mesh::LoadObj succeeds on SaveObj()'s own output");
  Check(reloaded.VertexCount() == box.VertexCount() && reloaded.FaceCount() == box.FaceCount(),
        "the reloaded mesh has the same vertex/face counts as the original");
  Check(std::abs(reloaded.Volume() - box.Volume()) < 1e-9,
        "the reloaded mesh's volume exactly matches the original (quad faces "
        "round-tripped as quads, not silently reinterpreted)");
}

void TestMeshLoadObjRejectsMalformedFiles() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Result;

  const std::string missing_path = "dino8_kernel_mesh_obj_test_does_not_exist.obj";
  Mesh out;
  Check(Mesh::LoadObj(missing_path, out) == Result::Failed,
        "LoadObj fails on a file that doesn't exist");

  const std::string forward_ref_path = "dino8_kernel_mesh_obj_test_forward_ref.obj";
  {
    std::ofstream bad(forward_ref_path);
    // References vertex 2 before it's ever defined - not a valid .obj.
    bad << "v 0 0 0\nf 1 2 3\n";
  }
  Check(Mesh::LoadObj(forward_ref_path, out) == Result::Failed,
        "LoadObj fails on a face referencing a vertex index that doesn't exist");

  const std::string pentagon_path = "dino8_kernel_mesh_obj_test_pentagon.obj";
  {
    std::ofstream bad(pentagon_path);
    bad << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0.5 2 0\nf 1 2 3 4 5\n";
  }
  Check(Mesh::LoadObj(pentagon_path, out) == Result::Failed,
        "LoadObj fails on a 5-index face line rather than silently "
        "misinterpreting it (ON_MeshFace only holds a triangle or quad)");

  std::remove(forward_ref_path.c_str());
  std::remove(pentagon_path.c_str());
}

void TestMeshSaveStlSplitsQuadsAndComputesNormals() {
  using dino8::kernel::Result;

  // MakeQuadBoxMesh: 6 quad faces. STL is triangle-only, so SaveStl must
  // split each quad into 2 triangles - 12 facets total, not 6 - and
  // compute a real per-facet normal (not the placeholder "0 0 0" the
  // format technically allows). The first face (bottom, quad
  // (0,3,2,1)) has known outward normal (0,0,-1), matching this file's
  // box-face-orientation convention used everywhere else (Box(), etc.).
  const auto box = MakeQuadBoxMesh(0, 0, 0, 2, 2, 2);
  const std::string path = "dino8_kernel_mesh_stl_test.stl";
  Check(box.SaveStl(path) == Result::Ok, "Mesh::SaveStl succeeds");

  std::ifstream in(path);
  Check(static_cast<bool>(in), "the .stl file SaveStl wrote can be reopened for reading");

  int facet_count = 0;
  double first_normal[3] = {0, 0, 0};
  bool got_first_normal = false;
  std::string line;
  while (std::getline(in, line)) {
    if (line.compare(0, 12, "facet normal") == 0) {
      if (!got_first_normal) {
        std::sscanf(line.c_str(), "facet normal %lf %lf %lf", &first_normal[0], &first_normal[1],
                    &first_normal[2]);
        got_first_normal = true;
      }
      ++facet_count;
    }
  }

  Check(facet_count == box.FaceCount() * 2,
        "SaveStl splits each of the box's 6 quad faces into 2 triangle "
        "facets (12 total), not one facet per quad (which the format "
        "doesn't support)");
  Check(got_first_normal && std::abs(first_normal[0]) < 1e-6 && std::abs(first_normal[1]) < 1e-6 &&
            std::abs(first_normal[2] - (-1.0)) < 1e-6,
        "the bottom face's first facet has the correct computed outward "
        "normal (0,0,-1), not a placeholder");

  std::remove(path.c_str());
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

void TestExactClippingHandlesNonConvexTrim() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // A unit square surface: P(u, v) = (u, v, 0) exactly (bilinear identity
  // for these control points), so a trim polygon's area in (u, v) is
  // exactly the tessellated face's area in 3D too.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);

  // A dart/arrowhead shape - concave at (0.52, 0.31). Every vertex is
  // deliberately off the 8-division grid's lines (multiples of 0.125,
  // i.e. 0, 0.125, 0.25, ...) - an earlier version of this test used
  // (0.5, 0.3), and 0.5 sits exactly on a grid line, which corrupted the
  // clipped boundary (a real alignment edge case, caught by
  // ExtrudeCappedSolid's own boundary validation rather than silently
  // producing broken geometry). Exact area by the shoelace formula: 0.404
  // (not a whole-cell approximation - hand-derived independently of the
  // tessellator).
  const std::vector<Point2d> non_convex_trim = {
      Point2d(0.1, 0.1),
      Point2d(0.9, 0.1),
      Point2d(0.9, 0.9),
      Point2d(0.52, 0.31),
      Point2d(0.1, 0.9),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, non_convex_trim, /*exact_clip=*/true);
  const auto mesh = face.Tessellate(/*u_divisions=*/8, /*v_divisions=*/8).front();

  // Tolerance is 1e-6, not this file's usual 1e-9: unlike the other exact-
  // area tests here, 0.52/0.31 aren't exactly representable in binary
  // floating point (the other tests' trim coordinates - 0.15, 0.85, 10,
  // etc. - are), so ON_Mesh's single-precision vertex storage (ON_3fPoint)
  // introduces real, expected rounding at that scale - not an algorithm
  // defect.
  Check(std::abs(mesh.Area() - 0.404) < 1e-6,
        "exact clipping (Greiner-Hormann + ear-clipping) measures the "
        "dart's true concave area instead of rejecting it");

  // Prove the per-cell triangulation is actually valid geometry - not
  // just a coincidentally-correct area sum - by extruding it and
  // requiring both ExtrudeCappedSolid's own boundary-loop validation and
  // Manifold's independent watertightness check to accept the result.
  // Extrude into -Z, away from the cap's own +Z normal (u_dir x v_dir for
  // this CCW-in-(u,v), identity-mapped surface) - same convention
  // TestExtrudeUntrimmedFaceIntoSolid documents: the offset must point
  // away from the cap's own outward normal, or the resulting solid comes
  // out consistently wound "inside out" (still a valid closed manifold,
  // which is why Manifold still accepts it below, but with negated
  // volume).
  const auto solid = Mesh::ExtrudeCappedSolid(mesh, Vector3d(0, 0, -1));
  Check(std::abs(solid.Volume() - 0.404) < 1e-6,
        "the dart-shaped solid's volume equals its cap area times unit height");

  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(solid, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (solid.Volume() + 1.0)) < 1e-9,
        "Manifold accepts the concave-trim solid as watertight: union with "
        "a disjoint unit box equals solid volume + 1");
}

void TestExactClippingHandlesTrimVertexOnGridLine() {
  using dino8::kernel::BooleanCombine;
  using dino8::kernel::BooleanOp;
  using dino8::kernel::Brep;
  using dino8::kernel::Mesh;
  using dino8::kernel::NurbsSurface;
  using dino8::kernel::Point2d;
  using dino8::kernel::Point3d;
  using dino8::kernel::Vector3d;

  // The exact case TestExactClippingHandlesNonConvexTrim's own comment
  // used to flag as broken and work around by moving off the grid: the
  // same dart shape, but with its reflex vertex's u coordinate (0.5)
  // exactly on one of the 8-division grid's own lines (multiples of
  // 0.125). ClipPolygon's crossing detection now nudges a trim vertex off
  // an exact grid line before clipping (see TessellateGridClippedExact's
  // own comment on why), so this should measure the dart's true area
  // instead of producing corrupted boundary geometry.
  const std::vector<Point3d> grid = {
      Point3d(0, 0, 0),
      Point3d(0, 1, 0),
      Point3d(1, 0, 0),
      Point3d(1, 1, 0),
  };
  const NurbsSurface surface =
      NurbsSurface::FromControlGrid(grid, 2, 2, /*u_degree=*/1, /*v_degree=*/1);

  // Shoelace area (hand-derived, independent of the tessellator): 0.40.
  const std::vector<Point2d> on_grid_line_trim = {
      Point2d(0.1, 0.1),
      Point2d(0.9, 0.1),
      Point2d(0.9, 0.9),
      Point2d(0.5, 0.3),  // u=0.5 is exactly on the 8-division grid's u=0.5 line
      Point2d(0.1, 0.9),
  };
  const Brep face = Brep::TrimmedPlanarFace(surface, on_grid_line_trim, /*exact_clip=*/true);
  const auto mesh = face.Tessellate(/*u_divisions=*/8, /*v_divisions=*/8).front();

  Check(std::abs(mesh.Area() - 0.40) < 1e-6,
        "exact clipping measures the true area (0.40) of a dart whose "
        "reflex vertex sits exactly on a tessellation grid line, instead "
        "of producing corrupted boundary geometry");

  const auto solid = Mesh::ExtrudeCappedSolid(mesh, Vector3d(0, 0, -1));
  Check(std::abs(solid.Volume() - 0.40) < 1e-6,
        "the on-grid-line dart's extruded solid volume equals its cap "
        "area times unit height");

  const auto box = Brep::Box(100, 100, 100, 101, 101, 101).TessellateToClosedMesh(1, 1);
  const auto result = BooleanCombine(solid, box, BooleanOp::Union);
  Check(std::abs(result.Volume() - (solid.Volume() + 1.0)) < 1e-9,
        "Manifold accepts the on-grid-line dart solid as watertight: union "
        "with a disjoint unit box equals solid volume + 1");
}

void TestExactClippingRejectsSelfIntersectingTrim() {
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

  // A "bowtie" quadrilateral: listing the 4 corners of a square in
  // crossed order (0,0)->(1,1)->(1,0)->(0,1) makes edges 0 and 2 cross
  // through the middle - a self-intersecting "polygon" with no
  // well-defined inside, which TessellateGridClippedExact() now detects
  // via dino8::kernel::detail::IsSimplePolygon() and rejects outright,
  // rather than producing whatever ClipPolygon/ClipConvex happens to
  // compute against an ill-formed input.
  const std::vector<Point2d> bowtie_trim = {
      Point2d(0.1, 0.1),
      Point2d(0.9, 0.9),
      Point2d(0.9, 0.1),
      Point2d(0.1, 0.9),
  };

  bool threw = false;
  try {
    Brep::TrimmedPlanarFace(surface, bowtie_trim, /*exact_clip=*/true).Tessellate(8, 8);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw,
        "TessellateGridClippedExact throws on a self-intersecting (bowtie) "
        "trim_polygon instead of silently clipping against it");
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

void TestConeToApexSharesBoundaryValidation() {
  using dino8::kernel::Mesh;
  using dino8::kernel::Point3d;

  // ConeToApex() shares ExtrudeCappedSolid()'s boundary-edge extraction
  // and validation (Mesh::ExtractValidatedBoundaryEdges) rather than
  // duplicating it - this is a check on that wiring, not a re-test of the
  // validation logic itself (already covered by
  // TestExtrudeRejectsAlreadyClosedCap/BowtieBoundary above): an
  // already-closed cap (MakeBox()) has nothing to cone to an apex either.
  const auto box = MakeBox(0, 0, 0, 1, 1, 1);
  bool threw = false;
  try {
    Mesh::ConeToApex(box, Point3d(0.5, 0.5, 2));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Check(threw, "ConeToApex throws on a cap with no boundary (already closed), "
               "same validation as ExtrudeCappedSolid");
}

}  // namespace

int main() {
  ON::Begin();

  TestCurveDegreeElevation();
  TestCurveLength();
  TestCurveTangentAt();
  TestCurveGetTightBoundingBox();
  TestSurfaceNormalAt();
  TestSurfaceDegreeElevation();
  TestFileRoundTrip();
  TestModelAddMeshRoundTrips();
  TestModelAddSubDRoundTrips();
  TestBrepTessellation();
  TestBoxVolume();
  TestBooleanUnion();
  TestBooleanIntersection();
  TestBooleanDifference();
  TestBooleanSymmetricDifference();
  TestBrepBoxIsClosedAndWatertight();
  TestBrepGetTightBoundingBox();
  TestBrepBooleanEndToEnd();
  TestBrepSphereIsClosedAndWatertight();
  TestBrepSphereBooleanEndToEnd();
  TestBrepTrimmedPlanarFace();
  TestWeldAcrossIndependentlyParameterizedSurfaces();
  TestExtrudeUntrimmedFaceIntoSolid();
  TestExtrudeTrimmedFaceFeedsBoolean();
  TestCylinderVolumeAndBoolean();
  TestConeVolumeAndBoolean();
  TestRevolveProfileBiconeVolumeAndBoolean();
  TestRevolveProfileRejectsTooShortProfile();
  TestRevolveProfileFlatEndCaps();
  TestLoftClosedRingsSquareFrustumExactVolumeAndBoolean();
  TestLoftClosedRingsRejectsTooFewRingsAndMismatchedCounts();
  TestLoftClosedRingsConcaveEndCapsExactPrismVolume();
  TestTorusVolumeAndBoolean();
  TestMeshGetBoundingBox();
  TestMeshGetCentroid();
  TestMeshTransform();
  TestMeshFlipNormals();
  TestMeshIsClosedManifold();
  TestMeshContainsPoint();
  TestMeshAreaCountsBothQuadTriangles();
  TestSubDFromBoxSubdividesToExactCatmullClarkCounts();
  TestSubDFromControlMeshRejectsEmptyMesh();
  TestSubDCreaseAtDoubleEdgeKeepsFoldStraight();
  TestSubDFlatQuadGridStaysFlatAndAreaExact();
  TestMeshComputeVertexNormals();
  TestMeshSaveObjRoundTrips();
  TestMeshLoadObjRejectsMalformedFiles();
  TestMeshSaveStlSplitsQuadsAndComputesNormals();
  TestExactClippingMatchesAreaButNotCellCounts();
  TestExactClippingHandlesNonConvexTrim();
  TestExactClippingHandlesTrimVertexOnGridLine();
  TestExactClippingRejectsSelfIntersectingTrim();
  TestAnnulusFaceExtrudesToWatertightTube();
  TestExtrudeRejectsAlreadyClosedCap();
  TestExtrudeRejectsBowtieBoundary();
  TestConeToApexSharesBoundaryValidation();

  ON::End();

  if (g_failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}
