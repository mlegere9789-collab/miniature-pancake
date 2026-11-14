// OffsetRevolvedProfile() / OffsetRevolvedArcProfile(): exact whole-BODY offset of a solid of revolution (cylinder, cone, frustum,
// stepped profiles, sphere caps, half tori) - the case OffsetSolidConvexPlanar (planar faces only) and the mesh OffsetSolid cannot do
// as an exact B-rep.
//
// Oracles are independent of the construction (edge-line intersections + Green's integral):
//  - cylinder: pi (R+d)^2 (H+2d); frustum: pi h/3 (r0^2 + r0 r1 + r1^2) with radii from the explicit normal-offset line
//    r(z) = r_wall(z) + d L / H; cone: pi/3 r^2 h' with the moved wall meeting the axis at z_a = h + d L / R;
//  - dome / hemisphere: spherical-cap volume pi h^2 (3 rho - h) / 3 (radius rho + d, the flat base moved by d);
//  - half torus (ring section, no edge on the axis): Pappus, V = 2 pi R * area(circular segment of radius a + d);
//  - a grooved cylinder: cylinder minus the narrowed groove ring;
//  - round trips (+d then -d) and point membership of the tessellation against the offset body's own closed form.
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::MeridianEdge;
using dino8::kernel::Mesh;
using dino8::kernel::NurbsCurve;
using dino8::kernel::OffsetRevolvedArcProfile;
using dino8::kernel::OffsetRevolvedProfile;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;
using RZ = std::vector<std::pair<double, double>>;

NurbsCurve Section(const RZ& rz) {
  std::vector<Point3d> pts;
  for (const auto& q : rz) pts.emplace_back(q.first, 0.0, q.second);
  pts.push_back(pts.front());
  return NurbsCurve::FromControlPoints(pts, 1);
}

MeridianEdge L(double r, double z) {
  MeridianEdge e;
  e.r = r;
  e.z = z;
  return e;
}
MeridianEdge A(double r, double z, double cr, double cz, bool ccw) {
  MeridianEdge e;
  e.r = r;
  e.z = z;
  e.arc = true;
  e.cr = cr;
  e.cz = cz;
  e.counter_clockwise = ccw;
  return e;
}

bool Throws(const std::function<void()>& f, std::string* what = nullptr) {
  try {
    f();
  } catch (const std::exception& e) {
    if (what) *what = e.what();
    return true;
  }
  return false;
}

double CapVolume(double rho, double h) { return ON_PI * h * h * (3.0 * rho - h) / 3.0; }
double Frustum(double h, double r0, double r1) { return ON_PI * h / 3.0 * (r0 * r0 + r0 * r1 + r1 * r1); }

void CheckBody(const std::string& tag, const Brep& b, double expected, const std::function<int(double, double, double)>& inside, double box) {
  d8test::Check(b.raw().IsValid() && b.raw().IsSolid(), (tag + ": IsValid() and IsSolid()").c_str());
  const double vol = b.Volume();
  d8test::Check(std::fabs(vol - expected) <= 1e-5 * expected,
                (tag + ": exact Brep::Volume() " + std::to_string(vol) + " = independent closed form " + std::to_string(expected)).c_str());
  if (!inside) return;
  const Mesh mesh = b.TessellateToClosedMesh(64, 96);
  d8test::Check(mesh.Volume() < expected * 1.0005 && mesh.Volume() > 0.985 * expected, (tag + ": tessellated volume within 1.5% of the closed form").c_str());
  int judged = 0, bad = 0;
  const int n = 30;
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      for (int k = 0; k < n; ++k) {
        const double x = -box + 2 * box * (i + 0.37) / n, y = -box + 2 * box * (j + 0.41) / n, z = -box + 2 * box * (k + 0.29) / n;
        const int want = inside(x, y, z);
        if (want < 0) continue;
        ++judged;
        if (mesh.ContainsPoint(Point3d(x, y, z)) != (want == 1)) ++bad;
      }
    }
  }
  d8test::Check(judged > 3000 && bad == 0, (tag + ": point membership of the tessellation agrees with the body's closed form at " + std::to_string(judged) + " points, mismatches " + std::to_string(bad)).c_str());
}

}  // namespace

