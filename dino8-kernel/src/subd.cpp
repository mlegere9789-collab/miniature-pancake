#include "dino8/kernel/subd.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"

namespace dino8::kernel {

namespace {

// True if `uv`'s 4 corners are axis-aligned in their own (u, v) domain -
// every one of the 4 edges is a constant-u or constant-v isocurve - the
// same "plain quad" shape check Brep::Tessellate()'s own
// IsAxisAlignedQuadUv (brep.cpp, anonymous namespace, so not reachable
// from here) uses for the identical seam-matching purpose. Duplicated
// rather than shared across the two files: it's a few lines of pure
// (u, v) arithmetic with no Brep-specific state, and FromBrep() below is
// this file's only caller.
bool IsAxisAlignedQuadUvForSubD(const std::array<Point2d, 4>& uv) {
  for (int e = 0; e < 4; ++e) {
    const Point2d& from = uv[static_cast<size_t>(e)];
    const Point2d& to = uv[static_cast<size_t>((e + 1) % 4)];
    const double tol_u = 1e-9 * (1.0 + std::fabs(from.x) + std::fabs(to.x));
    const double tol_v = 1e-9 * (1.0 + std::fabs(from.y) + std::fabs(to.y));
    const bool u_constant = std::fabs(to.x - from.x) <= tol_u;
    const bool v_constant = std::fabs(to.y - from.y) <= tol_v;
    if (!u_constant && !v_constant) return false;  // a genuinely oblique edge
  }
  return true;
}

}  // namespace

SubD SubD::FromControlMesh(const Mesh& control_mesh, bool crease_at_double_edges) {
  SubD result;
  const ON_SubDFromMeshParameters& params = crease_at_double_edges
                                                 ? ON_SubDFromMeshParameters::InteriorCreases
                                                 : ON_SubDFromMeshParameters::Smooth;
  const ON_SubD* built = ON_SubD::CreateFromMesh(&control_mesh.raw(), &params, &result.subd_);
  if (built == nullptr) {
    throw std::runtime_error(
        "dino8::kernel::SubD::FromControlMesh: ON_SubD::CreateFromMesh failed "
        "(control_mesh may have no faces or invalid topology)");
  }
  return result;
}

SubD SubD::FromNurbsSurface(const NurbsSurface& surface, int u_divisions, int v_divisions) {
  if (u_divisions < 1 || v_divisions < 1) {
    throw std::invalid_argument(
        "dino8::kernel::SubD::FromNurbsSurface: u_divisions and v_divisions "
        "must be at least 1");
  }
  const Interval u_domain = surface.Domain(0);
  const Interval v_domain = surface.Domain(1);

  Mesh grid;
  ON_Mesh& raw = grid.raw();
  const int u_points = u_divisions + 1;
  const int v_points = v_divisions + 1;
  const auto grid_index = [v_points](int i, int j) { return i * v_points + j; };

  raw.m_V.Reserve(u_points * v_points);
  for (int i = 0; i < u_points; ++i) {
    const double u = u_domain.min + (u_domain.max - u_domain.min) * (static_cast<double>(i) / u_divisions);
    for (int j = 0; j < v_points; ++j) {
      const double v = v_domain.min + (v_domain.max - v_domain.min) * (static_cast<double>(j) / v_divisions);
      raw.m_V.Append(ON_3fPoint(surface.PointAt(u, v)));
    }
  }

  raw.m_F.Reserve(u_divisions * v_divisions);
  for (int i = 0; i < u_divisions; ++i) {
    for (int j = 0; j < v_divisions; ++j) {
      ON_MeshFace f;
      f.vi[0] = grid_index(i, j);
      f.vi[1] = grid_index(i + 1, j);
      f.vi[2] = grid_index(i + 1, j + 1);
      f.vi[3] = grid_index(i, j + 1);
      raw.m_F.Append(f);
    }
  }

  return SubD::FromControlMesh(grid);
}

SubD SubD::FromBrep(const Brep& brep, int divisions, double weld_tolerance) {
  if (divisions < 1) {
    throw std::invalid_argument("dino8::kernel::SubD::FromBrep: divisions must be at least 1");
  }
  const int face_count = brep.FaceCount();
  if (face_count <= 0) {
    throw std::invalid_argument("dino8::kernel::SubD::FromBrep: brep has no faces");
  }

  std::vector<Mesh> face_grids;
  face_grids.reserve(static_cast<size_t>(face_count));

  for (int face_index = 0; face_index < face_count; ++face_index) {
    if (!brep.FaceCoversWholeDomain(face_index)) {
      throw std::runtime_error(
          "dino8::kernel::SubD::FromBrep: face " + std::to_string(face_index) +
          " is trimmed - FromBrep only accepts a Brep whose faces are all untrimmed planar quads");
    }
    const ON_BrepFace& face = brep.raw().m_F[face_index];
    const ON_Surface* face_surface = face.SurfaceOf();
    if (face_surface == nullptr) {
      throw std::runtime_error("dino8::kernel::SubD::FromBrep: face " + std::to_string(face_index) +
                                " has no surface");
    }
    ON_NurbsSurface nurbs_surface;
    if (const auto* cast = ON_NurbsSurface::Cast(face_surface)) {
      nurbs_surface = *cast;
    } else if (face_surface->GetNurbForm(nurbs_surface) <= 0) {
      throw std::runtime_error("dino8::kernel::SubD::FromBrep: face " + std::to_string(face_index) +
                                " has no NURBS form");
    }
    NurbsSurface surface;
    surface.raw() = nurbs_surface;
    if (!surface.IsPlanar()) {
      throw std::runtime_error(
          "dino8::kernel::SubD::FromBrep: face " + std::to_string(face_index) +
          " is not planar - FromBrep only accepts a Brep whose faces are all untrimmed planar quads");
    }

    const Interval du = surface.Domain(0);
    const Interval dv = surface.Domain(1);
    const std::array<Point2d, 4> corner_uv = {Point2d(du.min, dv.min), Point2d(du.max, dv.min),
                                               Point2d(du.max, dv.max), Point2d(du.min, dv.max)};
    if (!IsAxisAlignedQuadUvForSubD(corner_uv)) {
      throw std::runtime_error("dino8::kernel::SubD::FromBrep: face " + std::to_string(face_index) +
                                " is not an axis-aligned quad in its own (u, v) domain");
    }
    std::array<Point3d, 4> corner;
    for (int c = 0; c < 4; ++c) {
      corner[static_cast<size_t>(c)] =
          surface.PointAt(corner_uv[static_cast<size_t>(c)].x, corner_uv[static_cast<size_t>(c)].y);
    }
    {
      double scale = 0.0;
      for (const Point3d& p : corner) scale = std::max(scale, p.MaximumCoordinate());
      const double tol = 1e-9 * (1.0 + scale);
      bool distinct = true;
      for (int c = 0; c < 4 && distinct; ++c) {
        for (int d = c + 1; d < 4; ++d) {
          if (corner[static_cast<size_t>(c)].DistanceTo(corner[static_cast<size_t>(d)]) <= tol) {
            distinct = false;
            break;
          }
        }
      }
      if (!distinct) {
        throw std::runtime_error("dino8::kernel::SubD::FromBrep: face " + std::to_string(face_index) +
                                  " does not have 4 distinct corners");
      }
    }

    // Bilinear grid over the 4 corner points - see FromBrep()'s own doc
    // comment (subd.h) for why this, not surface.PointAt(u, v) at
    // intermediate parameters, is what makes adjacent faces' shared
    // edges land on literally the same 3D points.
    auto bilinear = [&](double a, double b) {
      return (1.0 - a) * (1.0 - b) * corner[0] + a * (1.0 - b) * corner[1] + a * b * corner[2] +
             (1.0 - a) * b * corner[3];
    };
    Mesh grid;
    ON_Mesh& raw = grid.raw();
    const int points = divisions + 1;
    const auto grid_index = [points](int i, int j) { return i * points + j; };
    raw.m_V.Reserve(points * points);
    for (int i = 0; i < points; ++i) {
      const double a = static_cast<double>(i) / divisions;
      for (int j = 0; j < points; ++j) {
        const double b = static_cast<double>(j) / divisions;
        raw.m_V.Append(ON_3fPoint(bilinear(a, b)));
      }
    }
    raw.m_F.Reserve(divisions * divisions);
    for (int i = 0; i < divisions; ++i) {
      for (int j = 0; j < divisions; ++j) {
        ON_MeshFace f;
        f.vi[0] = grid_index(i, j);
        f.vi[1] = grid_index(i + 1, j);
        f.vi[2] = grid_index(i + 1, j + 1);
        f.vi[3] = grid_index(i, j + 1);
        raw.m_F.Append(f);
      }
    }
    // Match Brep::Tessellate()'s own per-face orientation convention so
    // every face of a closed Brep points outward consistently.
    if (face.m_bRev) grid = grid.FlipNormals();
    face_grids.push_back(std::move(grid));
  }

  const Mesh combined = Mesh::MergeAndWeld(face_grids, weld_tolerance);
  return SubD::FromControlMesh(combined, /*crease_at_double_edges=*/true);
}

void SubD::Subdivide(int levels) {
  if (levels <= 0) {
    return;
  }
  if (!subd_.GlobalSubdivide(static_cast<unsigned int>(levels))) {
    throw std::runtime_error(
        "dino8::kernel::SubD::Subdivide: ON_SubD::GlobalSubdivide failed "
        "(levels may exceed ON_SubD::maximum_subd_level, or the SubD is empty)");
  }
}

Mesh SubD::ToApproximateMesh() const {
  Mesh result;
  const ON_Mesh* out =
      subd_.GetControlNetMesh(&result.raw(), ON_SubDGetControlNetMeshPriority::Geometry);
  if (out == nullptr) {
    throw std::runtime_error(
        "dino8::kernel::SubD::ToApproximateMesh: ON_SubD::GetControlNetMesh failed");
  }
  // GetControlNetMesh() emits one ON_Mesh vertex per FACE-CORNER, not one
  // per shared ON_SubDVertex - confirmed directly: a SubD built by sharing
  // an existing vertex across faces added incrementally (e.g. Symmetrize()'s
  // own FindOrAddVertex/FindOrAddFace welds at the SubD level - VertexCount()
  // already reports the true, deduplicated count) still comes back from
  // this call with duplicate ON_Mesh vertices at bit-identical positions
  // wherever that shared vertex is a corner of more than one face, so the
  // exported mesh fails IsClosedManifold() even though the SubD itself is a
  // genuinely closed, manifold body (Check() reports 0 naked/non-manifold
  // edges). A SubD built directly via FromControlMesh() from an
  // already-closed mesh doesn't hit this (its own round trip is already
  // exact - see TestSubDMeshRoundTripIsExactAtLevelZero), so this welds
  // ONLY when there's something to weld: CombineIdenticalVertices() merges
  // bit-identical positions (ignoring normals/texture coordinates - this is
  // a topological "approximate" mesh, not a shaded render output, and
  // per-face-corner normals are expected to differ at a shared vertex until
  // an actual smoothing pass runs) and reports whether it changed anything,
  // so an already-deduplicated export (nothing coincident to merge) is
  // untouched.
  result.raw().CombineIdenticalVertices(/*bIgnoreVertexNormals=*/true, /*bIgnoreTextureCoordinates=*/true);
  return result;
}

Mesh SubD::Boolean(const SubD& other, BooleanOp op) const {
  // Both ToApproximateMesh() calls, and BooleanCombine() itself, throw
  // std::runtime_error on their own respective failures (an unbuildable
  // control-net mesh; a non-closed/non-manifold operand) - none of that is
  // caught or reinterpreted here, so a caller sees exactly the failing
  // step's own message.
  return BooleanCombine(ToApproximateMesh(), other.ToApproximateMesh(), op);
}

SubD SubD::Transform(const ON_Xform& xform) const {
  SubD result = *this;  // ON_SubD's copy ctor deep-copies (verified in SetEdgeSharpness()'s own comment)
  if (!result.subd_.Transform(xform)) {
    throw std::invalid_argument("dino8::kernel::SubD::Transform: ON_SubD::Transform failed (xform is not a valid transform)");
  }
  return result;
}

SubD SubD::Symmetrize(Vector3d plane_normal, double plane_offset, double point_tolerance) const {
  if (!plane_normal.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::SubD::Symmetrize: plane_normal must be nonzero");
  }

  const auto signed_distance = [&](const Point3d& p) {
    return plane_normal.x * p.x + plane_normal.y * p.y + plane_normal.z * p.z - plane_offset;
  };
  const auto reflect = [&](const Point3d& p, double signed_dist) {
    return p - 2.0 * signed_dist * plane_normal;
  };

  // Snapshot the ORIGINAL faces' corner points (and which of them sit ON
  // the mirror plane) before mutating `result` - once mirrored faces
  // start getting added below, result's own iterators would otherwise
  // walk those too.
  struct FaceCorners {
    std::vector<Point3d> points;
    std::vector<bool> on_plane;
  };
  std::vector<FaceCorners> original_faces;
  ON_SubDFaceIterator fit = subd_.FaceIterator();
  for (const ON_SubDFace* f = fit.FirstFace(); f != nullptr; f = fit.NextFace()) {
    const unsigned int count = f->EdgeCount();
    FaceCorners corners;
    corners.points.reserve(count);
    corners.on_plane.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
      const ON_SubDVertex* v = f->Vertex(i);
      if (v == nullptr) {
        throw std::runtime_error(
            "dino8::kernel::SubD::Symmetrize: a face has a null vertex");
      }
      const Point3d p = v->ControlNetPoint();
      corners.points.push_back(p);
      corners.on_plane.push_back(std::abs(signed_distance(p)) <= point_tolerance);
    }
    original_faces.push_back(std::move(corners));
  }

