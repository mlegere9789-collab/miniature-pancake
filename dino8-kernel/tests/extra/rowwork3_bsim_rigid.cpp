// Round 3 row work, Blender simulation rows: rigid body world (bpy.ops.rigidbody.*), cloth, fluid, hair.
// Oracles are closed forms (free fall, pendulum period series, linear spring oscillator, impulse algebra, polyline
// geometry, exact counts). Tolerances are derived from the integrator (velocity Verlet, second order) as stated at
// each check; none is tuned. Blender defaults that could not be fetched (docs.blender.org is outside the network
// policy) are marked "recollection" and are listed in each row's notes as needing an owner check.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/sim_hair.h"
#include "dino8/kernel/sim_rigid.h"
#include "dino8/kernel/sim_soft.h"
#include "test_registry.h"

using namespace dino8::sim;
using d8test::Check;

namespace {
const double kPi = 3.14159265358979323846;

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
bool NearV(const Vec3& a, const Vec3& b, double tol) { return (a - b).Length() <= tol; }

int MakeBody(RigidScene& s, const Vec3& loc, RbType t, RbShape sh, bool selected) {
  const int id = s.AddObject(loc);
  s.ObjectAdd(id, t);
  s.Find(id)->rb.shape = sh;
  s.Find(id)->selected = selected;
  return id;
}
}  // namespace

// ---------------------------------------------------------------------------------------- RIGIDBOD-0001
// Free fall z(t) = z0 - g t^2 / 2. Velocity Verlet integrates constant acceleration exactly, so the only error is
// rounding: 480 substeps of O(10) magnitudes, bounded far below 1e-9.
D8_EXTRA_TEST(RowRigidBodyBakeToKeyframesFollowsFreeFallClosedForm) {
  RigidScene s;
  s.WorldAdd();
  s.World().frame_start = 1;
  s.World().frame_end = 49;  // 48 frame intervals = 2 s at 24 fps
  const int id = MakeBody(s, Vec3(1, 2, 10), RbType::Active, RbShape::Sphere, true);
  s.Find(id)->rb.linear_damping = 0;
  auto keys = s.BakeToKeyframes(1);
  const auto& k = keys[id];
  Check(k.size() == 49, "bake to keyframes: one keyframe per frame, 49 for frames 1..49");
  bool ok = true;
  double worst = 0;
  for (const auto& kf : k) {
    const double t = (kf.frame - 1) / 24.0;
    const double ez = 10.0 - 0.5 * 9.81 * t * t;
    worst = std::max(worst, std::fabs(kf.loc.z - ez));
    worst = std::max(worst, std::fabs(kf.loc.x - 1) + std::fabs(kf.loc.y - 2));
    ok = ok && kf.frame >= 1 && kf.frame <= 49;
  }
  Check(ok, "bake to keyframes: frame numbers stay inside the world frame range");
  Check(worst <= 1e-9, "bake to keyframes: z equals z0 - g t^2/2 within 1e-9 (Verlet is exact for constant g)");

  RigidScene s2;
  s2.WorldAdd();
  s2.World().frame_end = 49;
  const int id2 = MakeBody(s2, Vec3(0, 0, 10), RbType::Active, RbShape::Sphere, true);
  s2.Find(id2)->rb.linear_damping = 0;
  auto k2 = s2.BakeToKeyframes(4)[id2];
  Check(k2.size() == 13 && k2[1].frame == 5 && k2.back().frame == 49,
        "bake to keyframes: step 4 over frames 1..49 gives floor(48/4)+1 = 13 keys at 1, 5, ..., 49");

  // A body that is not selected, or is Passive, gets no keyframes.
  RigidScene s3;
  s3.WorldAdd();
  const int pa = MakeBody(s3, Vec3(0, 0, 5), RbType::Passive, RbShape::Box, true);
  const int un = MakeBody(s3, Vec3(0, 0, 5), RbType::Active, RbShape::Sphere, false);
  auto k3 = s3.BakeToKeyframes(1);
  Check(k3.find(pa) == k3.end() && k3.find(un) == k3.end(),
        "bake to keyframes: passive and unselected bodies are not baked");

  // Damping-only motion (g = 0): v_k = v0 q^k, q = (1-d)^h; continuous x(t) = x0 + v0 (1 - (1-d)^t) / -ln(1-d).
  // The scheme is a left Riemann sum of a monotone decreasing speed, so |error| <= h * |v(0) - v(T)| (rigorous).
  RigidScene s4;
  s4.WorldAdd();
  s4.World().gravity = Vec3(0, 0, 0);
  s4.World().frame_end = 25;
  const int d = MakeBody(s4, Vec3(0, 0, 0), RbType::Active, RbShape::Sphere, true);
  s4.Find(d)->rb.vel = Vec3(3, 0, 0);  // linear_damping stays at its default 0.04
  const double dmp = s4.Find(d)->rb.linear_damping;
  auto k4 = s4.BakeToKeyframes(1)[d];
  const double T = 1.0, h = 1.0 / 240.0, v0 = 3.0;
  const double xt = v0 * (1.0 - std::pow(1.0 - dmp, T)) / (-std::log(1.0 - dmp));
  const double bound = h * (v0 - v0 * std::pow(1.0 - dmp, T));
  Check(Near(k4.back().loc.x, xt, bound) && bound < 2e-3,
        "bake to keyframes: default linear damping gives the exponential-decay distance within the Riemann bound");
}

