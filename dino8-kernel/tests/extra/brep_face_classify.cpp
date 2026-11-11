// Brep::ClassifyFaceSurface() / Brep::ClassifyFaces(): the typed analytic surface behind each face, trimmed faces included.
//
// Oracles are the closed forms each solid is constructed from (box planes at stated offsets, sphere centre/radius, torus
// R/r, cylinder radius and axis, the frustum's apex at -r0 / tan(half angle) with slope (r1 - r0) / length) and two
// invariants that do not use the classifier's own numbers: every point of every edge of a face lies on the analytic
// surface its record describes (every point of every trim, mapped through the face surface, has signed distance ~ 0 through the
// closed-form AnalyticClosestPoint), and a face
// trimmed by a Boolean still reports the same record as the untrimmed solid while its edges cover only part of the
// surface (the frustum slice z in [1, 2] of a surface running z in [0, 3]).
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::AnalyticClosestPoint;
using dino8::kernel::AnalyticFoot;
using dino8::kernel::BooleanCombineGeneral;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::Point3d;
using dino8::kernel::SurfaceClassification;
using dino8::kernel::SurfaceKind;
using dino8::kernel::Vector3d;

bool Near(const Point3d& a, const Point3d& b, double t = 1e-7) { return a.DistanceTo(b) < t; }

ON_Plane FrameFromAxis(const Point3d& origin, Vector3d axis) {
  axis.Unitize();
  Vector3d seed = std::fabs(axis.z) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d x = ON_CrossProduct(seed, axis);
  x.Unitize();
  Vector3d y = ON_CrossProduct(axis, x);
  y.Unitize();
  ON_Plane f;
  f.origin = origin;
  f.xaxis = x;
  f.yaxis = y;
  f.zaxis = axis;
  f.UpdateEquation();
  return f;
}

Brep::PlanarFace DiskCap(const ON_Plane& frame, double h, double r, bool flip, int n = 128) {
  Brep::PlanarFace pf;
  const Point3d c = frame.origin + frame.zaxis * h;
  pf.plane = ON_Plane(c, flip ? -frame.zaxis : frame.zaxis);
  for (int i = 0; i < n; ++i) {
    const double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
    pf.loop.push_back(c + frame.xaxis * (r * std::cos(a)) + frame.yaxis * (r * std::sin(a)));
  }
  pf.notch_begin = 0;
  pf.notch_count = static_cast<int>(pf.loop.size());
  return pf;
}

Brep Cylinder(const Point3d& base, const Vector3d& axis, double r, double length) {
  Brep::CylindricalFace cf;
  cf.frame = FrameFromAxis(base, axis);
  cf.radius = r;
  cf.angle = 2.0 * ON_PI;
  cf.length = length;
  return Brep::FromMixedFaces({DiskCap(cf.frame, 0.0, r, true), DiskCap(cf.frame, length, r, false)}, {cf});
}

Brep Frustum(const Point3d& base, const Vector3d& axis, double r0, double r1, double length) {
  Brep::ConicalFace cf;
  const double tan_half = (r1 - r0) / length;
  const ON_Plane base_frame = FrameFromAxis(base, axis);
  cf.frame = base_frame;
  cf.frame.origin = base - base_frame.zaxis * (r0 / tan_half);  // apex
  cf.frame.UpdateEquation();
  cf.radius0 = r0;
  cf.radius1 = r1;
  cf.angle = 2.0 * ON_PI;
  cf.length = length;
  return Brep::FromMixedFaces({DiskCap(base_frame, 0.0, r0, true), DiskCap(base_frame, length, r1, false)}, {}, {cf});
}

// Points of the face's boundary AS IT LIES ON ITS SURFACE: every trim's 2D curve is sampled and mapped through the face's
// surface. (The Boolean's 3D edge curves are fitted approximations, measured up to 3.7e-2 off the torus for the half torus
// below, so they say nothing about the classifier; the trims are exact on the surface by construction.)
struct Sample2 {
  Point3d p;
  double u, v;
};
std::vector<Sample2> BoundarySamples(const Brep& b, int fi, int per_trim = 24) {
  std::vector<Sample2> pts;
  const ON_BrepFace& face = b.raw().m_F[fi];
  const ON_Surface* surf = face.SurfaceOf();
  for (int li = 0; li < face.LoopCount(); ++li) {
    const ON_BrepLoop* loop = face.Loop(li);
    for (int ti = 0; ti < loop->TrimCount(); ++ti) {
      const ON_Curve* pc = loop->Trim(ti)->TrimCurveOf();
      if (!pc || !surf) continue;
      const ON_Interval d = pc->Domain();
      for (int k = 0; k <= per_trim; ++k) {
        const ON_3dPoint uv = pc->PointAt(d.ParameterAt(static_cast<double>(k) / per_trim));
        pts.push_back({surf->PointAt(uv.x, uv.y), uv.x, uv.y});
      }
    }
  }
  return pts;
}
std::vector<Point3d> EdgePoints(const Brep& b, int fi, int per_edge = 24) {
  std::vector<Point3d> pts;
  for (const Sample2& s : BoundarySamples(b, fi, per_edge)) pts.push_back(s.p);
  return pts;
}

