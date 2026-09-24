#include "dino8/kernel/subd.h"

#include <algorithm>
#include <stdexcept>

namespace dino8::kernel {

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
  if (!(sharpness >= 0.0) || sharpness > ON_SubDEdgeSharpness::MaximumValue) {
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
  // SetSharpnessForExperts is the same primitive OpenNURBS' own
  // ON_SubD::AddEdge(..., ON_SubDEdgeSharpness) overloads call on a
  // freshly-created edge - here applied to an existing one found via the
  // const FindVertex/FindEdge accessors, which is why the const_cast: it
  // just writes one field (ON_SubDEdge::m_sharpness), verified by reading
  // its implementation, with no other cached state to invalidate.
  const_cast<ON_SubDEdge*>(e)->SetSharpnessForExperts(ON_SubDEdgeSharpness::FromConstant(sharpness));
  return true;
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
// exact limit position/normal (the same real, non-stub
// ON_SubDVertex::SurfacePoint()/SurfaceNormal() LimitPoints() already
// uses) sidesteps that entirely - and is itself exact, not a fallback.
SubDSurfacePoint ExactVertexCorner(const ON_SubDFace* f, int corner_index) {
  const ON_SubDVertex* v = f->Vertex(static_cast<unsigned int>(corner_index));
  SubDSurfacePoint pt;
  const ON_3dPoint p = v->SurfacePoint();
  if (p.IsValid()) {
    pt.position = p;
    const ON_3dVector n = v->SurfaceNormal(f, /*bUndefinedNormalPossible=*/true);
    pt.normal = n.IsValid() ? n : ON_3dVector::ZeroVector;
    // The tangent plane at an extraordinary vertex needs the full
    // Catmull-Clark eigenbasis (not implemented here - see class
    // comment); reporting it as undefined is honest, not a bug.
    pt.tangent_u = ON_3dVector::ZeroVector;
    pt.tangent_v = ON_3dVector::ZeroVector;
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

}  // namespace dino8::kernel