// ---------------------------------------------------------------------------------------- RIGIDBOD-0002
D8_EXTRA_TEST(RowConnectRigidBodiesBuildsSelectedToActiveAndDistanceChains) {
  RigidScene s;
  const int a = MakeBody(s, Vec3(0, 0, 0), RbType::Active, RbShape::Box, true);
  const int b = MakeBody(s, Vec3(1, 0, 0), RbType::Active, RbShape::Box, true);
  const int c = MakeBody(s, Vec3(3, 0, 0), RbType::Active, RbShape::Box, true);
  const int e = MakeBody(s, Vec3(6, 0, 0), RbType::Active, RbShape::Box, true);
  const int unsel = MakeBody(s, Vec3(9, 0, 0), RbType::Active, RbShape::Box, false);
  const int nobody = s.AddObject(Vec3(12, 0, 0));
  s.Find(nobody)->selected = true;
  (void)unsel;
  s.SetActive(a);

  int n = s.Connect(ConnectPattern::SelectedToActive, PivotType::Center, ConstraintType::Fixed);
  Check(n == 3 && s.Constraints().size() == 3,
        "connect: selected to active makes one constraint per other selected body (3), none for unselected or bodyless objects");
  const double want_center[3] = {0.5, 1.5, 3.0};
  bool ok = true;
  const int other[3] = {b, c, e};
  for (int i = 0; i < 3; ++i) {
    const auto& k = s.Constraints()[static_cast<std::size_t>(i)];
    ok = ok && k.a == a && k.b == other[i] && Near(k.pivot.x, want_center[i], 1e-15) && k.type == ConstraintType::Fixed;
  }
  Check(ok, "connect: Center pivot is the midpoint of the two bodies (0.5, 1.5, 3.0), always between active and each other");

  RigidScene t;
  const int ta = MakeBody(t, Vec3(0, 0, 0), RbType::Active, RbShape::Box, true);
  const int tb = MakeBody(t, Vec3(10, 0, 0), RbType::Active, RbShape::Box, true);
  const int tc = MakeBody(t, Vec3(4, 0, 0), RbType::Active, RbShape::Box, true);
  t.SetActive(ta);
  Check(t.Connect(ConnectPattern::ChainDistance, PivotType::Selected, ConstraintType::Point) == 2,
        "connect: a chain over 3 bodies has 2 links");
  const auto& l0 = t.Constraints()[0];
  const auto& l1 = t.Constraints()[1];
  Check(l0.a == ta && l0.b == tc && l1.a == tc && l1.b == tb,
        "connect: chain from the active body goes to the nearest remaining body each time (0 -> 4 -> 10)");
  Check(Near(l0.pivot.x, 4, 1e-15) && Near(l1.pivot.x, 10, 1e-15),
        "connect: Selected pivot is the location of the second body of each link");
  RigidScene u;
  const int ua = MakeBody(u, Vec3(2, 0, 0), RbType::Active, RbShape::Box, true);
  MakeBody(u, Vec3(8, 0, 0), RbType::Active, RbShape::Box, true);
  u.SetActive(ua);
  u.Connect(ConnectPattern::SelectedToActive, PivotType::Active, ConstraintType::Hinge);
  Check(Near(u.Constraints()[0].pivot.x, 2, 1e-15) && u.Constraints()[0].type == ConstraintType::Hinge,
        "connect: Active pivot is the active body's location and the requested constraint type is used");
  RigidScene v;
  const int va = v.AddObject(Vec3());  // active object without a rigid body
  v.Find(va)->selected = true;
  v.SetActive(va);
  Check(v.Connect(ConnectPattern::SelectedToActive, PivotType::Center, ConstraintType::Fixed) == 0,
        "connect: an active object without a rigid body makes no constraints");
}

