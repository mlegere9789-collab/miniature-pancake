#include "dino8/kernel/brep.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

namespace {

// Resolved geometry for one face: its NURBS form plus, for a face whose
// trim loops come from genuine ON_Brep topology (a box from ON_BrepBox, a
// file loaded from .3dm, a boolean result), the outer loop and hole loops
// sampled into (u, v) polygons. Breps built by this class's own
// constructors carry their trim polygons in the side tables instead.
struct FaceGeometry {
  ON_NurbsSurface surface;
  std::vector<Point2d> outer;              // empty => untrimmed
  std::vector<std::vector<Point2d>> holes;
  bool exact_clip = false;
};

// Samples a loop's 2D trim curves into a closed (u, v) polygon.
std::vector<Point2d> SampleLoop(const ON_Brep& brep, const ON_BrepLoop& loop) {
  std::vector<Point2d> poly;
  for (int k = 0; k < loop.m_ti.Count(); ++k) {
    const int ti = loop.m_ti[k];
    if (ti < 0 || ti >= brep.m_T.Count()) continue;
    const ON_BrepTrim& trim = brep.m_T[ti];
    const ON_Curve* c2 = trim.TrimCurveOf();
    if (!c2) continue;
    const ON_Interval d = trim.Domain();
    int samples = 1;
    if (!c2->IsLinear()) {
      // Curved trims (circle seams, fillets): sample by span count.
      samples = std::max(8, 4 * c2->SpanCount());
    }
    for (int i = 0; i < samples; ++i) {
      const ON_3dPoint p = c2->PointAt(d.ParameterAt(static_cast<double>(i) / samples));
      poly.emplace_back(p.x, p.y);
    }
  }
  // Drop duplicate closing vertex if the sampling produced one.
  if (poly.size() > 1) {
    const Point2d& a = poly.front();
    const Point2d& b = poly.back();
    if (std::fabs(a.x - b.x) < 1e-12 && std::fabs(a.y - b.y) < 1e-12) poly.pop_back();
  }
  return poly;
}

// True when the loop is just the surface's full rectangular domain.
bool LoopIsFullDomain(const std::vector<Point2d>& poly, const ON_NurbsSurface& srf) {
  if (poly.size() != 4) return false;
  const ON_Interval du = srf.Domain(0), dv = srf.Domain(1);
  double umin = 1e300, umax = -1e300, vmin = 1e300, vmax = -1e300;
  for (const Point2d& p : poly) {
    umin = std::min(umin, p.x); umax = std::max(umax, p.x);
    vmin = std::min(vmin, p.y); vmax = std::max(vmax, p.y);
    // Every vertex must sit on a domain corner.
    const bool on_u = std::fabs(p.x - du.Min()) < 1e-9 * (1 + std::fabs(du.Min())) || std::fabs(p.x - du.Max()) < 1e-9 * (1 + std::fabs(du.Max()));
    const bool on_v = std::fabs(p.y - dv.Min()) < 1e-9 * (1 + std::fabs(dv.Min())) || std::fabs(p.y - dv.Max()) < 1e-9 * (1 + std::fabs(dv.Max()));
    if (!on_u || !on_v) return false;
  }
  return std::fabs(umin - du.Min()) < 1e-9 && std::fabs(umax - du.Max()) < 1e-9 &&
         std::fabs(vmin - dv.Min()) < 1e-9 && std::fabs(vmax - dv.Max()) < 1e-9;
}

bool ResolveFace(const ON_Brep& brep, int face_index,
                 const std::vector<std::vector<Point2d>>& side_trims,
                 const std::vector<bool>& side_exact,
                 const std::vector<std::vector<std::vector<Point2d>>>& side_holes,
                 FaceGeometry& out) {
  const ON_BrepFace& face = brep.m_F[face_index];
  const ON_Surface* face_surface = face.SurfaceOf();
  if (!face_surface) return false;
  if (const auto* ns = ON_NurbsSurface::Cast(face_surface)) {
    out.surface = *ns;
  } else if (face_surface->GetNurbForm(out.surface) <= 0) {
    return false;
  }
  // Side tables win when this Brep built the face itself.
  const size_t fi = static_cast<size_t>(face_index);
  if (fi < side_trims.size()) {
    out.outer = side_trims[fi];
    out.exact_clip = fi < side_exact.size() ? side_exact[fi] : false;
    if (fi < side_holes.size()) out.holes = side_holes[fi];
    return true;
  }
  // Otherwise derive trims from the brep's own loops.
  for (int li = 0; li < face.m_li.Count(); ++li) {
    const int loop_index = face.m_li[li];
    if (loop_index < 0 || loop_index >= brep.m_L.Count()) continue;
    const ON_BrepLoop& loop = brep.m_L[loop_index];
    std::vector<Point2d> poly = SampleLoop(brep, loop);
    if (poly.size() < 3) continue;
    if (loop.m_type == ON_BrepLoop::outer) {
      if (!LoopIsFullDomain(poly, out.surface)) out.outer = std::move(poly);
    } else if (loop.m_type == ON_BrepLoop::inner) {
      out.holes.push_back(std::move(poly));
    }
  }
  if (!out.holes.empty() && out.outer.empty()) {
    // Holes in an otherwise-untrimmed face: use the full domain as outer.
    const ON_Interval du = out.surface.Domain(0), dv = out.surface.Domain(1);
    out.outer = {Point2d(du.Min(), dv.Min()), Point2d(du.Max(), dv.Min()), Point2d(du.Max(), dv.Max()), Point2d(du.Min(), dv.Max())};
  }
  out.exact_clip = !out.outer.empty() && out.holes.empty();
  return true;
}

}  // namespace

