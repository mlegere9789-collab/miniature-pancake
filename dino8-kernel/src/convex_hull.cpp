#include "dino8/kernel/convex_hull.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dino8::kernel {

namespace {

double PointCloudExtent(const std::vector<Point3d>& points) {
  double max_extent = 0.0;
  for (const Point3d& p : points) {
    max_extent = std::max({max_extent, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
  }
  return max_extent;
}

// A relative tolerance in the same spirit as boolean.cpp's own
// RelativeTol: scales with the point cloud's own size instead of using
// one fixed epsilon for both a millimeter part and a kilometer-scale one.
double PointCloudTol(const std::vector<Point3d>& points) {
  return std::max(1e-9, PointCloudExtent(points) * 1e-9);
}

// Packs a DIRECTED edge (a, b) into one 64-bit key - deliberately not the
// min/max-first packing brep.cpp's own edge_of_vertex_pair uses (that one
// is for an UNDIRECTED edge shared by exactly two faces regardless of
// which walks it forward; this one is for finding a face's neighbor
// across an edge, which needs the two faces' own OPPOSITE walking
// directions to be distinguishable - see the horizon-finding loop below).
inline uint64_t DirectedEdgeKey(int a, int b) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
}

ON_Plane MakeTriPlane(const Point3d& a, const Point3d& b, const Point3d& c) {
  ON_3dVector n = ON_CrossProduct(b - a, c - a);
  n.Unitize();
  return ON_Plane(a, n);
}

// One live (or since-deleted) triangular facet of the hull under
// construction. `outside` holds indices (into the shared `points` array
// passed through QuickHull3D) of every not-yet-hulled point currently
// known to lie strictly outside this face's own plane - Barber, Dobkin &
// Huddleston's own "outside set."
struct HullFace {
  int v0 = -1, v1 = -1, v2 = -1;
  ON_Plane plane;
  std::vector<int> outside;
  bool alive = true;
};

HullFace MakeFace(const std::vector<Point3d>& pts, int v0, int v1, int v2) {
  HullFace f;
  f.v0 = v0;
  f.v1 = v1;
  f.v2 = v2;
  f.plane = MakeTriPlane(pts[v0], pts[v1], pts[v2]);
  return f;
}

// Assigns each of `candidates` to the outside set of the first face
// (among `face_indices`) it lies strictly outside of. A point outside
// more than one face is fine to assign to just one: QuickHull3D's own
// termination test is "does ANY live face have a nonempty outside set,"
// not "is this point recorded against every face it could be," so a
// single assignment is enough to guarantee the point gets processed
// (and, once processed, whichever face it's ultimately still outside of
// gets its own chance to claim it - see the redistribution step in
// QuickHull3D for why that's still correct after a face is deleted).
void DistributePoints(const std::vector<Point3d>& pts, const std::vector<int>& candidates,
                       std::vector<HullFace>& faces, const std::vector<int>& face_indices, double tol) {
  for (int idx : candidates) {
    for (int fi : face_indices) {
      if (faces[fi].plane.DistanceTo(pts[idx]) > tol) {
        faces[fi].outside.push_back(idx);
        break;
      }
    }
  }
}

// Seeds the initial tetrahedron per Barber/Dobkin/Huddleston's own
// construction (see convex_hull.h's own doc comment, step 1): the 6
// axis-extreme points, then the pair among those six with maximum
// separation, then the point (over ALL of `pts`) farthest from that
// line, then the point (again over ALL of `pts`) farthest from the plane
// those three span. Throws std::invalid_argument the moment any of those
// three "farthest" distances comes back within `tol` of zero - points
// all coincident, all collinear, or all coplanar, respectively; no 3D
// hull exists in any of those cases.
std::array<int, 4> SeedTetrahedron(const std::vector<Point3d>& pts, double tol) {
  std::vector<int> extremes;
  extremes.reserve(6);
  for (int axis = 0; axis < 3; ++axis) {
    auto coord = [axis](const Point3d& p) { return axis == 0 ? p.x : (axis == 1 ? p.y : p.z); };
    int lo = 0, hi = 0;
    for (int i = 1; i < static_cast<int>(pts.size()); ++i) {
      if (coord(pts[i]) < coord(pts[lo])) lo = i;
      if (coord(pts[i]) > coord(pts[hi])) hi = i;
    }
    extremes.push_back(lo);
    extremes.push_back(hi);
  }

  int p0 = extremes[0], p1 = extremes[1];
  double best = -1.0;
  for (size_t i = 0; i < extremes.size(); ++i) {
    for (size_t j = i + 1; j < extremes.size(); ++j) {
      const double d = pts[extremes[i]].DistanceTo(pts[extremes[j]]);
      if (d > best) {
        best = d;
        p0 = extremes[i];
        p1 = extremes[j];
      }
    }
  }
  if (best <= tol) {
    throw std::invalid_argument(
        "dino8::kernel::ExactConvexHull: points are all coincident (within "
        "tolerance) - no 3D hull exists");
  }

  ON_3dVector dir = pts[p1] - pts[p0];
  dir.Unitize();
  int p2 = -1;
  double best_line = -1.0;
  for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
    const ON_3dVector to_p = pts[i] - pts[p0];
    const double d = ON_CrossProduct(to_p, dir).Length();
    if (d > best_line) {
      best_line = d;
      p2 = i;
    }
  }
  if (best_line <= tol) {
    throw std::invalid_argument(
        "dino8::kernel::ExactConvexHull: points are all collinear - no 3D "
        "hull exists");
  }

  const ON_Plane seed_plane = MakeTriPlane(pts[p0], pts[p1], pts[p2]);
  int p3 = -1;
  double best_plane = -1.0;
  for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
    const double d = std::fabs(seed_plane.DistanceTo(pts[i]));
    if (d > best_plane) {
      best_plane = d;
      p3 = i;
    }
  }
  if (best_plane <= tol) {
    throw std::invalid_argument(
        "dino8::kernel::ExactConvexHull: points are all coplanar - no 3D "
        "hull exists");
  }