// ---------------------------------------------------------------------------------------- RIGIDBOD-0003 / 0004
// Pendulum: a Point constraint holds a body at fixed distance L from a world pivot. Exact period of the plane
// pendulum with amplitude theta0: T = 2 pi sqrt(L/g) (1 + theta0^2/16 + 11 theta0^4/3072 + ...), the next term is
// 173 theta0^6 / 737280. The integrator contributes the Verlet frequency error (omega h)^2/24 for the harmonic
// part; the projection step is allowed twice that.
namespace {
double MeasurePendulumPeriod(double L, double theta0, int substeps, int periods, double* rod_dev) {
  RigidScene s;
  s.WorldAdd();
  s.World().substeps_per_frame = substeps;
  const int bob = MakeBody(s, Vec3(L * std::sin(theta0), 0, -L * std::cos(theta0)), RbType::Active, RbShape::Sphere, true);
  s.Find(bob)->rb.linear_damping = 0;
  s.ConstraintAdd(ConstraintType::Point, -1, bob, Vec3(0, 0, 0));
  const double h = 1.0 / (24.0 * substeps);
  double prev_x = s.Find(bob)->loc.x, t = 0;
  std::vector<double> up;  // times of upward zero crossings of x
  *rod_dev = 0;
  for (long i = 0; i < 400000 && static_cast<int>(up.size()) < periods + 1; ++i) {
    s.StepSubstep(h);
    t += h;
    const double x = s.Find(bob)->loc.x;
    if (prev_x < 0 && x >= 0) up.push_back(t - h + h * (-prev_x) / (x - prev_x));
    prev_x = x;
    *rod_dev = std::max(*rod_dev, std::fabs(s.Find(bob)->loc.Length() - L));
  }
  if (static_cast<int>(up.size()) < periods + 1) return -1;
  return (up.back() - up.front()) / periods;
}
}  // namespace