Brep Brep::FromSurface(const NurbsSurface& surface) {
  Brep result;
  ON_Brep& brep = result.brep_;

  auto* surface_copy = new ON_NurbsSurface(surface.raw());
  const int surface_index = brep.AddSurface(surface_copy);

  ON_BrepFace& face = brep.NewFace(surface_index);
  (void)face;
  result.face_trim_loops_.emplace_back();  // untrimmed
  result.face_exact_clip_.push_back(false);
  result.face_hole_loops_.emplace_back();

  brep.SetTrimIsoFlags();

  return result;
}

Brep Brep::Box(double x0, double y0, double z0, double x1, double y1,
               double z1) {
  Brep result;
  ON_Brep& brep = result.brep_;

  const Point3d v0(x0, y0, z0);
  const Point3d v1(x1, y0, z0);
  const Point3d v2(x1, y1, z0);
  const Point3d v3(x0, y1, z0);
  const Point3d v4(x0, y0, z1);
  const Point3d v5(x1, y0, z1);
  const Point3d v6(x1, y1, z1);
  const Point3d v7(x0, y1, z1);

  // Each grid is [P(u=0,v=0), P(u=0,v=1), P(u=1,v=0), P(u=1,v=1)] -
  // NurbsSurface::FromControlGrid indexes a u_count=v_count=2 grid as
  // u*v_count+v, so this is the order that produces exactly those four
  // corners. Per-face corner order is chosen so u_dir x v_dir (the
  // tessellator's triangle-winding normal - see NurbsSurface's own
  // TessellateGrid comment) points outward for that face.
  const std::vector<std::vector<Point3d>> face_grids = {
      {v0, v1, v3, v2},  // bottom (-z)
      {v4, v7, v5, v6},  // top (+z)
      {v0, v4, v1, v5},  // front (-y)
      {v3, v2, v7, v6},  // back (+y)
      {v0, v3, v4, v7},  // left (-x)
      {v1, v5, v2, v6},  // right (+x)
  };

  for (const auto& grid : face_grids) {
    const NurbsSurface surface =
        NurbsSurface::FromControlGrid(grid, /*u_count=*/2, /*v_count=*/2,
                                       /*u_degree=*/1, /*v_degree=*/1);
    auto* surface_copy = new ON_NurbsSurface(surface.raw());
    const int surface_index = brep.AddSurface(surface_copy);
    brep.NewFace(surface_index);
    result.face_trim_loops_.emplace_back();  // untrimmed
    result.face_exact_clip_.push_back(false);
    result.face_hole_loops_.emplace_back();
  }

  brep.SetTrimIsoFlags();
  return result;
}

Brep Brep::Sphere(Point3d center, double radius) {
  Brep result;
  ON_Brep& brep = result.brep_;

  ON_Sphere sphere(center, radius);
  auto* surface = new ON_NurbsSurface();
  const int rc = sphere.GetNurbForm(*surface);
  if (rc == 0) {
    delete surface;
    throw std::runtime_error(
        "dino8::kernel::Brep::Sphere: ON_Sphere::GetNurbForm failed");
  }

  const int surface_index = brep.AddSurface(surface);
  brep.NewFace(surface_index);
  result.face_trim_loops_.emplace_back();  // untrimmed
  result.face_exact_clip_.push_back(false);
  result.face_hole_loops_.emplace_back();

  brep.SetTrimIsoFlags();
  return result;
}

