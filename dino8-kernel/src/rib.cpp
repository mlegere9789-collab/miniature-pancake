#include "dino8/kernel/rib.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "dino8/kernel/boolean_general.h"

namespace dino8::kernel {

namespace {

struct Seg2 {
  double ax, ay, bx, by;
};

double Cross2(double ux, double uy, double vx, double vy) { return ux * vy - uy * vx; }

// True when the closed segments touch or cross (collinear overlap included).
bool SegmentsTouch(const Seg2& s, const Seg2& t, double eps) {
  const double d1 = Cross2(s.bx - s.ax, s.by - s.ay, t.ax - s.ax, t.ay - s.ay);
  const double d2 = Cross2(s.bx - s.ax, s.by - s.ay, t.bx - s.ax, t.by - s.ay);
  const double d3 = Cross2(t.bx - t.ax, t.by - t.ay, s.ax - t.ax, s.ay - t.ay);
  const double d4 = Cross2(t.bx - t.ax, t.by - t.ay, s.bx - t.ax, s.by - t.ay);
  const double ls = std::hypot(s.bx - s.ax, s.by - s.ay);
  const double lt = std::hypot(t.bx - t.ax, t.by - t.ay);
  const double e1 = eps * ls, e2 = eps * lt;
  // Strictly opposite sides on both segments: a proper crossing.
  if (((d1 > e1 && d2 < -e1) || (d1 < -e1 && d2 > e1)) && ((d3 > e2 && d4 < -e2) || (d3 < -e2 && d4 > e2))) {
    return true;
  }
  // Touching: an endpoint of one lies on the other.
  auto on_seg = [&](double px, double py, const Seg2& g, double dist_cross, double e) {
    if (std::fabs(dist_cross) > e) return false;
    const double lo_x = std::min(g.ax, g.bx) - eps, hi_x = std::max(g.ax, g.bx) + eps;
    const double lo_y = std::min(g.ay, g.by) - eps, hi_y = std::max(g.ay, g.by) + eps;
    return px >= lo_x && px <= hi_x && py >= lo_y && py <= hi_y;
  };
  return on_seg(t.ax, t.ay, s, d1, e1) || on_seg(t.bx, t.by, s, d2, e1) || on_seg(s.ax, s.ay, t, d3, e2) ||
         on_seg(s.bx, s.by, t, d4, e2);
}

[[noreturn]] void Bad(const std::string& why) {
  throw std::invalid_argument("dino8::kernel::MakeRib: " + why);
}

[[noreturn]] void Refuse(const std::string& why) {
  throw std::runtime_error("dino8::kernel::MakeRib: " + why);
}

Point3d Add(const Point3d& p, const Vector3d& d) { return Point3d(p.x + d.x, p.y + d.y, p.z + d.z); }

Brep BuildWeb(const std::vector<Point3d>& profile, double thickness, bool symmetric, double* extent_out) {
  if (!(thickness > 0.0) || !std::isfinite(thickness)) Bad("thickness must be a positive finite number");
  const size_t n = profile.size();
  if (n < 3) Bad("profile needs at least 3 vertices (got " + std::to_string(n) + ")");

  // Extent and centroid of the vertex ring.
  double lo[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::max()};
  double hi[3] = {-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
                  -std::numeric_limits<double>::max()};
  Vector3d centroid(0, 0, 0);
  for (const Point3d& p : profile) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) Bad("profile has a non-finite vertex");
    const double c[3] = {p.x, p.y, p.z};
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], c[k]);
      hi[k] = std::max(hi[k], c[k]);
    }
    centroid = centroid + Vector3d(p.x, p.y, p.z);
  }
  centroid = centroid * (1.0 / static_cast<double>(n));
  const double extent = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
  if (!(extent > 0.0)) Bad("profile is a single point");
  *extent_out = extent;

  // Newell normal (|N| = twice the polygon's area for a planar ring).
  Vector3d nrm(0, 0, 0);
  for (size_t i = 0; i < n; ++i) {
    const Point3d& a = profile[i];
    const Point3d& b = profile[(i + 1) % n];
    nrm.x += (a.y - b.y) * (a.z + b.z);
    nrm.y += (a.z - b.z) * (a.x + b.x);
    nrm.z += (a.x - b.x) * (a.y + b.y);
  }
  const double twice_area = nrm.Length();
  if (!(twice_area > 1e-9 * extent * extent)) Bad("profile has (near) zero area");
  nrm = nrm * (1.0 / twice_area);

  for (size_t i = 0; i < n; ++i) {
    const double dist = (profile[i].x - centroid.x) * nrm.x + (profile[i].y - centroid.y) * nrm.y +
                        (profile[i].z - centroid.z) * nrm.z;
    if (std::fabs(dist) > 1e-6 * extent) {
      Bad("profile is not planar (vertex " + std::to_string(i) + " is " + std::to_string(std::fabs(dist)) +
          " from the best-fit plane, tolerance " + std::to_string(1e-6 * extent) + ")");
    }
    for (size_t j = i + 1; j < n; ++j) {
      const double d = std::hypot(std::hypot(profile[i].x - profile[j].x, profile[i].y - profile[j].y),
                                  profile[i].z - profile[j].z);
      if (d <= 1e-9 * extent) {
        Bad("profile repeats a vertex (" + std::to_string(i) + " and " + std::to_string(j) +
            "); list the ring open, without repeating the first vertex");
      }
    }
  }

  // Self-intersection: project onto the plane dropping the dominant normal axis.
  {
    int drop = 0;
    if (std::fabs(nrm.y) > std::fabs(nrm.x)) drop = 1;
    if (std::fabs(nrm.z) > std::fabs(drop == 0 ? nrm.x : nrm.y)) drop = 2;
    auto proj = [&](const Point3d& p) {
      if (drop == 0) return std::make_pair(p.y, p.z);
      if (drop == 1) return std::make_pair(p.x, p.z);
      return std::make_pair(p.x, p.y);
    };
    std::vector<Seg2> segs;
    for (size_t i = 0; i < n; ++i) {
      const auto a = proj(profile[i]);
      const auto b = proj(profile[(i + 1) % n]);
      segs.push_back({a.first, a.second, b.first, b.second});
    }
    for (size_t i = 0; i < n; ++i) {
      for (size_t j = i + 1; j < n; ++j) {
        const bool adjacent = (j == i + 1) || (i == 0 && j == n - 1);
        if (adjacent) continue;
        if (SegmentsTouch(segs[i], segs[j], 1e-9 * extent)) {
          Bad("profile self-intersects (edges " + std::to_string(i) + " and " + std::to_string(j) + " touch or cross)");
        }
      }
    }
  }

  // Orient the ring CCW about nrm, then sweep along +nrm.
  std::vector<Point3d> ring = profile;  // Newell normal => ring is CCW about nrm already
  const Vector3d half = nrm * (symmetric ? -0.5 * thickness : 0.0);
  const Vector3d sweep = nrm * thickness;
  std::vector<Point3d> bottom, top;
  for (const Point3d& p : ring) {
    bottom.push_back(Add(p, half));
    top.push_back(Add(Add(p, half), sweep));
  }

  auto plane_at = [](const Point3d& o, const Vector3d& normal) {
    return ON_Plane(ON_3dPoint(o.x, o.y, o.z), ON_3dVector(normal.x, normal.y, normal.z));
  };
  std::vector<Brep::PlanarFace> faces;
  {
    Brep::PlanarFace f;  // back face, outward = -nrm, loop CCW seen from outside => reversed ring
    f.plane = plane_at(bottom[0], Vector3d(-nrm.x, -nrm.y, -nrm.z));
    for (size_t i = n; i-- > 0;) f.loop.push_back(bottom[i]);
    faces.push_back(f);
  }
  {
    Brep::PlanarFace f;  // front face, outward = +nrm
    f.plane = plane_at(top[0], nrm);
    f.loop = top;
    faces.push_back(f);
  }
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    Brep::PlanarFace f;
    f.loop = {bottom[i], bottom[j], top[j], top[i]};
    ON_3dVector e(bottom[j].x - bottom[i].x, bottom[j].y - bottom[i].y, bottom[j].z - bottom[i].z);
    ON_3dVector s(sweep.x, sweep.y, sweep.z);
    ON_3dVector side = ON_CrossProduct(e, s);
    side.Unitize();
    f.plane = ON_Plane(ON_3dPoint(bottom[i].x, bottom[i].y, bottom[i].z), side);
    faces.push_back(f);
  }
  Brep tool = Brep::FromPlanarFaces(faces);
  if (!tool.raw().IsValid() || !tool.raw().IsSolid()) Refuse("internal: the swept web is not a valid closed solid");

  return tool;
}

}  // namespace