D8_EXTRA_TEST(OffsetRevolvedProfileCylinderAndRoundTrip) {
  const double R = 5, H = 10;
  const RZ can = {{0, 0}, {R, 0}, {R, H}, {0, H}};
  for (const double d : {1.0, -1.0, -2.0}) {
    const Brep b = OffsetRevolvedProfile(Section(can), Point3d(0, 0, 0), Vector3d(0, 0, 1), d);
    const double expected = ON_PI * (R + d) * (R + d) * (H + 2 * d);
    CheckBody("cylinder R5 H10 offset " + std::to_string(d), b, expected,
              [&](double x, double y, double z) {
                const double r = std::hypot(x, y);
                const double m = 0.12;
                if (std::fabs(r - (R + d)) < m || std::fabs(z + d) < m || std::fabs(z - (H + d)) < m) return -1;
                return (r < R + d && z > -d && z < H + d) ? 1 : 0;
              },
              9.0);
  }
  // round trips: +1 then -1 and -1 then +1 return the original volume (sharp-corner offsets of a convex body are inverse here)
  const Brep out = OffsetRevolvedArcProfile({L(0, -1), L(R + 1, -1), L(R + 1, H + 1), L(0, H + 1)}, Point3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), -1.0);
  d8test::Check(std::fabs(out.Volume() - ON_PI * R * R * H) <= 1e-5 * ON_PI * R * R * H, "offset +1 then -1 (second half of the round trip): volume back to pi R^2 H");
  const Brep in = OffsetRevolvedProfile(Section({{0, 1}, {R - 1, 1}, {R - 1, H - 1}, {0, H - 1}}), Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0);
  d8test::Check(std::fabs(in.Volume() - ON_PI * R * R * H) <= 1e-5 * ON_PI * R * R * H, "offset -1 then +1: volume back to pi R^2 H");
}

D8_EXTRA_TEST(OffsetRevolvedProfileFrustumAndCone) {
  {  // frustum r 6 -> 3 over 8: the moved wall is the line r(z) = r_wall(z) + d L / H
    const double R0 = 6, R1 = 3, H = 8, a = R1 - R0, Lw = std::hypot(a, H);
    const RZ f = {{0, 0}, {R0, 0}, {R1, H}, {0, H}};
    for (const double d : {1.0, -0.7}) {
      auto wall = [&](double z) { return R0 + a * z / H + d * Lw / H; };
      const double expected = Frustum(H + 2 * d, wall(-d), wall(H + d));
      const Brep b = OffsetRevolvedProfile(Section(f), Point3d(0, 0, 0), Vector3d(0, 0, 1), d);
      CheckBody("frustum r6->3 h8 offset " + std::to_string(d), b, expected,
                [&](double x, double y, double z) {
                  const double r = std::hypot(x, y);
                  if (std::fabs(z + d) < 0.12 || std::fabs(z - (H + d)) < 0.12 || std::fabs(r - wall(z)) < 0.14) return -1;
                  return (z > -d && z < H + d && r < wall(z)) ? 1 : 0;
                },
                9.0);
    }
  }
  {  // cone, apex up: the moved wall meets the axis at z_a = h + d L / R; the base moves to z = -d
    const double R = 5, h = 10, Lw = std::hypot(R, h), d = 0.8;
    const double za = h + d * Lw / R, rb = R * (1 + d / h) + d * Lw / h;
    const double expected = ON_PI / 3.0 * rb * rb * (za + d);
    const Brep b = OffsetRevolvedProfile(Section({{0, 0}, {R, 0}, {0, h}}), Point3d(0, 0, 0), Vector3d(0, 0, 1), d);
    CheckBody("cone r5 h10 offset 0.8 (sharp apex)", b, expected,
              [&](double x, double y, double z) {
                const double r = std::hypot(x, y);
                const double rz = rb * (za - z) / (za + d);
                if (std::fabs(z + d) < 0.12 || std::fabs(r - rz) < 0.14 || std::fabs(z - za) < 0.3) return -1;
                return (z > -d && z < za && r < rz) ? 1 : 0;
              },
              9.0);
  }
}

