#include "dino8/kernel/boss.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/surface_intersect.h"

namespace dino8::kernel {

namespace {

[[noreturn]] void Bad(const std::string& why) { throw std::invalid_argument("dino8::kernel::MakeBoss: " + why); }
[[noreturn]] void Refuse(const std::string& why) { throw std::runtime_error("dino8::kernel::MakeBoss: " + why); }

Point3d Add(const Point3d& p, const Vector3d& d) { return Point3d(p.x + d.x, p.y + d.y, p.z + d.z); }

Vector3d NewellNormal(const std::vector<Point3d>& ring, double* twice_area, double* extent) {
  const size_t n = ring.size();
  Vector3d nrm(0, 0, 0);
  double ext = 0;
  for (size_t i = 0; i < n; ++i) {
    const Point3d& a = ring[i];
    const Point3d& b = ring[(i + 1) % n];
    nrm.x += (a.y - b.y) * (a.z + b.z);
    nrm.y += (a.z - b.z) * (a.x + b.x);
    nrm.z += (a.x - b.x) * (a.y + b.y);
    ext = std::max(ext, std::hypot(std::hypot(a.x - ring[0].x, a.y - ring[0].y), a.z - ring[0].z));
  }
  *twice_area = nrm.Length();
  *extent = ext;
  if (*twice_area > 0.0) nrm = nrm * (1.0 / *twice_area);
  return nrm;
}

// Planar prism of an open vertex ring swept by `sweep`, from true planar faces (Brep::Extrude's FAN caps are avoided: a
// FAN-capped tool whose walls cross a face is measured to throw "edge claimed by 3 or more fragment loops").
Brep PlanarPrism(std::vector<Point3d> ring, Vector3d sweep) {
  const size_t n = ring.size();
  double ta = 0, ext = 0;
  Vector3d nrm = NewellNormal(ring, &ta, &ext);
  if (nrm.x * sweep.x + nrm.y * sweep.y + nrm.z * sweep.z < 0) {
    std::reverse(ring.begin(), ring.end());
    nrm = nrm * -1.0;
  }
  std::vector<Point3d> top;
  for (const Point3d& p : ring) top.push_back(Add(p, sweep));
  auto plane_at = [](const Point3d& o, const Vector3d& v) {
    return ON_Plane(ON_3dPoint(o.x, o.y, o.z), ON_3dVector(v.x, v.y, v.z));
  };
  std::vector<Brep::PlanarFace> faces;
  {
    Brep::PlanarFace f;
    f.plane = plane_at(ring[0], nrm * -1.0);
    for (size_t i = n; i-- > 0;) f.loop.push_back(ring[i]);
    faces.push_back(f);
  }
  {
    Brep::PlanarFace f;
    f.plane = plane_at(top[0], nrm);
    f.loop = top;
    faces.push_back(f);
  }
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    Brep::PlanarFace f;
    f.loop = {ring[i], ring[j], top[j], top[i]};
    ON_3dVector side = ON_CrossProduct(ON_3dVector(ring[j].x - ring[i].x, ring[j].y - ring[i].y, ring[j].z - ring[i].z),
                                       ON_3dVector(sweep.x, sweep.y, sweep.z));
    if (!side.Unitize()) Bad("polygon profile has a zero-length edge");
    f.plane = ON_Plane(ON_3dPoint(ring[i].x, ring[i].y, ring[i].z), side);
    faces.push_back(f);
  }
  return Brep::FromPlanarFaces(faces);
}

bool Apart(const BoundingBox& a, const BoundingBox& b, double tol) {
  return a.max.x < b.min.x - tol || a.min.x > b.max.x + tol || a.max.y < b.min.y - tol || a.min.y > b.max.y + tol ||
         a.max.z < b.min.z - tol || a.min.z > b.max.z + tol;
}

BossResult Fuse(const Brep& solid, const Brep& tool, const std::vector<Point3d>& base_samples) {
  BossResult out;
  try {
    out.tool_volume = tool.Volume();
  } catch (const std::exception& e) {
    Refuse(std::string("could not measure the boss volume: ") + e.what());
  }
  if (!(out.tool_volume > 0.0)) Refuse("the swept boss has non-positive volume (inverted tool)");

  const BoundingBox sb = solid.GetTightBoundingBox();
  const BoundingBox tb = tool.GetTightBoundingBox();
  if (Apart(sb, tb, 1e-9 * std::max(1.0, (sb.max - sb.min).Length()))) {
    Refuse("the boss's bounding box does not meet the solid's, so it cannot fuse onto it");
  }

  // The base must be buried: every sampled point of the base outline must be inside the solid.
  for (const Point3d& p : base_samples) {
    if (!ContainsPointExact(p, solid.raw())) {
      Refuse("the boss base is not embedded in the solid: the base point (" + std::to_string(p.x) + ", " +
             std::to_string(p.y) + ", " + std::to_string(p.z) +
             ") is not inside it; move the profile plane below the host face over the whole footprint");
    }
  }

  double solid_volume = 0;
  try {
    solid_volume = solid.raw().IsSolid() ? solid.VolumeOfSealedSolid() : solid.Volume();
  } catch (const std::exception& e) {
    Refuse(std::string("could not measure the solid's volume: ") + e.what());
  }
  SealedGeneralBoolean sealed;
  try {
    sealed = BooleanCombineGeneralSealed(solid, tool, BooleanOp::Union);
  } catch (const std::exception& e) {
    Refuse(std::string("fusing the boss onto the solid was refused (not returning a degraded body): ") + e.what());
  }
  out.added_volume = sealed.volume - solid_volume;
  if (!(out.added_volume > 1e-6 * out.tool_volume)) {
    Refuse("the fused body adds no measurable volume (added " + std::to_string(out.added_volume) + " of a " +
           std::to_string(out.tool_volume) + " boss): the boss lies inside the solid");
  }
  if (out.added_volume > out.tool_volume * 1.01) {
    Refuse("the fused volume grew by " + std::to_string(out.added_volume) + ", more than the boss's whole volume " +
           std::to_string(out.tool_volume) + ", which is impossible");
  }
  out.brep = std::move(sealed.brep);
  return out;
}

void CheckCommon(const Brep& solid, Vector3d& direction, double height) {
  if (solid.raw().m_F.Count() == 0) Bad("solid has no faces");
  if (!(height > 0.0) || !std::isfinite(height)) Bad("height must be a positive finite number");
  if (!direction.Unitize()) Bad("direction must be non-zero");
}

}  // namespace