Brep Brep::TrimmedPlanarFace(const NurbsSurface& surface,
                              const std::vector<Point2d>& trim_loop_uv,
                              bool exact_clip,
                              std::vector<std::vector<Point2d>> hole_loops_uv) {
  if (trim_loop_uv.size() < 3) {
    throw std::invalid_argument(
        "dino8::kernel::Brep::TrimmedPlanarFace: trim_loop_uv must have at "
        "least 3 points (fewer isn't a closed polygon at all - and, before "
        "this check, an empty trim_loop_uv silently meant \"no trim at "
        "all\" to Tessellate(), a genuine footgun this closes)");
  }
  if (exact_clip && !hole_loops_uv.empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Brep::TrimmedPlanarFace: hole_loops_uv is only "
        "supported with exact_clip=false (whole-cell tessellation)");
  }

  Brep result;
  ON_Brep& brep = result.brep_;

  auto* surface_copy = new ON_NurbsSurface(surface.raw());
  const int surface_index = brep.AddSurface(surface_copy);
  brep.NewFace(surface_index);
  result.face_trim_loops_.push_back(trim_loop_uv);
  result.face_exact_clip_.push_back(exact_clip);
  result.face_hole_loops_.push_back(std::move(hole_loops_uv));

  brep.SetTrimIsoFlags();
  return result;
}

int Brep::FaceCount() const { return brep_.m_F.Count(); }

namespace {

// Newell's method: robust to a slightly non-planar or noisy polygon
// (unlike a two-edge cross product), and its sign follows the loop's own
// winding directly - the polygon and the plane it returns are always
// mutually consistent, which is exactly what a half-space boolean needs.
ON_3dVector NewellNormal(const std::vector<Point3d>& loop) {
  ON_3dVector n(0, 0, 0);
  const size_t k = loop.size();
  for (size_t i = 0; i < k; ++i) {
    const Point3d& p = loop[i];
    const Point3d& q = loop[(i + 1) % k];
    n.x += (p.y - q.y) * (p.z + q.z);
    n.y += (p.z - q.z) * (p.x + q.x);
    n.z += (p.x - q.x) * (p.y + q.y);
  }
  n.Unitize();
  return n;
}

}  // namespace

std::vector<Brep::PlanarFace> Brep::PlanarFaces() const {
  std::vector<PlanarFace> result;
  result.reserve(static_cast<size_t>(brep_.m_F.Count()));
  for (int i = 0; i < brep_.m_F.Count(); ++i) {
    FaceGeometry fg;
    if (!ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fg)) continue;
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (!wrapper.IsPlanar()) {
      throw std::invalid_argument(
          "dino8::kernel::Brep::PlanarFaces: face " + std::to_string(i) +
          " is not planar - this is a planar-only B-rep boolean, see its own doc comment");
    }
    std::vector<Point2d> uv;
    if (!fg.outer.empty()) {
      uv = fg.outer;
    } else {
      const ON_Interval du = fg.surface.Domain(0), dv = fg.surface.Domain(1);
      // Increasing-parameter order around the rectangle - matches every
      // planar-face factory's own "u_dir x v_dir points outward" winding.
      uv = {Point2d(du[0], dv[0]), Point2d(du[1], dv[0]), Point2d(du[1], dv[1]), Point2d(du[0], dv[1])};
    }
    PlanarFace face;
    face.loop.reserve(uv.size());
    for (const Point2d& p : uv) face.loop.push_back(fg.surface.PointAt(p.x, p.y));
    const ON_3dVector n = NewellNormal(face.loop);
    face.plane = ON_Plane(face.loop[0], n);
    result.push_back(std::move(face));
  }
  return result;
}

