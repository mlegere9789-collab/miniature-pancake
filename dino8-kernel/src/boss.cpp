#include "dino8/kernel/boss.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/surface_intersect.h"
#include "host_reseam.h"

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
    out.tool_volume = tool.raw().IsSolid() ? tool.VolumeOfSealedSolid() : tool.Volume();
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

  // A solid built from loose faces (Brep::FromMixedFaces) has no measurable volume of its own (see MakeRib): then the added volume
  // is the boss minus the exact volume of boss ^ solid.
  double solid_volume = 0;
  bool have_solid_volume = true;
  try {
    solid_volume = solid.raw().IsSolid() ? solid.VolumeOfSealedSolid() : solid.Volume();
  } catch (const std::exception&) {
    have_solid_volume = false;
  }
  // A refused fusion is retried on copies of a rotationally symmetric host (a single untrimmed sphere / torus) with its seam moved:
  // the same solid, parametrized differently (see host_reseam.h).
  SealedGeneralBoolean sealed;
  {
    std::string first_why;
    bool fused = false;
    const auto attempt = [&](const Brep& host) {
      try {
        sealed = BooleanCombineGeneralSealed(host, tool, BooleanOp::Union, detail::ScaledBooleanTolerance(host, tool));
        fused = true;
      } catch (const std::exception& e) {
        if (first_why.empty()) first_why = e.what();
      }
    };
    attempt(solid);
    size_t variants = 0;
    if (!fused) {
      const std::vector<Brep> alt = detail::ReseamedHostVariants(solid);
      variants = alt.size();
      for (const Brep& h : alt) {
        attempt(h);
        if (fused) break;
      }
    }
    if (!fused) {
      Refuse("fusing the boss onto the solid was refused (not returning a degraded body): " + first_why +
             (variants ? " [also refused with the host's seam moved to " + std::to_string(variants) + " other positions]" : std::string()));
    }
  }
  if (have_solid_volume) {
    out.added_volume = sealed.volume - solid_volume;
  } else {
    try {
      out.added_volume = out.tool_volume - BooleanCombineGeneralSealed(solid, tool, BooleanOp::Intersection, detail::ScaledBooleanTolerance(solid, tool)).volume;
    } catch (const std::exception& e) {
      Refuse(std::string("the solid has no measurable volume and boss ^ solid could not be measured either: ") + e.what());
    }
  }
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

PocketResult Cut(const Brep& solid, const Brep& tool, const std::vector<Point3d>& entry_samples, const std::vector<Point3d>& floor_samples) {
  PocketResult out;
  try {
    out.tool_volume = tool.raw().IsSolid() ? tool.VolumeOfSealedSolid() : tool.Volume();
  } catch (const std::exception& e) {
    Refuse(std::string("could not measure the pocket tool volume: ") + e.what());
  }
  if (!(out.tool_volume > 0.0)) Refuse("the swept pocket tool has non-positive volume (inverted tool)");
  const BoundingBox sb = solid.GetTightBoundingBox();
  const BoundingBox tb = tool.GetTightBoundingBox();
  if (Apart(sb, tb, 1e-9 * std::max(1.0, (sb.max - sb.min).Length()))) {
    Refuse("the pocket's bounding box does not meet the solid's, so it cannot cut into it");
  }
  // The entry plane must be clear of the material over the whole footprint, the floor buried in it.
  for (const Point3d& p : entry_samples) {
    if (ContainsPointExact(p, solid.raw())) {
      Refuse("the pocket entry is not outside the solid: the entry point (" + std::to_string(p.x) + ", " + std::to_string(p.y) + ", " +
             std::to_string(p.z) + ") is inside it; lift the profile plane above the host face over the whole footprint");
    }
  }
  for (const Point3d& p : floor_samples) {
    if (!ContainsPointExact(p, solid.raw())) {
      Refuse("the pocket floor is not inside the solid: the floor point (" + std::to_string(p.x) + ", " + std::to_string(p.y) + ", " +
             std::to_string(p.z) + ") is outside it; the depth does not reach the material over the whole footprint");
    }
  }
  double solid_volume = 0;
  bool have_solid_volume = true;
  try {
    solid_volume = solid.raw().IsSolid() ? solid.VolumeOfSealedSolid() : solid.Volume();
  } catch (const std::exception&) {
    have_solid_volume = false;
  }
  SealedGeneralBoolean sealed;
  {
    std::string first_why;
    bool done = false;
    const auto attempt = [&](const Brep& host) {
      try {
        sealed = BooleanCombineGeneralSealed(host, tool, BooleanOp::Difference, detail::ScaledBooleanTolerance(host, tool));
        done = true;
      } catch (const std::exception& e) {
        if (first_why.empty()) first_why = e.what();
      }
    };
    attempt(solid);
    size_t variants = 0;
    if (!done) {
      const std::vector<Brep> alt = detail::ReseamedHostVariants(solid);
      variants = alt.size();
      for (const Brep& h : alt) {
        attempt(h);
        if (done) break;
      }
    }
    if (!done) {
      Refuse("cutting the pocket was refused (not returning a degraded body): " + first_why +
             (variants ? " [also refused with the host's seam moved to " + std::to_string(variants) + " other positions]" : std::string()));
    }
  }
  if (have_solid_volume) {
    out.removed_volume = solid_volume - sealed.volume;
  } else {
    try {
      out.removed_volume = BooleanCombineGeneralSealed(solid, tool, BooleanOp::Intersection, detail::ScaledBooleanTolerance(solid, tool)).volume;
    } catch (const std::exception& e) {
      Refuse(std::string("the solid has no measurable volume and tool ^ solid could not be measured either: ") + e.what());
    }
  }
  if (!(out.removed_volume > 1e-6 * out.tool_volume)) {
    Refuse("the pocket removes no measurable volume (removed " + std::to_string(out.removed_volume) + " of a " + std::to_string(out.tool_volume) +
           " tool): the tool does not reach the material");
  }
  if (out.removed_volume > out.tool_volume * 1.01) {
    Refuse("the cut removed " + std::to_string(out.removed_volume) + ", more than the pocket tool's whole volume " + std::to_string(out.tool_volume) +
           ", which is impossible");
  }
  out.brep = std::move(sealed.brep);
  return out;
}

