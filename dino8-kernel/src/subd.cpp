#include "dino8/kernel/subd.h"

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

    const ON_SubDVertex* v[4] = {f->Vertex(0), f->Vertex(1), f->Vertex(2), f->Vertex(3)};
    if (!v[0] || !v[1] || !v[2] || !v[3]) continue;

    const ON_SubDEdge* e[4] = {f->Edge(0), f->Edge(1), f->Edge(2), f->Edge(3)};
    const ON_SubDFace* nf[4] = {
        e[0] ? e[0]->NeighborFace(f, true) : nullptr,
        e[1] ? e[1]->NeighborFace(f, true) : nullptr,
        e[2] ? e[2]->NeighborFace(f, true) : nullptr,
        e[3] ? e[3]->NeighborFace(f, true) : nullptr,
    };

    bool regular = IsOrdinaryInterior(v[0]) && IsOrdinaryInterior(v[1]) &&
                    IsOrdinaryInterior(v[2]) && IsOrdinaryInterior(v[3]) &&
                    nf[0] && nf[1] && nf[2] && nf[3];

    // grid[row][col]: face corners occupy the middle 2x2 block, v[0]..v[3]
    // going v[0]->v[1] (row1, col1->col2), v[1]->v[2] (col2, row1->row2),
    // matching the face's own CCW winding.
    ON_3dPoint grid[4][4];
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

}  // namespace dino8::kernel