namespace {

// Coincident-point vertex welding for FromMixedFaces()'s own genuine
// ON_Brep topology (real vertices/edges/trims/loops instead of just
// NewFace(surface_index)): the same principle Mesh::MergeAndWeld() already
// relies on for welding a tessellation's own seams shut, reused here as
// the identity test that gives PlanarFace/CylindricalFace loop points -
// which carry no vertex identity of their own - a shared ON_BrepVertex
// wherever two faces' own loops meet at "the same" 3D point. tol = 1e-6
// matches Mesh::MergeAndWeld's own proven default exactly, not a newly
// invented tolerance; see brep.h's FromMixedFaces doc comment for the
// real, disclosed limit this implies (features smaller than that mis-weld).
constexpr double kBrepWeldTolerance = 1e-6;

struct WeldKey {
  long long x = 0, y = 0, z = 0;
  bool operator==(const WeldKey& other) const {
    return x == other.x && y == other.y && z == other.z;
  }
};

struct WeldKeyHash {
  size_t operator()(const WeldKey& k) const {
    size_t h = std::hash<long long>()(k.x);
    h ^= std::hash<long long>()(k.y) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= std::hash<long long>()(k.z) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};

// Welds coincident 3D points into canonical global vertex ids, local and
// temporary to one FromMixedFaces() call (never stored on Brep itself -
// see brep.h's own comment on why PlanarFace/CylindricalFace need no
// struct changes for this).
class VertexWelder {
 public:
  int Weld(const Point3d& p) {
    const WeldKey key{std::llround(p.x / kBrepWeldTolerance), std::llround(p.y / kBrepWeldTolerance),
                       std::llround(p.z / kBrepWeldTolerance)};
    const auto it = index_of_.find(key);
    if (it != index_of_.end()) return it->second;
    const int id = static_cast<int>(points_.size());
    points_.push_back(p);
    index_of_.emplace(key, id);
    return id;
  }
  const std::vector<Point3d>& Points() const { return points_; }