  SubD result = *this;  // keeps the original half exactly as-is

  // A pre-existing edge between two vertices already IN `result` (found by
  // walking v0's own edge list - `result` has no ON_SubD-level "find edge
  // by vertex pair" lookup, so this is the direct equivalent). Returns
  // nullptr if none connects them yet.
  const auto find_edge_between = [](const ON_SubDVertex* v0, const ON_SubDVertex* v1) -> const ON_SubDEdge* {
    const unsigned int n = v0->EdgeCount();
    for (unsigned int i = 0; i < n; ++i) {
      const ON_SubDEdge* e = v0->Edge(i);
      if (e != nullptr && e->OtherEndVertex(v0) == v1) return e;
    }
    return nullptr;
  };

  for (const FaceCorners& face : original_faces) {
    const unsigned int count = static_cast<unsigned int>(face.points.size());
    // Reversed order: a reflection always flips orientation, so walking
    // the original loop backwards is what keeps the mirrored face's
    // winding consistent with the rest of `result` (see this method's
    // own "flip" doc comment).
    std::vector<const ON_SubDVertex*> mirrored_vertices(count);
    std::vector<bool> mirrored_on_plane(count);
    for (unsigned int i = 0; i < count; ++i) {
      const unsigned int src = count - 1 - i;
      const Point3d& original_point = face.points[src];
      const double s = signed_distance(original_point);
      // On-plane: reuse the SAME vertex (found by position in `result`,
      // which already holds it from the initial copy) instead of adding
      // a duplicate at its own unchanged position - this is the "weld".
      const Point3d target = std::abs(s) <= point_tolerance ? original_point : reflect(original_point, s);
      const ON_SubDVertex* v = result.subd_.FindOrAddVertex(&target.x, point_tolerance);
      if (v == nullptr) {
        throw std::runtime_error(
            "dino8::kernel::SubD::Symmetrize: ON_SubD::FindOrAddVertex failed");
      }
      mirrored_vertices[i] = v;
      mirrored_on_plane[i] = face.on_plane[src];
    }

    // An edge whose BOTH endpoints are on-plane already existed before
    // this mirrored face was added - it's exactly one of the naked
    // boundary edges being welded shut, real by-construction Crease tag
    // and all (not a user's own intentional sharp edge - that's a
    // separate per-edge sharpness weight, see SetEdgeSharpness(),
    // untouched here). FindOrAddFace() below only resolves NEW (Unset)
    // components, so this edge's own stale Crease survives it unless
    // reset to Unset here first - a real bug this file's own
    // TestSubDSymmetrizeWeldsSeamAndFlipsMirroredFaces caught via
    // ToApproximateMesh().IsClosedManifold() (topology *and* tag
    // sensitive, unlike SubD::Check()'s own edge/vertex counts, which
    // stayed clean throughout): GetControlNetMesh() duplicates a mesh
    // vertex per side of any still-Crease-tagged edge, even one with two
    // faces, so the "welded" seam rendered as two coincident but UNWELDED
    // mesh boundaries.
    // The seam VERTICES themselves need the exact same treatment as their
    // edges, for the exact same reason: a vertex's own tag is just as
    // sticky as an edge's under UpdateVertexTags(bUnsetVertexTagsOnly=
    // true) (both skip anything not already Unset), so a boundary
    // vertex's stale Crease/Dart tag survives even once every one of its
    // incident edges above has been correctly reset and re-inferred as
    // Smooth - confirmed directly: without this, doubled.IsValid() itself
    // fails (a Crease-tagged vertex with zero Crease edges among its
    // incident set is an inconsistent SubD, not just a mesh-export
    // cosmetic issue like the edge-only fix above already caught).
    for (unsigned int i = 0; i < count; ++i) {
      if (!mirrored_on_plane[i]) continue;
      const ON_SubDVertex* v = mirrored_vertices[i];
      if (v->m_vertex_tag == ON_SubDVertexTag::Crease || v->m_vertex_tag == ON_SubDVertexTag::Dart) {
        const_cast<ON_SubDVertex*>(v)->m_vertex_tag = ON_SubDVertexTag::Unset;
      }
    }
    for (unsigned int i = 0; i < count; ++i) {
      const unsigned int i1 = (i + 1) % count;
      if (!mirrored_on_plane[i] || !mirrored_on_plane[i1]) continue;
      const ON_SubDEdge* seam = find_edge_between(mirrored_vertices[i], mirrored_vertices[i1]);
      if (seam != nullptr && seam->m_edge_tag == ON_SubDEdgeTag::Crease) {
        const_cast<ON_SubDEdge*>(seam)->m_edge_tag = ON_SubDEdgeTag::Unset;
      }
    }

    ON_SubDFace* added =
        result.subd_.FindOrAddFace(ON_SubDEdgeTag::Unset, mirrored_vertices.data(), count);
    if (added == nullptr) {
      throw std::runtime_error(
          "dino8::kernel::SubD::Symmetrize: ON_SubD::FindOrAddFace failed for a mirrored face");
    }
  }

  // New vertices/edges above (and every seam edge/vertex just reset to
  // Unset) get resolved from context here - an edge with faces on both
  // sides becomes Smooth, a still-naked one stays a boundary edge, etc.
  // Restricted to Unset-only on purpose: anything else in this SubD keeps
  // whatever tag it already had, including a genuinely intentional sharp
  // edge unrelated to this mirror plane.
  result.subd_.UpdateAllTagsAndSectorCoefficients(/*bUnsetValuesOnly=*/true);
  return result;
}

int SubD::FaceCount() const { return static_cast<int>(subd_.FaceCount()); }
int SubD::VertexCount() const { return static_cast<int>(subd_.VertexCount()); }
int SubD::EdgeCount() const { return static_cast<int>(subd_.EdgeCount()); }

bool SubD::IsValid() const {
  // Low bit set -> ON_SubD::IsValid() suppresses its own ON_Error() call
  // on failure (masked off again before use - never actually
  // dereferenced as a real ON_TextLog*, see this method's own header
  // comment). This is a validity check, not an assertion, so a "no"
  // answer must never have that side effect.
  return subd_.IsValid(reinterpret_cast<ON_TextLog*>(1));
}

namespace {

// A tiny union-find over a dense [0, n) index space - shared by Check()'s
// two independent components-questions (per-vertex fan connectivity, and
// whole-SubD body connectivity) so neither needs to duplicate the other.
class UnionFind {
 public:
  explicit UnionFind(size_t n) : parent_(n) {
    for (size_t i = 0; i < n; ++i) parent_[i] = i;
  }
  size_t Find(size_t i) {
    while (parent_[i] != i) {
      parent_[i] = parent_[parent_[i]];
      i = parent_[i];
    }
    return i;
  }
  void Union(size_t a, size_t b) {
    a = Find(a);
    b = Find(b);
    if (a != b) parent_[a] = b;
  }

 private:
  std::vector<size_t> parent_;
};

// Groups `points` by mutual `tolerance`-closeness via a spatial grid +
// union-find - the same clustering shape mesh.cpp's own WeldGroups() uses
// for Mesh::CheckReport::duplicate_vertices, reimplemented here rather than
// shared because that one keys off ON_Mesh vertex INDICES into a fixed
// mesh_.m_V array, while this one just takes a flat point list. Returns one
// group id per input point (points[i] and points[j] are in the same group
// iff result[i] == result[j]); NOT necessarily transitive by the pairwise
// tolerance test alone (the same single-linkage caveat WeldGroups()'s own
// comment documents - a chain of near-tolerance pairs can join into one
// group spanning more than `tolerance`), which is fine here since this is
// only ever used to COUNT/LOCATE existing near-coincident vertices, never
// to decide how far apart a weld is allowed to move one.
std::vector<size_t> GroupByProximity(const std::vector<ON_3dPoint>& points, double tolerance) {
  const size_t n = points.size();
  UnionFind uf(n);
  const double cell = std::max(tolerance, 1e-12);
  const auto key_of = [&](const ON_3dPoint& p) {
    return std::make_tuple(static_cast<long long>(std::floor(p.x / cell)),
                            static_cast<long long>(std::floor(p.y / cell)),
                            static_cast<long long>(std::floor(p.z / cell)));
  };
  std::map<std::tuple<long long, long long, long long>, std::vector<size_t>> grid;
  for (size_t i = 0; i < n; ++i) grid[key_of(points[i])].push_back(i);

  for (size_t i = 0; i < n; ++i) {
    const auto [kx, ky, kz] = key_of(points[i]);
    for (long long dx = -1; dx <= 1; ++dx) {
      for (long long dy = -1; dy <= 1; ++dy) {
        for (long long dz = -1; dz <= 1; ++dz) {
          const auto it = grid.find(std::make_tuple(kx + dx, ky + dy, kz + dz));
          if (it == grid.end()) continue;
          for (const size_t j : it->second) {
            if (j <= i) continue;
            if (points[i].DistanceTo(points[j]) <= tolerance) uf.Union(i, j);
          }
        }
      }
    }
  }

  std::vector<size_t> groups(n);
  for (size_t i = 0; i < n; ++i) groups[i] = uf.Find(i);
  return groups;
}

}  // namespace