void CheckCommon(const Brep& solid, Vector3d& direction, double height) {
  if (solid.raw().m_F.Count() == 0) Bad("solid has no faces");
  if (!(height > 0.0) || !std::isfinite(height)) Bad("height (depth) must be a positive finite number");
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

PocketResult MakePocket(const Brep& solid, const std::vector<Point3d>& ring, Vector3d direction, double depth) {
  CheckCommon(solid, direction, depth);
  const size_t n = ring.size();
  if (n < 3) Bad("polygon profile needs at least 3 vertices (got " + std::to_string(n) + ")");
  double ta = 0, ext = 0;
  const Vector3d nrm = NewellNormal(ring, &ta, &ext);
  if (!(ext > 0) || !(ta > 1e-9 * ext * ext)) Bad("polygon profile has (near) zero area");
  for (size_t i = 0; i < n; ++i) {
    const double dist = (ring[i].x - ring[0].x) * nrm.x + (ring[i].y - ring[0].y) * nrm.y + (ring[i].z - ring[0].z) * nrm.z;
    if (std::fabs(dist) > 1e-6 * ext) Bad("polygon profile is not planar (vertex " + std::to_string(i) + ")");
  }
  if (std::fabs(nrm.x * direction.x + nrm.y * direction.y + nrm.z * direction.z) < 1e-3) Bad("direction lies in the profile plane (|sin| < 1e-3)");
  const Vector3d sweep = direction * depth;
  std::vector<Point3d> entry, floor_pts;
  for (size_t i = 0; i < n; ++i) {
    const Point3d& a = ring[i];
    const Point3d& b = ring[(i + 1) % n];
    const Point3d mid(0.5 * (a.x + b.x), 0.5 * (a.y + b.y), 0.5 * (a.z + b.z));
    entry.push_back(a);
    entry.push_back(mid);
    floor_pts.push_back(Add(a, sweep));
    floor_pts.push_back(Add(mid, sweep));
  }
  const Brep tool = PlanarPrism(ring, sweep);
  if (!tool.raw().IsValid() || !tool.raw().IsSolid()) Refuse("internal: the swept polygon pocket tool is not a valid closed solid");
  return Cut(solid, tool, entry, floor_pts);
}

PocketResult MakePocket(const Brep& solid, const NurbsCurve& profile, Vector3d direction, double depth) {
  CheckCommon(solid, direction, depth);
  const ON_NurbsCurve& c = profile.raw();
  if (!c.IsClosed()) Bad("profile must be a closed curve");
  ON_Plane plane;
  const ON_BoundingBox cbb = c.BoundingBox();
  if (!c.IsPlanar(&plane, 1e-6 * std::max(1.0, cbb.Diagonal().Length()))) Bad("profile curve is not planar");
  const ON_3dVector pn = plane.zaxis;
  if (std::fabs(pn.x * direction.x + pn.y * direction.y + pn.z * direction.z) < 1e-3) Bad("direction lies in the profile plane (|sin| < 1e-3)");
  if (c.Degree() == 1) {
    std::vector<Point3d> ring;
    for (int i = 0; i < c.CVCount(); ++i) {
      ON_3dPoint p;
      c.GetCV(i, p);
      ring.push_back(Point3d(p.x, p.y, p.z));
    }
    if (ring.size() > 1 && std::hypot(std::hypot(ring.front().x - ring.back().x, ring.front().y - ring.back().y), ring.front().z - ring.back().z) < 1e-9) {
      ring.pop_back();
    }
    return MakePocket(solid, ring, direction, depth);
  }
  const Vector3d sweep = direction * depth;
  std::vector<Point3d> entry, floor_pts;
  const ON_Interval dom = c.Domain();
  for (int i = 0; i < 32; ++i) {
    const ON_3dPoint p = c.PointAt(dom.ParameterAt(i / 32.0));
    entry.push_back(Point3d(p.x, p.y, p.z));
    floor_pts.push_back(Add(Point3d(p.x, p.y, p.z), sweep));
  }
  // Same seam retry as MakeBoss: a refused cut is retried with the profile's seam moved (the wall's seam crosses the intersection
  // curve once and leaves a zero-length edge there; whether sealing copes depends on where).
  std::string first_refusal;
  for (const double frac : {0.0, 0.25, 0.5, 0.75, 0.1, 0.9, 0.37, 0.63}) {
    ON_NurbsCurve moved = c;
    if (frac > 0.0 && !moved.ChangeClosedCurveSeam(dom.ParameterAt(frac))) continue;
    NurbsCurve shifted;
    shifted.raw() = moved;
    const Brep tool = Brep::Extrude(shifted, sweep, true);
    if (!tool.raw().IsValid()) Refuse("internal: the extruded pocket tool is not a valid ON_Brep");
    try {
      return Cut(solid, tool, entry, floor_pts);
    } catch (const std::runtime_error& e) {
      if (std::string(e.what()).find("cutting the pocket was refused") == std::string::npos) throw;
      if (first_refusal.empty()) first_refusal = e.what();
    }
  }
  throw std::runtime_error(first_refusal + " [also refused with the profile seam moved to 7 other positions of the domain]");
}

namespace {

// Dense samples of a closed curve (its CVs and edge midpoints when it is a polygon, 32 parameter samples otherwise), for the
// embedding checks of the base / entry and floor.
std::vector<Point3d> OutlineSamples(const ON_NurbsCurve& c) {
  std::vector<Point3d> out;
  if (c.Degree() == 1) {
    std::vector<Point3d> ring;
    for (int i = 0; i < c.CVCount(); ++i) {
      ON_3dPoint p;
      c.GetCV(i, p);
      ring.push_back(Point3d(p.x, p.y, p.z));
    }
    if (ring.size() > 1 && std::hypot(std::hypot(ring.front().x - ring.back().x, ring.front().y - ring.back().y), ring.front().z - ring.back().z) < 1e-9) {
      ring.pop_back();
    }
    for (size_t i = 0; i < ring.size(); ++i) {
      const Point3d& a = ring[i];
      const Point3d& b = ring[(i + 1) % ring.size()];
      out.push_back(a);
      out.push_back(Point3d(0.5 * (a.x + b.x), 0.5 * (a.y + b.y), 0.5 * (a.z + b.z)));
    }
    return out;
  }
  const ON_Interval dom = c.Domain();
  for (int i = 0; i < 32; ++i) {
    const ON_3dPoint p = c.PointAt(dom.ParameterAt(i / 32.0));
    out.push_back(Point3d(p.x, p.y, p.z));
  }
  return out;
}

// Even-odd point-in-polygon in the plane's own (x, y) coordinates.
bool InsidePolygon2d(const ON_Plane& plane, const std::vector<Point3d>& poly, const Point3d& q) {
  const auto uv = [&](const Point3d& p) {
    const ON_3dVector d(p.x - plane.origin.x, p.y - plane.origin.y, p.z - plane.origin.z);
    return std::make_pair(d * plane.xaxis, d * plane.yaxis);
  };
  const auto pq = uv(q);
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const auto a = uv(poly[i]);
    const auto b = uv(poly[j]);
    if ((a.second > pq.second) != (b.second > pq.second) && pq.first < (b.first - a.first) * (pq.second - a.second) / (b.second - a.second) + a.first) in = !in;
  }
  return in;
}

// The ring-shaped tool: the outer profile's prism with each hole's prism (run `margin` past both ends, so no cap is coincident)
// subtracted by the SEALED boolean, so the tool is a valid closed solid (ExtrudeProfileWithHoles' unsealed result was measured to be
// an invalid ON_Brep for circles and not watertight for rectangles). Polygons are swept as true planar faces, curves via Extrude.
Brep SweptCurve(const ON_NurbsCurve& c, Vector3d sweep) {
  if (c.Degree() == 1) {
    std::vector<Point3d> ring;
    for (int i = 0; i < c.CVCount(); ++i) {
      ON_3dPoint p;
      c.GetCV(i, p);
      ring.push_back(Point3d(p.x, p.y, p.z));
    }
    if (ring.size() > 1 && std::hypot(std::hypot(ring.front().x - ring.back().x, ring.front().y - ring.back().y), ring.front().z - ring.back().z) < 1e-9) {
      ring.pop_back();
    }
    return PlanarPrism(ring, sweep);
  }
  NurbsCurve k;
  k.raw() = c;
  return Brep::Extrude(k, sweep, true);
}

Brep RingTool(const ON_NurbsCurve& outer, const std::vector<NurbsCurve>& holes, Vector3d sweep, double margin) {
  Brep tool = SweptCurve(outer, sweep);
  if (!tool.raw().IsValid()) Refuse("internal: the outer prism of the ring tool is not a valid ON_Brep");
  Vector3d unit = sweep;
  const double len = sweep.Length();
  unit.Unitize();
  for (const NurbsCurve& h : holes) {
    ON_NurbsCurve moved = h.raw();
    moved.Translate(ON_3dVector(-unit.x * margin, -unit.y * margin, -unit.z * margin));
    const Brep hole_tool = SweptCurve(moved, unit * (len + 2.0 * margin));
    if (!hole_tool.raw().IsValid()) Refuse("internal: a hole prism of the ring tool is not a valid ON_Brep");
    try {
      tool = BooleanCombineGeneralSealed(tool, hole_tool, BooleanOp::Difference).brep;
    } catch (const std::exception& e) {
      Refuse(std::string("could not cut a hole out of the ring tool: ") + e.what());
    }
  }
  return tool;
}

}  // namespace