D8_EXTRA_TEST(RowRigidBodyConstraintAddPointConstraintGivesPendulumPeriod) {
  const double L = 1.0, g = 9.81, theta0 = 0.1;
  const int substeps = 100;
  double dev = 0;
  const double Tm = MeasurePendulumPeriod(L, theta0, substeps, 5, &dev);
  const double T0 = 2 * kPi * std::sqrt(L / g);
  const double Texact = T0 * (1 + theta0 * theta0 / 16 + 11 * std::pow(theta0, 4) / 3072);
  const double h = 1.0 / (24.0 * substeps), w = std::sqrt(g / L);
  const double rel_tol = 2 * (w * h) * (w * h) / 24 + 173 * std::pow(theta0, 6) / 737280 +
                         2 * h * (w * h) * (w * h) / (5 * T0);  // Verlet + projection, series tail, crossing interpolation
  std::printf("pendulum: measured %.9f exact %.9f rel err %.3e tol %.3e\n", Tm, Texact, (Tm - Texact) / Texact, rel_tol);
  Check(Tm > 0, "constraint add: the pendulum crosses zero 6 times");
  Check(std::fabs(Tm - Texact) / Texact <= rel_tol,
        "constraint add: Point constraint pendulum period matches 2 pi sqrt(L/g)(1 + th^2/16 + 11 th^4/3072) within the Verlet bound");
  Check(dev <= 1e-12, "constraint add: the rod length is held to 1e-12");
  Check(Tm > T0, "constraint add: finite amplitude lengthens the period (T > T0)");

  RigidScene s;
  s.WorldAdd();
  const int bob = MakeBody(s, Vec3(0, 0, -1), RbType::Active, RbShape::Sphere, true);
  const int c1 = s.ConstraintAdd(ConstraintType::Point, -1, bob, Vec3(0, 0, 0));
  const int c2 = s.ConstraintAdd(ConstraintType::Hinge, -1, bob, Vec3(1, 0, 0));
  Check(c1 != c2 && s.Constraints().size() == 2 && s.Constraints()[1].type == ConstraintType::Hinge &&
            s.Constraints()[0].enabled,
        "constraint add: each constraint gets its own id, keeps its type and starts enabled");
}

D8_EXTRA_TEST(RowRigidBodyConstraintRemoveFreesTheBody) {
  RigidScene s;
  s.WorldAdd();
  const int bob = MakeBody(s, Vec3(0.6, 0, -0.8), RbType::Active, RbShape::Sphere, true);
  s.Find(bob)->rb.linear_damping = 0;
  const int keep = s.ConstraintAdd(ConstraintType::Fixed, bob, -1, Vec3());
  const int pend = s.ConstraintAdd(ConstraintType::Point, -1, bob, Vec3(0, 0, 0));
  Check(s.ConstraintRemove(pend) && s.Constraints().size() == 1 && s.Constraints()[0].id == keep,
        "constraint remove: the named constraint is gone, the other one is untouched");
  Check(!s.ConstraintRemove(pend) && !s.ConstraintRemove(9999), "constraint remove: a missing id changes nothing");
  // Without the pendulum constraint the body falls freely: z = z0 - g t^2 / 2 for 1 s (exact for Verlet).
  for (int i = 0; i < 24; ++i) s.StepFrame();
  Check(Near(s.Find(bob)->loc.z, -0.8 - 0.5 * 9.81, 1e-9) && Near(s.Find(bob)->loc.x, 0.6, 1e-12),
        "constraint remove: the released body free-falls (x unchanged, z = -0.8 - g/2 after 1 s)");
}