 private:
  std::unordered_map<WeldKey, int, WeldKeyHash> index_of_;
  std::vector<Point3d> points_;
};

// Per-face bookkeeping needed to build genuine loop/trim/edge topology,
// gathered alongside each face's existing surface-building code below
// without changing any of that math. `vids`/`trim_uv` are parallel arrays,
// one welded global vertex id and one (u, v) trim point per loop point, in
// loop order (already CCW-outward per every planar-face factory's own
// "u_dir x v_dir points outward" convention - see PlanarFaces()'s own
// comment - so this reuses that winding rather than re-deriving it).
struct FaceTopology {
  std::vector<int> vids;
  std::vector<Point2d> trim_uv;
  // Only set for a CylindricalFace's own 4-point (u, v) rectangle loop
  // (see BuildFaceLoop's own comment for why its two cap segments, index 0
  // and 2, need this instead of a plain straight edge). Borrowed - owned
  // by brep.m_S, valid for this whole FromMixedFaces() call.
  ON_NurbsSurface* cylindrical_surface = nullptr;
  double cylindrical_u_max = 0.0;
  double cylindrical_length = 0.0;
};

// Builds one face's genuine ON_BrepLoop plus its edges/trims (spec
// sections 2-3): an edge is created the first time its own {min(vid),
// max(vid)} welded vertex pair is seen and shared automatically the
// second time (the same pair from the adjacent face's own loop) via
// `edge_of_vertex_pair`; a third use is a non-manifold edge, out of scope
// exactly like every other planar-only/convex-only note already in this
// codebase (boolean.h/fillet.h), so it throws rather than silently
// misbuilding a third trim onto it.
//
// The 3D edge curve is a straight ON_LineCurve between the two welded
// points in every case except a CylindricalFace's own two circular cap
// segments (index 0 at v=0, index 2 at v=length, of its 4-point
// [u:0..u_max, v:0..length] rectangle loop - see brep.h's FromMixedFaces
// comment for that rectangle's own construction), which instead use the
// surface's own isocurve (ON_Surface::IsoCurve(0, v)) so the edge's C3
// curve and the trim's 2D-to-surface composition are identical by
// construction, not independently reconstructed and merely close. The
// rectangle's other two segments (index 1 at u=u_max, index 3 at u=0) -
// the fillet's two straight "rail" lines - need no such special-casing:
// they're genuinely straight, so the plain ON_LineCurve path already
// welds them against FilletConvexEdge's own re-trimmed planar faces with
// zero extra work, exactly as that function's own doc comment states.
//
// ON_Surface::IsoCurve(0, c)'s own natural direction is increasing-u
// (point at parameter t is srf(t, c)): segment 0 (u: 0 -> u_max) walks
// that same direction, but segment 2 (u: u_max -> 0, per the rectangle's
// own CCW order) walks it backwards - `iso_reversed` below accounts for
// that so the new edge's own v0/v1 vertex assignment always matches its
// own 3D curve's real start/end point, which every other edge here (and
// ON_Brep's own topology in general) requires.
void BuildFaceLoop(ON_Brep& brep, ON_BrepFace& face, const FaceTopology& topo,
                    std::unordered_map<uint64_t, int>& edge_of_vertex_pair) {
  ON_BrepLoop& loop = brep.NewLoop(ON_BrepLoop::outer, face);
  const size_t n = topo.vids.size();
  for (size_t k = 0; k < n; ++k) {
    const size_t k1 = (k + 1) % n;
    const int vid_from = topo.vids[k];
    const int vid_to = topo.vids[k1];

    const bool is_cap = topo.cylindrical_surface != nullptr && (k == 0 || k == 2);
    const bool iso_reversed = is_cap && k == 2;

    const uint32_t lo = static_cast<uint32_t>(std::min(vid_from, vid_to));
    const uint32_t hi = static_cast<uint32_t>(std::max(vid_from, vid_to));
    const uint64_t key = (static_cast<uint64_t>(lo) << 32) | hi;

    int edge_index;
    const auto it = edge_of_vertex_pair.find(key);
    if (it == edge_of_vertex_pair.end()) {
      ON_Curve* c3 = nullptr;
      int curve_start_vid = vid_from;
      int curve_end_vid = vid_to;
      if (is_cap) {
        const double v_const = (k == 0) ? 0.0 : topo.cylindrical_length;
        ON_Curve* iso = topo.cylindrical_surface->IsoCurve(/*dir=*/0, v_const);
        if (!iso) {
          throw std::runtime_error(
              "dino8::kernel::Brep::FromMixedFaces: ON_Surface::IsoCurve failed "
              "building a CylindricalFace's own cap edge");
        }
        if (!iso->Trim(ON_Interval(0.0, topo.cylindrical_u_max))) {
          delete iso;
          throw std::runtime_error(
              "dino8::kernel::Brep::FromMixedFaces: trimming a CylindricalFace's "
              "own cap isocurve to its real sweep angle failed");
        }
        iso->SetDomain(0.0, 1.0);
        c3 = iso;
        if (iso_reversed) {
          curve_start_vid = vid_to;
          curve_end_vid = vid_from;
        }
      } else {
        c3 = new ON_LineCurve(brep.m_V[vid_from].point, brep.m_V[vid_to].point);
        c3->SetDomain(0.0, 1.0);
      }
      const int c3i = brep.AddEdgeCurve(c3);
      ON_BrepEdge& edge = brep.NewEdge(brep.m_V[curve_start_vid], brep.m_V[curve_end_vid], c3i);
      // A real, checked-directly discovery (not assumed from the spec's
      // own text): the PUBLIC OpenNURBS build's own ON_Brep::
      // SetEdgeTolerance is a deliberate stub for any edge with a trim -
      // its own comment says so verbatim ("TL_Brep::SetEdgeTolerance
      // overrides ON_Brep::SetEdgeTolerance and sets the tolerance
      // correctly") - TL_Brep being Rhino's own closed-source topology
      // library, not part of the public SDK this kernel is built on. Left
      // alone, every edge here would keep ON_UNSET_VALUE forever and
      // IsValid() would report every single one as invalid, regardless of
      // how correct the actual topology is. The honest fix, matching
      // example_brep.cpp's own MakeTwistedCubeEdge (see its own "this
      // simple example is exact" comment): every edge this function
      // builds genuinely IS exact - a straight line between the same two
      // points its own endpoint vertices store, or a CylindricalFace
      // cap's own true isocurve - so 0.0 is the real answer, not a
      // plugged-in default. Pass 5 below calls SetTolerancesBoxesAndFlags
      // with bLazy=true specifically so it leaves this alone instead of
      // overwriting it back to unset.
      edge.m_tolerance = 0.0;
      edge_index = edge.m_edge_index;
      edge_of_vertex_pair.emplace(key, edge_index);
    } else {
      edge_index = it->second;
      if (brep.m_E[edge_index].m_ti.Count() >= 2) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: an edge is shared by 3 or "
            "more faces (non-manifold) - out of scope here, matching every "
            "other planar-only/convex-only scope note already in this "
            "codebase (boolean.h/fillet.h)");
      }
    }

    const ON_BrepEdge& edge = brep.m_E[edge_index];
    const bool bRev3d = (edge.m_vi[0] != vid_from);

