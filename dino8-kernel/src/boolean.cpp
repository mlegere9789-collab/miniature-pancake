#include "dino8/kernel/boolean.h"

#include <cmath>
#include <stdexcept>

#include <manifold/manifold.h>

namespace dino8::kernel {

namespace {

// Manifold's own default tolerance is tuned for "a few units" scale models.
// A geometrically meaningful merge/coplanar-collapse tolerance instead
// scales with the mesh's own size, so near-tangent or near-coincident
// features get treated consistently whether the model is millimeter-scale
// or kilometer-scale. This does NOT fix the separate, structural limitation
// that ON_Mesh (and therefore this Mesh/ToManifold/FromManifold round trip)
// stores vertex coordinates as single-precision floats throughout the
// kernel - see the note on FromManifold below - it only makes Manifold's
// own robustness pass (coplanar triangle merging, short-edge collapse)
// scale-aware once the mesh has already been built and quantized to float.
// SetTolerance() only ever raises the effective tolerance (it clamps to
// max(epsilon, requested) when the requested value is smaller than the
// current tolerance - see Manifold::SetTolerance), so this is always safe
// to apply.
double AdaptiveManifoldTolerance(const ON_Mesh& raw) {
  ON_BoundingBox bbox;
  if (!raw.GetBoundingBox(bbox) || !bbox.IsValid()) return 0.0;
  const double diag = bbox.Diagonal().Length();
  if (!std::isfinite(diag) || diag <= 0) return 0.0;
  // 1e-6 of the mesh's own diagonal: small enough not to erase real detail
  // on ordinary models, but large enough to bridge the float-precision
  // noise (~1e-7 relative) that FromManifold's single-precision vertices
  // already introduce at any scale.
  return diag * 1e-6;
}

manifold::Manifold ToManifold(const Mesh& mesh) {
  const ON_Mesh& raw = mesh.raw();

  manifold::MeshGL gl;
  gl.numProp = 3;
  gl.vertProperties.reserve(static_cast<size_t>(raw.m_V.Count()) * 3);
  for (int i = 0; i < raw.m_V.Count(); ++i) {
    const ON_3fPoint& v = raw.m_V[i];
    gl.vertProperties.push_back(v.x);
    gl.vertProperties.push_back(v.y);
    gl.vertProperties.push_back(v.z);
  }

  gl.triVerts.reserve(static_cast<size_t>(raw.m_F.Count()) * 6);
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& face = raw.m_F[i];
    gl.triVerts.push_back(static_cast<uint32_t>(face.vi[0]));
    gl.triVerts.push_back(static_cast<uint32_t>(face.vi[1]));
    gl.triVerts.push_back(static_cast<uint32_t>(face.vi[2]));
    if (face.IsQuad()) {
      gl.triVerts.push_back(static_cast<uint32_t>(face.vi[0]));
      gl.triVerts.push_back(static_cast<uint32_t>(face.vi[2]));
      gl.triVerts.push_back(static_cast<uint32_t>(face.vi[3]));
    }
  }

  manifold::Manifold m(gl);
  if (m.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::BooleanCombine: input mesh is not a valid closed "
        "manifold (Manifold::Status() != NoError) - booleans require "
        "watertight solids, not arbitrary tessellated surfaces");
  }
  const double tol = AdaptiveManifoldTolerance(raw);
  if (tol > 0) m = m.SetTolerance(tol);
  return m;
}

// NOTE - genuine kernel-level precision ceiling, not fixed here: `ON_Mesh`
// (dino8::kernel::Mesh's underlying storage, used identically by every
// other mesh consumer in this codebase - rendering, BrepMesher, Remesh,
// etc., not just booleans) stores vertex coordinates in `ON_3fPoint`,
// i.e. single-precision floats, independent of Manifold's own (double-
// precision) internal representation. Every round trip through
// ToManifold()/FromManifold() therefore quantizes coordinates to ~7
// significant decimal digits. For a solid whose absolute coordinate
// magnitude is large (e.g. ~1e6 units), that quantization step is itself
// on the order of 0.1 unit - larger than many real modelling tolerances -
// regardless of how tight the tolerance passed to Manifold is. This is not
// something AdaptiveManifoldTolerance (above) can compensate for: that
// tolerance only controls how aggressively Manifold merges/collapses
// features that are already coincident to within the stored (already-
// quantized) float coordinates; it cannot recover precision the float
// storage already discarded. Fixing this for real would mean migrating
// dino8::kernel::Mesh off ON_Mesh's single-precision vertex array to a
// double-precision store throughout the kernel (rendering, meshing, every
// other consumer) - a large, invasive change out of scope here. See
// dino8-app/tests/adversarial_corpus_notes.md for the specific test case
// this limits and why it's a structural ceiling rather than a boolean bug.
Mesh FromManifold(const manifold::Manifold& m) {
  const manifold::MeshGL gl = m.GetMeshGL();

  Mesh mesh;
  ON_Mesh& raw = mesh.raw();

  raw.m_V.Reserve(static_cast<int>(gl.NumVert()));
  for (uint32_t i = 0; i < gl.NumVert(); ++i) {
    raw.m_V.Append(ON_3fPoint(gl.vertProperties[i * gl.numProp + 0],
                               gl.vertProperties[i * gl.numProp + 1],
                               gl.vertProperties[i * gl.numProp + 2]));
  }

  raw.m_F.Reserve(static_cast<int>(gl.NumTri()));
  for (uint32_t i = 0; i < gl.NumTri(); ++i) {
    ON_MeshFace face;
    face.vi[0] = static_cast<int>(gl.triVerts[i * 3 + 0]);
    face.vi[1] = static_cast<int>(gl.triVerts[i * 3 + 1]);
    face.vi[2] = static_cast<int>(gl.triVerts[i * 3 + 2]);
    face.vi[3] = face.vi[2];
    raw.m_F.Append(face);
  }

  return mesh;
}