// ---------------------------------------------------------------------------------------- RIGIDBOD-0005
D8_EXTRA_TEST(RowCalculateMassIsDensityTimesClosedFormVolume) {
  RigidScene s;
  const int box = MakeBody(s, Vec3(), RbType::Active, RbShape::Box, true);
  s.Find(box)->rb.dims.size = Vec3(2, 3, 4);
  const int sph = MakeBody(s, Vec3(), RbType::Active, RbShape::Sphere, true);
  s.Find(sph)->rb.dims.radius = 0.5;
  const int cyl = MakeBody(s, Vec3(), RbType::Active, RbShape::Cylinder, true);
  s.Find(cyl)->rb.dims.radius = 1.0;
  s.Find(cyl)->rb.dims.height = 2.0;
  const int con = MakeBody(s, Vec3(), RbType::Active, RbShape::Cone, true);
  s.Find(con)->rb.dims.radius = 3.0;
  s.Find(con)->rb.dims.height = 2.0;
  const int cap = MakeBody(s, Vec3(), RbType::Active, RbShape::Capsule, true);
  s.Find(cap)->rb.dims.radius = 1.0;
  s.Find(cap)->rb.dims.height = 3.0;
  const int off = MakeBody(s, Vec3(), RbType::Active, RbShape::Box, false);  // not selected
  const int nob = s.AddObject(Vec3());
  s.Find(nob)->selected = true;  // selected without a body

  const int n = s.MassCalculate(1000.0);  // kg/m^3 (water)
  Check(n == 5, "mass calculate: only selected objects with a body are changed (5 of 7)");
  Check(Near(s.Find(box)->rb.mass, 24000.0, 1e-9), "mass calculate: box 2x3x4 at 1000 kg/m^3 = 24000 kg");
  Check(Near(s.Find(sph)->rb.mass, 1000.0 * 4.0 / 3.0 * kPi * 0.125, 1e-9),
        "mass calculate: sphere r=0.5 = 1000 * 4/3 pi r^3 = 523.5988 kg");
  Check(Near(s.Find(cyl)->rb.mass, 1000.0 * kPi * 2.0, 1e-9), "mass calculate: cylinder r=1 h=2 = 2000 pi kg");
  Check(Near(s.Find(con)->rb.mass, 1000.0 * kPi * 9.0 * 2.0 / 3.0, 1e-9), "mass calculate: cone r=3 h=2 = 6000 pi kg");
  Check(Near(s.Find(cap)->rb.mass, 1000.0 * (kPi * 3.0 + 4.0 / 3.0 * kPi), 1e-9),
        "mass calculate: capsule r=1, cylinder part 3 = 1000 (3 pi + 4 pi/3) kg");
  Check(s.Find(off)->rb.mass == 1.0, "mass calculate: an unselected body keeps its mass");

  // Convex hull / mesh volume from the triangles: cube of side 2 centred on the origin, outward windings = 8.
  const std::vector<Vec3> v = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                               {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
  const std::vector<std::size_t> tri = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                        2, 3, 7, 2, 7, 6, 1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3};
  Check(Near(MeshVolume(v, tri), 8.0, 1e-12), "mesh volume: a closed outward cube mesh of side 2 has volume 8");
  // Unit corner tetrahedron: V = 1/6 independent of where the origin is (translation invariance of the closed mesh).
  std::vector<Vec3> tv = {{5, 5, 5}, {6, 5, 5}, {5, 6, 5}, {5, 5, 6}};
  const std::vector<std::size_t> tt = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
  Check(Near(MeshVolume(tv, tt), 1.0 / 6.0, 1e-12), "mesh volume: a unit corner tetrahedron has volume 1/6 away from the origin");
  const int hull = MakeBody(s, Vec3(), RbType::Active, RbShape::ConvexHull, true);
  s.Find(hull)->rb.dims.mesh_volume = MeshVolume(v, tri);
  s.MassCalculate(500.0);
  Check(Near(s.Find(hull)->rb.mass, 4000.0, 1e-9), "mass calculate: a convex hull body uses the mesh volume (8 * 500 = 4000 kg)");
}