SubD::SubDCheckReport SubD::Check(double duplicate_vertex_tolerance) const {
  SubDCheckReport report;

  ON_SubDEdgeIterator eit = subd_.EdgeIterator();
  for (const ON_SubDEdge* e = eit.FirstEdge(); e != nullptr; e = eit.NextEdge()) {
    const unsigned int face_count = e->FaceCount();
    if (face_count == 1) {
      ++report.naked_edges;
    } else if (face_count >= 3) {
      ++report.non_manifold_edges;
      const ON_SubDVertex* v0 = e->Vertex(0);
      const ON_SubDVertex* v1 = e->Vertex(1);
      unsigned int a = v0 != nullptr ? v0->m_id : 0;
      unsigned int b = v1 != nullptr ? v1->m_id : 0;
      if (a > b) std::swap(a, b);
      report.non_manifold_edge_list.emplace_back(a, b);
    }
  }

  // Non-manifold ("bowtie") vertices: for each vertex, union its own
  // incident faces via whichever of the vertex's incident edges they
  // share, then check whether that leaves more than one group. A
  // well-formed vertex's incident faces always form exactly one fan
  // (open, at a boundary vertex, or closed, at an interior one); more
  // than one group means two or more otherwise-unconnected cones of
  // faces meet only at this single point.
  ON_SubDVertexIterator vit = subd_.VertexIterator();
  for (const ON_SubDVertex* v = vit.FirstVertex(); v != nullptr; v = vit.NextVertex()) {
    const unsigned int face_count = v->FaceCount();
    if (face_count < 2) continue;  // 0 or 1 incident face can't be split into >1 group

    std::vector<const ON_SubDFace*> faces(face_count);
    for (unsigned int i = 0; i < face_count; ++i) faces[i] = v->Face(i);
    const auto face_index = [&faces](const ON_SubDFace* f) -> int {
      for (size_t i = 0; i < faces.size(); ++i) {
        if (faces[i] == f) return static_cast<int>(i);
      }
      return -1;
    };

    UnionFind uf(faces.size());
    const unsigned int edge_count = v->EdgeCount();
    for (unsigned int i = 0; i < edge_count; ++i) {
      const ON_SubDEdge* e = v->Edge(i);
      if (e == nullptr) continue;
      const unsigned int edge_face_count = e->FaceCount();
      int first = -1;
      for (unsigned int j = 0; j < edge_face_count; ++j) {
        const int idx = face_index(e->Face(j));
        if (idx < 0) continue;  // this edge's face doesn't touch v - can't happen, defensive only
        if (first < 0) {
          first = idx;
        } else {
          uf.Union(static_cast<size_t>(first), static_cast<size_t>(idx));
        }
      }
    }

    size_t group_count = 0;
    {
      std::vector<bool> seen_root(faces.size(), false);
      for (size_t i = 0; i < faces.size(); ++i) {
        const size_t root = uf.Find(i);
        if (!seen_root[root]) {
          seen_root[root] = true;
          ++group_count;
        }
      }
    }
    if (group_count > 1) {
      ++report.non_manifold_vertices;
      report.non_manifold_vertex_list.push_back(v->m_id);
    }
  }

  // Whole-SubD body count: union every pair of faces that share an edge,
  // then count the distinct groups among ALL faces (not just one
  // vertex's) - the same "faces sharing an edge are the same piece"
  // definition Brep::SplitDisjointPieces()'s own
  // ON_Brep::LabelConnectedComponents() uses for Breps.
  std::vector<const ON_SubDFace*> all_faces;
  ON_SubDFaceIterator fit = subd_.FaceIterator();
  for (const ON_SubDFace* f = fit.FirstFace(); f != nullptr; f = fit.NextFace()) {
    all_faces.push_back(f);
  }
  if (!all_faces.empty()) {
    std::unordered_map<const ON_SubDFace*, size_t> all_face_index;
    all_face_index.reserve(all_faces.size() * 2);
    for (size_t i = 0; i < all_faces.size(); ++i) all_face_index.emplace(all_faces[i], i);

    UnionFind uf(all_faces.size());
    ON_SubDEdgeIterator eit2 = subd_.EdgeIterator();
    for (const ON_SubDEdge* e = eit2.FirstEdge(); e != nullptr; e = eit2.NextEdge()) {
      const unsigned int face_count = e->FaceCount();
      if (face_count < 2) continue;
      const auto first_it = all_face_index.find(e->Face(0));
      if (first_it == all_face_index.end()) continue;  // defensive only - every edge's faces are in the SubD's own face list
      for (unsigned int j = 1; j < face_count; ++j) {
        const auto other_it = all_face_index.find(e->Face(j));
        if (other_it == all_face_index.end()) continue;
        uf.Union(first_it->second, other_it->second);
      }
    }

    std::vector<bool> seen_root(all_faces.size(), false);
    for (size_t i = 0; i < all_faces.size(); ++i) {
      const size_t root = uf.Find(i);
      if (!seen_root[root]) {
        seen_root[root] = true;
        ++report.body_count;
      }
    }
  }

  // Duplicate (coincident-but-distinct) vertices: every vertex is a
  // candidate, grouped purely by spatial proximity (no topological
  // relationship required) via GroupByProximity() above.
  {
    std::vector<unsigned int> vertex_ids;
    std::vector<ON_3dPoint> points;
    ON_SubDVertexIterator vit2 = subd_.VertexIterator();
    for (const ON_SubDVertex* v = vit2.FirstVertex(); v != nullptr; v = vit2.NextVertex()) {
      vertex_ids.push_back(v->m_id);
      points.push_back(v->ControlNetPoint());
    }
    const std::vector<size_t> groups = GroupByProximity(points, duplicate_vertex_tolerance);
    std::unordered_map<size_t, int> group_size;
    for (const size_t g : groups) ++group_size[g];
    for (size_t i = 0; i < groups.size(); ++i) {
      if (group_size[groups[i]] > 1) {
        ++report.duplicate_vertices;
        report.duplicate_vertex_list.push_back(vertex_ids[i]);
      }
    }
  }

  return report;
}

std::vector<SubD> SubD::SplitDisjointPieces() const {
  std::vector<const ON_SubDFace*> all_faces;
  {
    ON_SubDFaceIterator fit = subd_.FaceIterator();
    for (const ON_SubDFace* f = fit.FirstFace(); f != nullptr; f = fit.NextFace()) all_faces.push_back(f);
  }
  if (all_faces.empty()) return {};

  // Exactly Check()'s own body_count grouping (faces sharing an edge, via
  // FaceCount() >= 2), reproduced here rather than shared because that one
  // only needs the group COUNT while this needs the actual membership.
  std::unordered_map<const ON_SubDFace*, size_t> face_index;
  face_index.reserve(all_faces.size() * 2);
  for (size_t i = 0; i < all_faces.size(); ++i) face_index.emplace(all_faces[i], i);

  UnionFind uf(all_faces.size());
  {
    ON_SubDEdgeIterator eit = subd_.EdgeIterator();
    for (const ON_SubDEdge* e = eit.FirstEdge(); e != nullptr; e = eit.NextEdge()) {
      const unsigned int face_count = e->FaceCount();
      if (face_count < 2) continue;
      const auto first_it = face_index.find(e->Face(0));
      if (first_it == face_index.end()) continue;  // defensive only - every edge's faces are in the SubD's own face list
      for (unsigned int j = 1; j < face_count; ++j) {
        const auto other_it = face_index.find(e->Face(j));
        if (other_it == face_index.end()) continue;
        uf.Union(first_it->second, other_it->second);
      }
    }
  }

  // Group faces by root, preserving first-encountered order so the
  // returned pieces come back in a stable, reproducible order rather than
  // whatever order union-find roots happen to land on.
  std::vector<size_t> root_order;
  std::unordered_map<size_t, std::vector<size_t>> groups;
  for (size_t i = 0; i < all_faces.size(); ++i) {
    const size_t root = uf.Find(i);
    auto it = groups.find(root);
    if (it == groups.end()) {
      groups.emplace(root, std::vector<size_t>{i});
      root_order.push_back(root);
    } else {
      it->second.push_back(i);
    }
  }

  if (root_order.size() <= 1) return {*this};

  std::vector<SubD> pieces;
  pieces.reserve(root_order.size());
  for (const size_t root : root_order) {
    const std::vector<size_t>& member_faces = groups[root];

    std::unordered_set<unsigned int> vertex_ids_in_piece;
    for (const size_t idx : member_faces) {
      const ON_SubDFace* f = all_faces[idx];
      const unsigned int n = f->EdgeCount();
      for (unsigned int j = 0; j < n; ++j) {
        const ON_SubDVertex* v = f->Vertex(j);
        if (v != nullptr) vertex_ids_in_piece.insert(v->m_id);
      }
    }

    // Same snapshot-and-replay rebuild Weld() uses above: add every
    // member vertex via AddVertexForExperts() (preserving its original
    // id and position), rebuild every member face via FindOrAddFace(),
    // then reapply each wholly-interior original edge's tag/sharpness.
    // Walking THIS SubD's own vertex iterator (rather than
    // vertex_ids_in_piece's own unordered order) keeps each piece's
    // vertex insertion order matching the original SubD's, same as
    // Weld()'s own vertices_snapshot does.
    ON_SubD piece_subd;
    ON_SubDVertexIterator vit = subd_.VertexIterator();
    for (const ON_SubDVertex* v = vit.FirstVertex(); v != nullptr; v = vit.NextVertex()) {
      if (vertex_ids_in_piece.count(v->m_id) == 0) continue;
      const ON_3dPoint p = v->ControlNetPoint();
      if (piece_subd.AddVertexForExperts(v->m_id, ON_SubDVertexTag::Unset, &p.x, 0, 0) == nullptr) {
        throw std::runtime_error(
            "dino8::kernel::SubD::SplitDisjointPieces: ON_SubD::AddVertexForExperts failed");
      }
    }

    for (const size_t idx : member_faces) {
      const ON_SubDFace* f = all_faces[idx];
      const unsigned int n = f->EdgeCount();
      std::vector<const ON_SubDVertex*> corners(n);
      for (unsigned int j = 0; j < n; ++j) {
        const ON_SubDVertex* v = f->Vertex(j);
        corners[j] = v != nullptr ? piece_subd.VertexFromId(v->m_id) : nullptr;
        if (corners[j] == nullptr) {
          throw std::runtime_error(
              "dino8::kernel::SubD::SplitDisjointPieces: a rebuilt face corner vertex is missing");
        }
      }
      if (piece_subd.FindOrAddFace(ON_SubDEdgeTag::Unset, corners.data(), corners.size()) == nullptr) {
        throw std::runtime_error(
            "dino8::kernel::SubD::SplitDisjointPieces: ON_SubD::FindOrAddFace failed while rebuilding a face");
      }
    }

    {
      ON_SubDEdgeIterator eit = subd_.EdgeIterator();
      for (const ON_SubDEdge* e = eit.FirstEdge(); e != nullptr; e = eit.NextEdge()) {
        if (e->FaceCount() != 2) continue;
        if (e->m_vertex[0] == nullptr || e->m_vertex[1] == nullptr) continue;
        if (vertex_ids_in_piece.count(e->m_vertex[0]->m_id) == 0) continue;
        if (vertex_ids_in_piece.count(e->m_vertex[1]->m_id) == 0) continue;
        const ON_SubDVertex* a = piece_subd.VertexFromId(e->m_vertex[0]->m_id);
        const ON_SubDVertex* b = piece_subd.VertexFromId(e->m_vertex[1]->m_id);
        if (a == nullptr || b == nullptr) continue;
        const ON_SubDEdge* pe = piece_subd.FindEdge(a, b).Edge();
        if (pe == nullptr) continue;  // defensive only - both endpoints are always in this piece's face(s)
        const_cast<ON_SubDEdge*>(pe)->m_edge_tag = e->m_edge_tag;
        if (e->m_edge_tag == ON_SubDEdgeTag::Smooth || e->m_edge_tag == ON_SubDEdgeTag::SmoothX) {
          const_cast<ON_SubDEdge*>(pe)->SetSharpnessForExperts(e->Sharpness(/*bUseCreaseSharpness=*/false));
        }
      }
    }

    piece_subd.UpdateAllTagsAndSectorCoefficients(/*bUnsetValuesOnly=*/true);
    SubD piece;
    piece.subd_ = piece_subd;
    pieces.push_back(std::move(piece));
  }
  return pieces;
}

int SubD::CreaseEdgeCount() const {
  int count = 0;
  ON_SubDEdgeIterator eit = subd_.EdgeIterator();
  for (const ON_SubDEdge* e = eit.FirstEdge(); e != nullptr; e = eit.NextEdge()) {
    if (e->IsCrease()) {
      ++count;
    }
  }
  return count;
}

bool SubD::SetEdgeSharpness(const Point3d& p0, const Point3d& p1, double sharpness,
                            double point_tolerance) {
  return SetEdgeSharpness(p0, p1, sharpness, sharpness, point_tolerance);
}