  return {p0, p1, p2, p3};
}

// The QuickHull main loop (convex_hull.h's own doc comment, steps 1-4).
// Returns every face ever created, live and deleted alike (callers must
// filter on `alive`) - deleted ones are kept in place rather than erased
// so every earlier-recorded face index into this vector stays valid.
std::vector<HullFace> QuickHull3D(const std::vector<Point3d>& pts, double tol) {
  const std::array<int, 4> seed = SeedTetrahedron(pts, tol);

  // Orders each tetrahedron face's own three vertices so its plane's
  // normal points away from the fourth ("opposite") vertex - the
  // standard "does the leftover point sit on the wrong side? then flip
  // the winding" trick, valid here because the four seed points are
  // already known affinely independent (SeedTetrahedron's own
  // non-degeneracy checks): every 3-point subset of an affinely
  // independent 4-point set spans a plane that strictly excludes the
  // fourth point, so this sidedness test is never ambiguous.
  auto make_outward = [&](int a, int b, int c, int opposite) {
    HullFace f = MakeFace(pts, a, b, c);
    if (f.plane.DistanceTo(pts[opposite]) > 0.0) {
      f = MakeFace(pts, a, c, b);  // reversed winding => reversed (now-outward) normal
    }
    return f;
  };

  std::vector<HullFace> faces;
  faces.reserve(pts.size());  // a generous but not exact bound; grows fine either way
  faces.push_back(make_outward(seed[0], seed[1], seed[2], seed[3]));
  faces.push_back(make_outward(seed[0], seed[1], seed[3], seed[2]));
  faces.push_back(make_outward(seed[0], seed[2], seed[3], seed[1]));
  faces.push_back(make_outward(seed[1], seed[2], seed[3], seed[0]));

  std::unordered_map<uint64_t, int> edge_to_face;
  auto index_face_edges = [&](int fi) {
    const HullFace& f = faces[fi];
    edge_to_face[DirectedEdgeKey(f.v0, f.v1)] = fi;
    edge_to_face[DirectedEdgeKey(f.v1, f.v2)] = fi;
    edge_to_face[DirectedEdgeKey(f.v2, f.v0)] = fi;
  };
  for (int fi = 0; fi < 4; ++fi) index_face_edges(fi);

  std::vector<int> remaining;
  remaining.reserve(pts.size());
  for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
    if (i == seed[0] || i == seed[1] || i == seed[2] || i == seed[3]) continue;
    remaining.push_back(i);
  }
  DistributePoints(pts, remaining, faces, {0, 1, 2, 3}, tol);

  while (true) {
    int fi = -1;
    for (int i = 0; i < static_cast<int>(faces.size()); ++i) {
      if (faces[i].alive && !faces[i].outside.empty()) {
        fi = i;
        break;
      }
    }
    if (fi < 0) break;  // every live face's outside set is empty - done (step 4)

    const HullFace& F = faces[fi];
    int apex = -1;
    double best = -1.0;
    for (int idx : F.outside) {
      const double d = F.plane.DistanceTo(pts[idx]);
      if (d > best) {
        best = d;
        apex = idx;
      }
    }

    // Every live face visible from `apex` - on a convex polytope this is
    // always a single connected patch (containing F, since apex is in
    // F's own outside set), so a plain per-face distance test finds
    // exactly the right set without needing to walk face adjacency.
    std::vector<int> visible;
    for (int i = 0; i < static_cast<int>(faces.size()); ++i) {
      if (faces[i].alive && faces[i].plane.DistanceTo(pts[apex]) > tol) visible.push_back(i);
    }
    const std::unordered_set<int> visible_set(visible.begin(), visible.end());

    // Horizon: a visible face's directed edge (a, b) is on the horizon
    // exactly when the matching OPPOSITE-direction edge (b, a) - which a
    // closed, consistently wound 2-manifold always has exactly one
    // neighboring face using - belongs to a face that is NOT visible (or
    // to no live face at all, which shouldn't happen on well-formed
    // input but is treated the same conservative way: still a horizon
    // edge, rather than silently dropped).
    std::vector<std::pair<int, int>> horizon;
    for (int vi : visible) {
      const HullFace& vf = faces[vi];
      const int vs[3] = {vf.v0, vf.v1, vf.v2};
      for (int k = 0; k < 3; ++k) {
        const int a = vs[k], b = vs[(k + 1) % 3];
        const auto it = edge_to_face.find(DirectedEdgeKey(b, a));
        const bool neighbor_visible = it != edge_to_face.end() && visible_set.count(it->second) != 0;
        if (!neighbor_visible) horizon.emplace_back(a, b);
      }
    }

    // Every point any deleted face still had in its own outside set
    // (other than the apex itself, which is about to become a hull
    // vertex) needs a new home. It's enough to check these ONLY against
    // the brand-new cap faces, not the whole hull: the untouched part of
    // the hull didn't change shape at all, so a point that wasn't
    // outside any of those faces before certainly isn't now; the only
    // way it can still be outside the ENLARGED hull is by being outside
    // one of the new faces that cap the enlargement.
    std::vector<int> orphans;
    for (int vi : visible) {
      for (int idx : faces[vi].outside) {
        if (idx != apex) orphans.push_back(idx);
      }
    }

    for (int vi : visible) {
      const HullFace& vf = faces[vi];
      edge_to_face.erase(DirectedEdgeKey(vf.v0, vf.v1));
      edge_to_face.erase(DirectedEdgeKey(vf.v1, vf.v2));
      edge_to_face.erase(DirectedEdgeKey(vf.v2, vf.v0));
      faces[vi].alive = false;
    }

    std::vector<int> new_faces;
    new_faces.reserve(horizon.size());
    for (const auto& edge : horizon) {
      // (apex, a, b) preserves the horizon edge's own (a, b) direction as
      // recorded in the visible face's own outward-wound loop - the
      // standard incremental-hull construction that keeps every new
      // face's winding consistently outward with no separate fix-up pass.
      faces.push_back(MakeFace(pts, apex, edge.first, edge.second));
      const int nfi = static_cast<int>(faces.size()) - 1;
      index_face_edges(nfi);
      new_faces.push_back(nfi);
    }

    DistributePoints(pts, orphans, faces, new_faces, tol);
  }

  return faces;
}