// ---------------------------------------------------------------------------------------- RIGIDBOD-0006 / 0007 / 0008 / 0011 / 0012
D8_EXTRA_TEST(RowRigidBodyObjectAddGivesDefaultsAndRefusesDuplicates) {
  RigidScene s;
  const int a = s.AddObject(Vec3(1, 2, 3));
  Check(!s.HasWorld(), "object add: a scene starts without a rigid body world");
  Check(s.ObjectAdd(a, RbType::Active), "object add: Active body added to a plain object");
  const SimObject* o = s.Find(a);
  Check(o->has_body && o->rb.type == RbType::Active && o->rb.mass == 1.0 && o->rb.friction == 0.5 &&
            o->rb.restitution == 0.0 && o->rb.shape == RbShape::ConvexHull,
        "object add: defaults mass 1, friction 0.5, bounciness 0, Convex Hull (recollection of Blender 5.3 defaults)");
  Check(s.HasWorld(), "object add: a world is created when none exists");
  Check(NearV(o->loc, Vec3(1, 2, 3), 0), "object add: the object does not move");
  Check(!s.ObjectAdd(a, RbType::Passive) && s.Find(a)->rb.type == RbType::Active,
        "object add: a second add on the same object is refused and changes nothing");
  Check(!s.ObjectAdd(777, RbType::Active), "object add: an unknown object is refused");
  const int p = s.AddObject(Vec3());
  s.ObjectAdd(p, RbType::Passive);
  Check(s.Find(p)->rb.type == RbType::Passive, "object add: Passive type is honoured");

  // Add Rigid Bodies (plural) acts on the selection only.
  RigidScene m;
  const int m1 = m.AddObject(Vec3()), m2 = m.AddObject(Vec3()), m3 = m.AddObject(Vec3());
  m.Find(m1)->selected = m.Find(m2)->selected = true;
  Check(m.ObjectsAdd(RbType::Active) == 2 && m.Find(m1)->has_body && m.Find(m2)->has_body && !m.Find(m3)->has_body,
        "objects add: exactly the 2 selected objects get a body");
  Check(m.ObjectsAdd(RbType::Active) == 0, "objects add: repeating it changes nothing");
  Check(m.ObjectsRemove() == 2 && !m.Find(m1)->has_body && !m.Find(m2)->has_body,
        "objects remove: the selected objects lose their body");
}

D8_EXTRA_TEST(RowRigidBodyObjectRemoveLeavesOthersAndWorld) {
  RigidScene s;
  const int a = MakeBody(s, Vec3(1, 0, 0), RbType::Active, RbShape::Sphere, false);
  const int b = MakeBody(s, Vec3(2, 0, 0), RbType::Passive, RbShape::Box, false);
  s.Find(b)->rb.mass = 7;
  Check(s.ObjectRemove(a) && !s.Find(a)->has_body, "object remove: the body is gone");
  Check(s.Find(b)->has_body && s.Find(b)->rb.mass == 7 && s.Find(b)->rb.type == RbType::Passive,
        "object remove: other bodies are untouched");
  Check(NearV(s.Find(a)->loc, Vec3(1, 0, 0), 0) && s.HasWorld(), "object remove: the object stays where it is and the world stays");
  Check(!s.ObjectRemove(a) && !s.ObjectRemove(555), "object remove: removing again, or a missing object, changes nothing");
  // A removed body no longer moves in the simulation.
  s.StepFrame();
  Check(NearV(s.Find(a)->loc, Vec3(1, 0, 0), 0), "object remove: the object without a body does not fall");
}

D8_EXTRA_TEST(RowRigidBodySettingsCopyCopiesSettingsNotMotion) {
  RigidScene s;
  const int src = MakeBody(s, Vec3(0, 0, 0), RbType::Passive, RbShape::Cylinder, true);
  RigidBody& r = s.Find(src)->rb;
  r.mass = 12.5;
  r.friction = 0.9;
  r.restitution = 0.3;
  r.linear_damping = 0.2;
  r.dims.radius = 0.7;
  r.vel = Vec3(9, 9, 9);
  const int t1 = MakeBody(s, Vec3(5, 0, 0), RbType::Active, RbShape::Box, true);
  s.Find(t1)->rb.vel = Vec3(1, 2, 3);
  const int t2 = MakeBody(s, Vec3(6, 0, 0), RbType::Active, RbShape::Box, false);  // not selected
  const int t3 = s.AddObject(Vec3(7, 0, 0));
  s.Find(t3)->selected = true;  // no body
  s.SetActive(src);
  const RigidBody before = s.Find(src)->rb;
  Check(s.SettingsCopy() == 1, "settings copy: only the selected object that has a body is written");
  const RigidBody& d = s.Find(t1)->rb;
  Check(d.type == RbType::Passive && d.mass == 12.5 && d.friction == 0.9 && d.restitution == 0.3 &&
            d.linear_damping == 0.2 && d.shape == RbShape::Cylinder && d.dims.radius == 0.7,
        "settings copy: type, mass, friction, bounciness, damping and shape arrive equal to the source");
  Check(NearV(d.vel, Vec3(1, 2, 3), 0) && NearV(s.Find(t1)->loc, Vec3(5, 0, 0), 0),
        "settings copy: the target keeps its own velocity and location");
  Check(s.Find(src)->rb.mass == before.mass && s.Find(src)->rb.shape == before.shape && NearV(s.Find(src)->rb.vel, Vec3(9, 9, 9), 0),
        "settings copy: the source is unchanged");
  Check(s.Find(t2)->rb.mass == 1.0 && s.Find(t2)->rb.type == RbType::Active && !s.Find(t3)->has_body,
        "settings copy: unselected and bodyless objects are untouched");
}