bool SubD::SetEdgeSharpness(const Point3d& p0, const Point3d& p1,
                            double sharpness_at_p0, double sharpness_at_p1,
                            double point_tolerance) {
  if (!(sharpness_at_p0 >= 0.0) || sharpness_at_p0 > ON_SubDEdgeSharpness::MaximumValue) {
    return false;
  }
  if (!(sharpness_at_p1 >= 0.0) || sharpness_at_p1 > ON_SubDEdgeSharpness::MaximumValue) {
    return false;
  }
  const ON_SubDVertex* v0 = subd_.FindVertex(&p0.x, point_tolerance);
  const ON_SubDVertex* v1 = subd_.FindVertex(&p1.x, point_tolerance);
  if (v0 == nullptr || v1 == nullptr) {
    return false;
  }
  const ON_SubDEdge* e = subd_.FindEdge(v0, v1).Edge();
  if (e == nullptr || !e->IsSmooth()) {
    return false;
  }
  // ON_SubDEdgeSharpness::FromInterval(s0, s1) is stored positionally
  // against the edge's OWN m_vertex[0]/m_vertex[1] order, which need not
  // match the (p0, p1) order the caller passed in - so map the caller's
  // per-point weights onto the edge's own end indices before writing.
  const bool p0_is_end0 = (e->Vertex(0u) == v0);
  const double s_end0 = p0_is_end0 ? sharpness_at_p0 : sharpness_at_p1;
  const double s_end1 = p0_is_end0 ? sharpness_at_p1 : sharpness_at_p0;
  // SetSharpnessForExperts is the same primitive OpenNURBS' own
  // ON_SubD::AddEdge(..., ON_SubDEdgeSharpness) overloads call on a
  // freshly-created edge - here applied to an existing one found via the
  // const FindVertex/FindEdge accessors, which is why the const_cast: it
  // just writes one field (ON_SubDEdge::m_sharpness), verified by reading
  // its implementation, with no other cached state to invalidate.
  const_cast<ON_SubDEdge*>(e)->SetSharpnessForExperts(ON_SubDEdgeSharpness::FromInterval(s_end0, s_end1));
  return true;
}

SubDEdgeSharpnessInfo SubD::EdgeSharpnessAt(const Point3d& p0, const Point3d& p1,
                                            double point_tolerance) const {
  SubDEdgeSharpnessInfo info;
  const ON_SubDVertex* v0 = subd_.FindVertex(&p0.x, point_tolerance);
  const ON_SubDVertex* v1 = subd_.FindVertex(&p1.x, point_tolerance);
  if (v0 == nullptr || v1 == nullptr) {
    return info;
  }
  const ON_SubDEdge* e = subd_.FindEdge(v0, v1).Edge();
  if (e == nullptr || !e->IsSmooth()) {
    return info;
  }
  // Same p0/p1-vs-m_vertex[0]/[1] mapping SetEdgeSharpness()'s own
  // per-end overload already uses, applied here to reading.
  const bool p0_is_end0 = (e->Vertex(0u) == v0);
  info.sharpness_at_p0 = p0_is_end0 ? e->EndSharpness(0u) : e->EndSharpness(1u);
  info.sharpness_at_p1 = p0_is_end0 ? e->EndSharpness(1u) : e->EndSharpness(0u);
  info.found = true;
  return info;
}

bool SubD::SetCrease(const Point3d& p0, const Point3d& p1, bool crease, double point_tolerance) {
  const ON_SubDVertex* v0 = subd_.FindVertex(&p0.x, point_tolerance);
  const ON_SubDVertex* v1 = subd_.FindVertex(&p1.x, point_tolerance);
  if (v0 == nullptr || v1 == nullptr) {
    return false;
  }
  const ON_SubDEdge* e = subd_.FindEdge(v0, v1).Edge();
  if (e == nullptr) {
    return false;
  }
  const ON_SubDComponentPtr cptr = ON_SubDComponentPtr::Create(e);
  const unsigned int changed = subd_.SetEdgeTags(
      &cptr, 1, crease ? ON_SubDEdgeTag::Crease : ON_SubDEdgeTag::Smooth);
  return changed == 1;
}

namespace {

// The other of `v`'s naked (single-face) edges - not `exclude` - or
// nullptr if there isn't exactly one such edge. A boundary vertex has
// exactly 2 naked edges; this is how SubD::CapBoundaryLoop() walks from
// one to the "next" one around the loop.
const ON_SubDEdge* OtherNakedEdge(const ON_SubDVertex* v, const ON_SubDEdge* exclude) {
  if (v == nullptr) return nullptr;
  const ON_SubDEdge* found = nullptr;
  for (unsigned int i = 0; i < v->EdgeCount(); ++i) {
    const ON_SubDEdge* e = v->Edge(i);
    if (e == nullptr || e == exclude || e->FaceCount() != 1) continue;
    if (found != nullptr) return nullptr;  // a bowtie's other side - ambiguous here, not guessed
    found = e;
  }
  return found;
}

}  // namespace

bool SubD::CapBoundaryLoop(const Point3d& start, double point_tolerance) {
  const ON_SubDVertex* v0 = subd_.FindVertex(&start.x, point_tolerance);
  if (v0 == nullptr) return false;

  const ON_SubDEdge* first_edge = nullptr;
  for (unsigned int i = 0; i < v0->EdgeCount(); ++i) {
    const ON_SubDEdge* e = v0->Edge(i);
    if (e != nullptr && e->FaceCount() == 1) {
      first_edge = e;
      break;
    }
  }
  if (first_edge == nullptr) return false;

  std::vector<const ON_SubDEdge*> loop;
  const ON_SubDEdge* e = first_edge;
  const ON_SubDVertex* v = v0;
  const unsigned int budget = subd_.EdgeCount();
  for (unsigned int steps = 0; steps <= budget; ++steps) {
    loop.push_back(e);
    const ON_SubDVertex* next_v = e->OtherEndVertex(v);
    if (next_v == nullptr) return false;
    if (next_v == v0) {
      ON_SimpleArray<ON_SubDEdge*> edges(static_cast<int>(loop.size()));
      // AddFace() takes non-const ON_SubDEdge* - the same const_cast
      // pattern OpenNURBS' own AddEdge()/SetEdgeTags() callers use on
      // pointers obtained from const accessors (FindVertex()/Edge()
      // here), never on anything actually declared const by the caller.
      for (const ON_SubDEdge* le : loop) edges.Append(const_cast<ON_SubDEdge*>(le));
      ON_SubDFace* face = subd_.AddFace(edges);
      if (face == nullptr) return false;

      std::vector<ON_SubDComponentPtr> cptrs;
      cptrs.reserve(loop.size());
      for (const ON_SubDEdge* le : loop) cptrs.push_back(ON_SubDComponentPtr::Create(le));
      subd_.SetEdgeTags(cptrs.data(), cptrs.size(), ON_SubDEdgeTag::Smooth);
      return true;
    }
    const ON_SubDEdge* next_edge = OtherNakedEdge(next_v, e);
    if (next_edge == nullptr) return false;
    e = next_edge;
    v = next_v;
  }
  return false;
}

namespace {

// The one face incident to both `v0` and `v1` (as one of its own corner
// vertices each), or nullptr if there isn't exactly one such face -
// InsertEdge()'s own vertex-pair-to-face lookup, since OpenNURBS'
// ON_SubD::SplitFace() itself takes a face plus two of its vertices, not
// a bare vertex pair.
const ON_SubDFace* FindSharedFace(const ON_SubDVertex* v0, const ON_SubDVertex* v1) {
  const ON_SubDFace* found = nullptr;
  for (unsigned int i = 0; i < v0->FaceCount(); ++i) {
    const ON_SubDFace* f = v0->Face(i);
    if (f == nullptr) continue;
    bool has_v1 = false;
    for (unsigned int j = 0; j < f->EdgeCount(); ++j) {
      if (f->Vertex(j) == v1) {
        has_v1 = true;
        break;
      }
    }
    if (has_v1) {
      if (found != nullptr) return nullptr;  // ambiguous - shared by 2+ faces
      found = f;
    }
  }
  return found;
}

}  // namespace

bool SubD::InsertEdge(const Point3d& p0, const Point3d& p1, double point_tolerance) {
  const ON_SubDVertex* v0 = subd_.FindVertex(&p0.x, point_tolerance);
  const ON_SubDVertex* v1 = subd_.FindVertex(&p1.x, point_tolerance);
  if (v0 == nullptr || v1 == nullptr || v0 == v1) return false;

  const ON_SubDFace* face = FindSharedFace(v0, v1);
  if (face == nullptr) return false;

  // ON_SubD::SplitFace() takes non-const pointers - the same const_cast
  // pattern CapBoundaryLoop() above already uses on pointers obtained
  // from const accessors (FindVertex() here), never on anything actually
  // declared const by the caller.
  const ON_SubDEdge* inserted = subd_.SplitFace(const_cast<ON_SubDFace*>(face),
                                                 const_cast<ON_SubDVertex*>(v0),
                                                 const_cast<ON_SubDVertex*>(v1));
  return inserted != nullptr;
}

bool SubD::SpinEdge(const Point3d& p0, const Point3d& p1, bool spin_clockwise,
                    double point_tolerance) {
  const ON_SubDVertex* v0 = subd_.FindVertex(&p0.x, point_tolerance);
  const ON_SubDVertex* v1 = subd_.FindVertex(&p1.x, point_tolerance);
  if (v0 == nullptr || v1 == nullptr) return false;

  const ON_SubDEdge* edge = subd_.FindEdge(v0, v1).Edge();
  if (edge == nullptr || !edge->HasInteriorEdgeTopology(true)) return false;

  const ON_SubDEdge* spun =
      subd_.SpinEdge(const_cast<ON_SubDEdge*>(edge), spin_clockwise);
  return spun != nullptr;
}

bool SubD::ExtrudeFace(unsigned int face_id, double distance) {
  if (distance == 0.0) return false;
  const ON_SubDFace* face = subd_.FaceFromId(face_id);
  if (face == nullptr) return false;

  Vector3d normal = face->ControlNetCenterNormal();
  if (!normal.Unitize()) return false;

  const ON_Xform xform = ON_Xform::TranslationTransformation(normal * distance);
  const ON_SubDComponentPtr cptr = ON_SubDComponentPtr::Create(face);
  const unsigned int changed = subd_.ExtrudeComponents(xform, &cptr, 1);
  return changed != 0;
}

bool SubD::ExpandFaces(const std::vector<unsigned int>& face_ids, double distance) {
  if (distance == 0.0) return false;
  if (face_ids.empty()) return false;

  std::vector<const ON_SubDFace*> faces;
  faces.reserve(face_ids.size());
  for (unsigned int id : face_ids) {
    const ON_SubDFace* face = subd_.FaceFromId(id);
    if (face == nullptr) return false;
    for (const ON_SubDFace* existing : faces) {
      if (existing == face) return false;  // duplicate id - ambiguous request
    }
    faces.push_back(face);
  }

  Vector3d direction = Vector3d::ZeroVector;
  for (const ON_SubDFace* face : faces) {
    Vector3d n = face->ControlNetCenterNormal();
    if (!n.Unitize()) return false;
    direction += n;
  }
  if (!direction.Unitize()) return false;

  const ON_Xform xform = ON_Xform::TranslationTransformation(direction * distance);
  std::vector<ON_SubDComponentPtr> cptrs;
  cptrs.reserve(faces.size());
  for (const ON_SubDFace* face : faces) cptrs.push_back(ON_SubDComponentPtr::Create(face));

  const unsigned int changed = subd_.ExtrudeComponents(xform, cptrs.data(), cptrs.size());
  return changed != 0;
}