    auto* c2 = new ON_LineCurve(topo.trim_uv[k], topo.trim_uv[k1]);
    c2->SetDomain(0.0, 1.0);
    const int c2i = brep.AddTrimCurve(c2);
    brep.NewTrim(brep.m_E[edge_index], bRev3d, loop, c2i);
  }
}

}  // namespace

Brep Brep::FromMixedFaces(const std::vector<Brep::PlanarFace>& faces,
                           const std::vector<Brep::CylindricalFace>& cylindrical_faces) {
  Brep result;
  ON_Brep& brep = result.brep_;
  VertexWelder welder;
  std::vector<FaceTopology> topo;
  for (const PlanarFace& f : faces) {
    if (f.loop.size() < 3) continue;  // degenerate slice - nothing left of this face
    const ON_Plane& pl = f.plane;
    double min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    std::vector<Point2d> local;
    local.reserve(f.loop.size());
    for (size_t k = 0; k < f.loop.size(); ++k) {
      const ON_3dVector d = f.loop[k] - pl.origin;
      const double x = d * pl.xaxis, y = d * pl.yaxis;
      local.emplace_back(x, y);
      if (k == 0) { min_x = max_x = x; min_y = max_y = y; }
      else { min_x = std::min(min_x, x); max_x = std::max(max_x, x); min_y = std::min(min_y, y); max_y = std::max(max_y, y); }
    }
    // A small margin so the trim loop's own extremal points never sit
    // exactly on the surface's own domain edge (a real, if rare, source
    // of clipping-boundary ambiguity in TessellateGridClippedExact).
    const double mx = std::max(1e-9, (max_x - min_x) * 0.05), my = std::max(1e-9, (max_y - min_y) * 0.05);
    min_x -= mx; max_x += mx; min_y -= my; max_y += my;
    const std::vector<Point3d> grid = {
        pl.origin + min_x * pl.xaxis + min_y * pl.yaxis,
        pl.origin + min_x * pl.xaxis + max_y * pl.yaxis,
        pl.origin + max_x * pl.xaxis + min_y * pl.yaxis,
        pl.origin + max_x * pl.xaxis + max_y * pl.yaxis,
    };
    const NurbsSurface surface = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
    // FromControlGrid's clamped-uniform knot vector puts the domain at
    // [0,1] regardless of the grid's real-world span, so rescale each
    // local (x, y) into that normalized domain to get the trim loop.
    std::vector<Point2d> trim;
    trim.reserve(local.size());
    for (const Point2d& p : local) trim.emplace_back((p.x - min_x) / (max_x - min_x), (p.y - min_y) / (max_y - min_y));
    auto* surface_copy = new ON_NurbsSurface(surface.raw());
    const int surface_index = brep.AddSurface(surface_copy);
    brep.NewFace(surface_index);
    result.face_trim_loops_.push_back(trim);
    // exact_clip=true: this face's trim polygon IS its exact boundary
    // (not an approximation of a curved one), so tessellation should
    // clip to it exactly rather than approximate via whole-cell in/out -
    // otherwise a caller measuring volume at low division counts would
    // see grid-approximation error on a shape that has none to begin with.
    result.face_exact_clip_.push_back(true);
    result.face_hole_loops_.emplace_back();

    // Genuine topology (see BuildFaceLoop above): weld this face's own
    // loop points - the exact same 3D points the side tables above just
    // recorded - into canonical global vertex ids, reusing f.loop/trim
    // rather than re-deriving either.
    FaceTopology t;
    t.trim_uv = trim;
    t.vids.reserve(f.loop.size());
    for (const Point3d& p : f.loop) t.vids.push_back(welder.Weld(p));
    topo.push_back(std::move(t));
  }

  for (const CylindricalFace& cf : cylindrical_faces) {
    const ON_Circle circle(cf.frame, cf.radius);
    const ON_Cylinder cyl(circle, cf.length);
    auto* surface = new ON_NurbsSurface();
    const int rc = cyl.GetNurbForm(*surface);
    if (rc == 0) {
      delete surface;
      throw std::runtime_error(
          "dino8::kernel::Brep::FromMixedFaces: ON_Cylinder::GetNurbForm failed "
          "(invalid frame/radius/length)");
    }
    // The cylinder's own NURBS surface parameterizes u by the base
    // circle's NURBS-curve parameter (NOT true radian angle - see this
    // method's own doc comment) and v linearly by true height (v == the
    // real distance along `frame.zaxis`, since ON_Cylinder::GetNurbForm
    // sets the v-knots directly to [height[0], height[1]] with no
    // reparameterization). So v = 0 and v = cf.length are exactly right,
    // but the u-bound for `cf.angle` of true sweep has to be found via
    // the real NURBS<->radian conversion ON_Circle itself provides.
    double u_max = 0.0;
    if (!circle.GetNurbFormParameterFromRadian(cf.angle, &u_max)) {
      delete surface;
      throw std::invalid_argument(
          "dino8::kernel::Brep::FromMixedFaces: CylindricalFace::angle is out "
          "of ON_Circle's own [0, 2*pi] NURBS-parameterization domain");
    }
    const int surface_index = brep.AddSurface(surface);
    brep.NewFace(surface_index);
    // Same increasing-parameter corner order PlanarFaces()'s own
    // untrimmed-domain fallback uses - and, per this method's own doc
    // comment, u_dir x v_dir already points radially outward for
    // ON_Cylinder::GetNurbForm's natural parameterization (verified
    // directly: at u=0 the tangent in u is r*(local +y) and dP/dv is
    // frame.zaxis, whose cross product is r*frame.xaxis - the true
    // outward radial direction at angle 0), so no m_bRev flip is needed
    // here, matching Sphere()'s own precedent of never setting it either.
    const std::vector<Point2d> trim = {Point2d(0.0, 0.0), Point2d(u_max, 0.0),
                                        Point2d(u_max, cf.length), Point2d(0.0, cf.length)};
    result.face_trim_loops_.push_back(trim);
    // exact_clip=true for the same reason FromPlanarFaces()'s own faces
    // use it above: this trim rectangle IS the patch's exact boundary
    // (the two straight rails at u=0/u=u_max and the two circular arcs at
    // v=0/v=length), not an approximation of one.
    result.face_exact_clip_.push_back(true);
    result.face_hole_loops_.emplace_back();

    // Genuine topology (see BuildFaceLoop above): the same 4 corner
    // points the trim rectangle's own UV corners map to through this
    // exact surface - welded into the same global vertex space as every
    // PlanarFace loop above, which is precisely what lets a fillet's two
    // straight rails share real ON_BrepEdge objects with the adjacent
    // re-trimmed planar faces' own matching corners, with no special
    // casing (see FilletConvexEdge's own doc comment for why those rail
    // points are exact to floating-point precision, not merely close).
    FaceTopology t;
    t.trim_uv = trim;
    t.vids = {welder.Weld(surface->PointAt(0.0, 0.0)), welder.Weld(surface->PointAt(u_max, 0.0)),
              welder.Weld(surface->PointAt(u_max, cf.length)), welder.Weld(surface->PointAt(0.0, cf.length))};
    t.cylindrical_surface = surface;
    t.cylindrical_u_max = u_max;
    t.cylindrical_length = cf.length;
    topo.push_back(std::move(t));
  }

  // Pass 2 (see this feature's own spec): materialize one real
  // ON_BrepVertex per canonical welded point, in weld-id order - `brep`
  // starts with an empty m_V, and NewVertex() always appends at the next
  // index, so this makes brep.m_V's own indices exactly match every
  // `vids` entry recorded above.
  for (const Point3d& p : welder.Points()) brep.NewVertex(p);

  // Passes 3 + 4: one genuine ON_BrepLoop plus its edges/trims per face,
  // sharing an edge automatically wherever two faces' own welded vertex
  // pairs match (see BuildFaceLoop's own doc comment for exactly how).
  std::unordered_map<uint64_t, int> edge_of_vertex_pair;
  for (size_t fi = 0; fi < topo.size(); ++fi) {
    BuildFaceLoop(brep, brep.m_F[static_cast<int>(fi)], topo[fi], edge_of_vertex_pair);
  }

  // Pass 5: NewVertex()/NewEdge()/NewTrim() above all leave m_tolerance at
  // ON_UNSET_VALUE - a sentinel ON_Brep::IsValid() rejects outright - so
  // this real geometry-derived tolerance/flag pass (replacing the old
  // bare SetTrimIsoFlags() call FromSurface()/Box()/Sphere()/
  // TrimmedPlanarFace() still use, since they don't build this topology)
  // is required here, not optional polish. bLazy=true so this leaves
  // BuildFaceLoop's own already-correct edge.m_tolerance=0.0 alone
  // (see its own comment for why: the public build's SetEdgeTolerance is
  // a stub that would otherwise reset it right back to unset) while still
  // genuinely computing every vertex/trim tolerance, loop type, iso flag,
  // and trim bounding box left at their own NewVertex()/NewTrim() sentinel.
  brep.SetTolerancesBoxesAndFlags(/*bLazy=*/true);
  return result;
}