manifold::OpType ToManifoldOp(BooleanOp op) {
  switch (op) {
    case BooleanOp::Union:
      return manifold::OpType::Add;
    case BooleanOp::Intersection:
      return manifold::OpType::Intersect;
    case BooleanOp::Difference:
      return manifold::OpType::Subtract;
    case BooleanOp::SymmetricDifference:
      break;  // handled separately in BooleanCombine - not a single Manifold op
  }
  throw std::invalid_argument("dino8::kernel::BooleanCombine: unknown BooleanOp");
}

}  // namespace

Mesh BooleanCombine(const Mesh& a, const Mesh& b, BooleanOp op) {
  if (op == BooleanOp::SymmetricDifference) {
    const Mesh union_mesh = BooleanCombine(a, b, BooleanOp::Union);
    const Mesh intersection_mesh = BooleanCombine(a, b, BooleanOp::Intersection);
    return BooleanCombine(union_mesh, intersection_mesh, BooleanOp::Difference);
  }

  const manifold::Manifold result =
      ToManifold(a).Boolean(ToManifold(b), ToManifoldOp(op));
  if (result.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::BooleanCombine: boolean operation failed "
        "(Manifold::Status() != NoError after Boolean())");
  }
  return FromManifold(result);
}

std::pair<Mesh, Mesh> SplitByPlane(const Mesh& mesh, Vector3d plane_normal, double plane_offset) {
  const auto halves = ToManifold(mesh).SplitByPlane(
      manifold::vec3(plane_normal.x, plane_normal.y, plane_normal.z), plane_offset);
  if (halves.first.Status() != manifold::Manifold::Error::NoError ||
      halves.second.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::SplitByPlane: split failed (Manifold::Status() != "
        "NoError after SplitByPlane())");
  }
  return {FromManifold(halves.first), FromManifold(halves.second)};
}

Mesh ConvexHull(const std::vector<Point3d>& points) {
  if (points.size() < 4) {
    throw std::invalid_argument(
        "dino8::kernel::ConvexHull: needs at least 4 points - fewer can't "
        "bound a nonzero 3D volume");
  }

  std::vector<manifold::vec3> manifold_points;
  manifold_points.reserve(points.size());
  for (const Point3d& p : points) {
    manifold_points.emplace_back(p.x, p.y, p.z);
  }

  const manifold::Manifold hull = manifold::Manifold::Hull(manifold_points);
  if (hull.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::ConvexHull: Manifold::Hull failed (Manifold::"
        "Status() != NoError) - e.g. every point coplanar, so no 3D hull "
        "exists");
  }
  return FromManifold(hull);
}

Mesh Simplify(const Mesh& mesh, double tolerance) {
  const manifold::Manifold simplified = ToManifold(mesh).Simplify(tolerance);
  if (simplified.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::Simplify: Manifold::Simplify failed (Manifold::"
        "Status() != NoError)");
  }
  return FromManifold(simplified);
}

Mesh MinkowskiSum(const Mesh& a, const Mesh& b) {
  const manifold::Manifold result = ToManifold(a).MinkowskiSum(ToManifold(b));
  if (result.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::MinkowskiSum: Manifold::MinkowskiSum failed "
        "(Manifold::Status() != NoError)");
  }
  return FromManifold(result);
}

Mesh MinkowskiDifference(const Mesh& a, const Mesh& b) {
  const manifold::Manifold result = ToManifold(a).MinkowskiDifference(ToManifold(b));
  if (result.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::MinkowskiDifference: Manifold::MinkowskiDifference "
        "failed (Manifold::Status() != NoError)");
  }
  return FromManifold(result);
}

std::vector<Mesh> Decompose(const Mesh& mesh) {
  const std::vector<manifold::Manifold> pieces = ToManifold(mesh).Decompose();
  std::vector<Mesh> result;
  result.reserve(pieces.size());
  for (const manifold::Manifold& piece : pieces) {
    if (piece.Status() != manifold::Manifold::Error::NoError) {
      throw std::runtime_error(
          "dino8::kernel::Decompose: Manifold::Decompose produced an "
          "invalid piece (Manifold::Status() != NoError)");
    }
    result.push_back(FromManifold(piece));
  }
  return result;
}

double MinGap(const Mesh& a, const Mesh& b, double search_length) {
  return ToManifold(a).MinGap(ToManifold(b), search_length);
}

Mesh RefineToLength(const Mesh& mesh, double length) {
  const manifold::Manifold refined = ToManifold(mesh).RefineToLength(length);
  if (refined.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::RefineToLength: Manifold::RefineToLength failed "
        "(Manifold::Status() != NoError)");
  }
  return FromManifold(refined);
}

Mesh SmoothAndRefine(const Mesh& mesh, double target_length, double min_sharp_angle,
                      double min_smoothness) {
  const manifold::Manifold smoothed =
      ToManifold(mesh).SmoothOut(min_sharp_angle, min_smoothness);
  if (smoothed.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::SmoothAndRefine: Manifold::SmoothOut failed "
        "(Manifold::Status() != NoError)");
  }
  const manifold::Manifold refined = smoothed.RefineToLength(target_length);
  if (refined.Status() != manifold::Manifold::Error::NoError) {
    throw std::runtime_error(
        "dino8::kernel::SmoothAndRefine: Manifold::RefineToLength failed "
        "(Manifold::Status() != NoError)");
  }
  return FromManifold(refined);
}

size_t CountDegenerateTriangles(const Mesh& mesh) { return ToManifold(mesh).NumDegenerateTris(); }

}  // namespace dino8::kernel