bool SubD::Weld(unsigned int keep_vertex_id, unsigned int discard_vertex_id, double weld_tolerance) {
  const ON_SubDVertex* v0 = subd_.VertexFromId(keep_vertex_id);
  const ON_SubDVertex* v1 = subd_.VertexFromId(discard_vertex_id);
  if (v0 == nullptr || v1 == nullptr || v0 == v1) return false;
  if (v0->ControlNetPoint().DistanceTo(v1->ControlNetPoint()) > weld_tolerance) return false;
  if (subd_.FindEdge(v0, v1).Edge() != nullptr) return false;  // already connected - nothing to weld

  // Refuse if v0 and v1 are already two distinct corners of the same
  // face - merging them would collapse that face to fewer distinct
  // corners than it actually has, the same "already adjacent" ambiguity
  // convention InsertEdge() above already refuses rather than guessing.
  for (unsigned int i = 0; i < v0->FaceCount(); ++i) {
    const ON_SubDFace* f = v0->Face(i);
    if (f == nullptr) continue;
    for (unsigned int j = 0; j < f->EdgeCount(); ++j) {
      if (f->Vertex(j) == v1) return false;
    }
  }

  // NOTE on why this is a whole-net rebuild rather than local surgery: the
  // obvious approach - ON_SubD::DeleteComponents(v1) to tear down v1 and
  // everything touching it, then FindOrAddFace() the affected faces back
  // together onto v0 - turns out to be unsafe. DeleteComponents()'s own
  // "delete isolated edges" pass (bDeleteIsolatedEdges=true, always on for
  // the public overload - verified by reading ON_SubDimple::
  // DeleteComponents in opennurbs_subd.cpp) also deletes any OTHER vertex
  // left with zero faces once v1's own faces are gone, even if that vertex
  // still has edges - not just v1 itself. A vertex whose only face WAS one
  // of v1's former faces (an ordinary, common case - e.g. two quads placed
  // edge-to-edge share no OTHER face) gets silently swept away too, taking
  // the very corners this method needs to reconnect with it and leaving
  // rebuild_faces holding dangling pointers (confirmed by direct
  // reproduction: welding one coincident corner pair of two disjoint quads
  // this way dropped the vertex count by 4, not 1, and left the result
  // IsValid()==false).
  //
  // So instead: snapshot the CURRENT control net's entire vertex/face/edge
  // set (ids, positions, corner-id lists, tags, sharpness), remap every
  // reference to v1's id onto v0's id, and rebuild a fresh ON_SubD from
  // that snapshot via ON_SubD::AddVertexForExperts() - explicitly
  // documented for exactly this "copying portions of an existing SubD to a
  // new SubD" use case - preserving every ORIGINAL vertex's own id, so a
  // vertex/edge belonging to both the old and new net can be found by the
  // very same id in either. Every original edge's tag and sharpness is
  // reapplied by that same id pair afterward; only a handful of brand-new
  // edges (directly between v0 and a former neighbor of v1) are left
  // Unset for the final recompute to resolve, and no vertex's own tag
  // needs any special-case preservation at all - every vertex starts
  // Unset and is re-derived purely from its (correctly restored) edges,
  // the same "vertex tags are always DERIVED, never stored history" fact
  // Symmetrize()'s own seam handling above already relies on.
  struct VSnap {
    unsigned int id;
    Point3d point;
  };
  std::vector<VSnap> vertices_snapshot;
  {
    ON_SubDVertexIterator vit = subd_.VertexIterator();
    for (const ON_SubDVertex* v = vit.FirstVertex(); v != nullptr; v = vit.NextVertex()) {
      if (v == v1) continue;  // discarded - never re-added
      vertices_snapshot.push_back({v->m_id, v->ControlNetPoint()});
    }
  }

  const auto remap = [&](const ON_SubDVertex* v) { return v == v1 ? keep_vertex_id : v->m_id; };

  struct FSnap {
    std::vector<unsigned int> corner_ids;
  };
  std::vector<FSnap> faces_snapshot;
  {
    ON_SubDFaceIterator fit = subd_.FaceIterator();
    for (const ON_SubDFace* f = fit.FirstFace(); f != nullptr; f = fit.NextFace()) {
      const unsigned int n = f->EdgeCount();
      FSnap fs;
      fs.corner_ids.reserve(n);
      for (unsigned int j = 0; j < n; ++j) fs.corner_ids.push_back(remap(f->Vertex(j)));
      faces_snapshot.push_back(std::move(fs));
    }
  }

  struct ESnap {
    unsigned int a, b;
    ON_SubDEdgeTag tag;
    ON_SubDEdgeSharpness sharpness;
  };
  std::vector<ESnap> edges_snapshot;
  {
    ON_SubDEdgeIterator eit = subd_.EdgeIterator();
    for (const ON_SubDEdge* e = eit.FirstEdge(); e != nullptr; e = eit.NextEdge()) {
      if (e->m_vertex[0] == nullptr || e->m_vertex[1] == nullptr) {
        throw std::runtime_error("dino8::kernel::SubD::Weld: an edge has a null vertex");
      }
      // Only a genuinely INTERIOR edge's (FaceCount()==2) tag is worth
      // reapplying verbatim - it can only ever be Smooth or a real,
      // intentional SetCrease()-set Crease, neither of which this merge
      // ever has reason to change. A naked (FaceCount()==1) edge's own
      // Crease tag is never that: it's purely the "an open SubD's own
      // boundary edges are themselves always creases" construction
      // convention, stale the instant a weld gives the edge a second
      // face - so it's deliberately left OUT of the snapshot (never
      // reapplied), leaving it Unset for UpdateAllTagsAndSectorCoefficients()
      // below to re-derive fresh from whatever its ACTUAL new face count
      // turns out to be: Crease again if it's still naked, Smooth if this
      // weld just closed it into a real interior edge. The same holds for
      // a pathological non-manifold (3+ face) original edge, which that
      // recompute forces to Crease regardless, matching its untouched
      // original state exactly.
      if (e->FaceCount() != 2) continue;
      edges_snapshot.push_back(
          {remap(e->m_vertex[0]), remap(e->m_vertex[1]), e->m_edge_tag, e->Sharpness(/*bUseCreaseSharpness=*/false)});
    }
  }

  ON_SubD new_subd;
  for (const VSnap& vs : vertices_snapshot) {
    if (new_subd.AddVertexForExperts(vs.id, ON_SubDVertexTag::Unset, &vs.point.x, 0, 0) == nullptr) {
      throw std::runtime_error("dino8::kernel::SubD::Weld: ON_SubD::AddVertexForExperts failed");
    }
  }
  for (const FSnap& fs : faces_snapshot) {
    std::vector<const ON_SubDVertex*> corners(fs.corner_ids.size());
    for (size_t i = 0; i < fs.corner_ids.size(); ++i) {
      corners[i] = new_subd.VertexFromId(fs.corner_ids[i]);
      if (corners[i] == nullptr) {
        throw std::runtime_error("dino8::kernel::SubD::Weld: a rebuilt face corner vertex is missing");
      }
    }
    if (new_subd.FindOrAddFace(ON_SubDEdgeTag::Unset, corners.data(), corners.size()) == nullptr) {
      throw std::runtime_error(
          "dino8::kernel::SubD::Weld: ON_SubD::FindOrAddFace failed while rebuilding a face");
    }
  }
  for (const ESnap& es : edges_snapshot) {
    const ON_SubDVertex* a = new_subd.VertexFromId(es.a);
    const ON_SubDVertex* b = new_subd.VertexFromId(es.b);
    if (a == nullptr || b == nullptr) continue;
    const ON_SubDEdge* e = new_subd.FindEdge(a, b).Edge();
    // A null result here only ever means a and b were v0 and v1 themselves
    // (an edge directly between the kept and discarded vertex, already
    // refused above) or - the double-edge case - two distinct original
    // edges (v1-X and v0-X) that legitimately collapse onto the SAME
    // rebuilt edge once v1 merges into v0; either way there is exactly one
    // rebuilt edge to tag, and skipping a not-found one throws away
    // nothing new.
    if (e == nullptr) continue;
    const_cast<ON_SubDEdge*>(e)->m_edge_tag = es.tag;
    if (es.tag == ON_SubDEdgeTag::Smooth || es.tag == ON_SubDEdgeTag::SmoothX) {
      const_cast<ON_SubDEdge*>(e)->SetSharpnessForExperts(es.sharpness);
    }
  }

  new_subd.UpdateAllTagsAndSectorCoefficients(/*bUnsetValuesOnly=*/true);
  subd_ = new_subd;
  return true;
}

std::vector<SubDLimitPoint> SubD::LimitPoints() const {
  std::vector<SubDLimitPoint> out;
  ON_SubDVertexIterator vit = subd_.VertexIterator();
  for (const ON_SubDVertex* v = vit.FirstVertex(); v != nullptr; v = vit.NextVertex()) {
    SubDLimitPoint lp;
    lp.vertex_id = v->m_id;
    lp.control_point = v->ControlNetPoint();
    lp.valence = static_cast<int>(v->EdgeCount());
    lp.smooth = v->IsSmooth();
    // ON_SubDVertex::SurfacePoint() returns ON_3dPoint::NanPoint on
    // failure rather than a bool - so the validity check IS the failure
    // check, and it's an error here, never a NaN handed back as data.
    const ON_3dPoint p = v->SurfacePoint();
    if (!p.IsValid()) {
      throw std::runtime_error(
          "dino8::kernel::SubD::LimitPoints: ON_SubDVertex::SurfacePoint failed for "
          "a vertex (no incident faces, or invalid SubD topology)");
    }
    lp.limit_point = p;
    // The normal is per sector; use the sector containing the vertex's
    // first face (SurfaceNormal(nullptr, ...) refuses a crease/corner
    // vertex outright, since it would be ambiguous there). Undefined ->
    // zero vector, documented on SubDLimitPoint.
    const ON_SubDFace* sector_face = v->FaceCount() > 0 ? v->Face(0) : nullptr;
    const ON_3dVector n = sector_face ? v->SurfaceNormal(sector_face, /*bUndefinedNormalPossible=*/true)
                                      : ON_3dVector::NanVector;
    lp.limit_normal = n.IsValid() ? n : ON_3dVector::ZeroVector;
    out.push_back(lp);
  }
  return out;
}