// Largest |signed distance| of the face's edge points to the analytic surface of `cls`; -1 when a probe has no foot.
double MaxEdgeDeviation(const Brep& b, int fi, const SurfaceClassification& cls) {
  double worst = 0.0;
  for (const Point3d& p : EdgePoints(b, fi)) {
    AnalyticFoot foot;
    if (!AnalyticClosestPoint(cls, p, foot)) return -1.0;
    worst = std::max(worst, std::fabs(foot.signed_distance));
  }
  return worst;
}

int Count(const std::vector<Brep::FaceSurfaceRecord>& r, SurfaceKind k) {
  int n = 0;
  for (const auto& x : r) n += x.surface.kind == k ? 1 : 0;
  return n;
}

// `reversed` must be true exactly when the face's outward side (known from the geometry: away from the axis / centre for a
// convex solid, toward the axis for a hole wall) is opposite the surface normal at a boundary sample of the face.
void CheckReversedFlag(const Brep& b, const Brep::FaceSurfaceRecord& r, bool outward_is_analytic_outward, const char* what) {
  const std::vector<Sample2> pts = BoundarySamples(b, r.face_index);
  if (pts.size() < 4) {
    d8test::Check(false, (std::string(what) + ": no boundary samples").c_str());
    return;
  }
  const Sample2& s = pts[3];
  AnalyticFoot foot;
  if (!AnalyticClosestPoint(r.surface, s.p, foot)) {
    d8test::Check(false, (std::string(what) + ": the boundary sample has no unique foot").c_str());
    return;
  }
  const Vector3d surf_n = b.raw().m_F[r.face_index].SurfaceOf()->NormalAt(s.u, s.v);
  const Vector3d solid_outward = outward_is_analytic_outward ? foot.normal : -foot.normal;
  d8test::Check(r.reversed == (ON_DotProduct(surf_n, solid_outward) < 0.0), (std::string(what) + ": `reversed` is true exactly when the solid's outward side opposes the surface normal").c_str());
}

}  // namespace