Brep Brep::FromPlanarFaces(const std::vector<Brep::PlanarFace>& faces) {
  return FromMixedFaces(faces, {});
}

BoundingBox Brep::GetTightBoundingBox() const {
  ON_BoundingBox box;
  if (!brep_.GetTightBoundingBox(box)) {
    throw std::runtime_error(
        "dino8::kernel::Brep::GetTightBoundingBox: ON_Brep::"
        "GetTightBoundingBox failed");
  }
  return BoundingBox{box.Min(), box.Max()};
}

std::vector<Mesh> Brep::Tessellate(int u_divisions, int v_divisions) const {
  std::vector<Mesh> result;
  result.reserve(static_cast<size_t>(brep_.m_F.Count()));
  for (int i = 0; i < brep_.m_F.Count(); ++i) {
    FaceGeometry fg;
    if (!ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fg)) continue;
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (fg.outer.empty()) {
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions));
    } else if (fg.exact_clip) {
      result.push_back(wrapper.TessellateGridClippedExact(u_divisions, v_divisions, fg.outer));
    } else {
      const std::vector<std::vector<Point2d>>* holes = fg.holes.empty() ? nullptr : &fg.holes;
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions, &fg.outer, holes));
    }
    if (brep_.m_F[i].m_bRev) result.back() = result.back().FlipNormals();
  }
  return result;
}