namespace {

// The vertex of `nf` (one of `v`'s incident faces, sharing the edge
// `v`-`shared` with `f`) that neighbors `v` but is not `shared` - the
// "outer" grid point one step past `v`, away from `f`, along that edge's
// far face.
const ON_SubDVertex* OuterNeighbor(const ON_SubDFace* nf, const ON_SubDVertex* v,
                                    const ON_SubDVertex* shared) {
  if (!nf) return nullptr;
  const unsigned int n = nf->EdgeCount();
  unsigned int idx = ON_UNSET_UINT_INDEX;
  for (unsigned int i = 0; i < n; ++i) {
    if (nf->Vertex(i) == v) { idx = i; break; }
  }
  if (idx == ON_UNSET_UINT_INDEX || n == 0) return nullptr;
  const ON_SubDVertex* a = nf->Vertex((idx + n - 1) % n);
  const ON_SubDVertex* b = nf->Vertex((idx + 1) % n);
  if (a == shared) return b;
  if (b == shared) return a;
  return nullptr;
}

// The 4th face incident to ordinary-interior vertex `v` - the one that is
// neither `f` (the regular face being evaluated) nor either of the two
// faces across `f`'s two edges at `v` (`nf_a`, `nf_b`) - i.e. the face
// diagonally opposite `f` around `v`.
const ON_SubDFace* DiagonalFace(const ON_SubDVertex* v, const ON_SubDFace* f,
                                 const ON_SubDFace* nf_a, const ON_SubDFace* nf_b) {
  if (!v || v->FaceCount() != 4) return nullptr;
  for (unsigned int i = 0; i < 4; ++i) {
    const ON_SubDFace* cand = v->Face(i);
    if (cand && cand != f && cand != nf_a && cand != nf_b) return cand;
  }
  return nullptr;
}

// `fd`'s own vertex diagonally opposite `v` within `fd` (`fd` is a quad
// containing `v`).
const ON_SubDVertex* DiagonalVertex(const ON_SubDFace* fd, const ON_SubDVertex* v) {
  if (!fd || fd->EdgeCount() != 4) return nullptr;
  unsigned int idx = ON_UNSET_UINT_INDEX;
  for (unsigned int i = 0; i < 4; ++i) {
    if (fd->Vertex(i) == v) { idx = i; break; }
  }
  if (idx == ON_UNSET_UINT_INDEX) return nullptr;
  return fd->Vertex((idx + 2) % 4);
}

bool IsOrdinaryInterior(const ON_SubDVertex* v) {
  return v != nullptr && v->EdgeCount() == 4 && v->FaceCount() == 4 &&
         v->IsSmooth();
}

// Standard uniform-cubic-B-spline-to-Bezier conversion for one span's 4
// control points (e.g. Piegl & Tiller, "The NURBS Book" - open, published
// math; the same conversion Boehm's knot-insertion algorithm produces
// when a uniform cubic knot is raised to triple multiplicity at both ends
// of one span).
void BsplineSpanToBezier(const ON_3dPoint p[4], ON_3dPoint b[4]) {
  b[0] = (p[0] + 4.0 * p[1] + p[2]) / 6.0;
  b[1] = (4.0 * p[1] + 2.0 * p[2]) / 6.0;
  b[2] = (2.0 * p[1] + 4.0 * p[2]) / 6.0;
  b[3] = (p[1] + 4.0 * p[2] + p[3]) / 6.0;
}

// Converts a 4x4 grid of regular Catmull-Clark control points (grid[row]
// [col], row/col = 0..3) in place to its equivalent 4x4 bicubic Bezier
// control grid: the 1D conversion applied to each row, then to each
// column - valid because tensor-product B-spline<->Bezier conversion
// separates exactly like this (it's just a change of basis applied
// independently in each parametric direction).
void GridToBezier(ON_3dPoint grid[4][4]) {
  ON_3dPoint tmp[4][4];
  for (int r = 0; r < 4; ++r) {
    const ON_3dPoint row_in[4] = {grid[r][0], grid[r][1], grid[r][2], grid[r][3]};
    ON_3dPoint row_out[4];
    BsplineSpanToBezier(row_in, row_out);
    for (int c = 0; c < 4; ++c) tmp[r][c] = row_out[c];
  }
  for (int c = 0; c < 4; ++c) {
    const ON_3dPoint col_in[4] = {tmp[0][c], tmp[1][c], tmp[2][c], tmp[3][c]};
    ON_3dPoint col_out[4];
    BsplineSpanToBezier(col_in, col_out);
    for (int r = 0; r < 4; ++r) grid[r][c] = col_out[r];
  }
}

// Builds `f`'s 4x4 Bezier-form control grid: the exact regular
// Catmull-Clark patch (grid[row][col], row/col = 0..3, row 0/3 = the
// v=0/v=1 boundary curve's Bezier control points, col 0/3 = the u=0/u=1
// boundary curve's, matching Vertex(0)=(0,0), Vertex(1)=(1,0),
// Vertex(2)=(1,1), Vertex(3)=(0,1)) when `f` is regular, or the
// tolerance-bounded bilinear-corner-interpolant approximation otherwise
// - see ToNurbsPatches()'s own doc comment for what each case means.
// `f` must be a quad (caller's responsibility - see ToNurbsPatches()'s
// own n-gon-skipping comment). Sets `regular_out` to which case applied.
void BuildFaceBezierGrid(const ON_SubDFace* f, ON_3dPoint grid[4][4], bool& regular_out) {
  const ON_SubDVertex* v[4] = {f->Vertex(0), f->Vertex(1), f->Vertex(2), f->Vertex(3)};
  const ON_SubDEdge* e[4] = {f->Edge(0), f->Edge(1), f->Edge(2), f->Edge(3)};
  const ON_SubDFace* nf[4] = {
      e[0] ? e[0]->NeighborFace(f, true) : nullptr,
      e[1] ? e[1]->NeighborFace(f, true) : nullptr,
      e[2] ? e[2]->NeighborFace(f, true) : nullptr,
      e[3] ? e[3]->NeighborFace(f, true) : nullptr,
  };

  bool regular = v[0] && v[1] && v[2] && v[3] && IsOrdinaryInterior(v[0]) &&
                  IsOrdinaryInterior(v[1]) && IsOrdinaryInterior(v[2]) &&
                  IsOrdinaryInterior(v[3]) && nf[0] && nf[1] && nf[2] && nf[3];

  // grid[row][col]: face corners occupy the middle 2x2 block, v[0]..v[3]
  // going v[0]->v[1] (row1, col1->col2), v[1]->v[2] (col2, row1->row2),
  // matching the face's own CCW winding.
  grid[1][1] = v[0]->ControlNetPoint();
  grid[1][2] = v[1]->ControlNetPoint();
  grid[2][2] = v[2]->ControlNetPoint();
  grid[2][1] = v[3]->ControlNetPoint();

  if (regular) {
    const ON_SubDVertex* outer01_0 = OuterNeighbor(nf[0], v[0], v[1]);
    const ON_SubDVertex* outer01_1 = OuterNeighbor(nf[0], v[1], v[0]);
    const ON_SubDVertex* outer12_1 = OuterNeighbor(nf[1], v[1], v[2]);
    const ON_SubDVertex* outer12_2 = OuterNeighbor(nf[1], v[2], v[1]);
    const ON_SubDVertex* outer23_2 = OuterNeighbor(nf[2], v[2], v[3]);
    const ON_SubDVertex* outer23_3 = OuterNeighbor(nf[2], v[3], v[2]);
    const ON_SubDVertex* outer30_3 = OuterNeighbor(nf[3], v[3], v[0]);
    const ON_SubDVertex* outer30_0 = OuterNeighbor(nf[3], v[0], v[3]);

    const ON_SubDFace* fd0 = DiagonalFace(v[0], f, nf[0], nf[3]);
    const ON_SubDFace* fd1 = DiagonalFace(v[1], f, nf[0], nf[1]);
    const ON_SubDFace* fd2 = DiagonalFace(v[2], f, nf[1], nf[2]);
    const ON_SubDFace* fd3 = DiagonalFace(v[3], f, nf[2], nf[3]);
    const ON_SubDVertex* diag0 = DiagonalVertex(fd0, v[0]);
    const ON_SubDVertex* diag1 = DiagonalVertex(fd1, v[1]);
    const ON_SubDVertex* diag2 = DiagonalVertex(fd2, v[2]);
    const ON_SubDVertex* diag3 = DiagonalVertex(fd3, v[3]);

    if (outer01_0 && outer01_1 && outer12_1 && outer12_2 && outer23_2 && outer23_3 &&
        outer30_3 && outer30_0 && diag0 && diag1 && diag2 && diag3) {
      grid[0][1] = outer01_0->ControlNetPoint();
      grid[0][2] = outer01_1->ControlNetPoint();
      grid[1][3] = outer12_1->ControlNetPoint();
      grid[2][3] = outer12_2->ControlNetPoint();
      grid[3][2] = outer23_2->ControlNetPoint();
      grid[3][1] = outer23_3->ControlNetPoint();
      grid[2][0] = outer30_3->ControlNetPoint();
      grid[1][0] = outer30_0->ControlNetPoint();
      grid[0][0] = diag0->ControlNetPoint();
      grid[0][3] = diag1->ControlNetPoint();
      grid[3][3] = diag2->ControlNetPoint();
      grid[3][0] = diag3->ControlNetPoint();
    } else {
      // A vertex reported valence 4 but its neighborhood didn't
      // resolve cleanly (e.g. a non-manifold pole) - fall back to the
      // honest irregular case below rather than use a half-filled grid.
      regular = false;
    }
  }

  if (regular) {
    GridToBezier(grid);
  } else {
    // Irregular face (extraordinary vertex, crease, or boundary): no
    // closed-form regular stencil applies. Fill the grid as a bilinear
    // interpolant of the 4 known corners, sampled at parameter values
    // i/3, j/3 - since a bilinear (ruled) surface's iso-parameter lines
    // are straight lines, any monotonic sampling along them still lies
    // exactly on that ruled surface, so the resulting "Bezier" patch
    // below reproduces the flat corner interpolant exactly (only the
    // internal parametrization is nonuniform, which is harmless for an
    // untrimmed Brep face). This is the tolerance-bounded
    // approximation documented on ToNurbsPatches().
    const ON_3dPoint c00 = grid[1][1];
    const ON_3dPoint c10 = grid[1][2];
    const ON_3dPoint c11 = grid[2][2];
    const ON_3dPoint c01 = grid[2][1];
    for (int r = 0; r < 4; ++r) {
      const double t = r / 3.0;
      for (int c = 0; c < 4; ++c) {
        const double s = c / 3.0;
        grid[r][c] = (1.0 - t) * ((1.0 - s) * c00 + s * c10) + t * ((1.0 - s) * c01 + s * c11);
      }
    }
  }

  regular_out = regular;
}

}  // namespace

std::vector<SubDNurbsPatch> SubD::ToNurbsPatches() const {
  std::vector<SubDNurbsPatch> patches;
  ON_SubDFaceIterator fit = subd_.FaceIterator();
  for (const ON_SubDFace* f = fit.FirstFace(); f != nullptr; f = fit.NextFace()) {
    // Catmull-Clark refinement always produces quads once subdivided at
    // least once; a level-0 face built straight from a triangle/n-gon
    // mesh can still be non-quad, and has no regular-patch stencil at
    // all - skip it (callers wanting every face covered should
    // Subdivide(1) first, which turns every face into a quad).
    if (f->EdgeCount() != 4) continue;
    if (!f->Vertex(0) || !f->Vertex(1) || !f->Vertex(2) || !f->Vertex(3)) continue;

    ON_3dPoint grid[4][4];
    bool regular = false;
    BuildFaceBezierGrid(f, grid, regular);

    std::vector<Point3d> cvs(16);
    for (int u = 0; u < 4; ++u) {
      for (int w = 0; w < 4; ++w) {
        cvs[static_cast<size_t>(u) * 4 + static_cast<size_t>(w)] = grid[w][u];
      }
    }
    SubDNurbsPatch patch;
    patch.surface = NurbsSurface::FromControlGrid(cvs, 4, 4, 3, 3);
    patch.exact = regular;
    patches.push_back(std::move(patch));
  }
  return patches;
}