BossResult MakeBoss(const Brep& solid, const std::vector<Point3d>& ring, Vector3d direction, double height) {
  CheckCommon(solid, direction, height);
  const size_t n = ring.size();
  if (n < 3) Bad("polygon profile needs at least 3 vertices (got " + std::to_string(n) + ")");
  double ta = 0, ext = 0;
  const Vector3d nrm = NewellNormal(ring, &ta, &ext);
  if (!(ext > 0) || !(ta > 1e-9 * ext * ext)) Bad("polygon profile has (near) zero area");
  for (size_t i = 0; i < n; ++i) {
    const double dist = (ring[i].x - ring[0].x) * nrm.x + (ring[i].y - ring[0].y) * nrm.y + (ring[i].z - ring[0].z) * nrm.z;
    if (std::fabs(dist) > 1e-6 * ext) Bad("polygon profile is not planar (vertex " + std::to_string(i) + ")");
  }
  if (std::fabs(nrm.x * direction.x + nrm.y * direction.y + nrm.z * direction.z) < 1e-3) {
    Bad("direction lies in the profile plane (|sin| < 1e-3)");
  }
  std::vector<Point3d> samples;
  for (size_t i = 0; i < n; ++i) {
    const Point3d& a = ring[i];
    const Point3d& b = ring[(i + 1) % n];
    samples.push_back(a);
    samples.push_back(Point3d(0.5 * (a.x + b.x), 0.5 * (a.y + b.y), 0.5 * (a.z + b.z)));
  }
  const Brep tool = PlanarPrism(ring, direction * height);
  if (!tool.raw().IsValid() || !tool.raw().IsSolid()) Refuse("internal: the swept polygon boss is not a valid closed solid");
  return Fuse(solid, tool, samples);
}

BossResult MakeBoss(const Brep& solid, const NurbsCurve& profile, Vector3d direction, double height) {
  CheckCommon(solid, direction, height);
  const ON_NurbsCurve& c = profile.raw();
  if (!c.IsClosed()) Bad("profile must be a closed curve");
  ON_Plane plane;
  const ON_BoundingBox cbb = c.BoundingBox();
  if (!c.IsPlanar(&plane, 1e-6 * std::max(1.0, cbb.Diagonal().Length()))) Bad("profile curve is not planar");
  const ON_3dVector pn = plane.zaxis;
  if (std::fabs(pn.x * direction.x + pn.y * direction.y + pn.z * direction.z) < 1e-3) {
    Bad("direction lies in the profile plane (|sin| < 1e-3)");
  }
  if (c.Degree() == 1) {
    std::vector<Point3d> ring;
    for (int i = 0; i < c.CVCount(); ++i) {
      ON_3dPoint p;
      c.GetCV(i, p);
      ring.push_back(Point3d(p.x, p.y, p.z));
    }
    if (ring.size() > 1 && std::hypot(std::hypot(ring.front().x - ring.back().x, ring.front().y - ring.back().y),
                                      ring.front().z - ring.back().z) < 1e-9) {
      ring.pop_back();
    }
    return MakeBoss(solid, ring, direction, height);
  }
  std::vector<Point3d> samples;
  const ON_Interval dom = c.Domain();
  for (int i = 0; i < 32; ++i) {
    const ON_3dPoint p = c.PointAt(dom.ParameterAt(i / 32.0));
    samples.push_back(Point3d(p.x, p.y, p.z));
  }
  // The extruded wall has a seam where the profile curve starts. Measured: when that seam line lands on the host's intersection
  // in an unlucky place (round boss on a sphere along (-1,-2,2)) the engine leaves a degenerate edge and the fusion is refused,
  // while the same boss with the profile's seam moved a quarter turn fuses (the wall seam always crosses the intersection
  // curve once and leaves a zero-length edge there; whether sealing copes depends on where). So a refused fusion is retried with the
  // seam moved to 7 other positions (1/4, 1/2, 3/4, 0.1, 0.9, 0.37, 0.63 of the domain); the FIRST refusal is reported if none works.
  std::string first_refusal;
  for (const double frac : {0.0, 0.25, 0.5, 0.75, 0.1, 0.9, 0.37, 0.63}) {
    ON_NurbsCurve moved = c;
    if (frac > 0.0 && !moved.ChangeClosedCurveSeam(dom.ParameterAt(frac))) continue;
    NurbsCurve shifted;
    shifted.raw() = moved;
    const Brep tool = Brep::Extrude(shifted, direction * height, true);
    if (!tool.raw().IsValid()) Refuse("internal: the extruded boss tool is not a valid ON_Brep");
    try {
      return Fuse(solid, tool, samples);
    } catch (const std::runtime_error& e) {
      if (std::string(e.what()).find("fusing the boss onto the solid was refused") == std::string::npos) throw;
      if (first_refusal.empty()) first_refusal = e.what();
    }
  }
  throw std::runtime_error(first_refusal + " [also refused with the profile seam moved to 7 other positions of the domain]");
}

}  // namespace dino8::kernel