D8_EXTRA_TEST(RowChangeCollisionShapeAppliesToSelectedBodiesOnly) {
  RigidScene s;
  const int a = MakeBody(s, Vec3(), RbType::Active, RbShape::ConvexHull, true);
  const int b = MakeBody(s, Vec3(), RbType::Passive, RbShape::Mesh, true);
  const int c = MakeBody(s, Vec3(), RbType::Active, RbShape::Box, false);
  const int d = s.AddObject(Vec3());
  s.Find(d)->selected = true;
  Check(s.ShapeChange(RbShape::Capsule) == 2 && s.Find(a)->rb.shape == RbShape::Capsule && s.Find(b)->rb.shape == RbShape::Capsule,
        "shape change: both selected bodies take the new shape");
  Check(s.Find(c)->rb.shape == RbShape::Box && !s.Find(d)->has_body, "shape change: unselected and bodyless objects are untouched");
  Check(s.Find(a)->rb.mass == 1.0 && s.Find(b)->rb.type == RbType::Passive,
        "shape change: nothing but the shape changes (mass and type kept)");
  s.ShapeChange(RbShape::Sphere);
  Check(s.Find(a)->rb.shape == RbShape::Sphere, "shape change: a second change replaces the first");
}

D8_EXTRA_TEST(RowRigidBodyWorldAddAndRemoveFollowDefaultsAndExistence) {
  RigidScene s;
  Check(!s.HasWorld() && !s.WorldRemove(), "world remove: nothing to remove in a new scene");
  Check(s.WorldAdd() && s.HasWorld(), "world add: the first add creates the world");
  const WorldSettings& w = s.World();
  Check(w.gravity.x == 0 && w.gravity.y == 0 && w.gravity.z == -9.81 && w.substeps_per_frame == 10 &&
            w.solver_iterations == 10 && w.time_scale == 1.0,
        "world add: defaults gravity (0,0,-9.81), 10 substeps, 10 solver iterations, speed 1 (recollection)");
  s.World().gravity = Vec3(0, 0, -1);
  Check(!s.WorldAdd() && s.World().gravity.z == -1, "world add: a second add is refused and keeps the settings");
  const int a = MakeBody(s, Vec3(), RbType::Active, RbShape::Box, false);
  Check(s.WorldRemove() && !s.HasWorld() && s.Find(a)->has_body, "world remove: the world is gone, the object's body settings stay");
  s.StepFrame();
  Check(NearV(s.Find(a)->loc, Vec3(), 0), "world remove: without a world nothing falls");
  Check(s.WorldAdd() && s.World().gravity.z == -9.81, "world add: after removal a fresh world has the defaults again");
}