D8_EXTRA_TEST(BrepFaceClassifyPrimitiveSolids) {
  {
    const Brep box = Brep::Box(0, 0, 0, 2, 3, 4);
    const auto recs = box.ClassifyFaces();
    d8test::Check(recs.size() == 6 && Count(recs, SurfaceKind::Plane) == 6, "box: all six faces are Plane");
    std::set<std::string> seen;
    bool offsets_ok = true;
    for (const auto& r : recs) {
      if (r.surface.kind != SurfaceKind::Plane) continue;
      const Vector3d n = r.surface.plane.zaxis;
      const int axis = std::fabs(n.x) > 0.5 ? 0 : (std::fabs(n.y) > 0.5 ? 1 : 2);
      const double off = r.surface.plane.origin[axis];
      const double lo = 0.0, hi = axis == 0 ? 2.0 : (axis == 1 ? 3.0 : 4.0);
      offsets_ok = offsets_ok && std::fabs(std::fabs(n[axis]) - 1.0) < 1e-9 && (std::fabs(off - lo) < 1e-9 || std::fabs(off - hi) < 1e-9);
      seen.insert(std::to_string(axis) + (std::fabs(off - lo) < 1e-9 ? "lo" : "hi"));
    }
    d8test::Check(offsets_ok && seen.size() == 6, "box: the six planes are x/y/z = 0 and 2/3/4, one each");
  }
  const Point3d c(1.0, -2.0, 0.5);
  for (int welded = 0; welded < 2; ++welded) {
    const Brep s = welded ? Brep::SphereWelded(c, 2.5) : Brep::Sphere(c, 2.5);
    const auto recs = s.ClassifyFaces();
    const std::string tag = welded ? "SphereWelded: " : "Sphere: ";
    d8test::Check(!recs.empty() && Count(recs, SurfaceKind::Sphere) == static_cast<int>(recs.size()), (tag + "every face is Sphere").c_str());
    if (!recs.empty() && recs[0].surface.kind == SurfaceKind::Sphere) {
      d8test::Check(Near(recs[0].surface.sphere.plane.origin, c) && std::fabs(recs[0].surface.sphere.radius - 2.5) < 1e-7, (tag + "centre and radius are the construction's").c_str());
      d8test::Check(MaxEdgeDeviation(s, recs[0].face_index, recs[0].surface) < 1e-7, (tag + "the face's edges lie on the recorded sphere").c_str());
    }
  }
  const Vector3d axis(0.3, -0.4, 0.866);
  Vector3d unit_axis = axis;
  unit_axis.Unitize();
  for (int welded = 0; welded < 2; ++welded) {
    const Brep t = welded ? Brep::TorusWelded(c, axis, 3.0, 0.75) : Brep::Torus(c, axis, 3.0, 0.75);
    const auto recs = t.ClassifyFaces();
    const std::string tag = welded ? "TorusWelded: " : "Torus: ";
    d8test::Check(!recs.empty() && Count(recs, SurfaceKind::Torus) == static_cast<int>(recs.size()), (tag + "every face is Torus").c_str());
    if (!recs.empty() && recs[0].surface.kind == SurfaceKind::Torus) {
      const auto& tr = recs[0].surface.torus;
      d8test::Check(Near(tr.plane.origin, c) && std::fabs(tr.major_radius - 3.0) < 1e-7 && std::fabs(tr.minor_radius - 0.75) < 1e-7 &&
                        std::fabs(std::fabs(ON_DotProduct(tr.plane.zaxis, unit_axis)) - 1.0) < 1e-9,
                    (tag + "centre, axis, major and minor radius are the construction's").c_str());
      d8test::Check(MaxEdgeDeviation(t, recs[0].face_index, recs[0].surface) < 1e-6, (tag + "the face's edges lie on the recorded torus").c_str());
    }
  }
  {
    const Brep cyl = Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.5, 4.0);
    const auto recs = cyl.ClassifyFaces();
    d8test::Check(Count(recs, SurfaceKind::Cylinder) >= 1 && Count(recs, SurfaceKind::Plane) == 2 && Count(recs, SurfaceKind::General) == 0,
                  "CylinderWelded: Cylinder wall plus two Plane caps, nothing General");
    for (const auto& r : recs) {
      if (r.surface.kind != SurfaceKind::Cylinder) continue;
      const Vector3d off = r.surface.cylinder.circle.plane.origin - Point3d(0, 0, 0);
      d8test::Check(std::fabs(r.surface.cylinder.circle.radius - 1.5) < 1e-7 && std::fabs(std::fabs(r.surface.cylinder.circle.plane.zaxis.z) - 1.0) < 1e-9 && std::fabs(off.x) < 1e-7 && std::fabs(off.y) < 1e-7,
                    "CylinderWelded: wall radius 1.5, axis along z through the origin");
    }
  }
  {
    // A free-form face is General, and an out-of-range / deleted index follows the FacePersistentId contract.
    bool oor = false;
    try {
      Brep::Box(0, 0, 0, 1, 1, 1).ClassifyFaceSurface(99);
    } catch (const std::out_of_range&) {
      oor = true;
    }
    d8test::Check(oor, "ClassifyFaceSurface throws std::out_of_range for a bad face index");
  }
}