namespace {

// Position of a cubic Bezier curve (control points p[0..3]) at t in [0,1].
ON_3dPoint EvalCubicBezierPos(const ON_3dPoint p[4], double t) {
  const double mt = 1.0 - t;
  return mt * mt * mt * p[0] + 3.0 * mt * mt * t * p[1] + 3.0 * mt * t * t * p[2] +
         t * t * t * p[3];
}

// Derivative (w.r.t. t) of a cubic Bezier curve (control points p[0..3])
// at t in [0,1] - the standard "3 * differences of a degree-2 Bezier"
// formula.
ON_3dVector EvalCubicBezierDeriv(const ON_3dPoint p[4], double t) {
  const double mt = 1.0 - t;
  return 3.0 * (mt * mt * (p[1] - p[0]) + 2.0 * mt * t * (p[2] - p[1]) + t * t * (p[3] - p[2]));
}

// Position and partial derivatives of the bicubic Bezier surface whose
// 4x4 control grid is `grid` (row = v-direction control index, col =
// u-direction control index - see BuildFaceBezierGrid()'s own comment),
// at (u, v) in [0,1]x[0,1]. Standard tensor-product evaluation: reduce
// each row to its u-curve's position AND derivative first, then reduce
// those 4 values in v (partial derivatives commute for a tensor-product
// surface, so reducing the u-derivative rows in v gives Su, and taking
// the v-derivative of the position rows gives Sv).
void EvalBicubicBezier(const ON_3dPoint grid[4][4], double u, double v, ON_3dPoint& position,
                       ON_3dVector& tangent_u, ON_3dVector& tangent_v) {
  ON_3dPoint row_pos[4];
  ON_3dPoint row_deriv_u[4];  // du of each row, stored as points so it can be run back through
                              // the same position-reduction basis functions
  for (int r = 0; r < 4; ++r) {
    row_pos[r] = EvalCubicBezierPos(grid[r], u);
    const ON_3dVector d = EvalCubicBezierDeriv(grid[r], u);
    row_deriv_u[r] = ON_3dPoint(d.x, d.y, d.z);
  }
  position = EvalCubicBezierPos(row_pos, v);
  tangent_v = EvalCubicBezierDeriv(row_pos, v);
  const ON_3dPoint du_point = EvalCubicBezierPos(row_deriv_u, v);
  tangent_u = ON_3dVector(du_point.x, du_point.y, du_point.z);
}

SubDSurfacePoint EvalPatchPoint(const ON_3dPoint grid[4][4], double u, double v, bool exact) {
  SubDSurfacePoint pt;
  EvalBicubicBezier(grid, u, v, pt.position, pt.tangent_u, pt.tangent_v);
  ON_3dVector n = ON_CrossProduct(pt.tangent_u, pt.tangent_v);
  pt.normal = n.Unitize() ? n : ON_3dVector::ZeroVector;
  pt.exact = exact;
  return pt;
}

// Re-expresses (u, v) in a local frame centered on face-corner index k
// (0..3): (0, 0) at Vertex(k), (1, 0) at Vertex(k+1), (0, 1) at
// Vertex(k-1) - the inverse of FromVertexLocal() below. For (u, v)
// inside the quadrant nearest Vertex(k) (both within 0.5 of Vertex(k)'s
// own (0,0)/(1,0)/(1,1)/(0,1) corner), uk and vk both land in [0, 0.5].
void ToVertexLocal(int k, double u, double v, double& uk, double& vk) {
  switch (k & 3) {
    case 0: uk = u; vk = v; break;
    case 1: uk = v; vk = 1.0 - u; break;
    case 2: uk = 1.0 - u; vk = 1.0 - v; break;
    default: uk = 1.0 - v; vk = u; break;  // case 3
  }
}

// The inverse of ToVertexLocal(): given a point (uk, vk) expressed in
// the local frame centered on whichever face-corner index a *different*
// face's Vertex(m) represents ((0,0) at Vertex(m), (1,0) toward
// Vertex(m+1), (0,1) toward Vertex(m-1)), returns that face's own
// (u, v) (Vertex(0) = (0,0), Vertex(1) = (1,0), Vertex(2) = (1,1),
// Vertex(3) = (0,1)).
void FromVertexLocal(int m, double uk, double vk, double& u, double& v) {
  switch (m & 3) {
    case 0: u = uk; v = vk; break;
    case 1: u = 1.0 - vk; v = uk; break;
    case 2: u = 1.0 - uk; v = 1.0 - vk; break;
    default: u = vk; v = 1.0 - uk; break;  // case 3
  }
}

// A distance tolerance for FindVertex() calls below, scaled to face
// `f`'s own size: SubdivisionPoint()'s independently-computed value and
// GlobalSubdivide()'s internally-computed one for the same point should
// agree far more tightly than this (same formula, same inputs,
// deterministic IEEE arithmetic - differing at most by summation-order
// rounding), so this is a generous, not a tight, tolerance - it exists
// to be scale-invariant, not to paper over a real mismatch.
double FindTolerance(const ON_SubDFace* f) {
  ON_3dPoint lo = ON_3dPoint::UnsetPoint;
  ON_3dPoint hi = ON_3dPoint::UnsetPoint;
  for (unsigned int i = 0; i < f->EdgeCount(); ++i) {
    const ON_SubDVertex* v = f->Vertex(i);
    if (!v) continue;
    const ON_3dPoint p = v->ControlNetPoint();
    if (!lo.IsValid()) {
      lo = p;
      hi = p;
      continue;
    }
    lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
    hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
  }
  const double diagonal = lo.IsValid() ? hi.DistanceTo(lo) : 0.0;
  return std::max(1e-9, diagonal * 1e-7);
}

// The unique face incident to all three of vF, vP, and vN (each has
// valence exactly 4 - vF is a face-point, vP/vN are edge-points, always
// true for quad-only Catmull-Clark subdivision - so this is a handful
// of pointer comparisons, not a mesh-wide search). Returns nullptr if no
// such face exists (should only happen if a FindVertex() call above it
// already failed to resolve cleanly).
const ON_SubDFace* FindCommonFace(const ON_SubDVertex* vF, const ON_SubDVertex* vP,
                                   const ON_SubDVertex* vN) {
  if (!vF || !vP || !vN) return nullptr;
  auto touches = [](const ON_SubDVertex* v, const ON_SubDFace* face) {
    for (unsigned int i = 0; i < v->FaceCount(); ++i) {
      if (v->Face(i) == face) return true;
    }
    return false;
  };
  for (unsigned int i = 0; i < vF->FaceCount(); ++i) {
    const ON_SubDFace* candidate = vF->Face(i);
    if (candidate && candidate->EdgeCount() == 4 && touches(vP, candidate) &&
        touches(vN, candidate)) {
      return candidate;
    }
  }
  return nullptr;
}

// Recursive core of SubD::EvaluateFace(): `s` is the mutable working
// copy (SubD::EvaluateFace()'s own `raw()` is never touched), `f` one
// of its CURRENT faces, (u, v) the parameter within it. See
// SubD::EvaluateFace()'s doc comment for the algorithm.
// A face-corner query (u, v both exactly 0 or 1) resolves to a real
// control-net vertex regardless of how many adaptive levels deep the
// recursion already is - "the quadrant nearest this corner" would
// otherwise be degenerate (zero-width) and, doubled again each level,
// eventually lands exactly on some OTHER face's corner by pure binary-
// fraction arithmetic (0.5 doubles to 1.0 exactly, etc.) - silently
// drifting to an unrelated vertex instead of continuing to track the
// original extraordinary one. Short-circuiting to the vertex's own
// exact limit point/tangent-plane/normal sidesteps that entirely.
//
// The tangent plane at an extraordinary/crease/boundary vertex needs the
// full Catmull-Clark eigenbasis, which OpenNURBS itself already computes
// (and long predates the "not implemented here" comment this replaces):
// `ON_SubDVertex::GetSurfacePoint(sector_face, ..., limit_point)`
// (opennurbs_subd_eval.cpp) is the same real, non-stub routine
// `SurfacePoint()`/`SurfaceNormal()` already call internally (verified by
// reading it directly, not assumed) - it builds the sector's own point
// ring and solves for its limit point/tangent/normal via that ring's own
// eigenstructure (`ON_SubDSectorType`/its subdivision matrix, read in
// opennurbs_subd_eval.cpp/opennurbs_subd_matrix.cpp), the exact same
// eigenbasis evaluation Stam's 1998 paper (cited on Symmetrize()'s own
// doc comment above) is named after, not a hand-rolled approximation of
// it. `limit_point.Tangent(0)`/`Tangent(1)` (`m_limitT1`/`m_limitT2`) are
// a genuine orthonormal basis for the exact tangent plane, with
// `Tangent(0) x Tangent(1)` in the same direction as `Normal()` - but,
// unlike EvalBicubicBezier()'s regular-patch tangent_u/tangent_v (real
// dS/du, dS/dv partial derivatives at THIS face's own (u, v)), they are
// unit vectors in a canonical sector-relative frame, not scaled or
// oriented to match this specific face's own (u, v) axes. Reporting a
// real, non-zero tangent-plane basis this way - rather than the zero
// vector - is what SubDSurfacePoint::exact = true has always promised
// callers for a corner query; the basis' own u/v-axis alignment was
// already undocumented for this fallback (the zero vector it replaces
// had none at all).
SubDSurfacePoint ExactVertexCorner(const ON_SubDFace* f, int corner_index) {
  const ON_SubDVertex* v = f->Vertex(static_cast<unsigned int>(corner_index));
  SubDSurfacePoint pt;
  ON_SubDSectorSurfacePoint limit_point;
  if (v->GetSurfacePoint(f, /*bUndefinedNormalIsPossible=*/true, limit_point)) {
    pt.position = limit_point.Point();
    const ON_3dVector n = limit_point.Normal();
    pt.normal = n.IsValid() ? n : ON_3dVector::ZeroVector;
    const ON_3dVector t1 = limit_point.Tangent(0);
    const ON_3dVector t2 = limit_point.Tangent(1);
    pt.tangent_u = t1.IsValid() ? t1 : ON_3dVector::ZeroVector;
    pt.tangent_v = t2.IsValid() ? t2 : ON_3dVector::ZeroVector;
    pt.exact = true;
  } else {
    pt.position = v->ControlNetPoint();
    pt.exact = false;
  }
  return pt;
}

SubDSurfacePoint EvaluateFaceAdaptive(ON_SubD& s, const ON_SubDFace* f, double u, double v,
                                      int depth_remaining) {
  ON_3dPoint grid[4][4];
  bool regular = false;
  BuildFaceBezierGrid(f, grid, regular);
  if (regular) {
    return EvalPatchPoint(grid, u, v, true);
  }
  if (u == 0.0 && v == 0.0) return ExactVertexCorner(f, 0);
  if (u == 1.0 && v == 0.0) return ExactVertexCorner(f, 1);
  if (u == 1.0 && v == 1.0) return ExactVertexCorner(f, 2);
  if (u == 0.0 && v == 1.0) return ExactVertexCorner(f, 3);
  if (depth_remaining <= 0) {
    return EvalPatchPoint(grid, u, v, false);
  }

  int k = 0;
  if (u < 0.5 && v < 0.5) {
    k = 0;
  } else if (u >= 0.5 && v < 0.5) {
    k = 1;
  } else if (u >= 0.5 && v >= 0.5) {
    k = 2;
  } else {
    k = 3;
  }
  double uk = 0.0, vk = 0.0;
  ToVertexLocal(k, u, v, uk, vk);
  const double uk2 = 2.0 * uk;
  const double vk2 = 2.0 * vk;

  const ON_SubDEdge* e_prev = f->Edge(static_cast<unsigned int>((k + 3) % 4));
  const ON_SubDEdge* e_next = f->Edge(static_cast<unsigned int>(k));
  if (!e_prev || !e_next) {
    return EvalPatchPoint(grid, u, v, false);
  }
  const ON_3dPoint face_ref = f->SubdivisionPoint();
  const ON_3dPoint prev_ref = e_prev->SubdivisionPoint();
  const ON_3dPoint next_ref = e_next->SubdivisionPoint();
  if (!face_ref.IsValid() || !prev_ref.IsValid() || !next_ref.IsValid()) {
    return EvalPatchPoint(grid, u, v, false);
  }

  // Safety cap: `s.GlobalSubdivide(1)` refines the WHOLE working copy
  // every call (there's no cheaper "just this face's neighborhood"
  // primitive used here - see EvaluateFace()'s own doc comment), so
  // cumulative cost across recursion levels grows with the working
  // copy's OWN current size, not just with depth - for a large starting
  // net and a caller-supplied `max_adaptive_levels` deep enough, that
  // product can reach many millions of faces and exhaust memory. Once
  // the working copy is already this large, refining it further isn't
  // worth what it costs - fall back honestly instead of risking that.
  constexpr unsigned int kMaxWorkingFaceCount = 500000;
  if (s.FaceCount() > kMaxWorkingFaceCount) {
    return EvalPatchPoint(grid, u, v, false);
  }

  const double tolerance = FindTolerance(f);
  if (!s.GlobalSubdivide(1)) {
    return EvalPatchPoint(grid, u, v, false);
  }

  const ON_SubDVertex* vF = s.FindVertex(&face_ref.x, tolerance);
  const ON_SubDVertex* vP = s.FindVertex(&prev_ref.x, tolerance);
  const ON_SubDVertex* vN = s.FindVertex(&next_ref.x, tolerance);
  const ON_SubDFace* child = FindCommonFace(vF, vP, vN);
  if (!child) {
    return EvalPatchPoint(grid, u, v, false);
  }

  // The child's 4th corner - whichever of its vertices is none of the 3
  // just located - is the refined position of the original Vertex(k);
  // its own array index there need not match k, so it's found by
  // elimination rather than assumed.
  int m = -1;
  for (int i = 0; i < 4; ++i) {
    const ON_SubDVertex* cv = child->Vertex(static_cast<unsigned int>(i));
    if (cv != vF && cv != vP && cv != vN) {
      m = i;
      break;
    }
  }
  if (m < 0) {
    return EvalPatchPoint(grid, u, v, false);
  }

  double u2 = 0.0, v2 = 0.0;
  FromVertexLocal(m, uk2, vk2, u2, v2);
  return EvaluateFaceAdaptive(s, child, u2, v2, depth_remaining - 1);
}

}  // namespace