RibResult MakeRibs(const Brep& solid, const std::vector<std::vector<Point3d>>& profiles, double thickness,
                   bool symmetric) {
  if (solid.raw().m_F.Count() == 0) Bad("solid has no faces");
  if (profiles.empty()) Bad("no rib profiles given");
  std::vector<Brep> webs;
  double extent = 0;
  for (const auto& prof : profiles) {
    double e = 0;
    webs.push_back(BuildWeb(prof, thickness, symmetric, &e));
    extent = std::max(extent, e);
  }

  RibResult out;
  for (const Brep& w : webs) {
    try {
      out.tool_volume += w.Volume();
    } catch (const std::exception& e) {
      Refuse(std::string("could not measure the web volume: ") + e.what());
    }
  }
  if (!(out.tool_volume > 0.0)) Refuse("internal: the swept web has non-positive volume");

  const double tol = 1e-9 * std::max(extent, 1.0);
  // Several webs go in as ONE compound tool, which needs them pairwise disjoint (measured: fusing a second web onto the
  // already-fused result of a first one is refused by the engine, a compound tool is not).
  for (size_t i = 0; i < webs.size(); ++i) {
    for (size_t j = i + 1; j < webs.size(); ++j) {
      const BoundingBox a = webs[i].GetTightBoundingBox();
      const BoundingBox b = webs[j].GetTightBoundingBox();
      const bool apart = a.max.x < b.min.x - tol || a.min.x > b.max.x + tol || a.max.y < b.min.y - tol ||
                         a.min.y > b.max.y + tol || a.max.z < b.min.z - tol || a.min.z > b.max.z + tol;
      if (!apart) {
        Bad("ribs " + std::to_string(i) + " and " + std::to_string(j) +
            " have overlapping bounding boxes; MakeRibs needs pairwise-separate webs (crossing ribs are not supported)");
      }
    }
  }
  const Brep tool = webs.size() == 1 ? webs[0] : Brep::Compound(webs);

  // Cheap disjointness refusal with the measured gap, before running the engine.
  for (size_t i = 0; i < webs.size(); ++i) {
    const BoundingBox sb = solid.GetTightBoundingBox();
    const BoundingBox tb = webs[i].GetTightBoundingBox();
    const bool miss = tb.max.x < sb.min.x - tol || tb.min.x > sb.max.x + tol || tb.max.y < sb.min.y - tol ||
                      tb.min.y > sb.max.y + tol || tb.max.z < sb.min.z - tol || tb.min.z > sb.max.z + tol;
    if (miss) {
      Refuse("rib " + std::to_string(i) + "'s bounding box does not meet the solid's bounding box, so it cannot fuse onto it");
    }
  }

  double solid_volume = 0;
  try {
    // A sealed result of an earlier fusion is IsSolid() but its tessellation need not be a closed manifold, so prefer the
    // divergence-theorem integral for a B-rep that is topologically closed.
    solid_volume = solid.raw().IsSolid() ? solid.VolumeOfSealedSolid() : solid.Volume();
  } catch (const std::exception& e) {
    Refuse(std::string("could not measure the solid's volume: ") + e.what());
  }

  SealedGeneralBoolean sealed;
  try {
    sealed = BooleanCombineGeneralSealed(solid, tool, BooleanOp::Union);
  } catch (const std::exception& e) {
    Refuse(std::string("fusing the rib onto the solid was refused (not returning a degraded body): ") + e.what());
  }
  out.added_volume = sealed.volume - solid_volume;
  if (!(out.added_volume > 1e-6 * out.tool_volume)) {
    Refuse("the fused body adds no measurable volume (added " + std::to_string(out.added_volume) + " of a " +
           std::to_string(out.tool_volume) + " web): the rib lies inside the solid or only grazes it");
  }
  if (out.added_volume > out.tool_volume * 1.01) {
    Refuse("the fused volume grew by " + std::to_string(out.added_volume) + ", more than the web's whole volume " +
           std::to_string(out.tool_volume) + ", which is impossible");
  }
  out.brep = std::move(sealed.brep);
  return out;
}

RibResult MakeRib(const Brep& solid, const std::vector<Point3d>& profile, double thickness, bool symmetric) {
  return MakeRibs(solid, std::vector<std::vector<Point3d>>{profile}, thickness, symmetric);
}

}  // namespace dino8::kernel