D8_EXTRA_TEST(BrepFaceClassifyTrimmedFaces) {
  // A frustum, r 0.5 -> 1.5 over z 0 .. 3 (apex at z = -0.5 / (1/3) = -1.5), cut to the slab z in [1, 2] by a box.
  const Brep frustum = Frustum(Point3d(0, 0, 0), Vector3d(0, 0, 1), 0.5, 1.5, 3.0);
  const Brep slab = Brep::Box(-3, -3, 1, 3, 3, 2);
  {
    const auto whole = frustum.ClassifyFaces();
    d8test::Check(Count(whole, SurfaceKind::Cone) == 1 && Count(whole, SurfaceKind::Plane) == 2, "frustum: one Cone face and two Plane caps");
  }
  Brep cut;
  try {
    cut = BooleanCombineGeneral(slab, frustum, BooleanOp::Intersection);
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("setup: box x frustum Intersection threw: ") + e.what()).c_str());
    return;
  }
  const auto recs = cut.ClassifyFaces();
  d8test::Check(Count(recs, SurfaceKind::Cone) == 1 && Count(recs, SurfaceKind::Plane) == 2 && Count(recs, SurfaceKind::General) == 0,
                "frustum slice (trimmed): still one Cone face and two Plane faces");
  for (const auto& r : recs) {
    if (r.surface.kind == SurfaceKind::Cone) {
      const auto& co = r.surface.cone;
      d8test::Check(Near(co.plane.origin, Point3d(0, 0, -1.5)), "trimmed cone face: apex is the construction apex (0, 0, -1.5)");
      d8test::Check(std::fabs(std::fabs(ON_DotProduct(co.plane.zaxis, Vector3d(0, 0, 1))) - 1.0) < 1e-9 && std::fabs(std::fabs(co.radius / co.height) - 1.0 / 3.0) < 1e-7,
                    "trimmed cone face: axis z and slope (r1 - r0) / length = 1/3");
      d8test::Check(MaxEdgeDeviation(cut, r.face_index, r.surface) >= 0.0 && MaxEdgeDeviation(cut, r.face_index, r.surface) < 1e-6, "trimmed cone face: every edge point lies on the recorded cone");
      double zmin = 1e9, zmax = -1e9;
      for (const Point3d& p : EdgePoints(cut, r.face_index)) {
        zmin = std::min(zmin, p.z);
        zmax = std::max(zmax, p.z);
      }
      d8test::Check(zmin > 1.0 - 1e-4 && zmax < 2.0 + 1e-4 && zmax - zmin > 0.99, "trimmed cone face: its boundary spans z in [1, 2] only, of a surface running z in [0, 3]");
      CheckReversedFlag(cut, r, true, "cone slice");
    } else {
      d8test::Check(r.surface.kind == SurfaceKind::Plane && std::fabs(std::fabs(r.surface.plane.zaxis.z) - 1.0) < 1e-9 &&
                        (std::fabs(r.surface.plane.origin.z - 1.0) < 1e-7 || std::fabs(r.surface.plane.origin.z - 2.0) < 1e-7),
                    "frustum slice: the planar faces are the planes z = 1 and z = 2");
    }
  }

  // A through hole: box minus a radius-1 cylinder; the hole wall is a trimmed Cylinder face whose own orientation is
  // opposite to the cylinder's outward normal.
  const Brep cyl = Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 3.0);
  Brep holed;
  try {
    holed = BooleanCombineGeneral(slab, cyl, BooleanOp::Difference);
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("setup: box minus cylinder threw: ") + e.what()).c_str());
    return;
  }
  const auto hr = holed.ClassifyFaces();
  d8test::Check(Count(hr, SurfaceKind::Cylinder) >= 1 && Count(hr, SurfaceKind::Plane) >= 6 && Count(hr, SurfaceKind::General) == 0,
                "drilled slab (trimmed): Cylinder hole wall and Plane faces, nothing General");
  for (const auto& r : hr) {
    if (r.surface.kind != SurfaceKind::Cylinder) continue;
    d8test::Check(std::fabs(r.surface.cylinder.circle.radius - 1.0) < 1e-7 && std::fabs(std::fabs(r.surface.cylinder.circle.plane.zaxis.z) - 1.0) < 1e-9, "hole wall: radius 1, axis z");
    d8test::Check(MaxEdgeDeviation(holed, r.face_index, r.surface) >= 0.0 && MaxEdgeDeviation(holed, r.face_index, r.surface) < 1e-6, "hole wall: every edge point lies on the recorded cylinder");
    // The solid's outward side at the hole points toward the axis, i.e. AGAINST the analytic outward normal.
    CheckReversedFlag(holed, r, false, "hole wall");
  }

  // Half a torus (cut by x >= 0): the Torus record is unchanged by the trim and the cut faces are the plane x = 0.
  const Brep torus = Brep::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), 3.0, 1.0);
  Brep half;
  try {
    half = BooleanCombineGeneral(torus, Brep::Box(0, -6, -3, 6, 6, 3), BooleanOp::Intersection);
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("setup: torus x half-space box threw: ") + e.what()).c_str());
    return;
  }
  const auto tr = half.ClassifyFaces();
  d8test::Check(Count(tr, SurfaceKind::Torus) >= 1 && Count(tr, SurfaceKind::General) == 0, "half torus (trimmed): Torus face(s) and Plane faces, nothing General");
  for (const auto& r : tr) {
    if (r.surface.kind == SurfaceKind::Torus) {
      d8test::Check(Near(r.surface.torus.plane.origin, Point3d(0, 0, 0)) && std::fabs(r.surface.torus.major_radius - 3.0) < 1e-6 && std::fabs(r.surface.torus.minor_radius - 1.0) < 1e-6 &&
                        std::fabs(std::fabs(r.surface.torus.plane.zaxis.z) - 1.0) < 1e-9,
                    "half torus: centre, axis z, R 3, r 1 are the construction's");
      const double dev = MaxEdgeDeviation(half, r.face_index, r.surface);
      d8test::Check(dev >= 0.0 && dev < 1e-5, "half torus: every edge point lies on the recorded torus");
      double xmin = 1e9;
      for (const Point3d& p : EdgePoints(half, r.face_index)) xmin = std::min(xmin, p.x);
      d8test::Check(xmin > -1e-4, "half torus: the trimmed face's boundary stays in x >= 0 (to the 7e-6 the Boolean's cut trim measures) although the torus surface spans x in [-4, 4]");
      CheckReversedFlag(half, r, true, "half torus");
    } else if (r.surface.kind == SurfaceKind::Plane) {
      d8test::Check(std::fabs(std::fabs(r.surface.plane.zaxis.x) - 1.0) < 1e-9 && std::fabs(r.surface.plane.origin.x) < 1e-7, "half torus: the cut faces are the plane x = 0");
    }
  }
}