SubDSurfacePoint SubD::EvaluateFace(unsigned int face_id, double u, double v,
                                    int max_adaptive_levels) const {
  const ON_SubDFace* f0 = subd_.FaceFromId(face_id);
  if (f0 == nullptr) {
    throw std::runtime_error(
        "dino8::kernel::SubD::EvaluateFace: face_id doesn't identify a face of the "
        "current subdivision level");
  }
  if (f0->EdgeCount() != 4) {
    throw std::runtime_error(
        "dino8::kernel::SubD::EvaluateFace: face is not a quad (Subdivide(1) first - "
        "see ToNurbsPatches()'s own doc comment)");
  }

  ON_3dPoint grid[4][4];
  bool regular = false;
  BuildFaceBezierGrid(f0, grid, regular);
  if (regular) {
    // Skip the clone below entirely when it can't possibly be needed -
    // a real (if minor) cost saving, not just a style choice, since
    // cloning is O(the whole net)'s worth of work per call.
    return EvalPatchPoint(grid, u, v, true);
  }

  ON_SubD working_copy(subd_);
  const ON_SubDFace* f0_copy = working_copy.FaceFromId(face_id);
  if (f0_copy == nullptr) {
    return EvalPatchPoint(grid, u, v, false);
  }
  return EvaluateFaceAdaptive(working_copy, f0_copy, u, v, max_adaptive_levels);
}

namespace {

// Bilinear interpolation of a quad's 4 corners at local (s, t) in
// [0,1]x[0,1] - same corner-ordering convention EvaluateFace() itself
// uses ((0,0)->p00, (1,0)->p10, (1,1)->p11, (0,1)->p01).
ON_3dPoint BilinearCorners(const ON_3dPoint& p00, const ON_3dPoint& p10, const ON_3dPoint& p11,
                           const ON_3dPoint& p01, double s, double t) {
  return (1.0 - s) * (1.0 - t) * p00 + s * (1.0 - t) * p10 + s * t * p11 + (1.0 - s) * t * p01;
}

}  // namespace

Mesh SubD::Tessellate(double tolerance, int max_resolution) const {
  if (!(tolerance > 0.0)) {
    throw std::invalid_argument("dino8::kernel::SubD::Tessellate: tolerance must be strictly positive");
  }
  if (max_resolution < 1) {
    throw std::invalid_argument("dino8::kernel::SubD::Tessellate: max_resolution must be at least 1");
  }

  std::vector<unsigned int> face_ids;
  ON_SubDFaceIterator fit = subd_.FaceIterator();
  for (const ON_SubDFace* f = fit.FirstFace(); f != nullptr; f = fit.NextFace()) {
    // Same "quads only" convention as ToNurbsPatchesAdaptive() - a
    // level-0 n-gon face needs Subdivide(1) first (EvaluateFace()'s own
    // requirement, which this delegates every sample point to).
    if (f->EdgeCount() != 4) continue;
    if (!f->Vertex(0) || !f->Vertex(1) || !f->Vertex(2) || !f->Vertex(3)) continue;
    face_ids.push_back(f->FaceId());
  }

  std::vector<Mesh> face_grids;
  face_grids.reserve(face_ids.size());

  for (unsigned int face_id : face_ids) {
    const auto sample_grid = [&](int res) {
      std::vector<std::vector<ON_3dPoint>> grid(static_cast<size_t>(res) + 1,
                                                 std::vector<ON_3dPoint>(static_cast<size_t>(res) + 1));
      for (int i = 0; i <= res; ++i) {
        const double u = static_cast<double>(i) / res;
        for (int j = 0; j <= res; ++j) {
          const double v = static_cast<double>(j) / res;
          grid[static_cast<size_t>(i)][static_cast<size_t>(j)] = EvaluateFace(face_id, u, v).position;
        }
      }
      return grid;
    };

    int n = 1;
    std::vector<std::vector<ON_3dPoint>> samples = sample_grid(n);
    for (;;) {
      double max_deviation = 0.0;
      for (int i = 0; i < n; ++i) {
        const double u_mid = (i + 0.5) / n;
        for (int j = 0; j < n; ++j) {
          const double v_mid = (j + 0.5) / n;
          const ON_3dPoint bilinear = BilinearCorners(
              samples[static_cast<size_t>(i)][static_cast<size_t>(j)],
              samples[static_cast<size_t>(i) + 1][static_cast<size_t>(j)],
              samples[static_cast<size_t>(i) + 1][static_cast<size_t>(j) + 1],
              samples[static_cast<size_t>(i)][static_cast<size_t>(j) + 1], 0.5, 0.5);
          const ON_3dPoint truth = EvaluateFace(face_id, u_mid, v_mid).position;
          max_deviation = std::max(max_deviation, bilinear.DistanceTo(truth));
        }
      }
      if (max_deviation <= tolerance || n >= max_resolution) break;
      n = std::min(n * 2, max_resolution);
      samples = sample_grid(n);
    }

    Mesh grid_mesh;
    ON_Mesh& raw = grid_mesh.raw();
    const int points = n + 1;
    const auto grid_index = [points](int i, int j) { return i * points + j; };
    raw.m_V.Reserve(points * points);
    for (int i = 0; i <= n; ++i) {
      for (int j = 0; j <= n; ++j) {
        raw.m_V.Append(ON_3fPoint(samples[static_cast<size_t>(i)][static_cast<size_t>(j)]));
      }
    }
    raw.m_F.Reserve(n * n);
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        ON_MeshFace face;
        face.vi[0] = grid_index(i, j);
        face.vi[1] = grid_index(i + 1, j);
        face.vi[2] = grid_index(i + 1, j + 1);
        face.vi[3] = grid_index(i, j + 1);
        raw.m_F.Append(face);
      }
    }
    face_grids.push_back(std::move(grid_mesh));
  }

  if (face_grids.empty()) {
    return Mesh();
  }
  return Mesh::MergeAndWeld(face_grids);
}

namespace {

SubDNurbsPatch GridToPatch(const ON_3dPoint grid[4][4], bool exact) {
  std::vector<Point3d> cvs(16);
  for (int u = 0; u < 4; ++u) {
    for (int w = 0; w < 4; ++w) {
      cvs[static_cast<size_t>(u) * 4 + static_cast<size_t>(w)] = grid[w][u];
    }
  }
  SubDNurbsPatch patch;
  patch.surface = NurbsSurface::FromControlGrid(cvs, 4, 4, 3, 3);
  patch.exact = exact;
  return patch;
}

// Recursive core of SubD::ToNurbsPatchesAdaptive(): `s` is treated as
// read-only (unlike EvaluateFaceAdaptive()'s single shared mutable
// working copy, since here up to 4 sibling children may each need their
// OWN further recursion, and mutating one shared copy across siblings
// would invalidate the others' already-found face pointers) - splitting
// only ever clones `s` into a fresh local copy, mirroring
// SubD::EvaluateFace()'s own top-level "clone once, raw() untouched"
// pattern, just done per-recursion-level instead of only once.
void ToNurbsPatchesAdaptiveRecurse(const ON_SubD& s, unsigned int face_id, int depth_remaining,
                                   std::vector<SubDNurbsPatch>& out) {
  const ON_SubDFace* f = s.FaceFromId(face_id);
  if (f == nullptr) return;  // Shouldn't happen; be defensive rather than crash.

  ON_3dPoint grid[4][4];
  bool regular = false;
  BuildFaceBezierGrid(f, grid, regular);

  constexpr unsigned int kMaxWorkingFaceCount = 500000;
  if (regular || depth_remaining <= 0 || s.FaceCount() > kMaxWorkingFaceCount) {
    out.push_back(GridToPatch(grid, regular));
    return;
  }

  // Capture the would-be refined positions of the face point and its 4
  // edge points BEFORE subdividing - the same SubdivisionPoint()-based
  // technique EvaluateFaceAdaptive() uses to relocate them afterward,
  // since `f`'s own pointer (and everything else in `s`) goes stale the
  // moment a copy is subdivided.
  const ON_3dPoint face_ref = f->SubdivisionPoint();
  ON_3dPoint edge_ref[4];
  bool refs_ok = face_ref.IsValid();
  for (unsigned int i = 0; refs_ok && i < 4; ++i) {
    const ON_SubDEdge* e = f->Edge(i);
    edge_ref[i] = e ? e->SubdivisionPoint() : ON_3dPoint::UnsetPoint;
    refs_ok = edge_ref[i].IsValid();
  }
  if (!refs_ok) {
    out.push_back(GridToPatch(grid, regular));
    return;
  }

  const double tolerance = FindTolerance(f);
  ON_SubD refined(s);
  if (!refined.GlobalSubdivide(1)) {
    out.push_back(GridToPatch(grid, regular));
    return;
  }

  const ON_SubDVertex* vF = refined.FindVertex(&face_ref.x, tolerance);
  const ON_SubDVertex* vE[4] = {nullptr, nullptr, nullptr, nullptr};
  bool found = vF != nullptr;
  for (int i = 0; found && i < 4; ++i) {
    vE[i] = refined.FindVertex(&edge_ref[i].x, tolerance);
    found = vE[i] != nullptr;
  }

  unsigned int child_id[4] = {0, 0, 0, 0};
  for (int k = 0; found && k < 4; ++k) {
    // Quadrant k's child touches the shared face point, the edge point
    // "before" it and the edge point "after" it - the same
    // FindCommonFace(vF, vP, vN) pairing EvaluateFaceAdaptive() uses for
    // whichever single quadrant a query point lands in, just run here
    // for all 4 in turn.
    const ON_SubDFace* child = FindCommonFace(vF, vE[(k + 3) % 4], vE[k]);
    found = child != nullptr;
    if (found) child_id[k] = child->FaceId();
  }

  if (!found) {
    out.push_back(GridToPatch(grid, regular));
    return;
  }

  for (int k = 0; k < 4; ++k) {
    ToNurbsPatchesAdaptiveRecurse(refined, child_id[k], depth_remaining - 1, out);
  }
}

}  // namespace

std::vector<SubDNurbsPatch> SubD::ToNurbsPatchesAdaptive(int max_adaptive_levels) const {
  if (max_adaptive_levels < 0) {
    throw std::invalid_argument(
        "dino8::kernel::SubD::ToNurbsPatchesAdaptive: max_adaptive_levels must be >= 0");
  }
  std::vector<unsigned int> face_ids;
  ON_SubDFaceIterator fit = subd_.FaceIterator();
  for (const ON_SubDFace* f = fit.FirstFace(); f != nullptr; f = fit.NextFace()) {
    if (f->EdgeCount() != 4) continue;
    if (!f->Vertex(0) || !f->Vertex(1) || !f->Vertex(2) || !f->Vertex(3)) continue;
    face_ids.push_back(f->FaceId());
  }

  std::vector<SubDNurbsPatch> patches;
  for (unsigned int id : face_ids) {
    ToNurbsPatchesAdaptiveRecurse(subd_, id, max_adaptive_levels, patches);
  }
  return patches;
}

}  // namespace dino8::kernel