Mesh Brep::TessellateToClosedMesh(int u_divisions, int v_divisions) const {
  return Mesh::MergeAndWeld(Tessellate(u_divisions, v_divisions));
}

std::vector<Mesh> Brep::TessellateAdaptive(double chord_tolerance) const {
  std::vector<Mesh> result;
  result.reserve(static_cast<size_t>(brep_.m_F.Count()));
  for (int i = 0; i < brep_.m_F.Count(); ++i) {
    FaceGeometry fg;
    if (!ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fg)) continue;
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (fg.outer.empty()) {
      result.push_back(wrapper.TessellateGridAdaptive(chord_tolerance));
    } else if (fg.exact_clip) {
      result.push_back(wrapper.TessellateGridClippedExactAdaptive(chord_tolerance, fg.outer));
    } else {
      const std::vector<std::vector<Point2d>>* holes = fg.holes.empty() ? nullptr : &fg.holes;
      result.push_back(wrapper.TessellateGridAdaptive(chord_tolerance, &fg.outer, holes));
    }
    if (brep_.m_F[i].m_bRev) result.back() = result.back().FlipNormals();
  }
  return result;
}

Mesh Brep::TessellateToClosedMeshAdaptive(double chord_tolerance) const {
  return Mesh::MergeAndWeld(TessellateAdaptive(chord_tolerance));
}

std::vector<Mesh> Brep::TessellateNonUniformAdaptive(double chord_tolerance) const {
  std::vector<Mesh> result;
  result.reserve(static_cast<size_t>(brep_.m_F.Count()));
  for (int i = 0; i < brep_.m_F.Count(); ++i) {
    FaceGeometry fg;
    if (!ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fg)) continue;
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (fg.outer.empty()) {
      result.push_back(wrapper.TessellateGridNonUniformAdaptive(chord_tolerance));
    } else if (fg.exact_clip) {
      result.push_back(wrapper.TessellateGridClippedExactAdaptive(chord_tolerance, fg.outer));
    } else {
      const std::vector<std::vector<Point2d>>* holes = fg.holes.empty() ? nullptr : &fg.holes;
      result.push_back(wrapper.TessellateGridNonUniformAdaptive(chord_tolerance, &fg.outer, holes));
    }
    if (brep_.m_F[i].m_bRev) result.back() = result.back().FlipNormals();
  }
  return result;
}

Mesh Brep::TessellateToClosedMeshNonUniformAdaptive(double chord_tolerance) const {
  return Mesh::MergeAndWeld(TessellateNonUniformAdaptive(chord_tolerance));
}

}  // namespace dino8::kernel