double Cross2Local(const Point2d& o, const Point2d& a, const Point2d& b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Andrew's monotone chain (see convex_hull.h's own doc comment): returns
// indices into `pts2d`, in CCW order, of that point set's 2D convex
// hull. `area_eps` is the (squared-length-scale) threshold below which a
// turn is treated as "not strictly left" and its middle point dropped -
// this is what collapses a straight run of several points lying exactly
// along one edge of a face (e.g. a QuickHull triangle fan's own internal
// diagonal endpoints, which are genuine hull vertices of the SOLID but
// not of any individual planar face's own minimal boundary) down to just
// that edge's two true corners.
std::vector<int> MonotoneChainHull(const std::vector<Point2d>& pts2d, double area_eps) {
  const int n = static_cast<int>(pts2d.size());
  if (n < 3) {
    std::vector<int> all(n);
    for (int i = 0; i < n; ++i) all[i] = i;
    return all;
  }

  std::vector<int> order(n);
  for (int i = 0; i < n; ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    if (pts2d[a].x != pts2d[b].x) return pts2d[a].x < pts2d[b].x;
    return pts2d[a].y < pts2d[b].y;
  });

  auto build_half = [&](auto begin, auto end) {
    std::vector<int> half;
    for (auto it = begin; it != end; ++it) {
      const int idx = *it;
      while (half.size() >= 2 &&
             Cross2Local(pts2d[half[half.size() - 2]], pts2d[half[half.size() - 1]], pts2d[idx]) <= area_eps) {
        half.pop_back();
      }
      half.push_back(idx);
    }
    return half;
  };

  std::vector<int> lower = build_half(order.begin(), order.end());
  std::vector<int> upper = build_half(order.rbegin(), order.rend());
  lower.pop_back();  // == order.back(), duplicated as upper's own first point
  upper.pop_back();  // == order.front(), duplicated as lower's own first point
  lower.insert(lower.end(), upper.begin(), upper.end());
  return lower;
}