// Not tied to one row: impulse algebra for rigid body collisions (used by the rows above). Momentum is conserved to
// rounding; kinetic energy changes by exactly -(1 - e^2) mu vn^2 / 2.
D8_EXTRA_TEST(RowRigidBodyImpulseConservesMomentumAndFollowsRestitutionEnergyLaw) {
  const double m1 = 2.0, m2 = 3.0, v1 = 4.0, v2 = -1.0;
  for (double e : {0.0, 0.5, 1.0}) {
    RigidBody a, b;
    a.mass = m1; b.mass = m2;
    a.vel = Vec3(v1, 0, 0);
    b.vel = Vec3(v2, 0, 0);
    const double p0 = m1 * v1 + m2 * v2;
    const double k0 = 0.5 * m1 * v1 * v1 + 0.5 * m2 * v2 * v2;
    RigidScene::Impulse(a, b, Vec3(1, 0, 0), e);
    const double p1 = m1 * a.vel.x + m2 * b.vel.x;
    const double k1 = 0.5 * m1 * a.vel.x * a.vel.x + 0.5 * m2 * b.vel.x * b.vel.x;
    const double mu = m1 * m2 / (m1 + m2), vn = v2 - v1;
    const double dk = -(1 - e * e) * 0.5 * mu * vn * vn;
    Check(Near(p1, p0, 1e-12), "impulse: total momentum is conserved");
    Check(Near(k1 - k0, dk, 1e-12), "impulse: kinetic energy change = -(1-e^2) mu vn^2 / 2");
    Check(Near(b.vel.x - a.vel.x, -e * vn, 1e-12), "impulse: separation speed = e times approach speed");
  }
  // Stepped two-sphere collision in zero gravity: same laws through StepSubstep.
  RigidScene s;
  s.WorldAdd();
  s.World().gravity = Vec3(0, 0, 0);
  const int a = MakeBody(s, Vec3(0, 0, 0), RbType::Active, RbShape::Sphere, false);
  const int b = MakeBody(s, Vec3(3, 0, 0), RbType::Active, RbShape::Sphere, false);
  for (int id : {a, b}) { s.Find(id)->rb.linear_damping = 0; s.Find(id)->rb.dims.radius = 0.5; }
  s.Find(a)->rb.vel = Vec3(1, 0, 0);
  s.Find(b)->rb.vel = Vec3(-1, 0, 0);
  s.Find(a)->rb.restitution = s.Find(b)->rb.restitution = 1.0;
  for (int i = 0; i < 480; ++i) s.StepSubstep(1.0 / 240.0);
  Check(Near(s.Find(a)->rb.vel.x, -1, 1e-12) && Near(s.Find(b)->rb.vel.x, 1, 1e-12),
        "stepped collision: equal masses, e = 1 exchange velocities exactly (momentum and energy conserved)");
  // Sphere dropped on a passive box: rebound speed e*v_impact, with v_impact within g*h of sqrt(2 g H) (the contact is
  // found at the end of a step, so the impact speed is off by at most one step of acceleration).
  RigidScene f;
  f.WorldAdd();
  const int ground = MakeBody(f, Vec3(0, 0, -0.5), RbType::Passive, RbShape::Box, false);
  f.Find(ground)->rb.dims.size = Vec3(10, 10, 1);
  const int ball = MakeBody(f, Vec3(0, 0, 1.5), RbType::Active, RbShape::Sphere, false);
  f.Find(ball)->rb.linear_damping = 0;
  f.Find(ball)->rb.dims.radius = 0.5;
  f.Find(ball)->rb.restitution = 0.5;
  double vmax_down = 0, rebound = 0;
  const double h = 1.0 / 2400.0;
  for (int i = 0; i < 4000 && rebound == 0; ++i) {
    f.StepSubstep(h);
    vmax_down = std::max(vmax_down, -f.Find(ball)->rb.vel.z);
    if (f.Find(ball)->rb.vel.z > 0) rebound = f.Find(ball)->rb.vel.z;
  }
  const double vimp = std::sqrt(2 * 9.81 * 1.0);  // fell H = 1.5 - 0.5 (surface at 0, centre starts at r + 1)
  Check(rebound > 0 && Near(rebound / 0.5, vimp, 9.81 * h * 2) && vmax_down < rebound / 0.5 + 1e-12,
        "ground bounce: rebound / e equals the impact speed sqrt(2 g H) within 2 g h (one step of acceleration, plus penetration)");
}