D8_EXTRA_TEST(OffsetRevolvedArcProfileDomeHemisphereAndHalfTorus) {
  {  // dome: sphere radius 5 centred z = -2, flat base at z = 0 (the app's Sphere ∩ Box cap), offset +1 and -0.5
    const double cz = -2, R = 5;
    // flat base (0,0)->(w,0), arc up to the apex (0, cz + R), axis down
    const double w = std::sqrt(R * R - cz * cz);
    const std::vector<MeridianEdge> e = {L(0, 0), A(w, 0, 0, cz, true), L(0, cz + R)};
    for (const double d : {1.0, -0.5}) {
      const double rho = R + d, base = -d;  // the base is a face with outward normal -z: it moves to z = -d
      const double expected = CapVolume(rho, cz + rho - base);
      const Brep b = OffsetRevolvedArcProfile(e, Point3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), d);
      CheckBody("dome R5 (centre z=-2) offset " + std::to_string(d), b, expected,
                [&](double x, double y, double z) {
                  const double q = std::sqrt(x * x + y * y + (z - cz) * (z - cz));
                  if (std::fabs(z - base) < 0.12 || std::fabs(q - rho) < 0.14) return -1;
                  return (z > base && q < rho) ? 1 : 0;
                },
                8.0);
    }
  }
  {  // hemisphere R = 5 centred on its base, offset +1: a sphere of radius 6 cut at z = -1
    const std::vector<MeridianEdge> e = {L(0, 0), A(5, 0, 0, 0, true), L(0, 5)};
    const Brep b = OffsetRevolvedArcProfile(e, Point3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), 1.0);
    CheckBody("hemisphere R5 offset +1", b, CapVolume(6.0, 6.0 + 1.0), nullptr, 0);
  }
  {  // half torus: ring section (no edge on the axis); the arc centre (4, 0) radius 2, chord on z = 0
    const double cr = 4, a = 2;
    const std::vector<MeridianEdge> e = {A(cr + a, 0, cr, 0, true), L(cr - a, 0)};
    for (const double d : {0.5, -0.5}) {
      const double rho = a + d, zc = d;  // centre sits d above the moved chord (z = -d); chord distance from centre = d
      // area of the part of the disc of radius rho above the chord at signed distance zc below the centre
      const double dist = std::fabs(zc);
      const double seg = rho * rho * std::acos(dist / rho) - dist * std::sqrt(rho * rho - dist * dist);  // smaller segment area
      const double area = zc > 0 ? ON_PI * rho * rho - seg : seg;  // chord below the centre keeps the large part; above, the small one
      const Brep b = OffsetRevolvedArcProfile(e, Point3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), d);
      CheckBody("half torus (major 4, minor 2) offset " + std::to_string(d), b, 2.0 * ON_PI * cr * area,
                [&](double x, double y, double z) {
                  const double rr = std::hypot(x, y);
                  const double q = std::sqrt((rr - cr) * (rr - cr) + z * z);
                  if (std::fabs(z + d) < 0.12 || std::fabs(q - rho) < 0.14) return -1;
                  return (z > -d && q < rho) ? 1 : 0;
                },
                7.0);
    }
  }
}

D8_EXTRA_TEST(OffsetRevolvedProfileSteppedGrooveAndRefusals) {
  const double R = 5;
  const RZ groove = {{0, 0}, {R, 0}, {R, 3}, {3, 3}, {3, 4}, {R, 4}, {R, 7}, {0, 7}};  // a 1-wide groove down to r = 3
  const Point3d o(0, 0, 0);
  const Vector3d z(0, 0, 1);
  {
    const double d = 0.4;  // groove walls z=3 (normal +z) and z=4 (normal -z) move to 3.4 and 3.6; the groove floor r=3 moves to 3.4
    const double expected = ON_PI * (R + d) * (R + d) * (7 + 2 * d) - ON_PI * ((R + d) * (R + d) - (3 + d) * (3 + d)) * 0.2;
    const Brep b = OffsetRevolvedProfile(Section(groove), o, z, d);
    CheckBody("grooved cylinder offset 0.4 (groove narrows to 0.2)", b, expected,
              [&](double x, double y, double zz) {
                const double r = std::hypot(x, y);
                if (std::fabs(r - (R + d)) < 0.14 || std::fabs(zz + d) < 0.12 || std::fabs(zz - (7 + d)) < 0.12 || std::fabs(zz - 3.4) < 0.12 ||
                    std::fabs(zz - 3.6) < 0.12 || std::fabs(r - (3 + d)) < 0.14) return -1;
                if (zz < -d || zz > 7 + d || r > R + d) return 0;
                if (zz > 3.4 && zz < 3.6 && r > 3 + d) return 0;
                return 1;
              },
              7.0);
  }
  std::string what;
  d8test::Check(Throws([&] { OffsetRevolvedProfile(Section(groove), o, z, 0.6); }, &what),
                "grooved cylinder offset 0.6: the groove (width 1) closes (0.6 + 0.6 > 1) - refused");
  const RZ can = {{0, 0}, {R, 0}, {R, 10}, {0, 10}};
  d8test::Check(Throws([&] { OffsetRevolvedProfile(Section(can), o, z, -5.0); }), "cylinder inward by its radius (r = 0): refused");
  d8test::Check(Throws([&] { OffsetRevolvedProfile(Section(can), o, z, -6.0); }), "cylinder inward past its radius: refused");
  d8test::Check(Throws([&] { OffsetRevolvedProfile(Section(can), o, z, -5.5); }), "cylinder inward 5.5 (radius and height both gone): refused");
  d8test::Check(Throws([&] { OffsetRevolvedProfile(Section(can), o, z, 0.0); }, &what) && what.find("non-zero") != std::string::npos, "distance 0 refused, naming it");
  d8test::Check(Throws([&] { OffsetRevolvedProfile(Section(can), o, z, std::nan("")); }), "NaN distance refused");
  d8test::Check(Throws([&] { OffsetRevolvedArcProfile({L(0, 0), A(5, 0, 0, 0, true), L(0, 5)}, o, z, Vector3d(1, 0, 0), -5.0); }, &what),
                "hemisphere inward by its radius: the arc collapses through its centre - refused");
  d8test::Check(!Throws([&] { OffsetRevolvedProfile(Section(can), o, z, -4.9); }), "baseline: cylinder inward 4.9 still builds (a thin pin)");
}