// Groups QuickHull3D's own triangular facets by supporting plane and
// recovers each group's exact polygonal boundary - the genuinely new
// part beyond a plain triangulated mesh hull; see convex_hull.h's own
// doc comment for the full description of both steps.
std::vector<Brep::PlanarFace> GroupFacesIntoPolygons(const std::vector<Point3d>& pts,
                                                      const std::vector<HullFace>& faces, double tol) {
  auto same_plane = [tol](const ON_Plane& p, const ON_Plane& q) {
    return std::fabs(p.DistanceTo(q.origin)) <= tol && p.zaxis.IsParallelTo(q.zaxis, 1e-6) == 1;
  };

  struct Group {
    ON_Plane plane;
    std::vector<int> verts;  // unique point indices incident to this plane's faces
  };
  std::vector<Group> groups;

  auto add_vertex = [](Group& g, int idx) {
    if (std::find(g.verts.begin(), g.verts.end(), idx) == g.verts.end()) g.verts.push_back(idx);
  };

  for (const HullFace& f : faces) {
    if (!f.alive) continue;
    int gi = -1;
    for (size_t i = 0; i < groups.size(); ++i) {
      if (same_plane(groups[i].plane, f.plane)) {
        gi = static_cast<int>(i);
        break;
      }
    }
    if (gi < 0) {
      groups.push_back(Group{f.plane, {}});
      gi = static_cast<int>(groups.size()) - 1;
    }
    add_vertex(groups[gi], f.v0);
    add_vertex(groups[gi], f.v1);
    add_vertex(groups[gi], f.v2);
  }

  const double extent = PointCloudExtent(pts);
  const double area_eps = std::max(1e-12, extent * extent * 1e-12);

  std::vector<Brep::PlanarFace> result;
  result.reserve(groups.size());
  for (const Group& g : groups) {
    // Same local (x, y) projection convention boolean.cpp's own
    // ProjectOntoPlaneAxes and Brep::FromPlanarFaces both already use -
    // so a loop built here is in the exact same 2D frame either of those
    // would independently derive from `g.plane`.
    std::vector<Point2d> proj;
    proj.reserve(g.verts.size());
    for (int idx : g.verts) {
      const ON_3dVector d = pts[idx] - g.plane.origin;
      proj.emplace_back(d * g.plane.xaxis, d * g.plane.yaxis);
    }

    const std::vector<int> hull_order = MonotoneChainHull(proj, area_eps);
    if (hull_order.size() < 3) continue;  // degenerate group - shouldn't occur for a genuine solid face

    Brep::PlanarFace pf;
    pf.plane = g.plane;
    pf.loop.reserve(hull_order.size());
    for (int local_idx : hull_order) pf.loop.push_back(pts[g.verts[local_idx]]);
    result.push_back(std::move(pf));
  }
  return result;
}

}  // namespace

Brep ExactConvexHull(const std::vector<Point3d>& points) {
  if (points.size() < 4) {
    throw std::invalid_argument(
        "dino8::kernel::ExactConvexHull: points must have at least 4 entries "
        "(fewer can't bound a nonzero 3D volume)");
  }
  const double tol = PointCloudTol(points);
  const std::vector<HullFace> faces = QuickHull3D(points, tol);
  const std::vector<Brep::PlanarFace> planar_faces = GroupFacesIntoPolygons(points, faces, tol);
  return Brep::FromPlanarFaces(planar_faces);
}

}  // namespace dino8::kernel