EmbossResult EmbossOnCurvedFace(const Brep& solid, const NurbsCurve& outer, const std::vector<NurbsCurve>& holes, Vector3d direction,
                                double depth, EmbossMode mode) {
  CheckCommon(solid, direction, depth);
  const bool deboss = (mode == EmbossMode::Deboss);
  EmbossResult out;
  if (holes.empty()) {
    if (deboss) {
      PocketResult r = MakePocket(solid, outer, direction, depth);
      out.brep = std::move(r.brep);
      out.tool_volume = r.tool_volume;
      out.volume_change = -r.removed_volume;
    } else {
      BossResult r = MakeBoss(solid, outer, direction * -1.0, depth);
      out.brep = std::move(r.brep);
      out.tool_volume = r.tool_volume;
      out.volume_change = r.added_volume;
    }
    return out;
  }

  const ON_NurbsCurve& c = outer.raw();
  if (!c.IsClosed()) Bad("profile must be a closed curve");
  ON_Plane plane;
  const ON_BoundingBox cbb = c.BoundingBox();
  const double scale = std::max(1.0, cbb.Diagonal().Length());
  if (!c.IsPlanar(&plane, 1e-6 * scale)) Bad("profile curve is not planar");
  if (std::fabs(plane.zaxis * ON_3dVector(direction.x, direction.y, direction.z)) < 1e-3) Bad("direction lies in the profile plane (|sin| < 1e-3)");
  const std::vector<Point3d> outer_pts = OutlineSamples(c);
  // Densify the outer outline for the containment test of the holes (a polygon's CVs alone would do; a curve needs more than 32).
  std::vector<Point3d> outer_poly;
  {
    const ON_Interval dom = c.Domain();
    if (c.Degree() == 1) {
      for (int i = 0; i + 1 < c.CVCount(); ++i) {
        ON_3dPoint p;
        c.GetCV(i, p);
        outer_poly.push_back(Point3d(p.x, p.y, p.z));
      }
    } else {
      for (int i = 0; i < 256; ++i) {
        const ON_3dPoint p = c.PointAt(dom.ParameterAt(i / 256.0));
        outer_poly.push_back(Point3d(p.x, p.y, p.z));
      }
    }
  }
  std::vector<Point3d> all_samples = outer_pts;
  std::vector<ON_BoundingBox> hole_boxes;
  for (size_t h = 0; h < holes.size(); ++h) {
    const ON_NurbsCurve& hc = holes[h].raw();
    const std::string tag = "hole " + std::to_string(h) + " ";
    if (!hc.IsClosed()) Bad(tag + "must be a closed curve");
    ON_Plane hp;
    if (!hc.IsPlanar(&hp, 1e-6 * scale)) Bad(tag + "is not planar");
    const std::vector<Point3d> hs = OutlineSamples(hc);
    for (const Point3d& p : hs) {
      const double dist = (p.x - plane.origin.x) * plane.zaxis.x + (p.y - plane.origin.y) * plane.zaxis.y + (p.z - plane.origin.z) * plane.zaxis.z;
      if (std::fabs(dist) > 1e-6 * scale) Bad(tag + "is not in the plane of the outer profile");
      if (!InsidePolygon2d(plane, outer_poly, p)) Bad(tag + "is not inside the outer profile");
      all_samples.push_back(p);
    }
    hole_boxes.push_back(hc.BoundingBox());
  }
  for (size_t i = 0; i < hole_boxes.size(); ++i) {
    for (size_t j = i + 1; j < hole_boxes.size(); ++j) {
      const ON_BoundingBox& a = hole_boxes[i];
      const ON_BoundingBox& b = hole_boxes[j];
      const bool apart = a.m_max.x < b.m_min.x || a.m_min.x > b.m_max.x || a.m_max.y < b.m_min.y || a.m_min.y > b.m_max.y || a.m_max.z < b.m_min.z || a.m_min.z > b.m_max.z;
      if (!apart) Bad("holes " + std::to_string(i) + " and " + std::to_string(j) + " overlap");
    }
  }

  const Vector3d sweep = (deboss ? direction : direction * -1.0) * depth;
  std::vector<Point3d> floor_pts;
  for (const Point3d& p : all_samples) floor_pts.push_back(Add(p, sweep));
  const ON_Interval dom = c.Domain();
  std::string first_refusal;
  for (const double frac : {0.0, 0.25, 0.5, 0.75, 0.1, 0.9, 0.37, 0.63}) {
    ON_NurbsCurve moved = c;
    if (frac > 0.0 && !moved.ChangeClosedCurveSeam(dom.ParameterAt(frac))) continue;
    const BoundingBox sbb = solid.GetTightBoundingBox();
    const Brep tool = RingTool(moved, holes, sweep, 1e-3 * std::max(1.0, (sbb.max - sbb.min).Length()));
    if (!tool.raw().IsValid()) Refuse("internal: the ring-shaped tool is not a valid ON_Brep");
    try {
      if (deboss) {
        PocketResult r = Cut(solid, tool, all_samples, floor_pts);
        out.brep = std::move(r.brep);
        out.tool_volume = r.tool_volume;
        out.volume_change = -r.removed_volume;
      } else {
        BossResult r = Fuse(solid, tool, all_samples);
        out.brep = std::move(r.brep);
        out.tool_volume = r.tool_volume;
        out.volume_change = r.added_volume;
      }
      return out;
    } catch (const std::runtime_error& e) {
      const std::string w = e.what();
      if (w.find("was refused") == std::string::npos) throw;
      if (first_refusal.empty()) first_refusal = w;
    }
  }
  throw std::runtime_error(first_refusal + " [also refused with the profile seam moved to 7 other positions of the domain]");
}

}  // namespace dino8::kernel
