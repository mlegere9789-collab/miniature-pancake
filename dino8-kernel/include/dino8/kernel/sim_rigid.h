#pragma once
// Rigid body world model for the Blender simulation rows (bpy.ops.rigidbody.*).
//
// Header-only and free of openNURBS: a small data model (world, bodies, constraints, selection) plus a fixed-step
// integrator. The integrator is velocity Verlet (second order, symplectic), so constant-acceleration motion is
// reproduced exactly and every other error bound can be stated as a closed form in h (see the tests).
//
// What this is NOT: a copy of Bullet or of Blender's solver. Only the data operations and the physics that has a
// closed form (free fall, impulse collisions, pendulum, momentum and energy balance) are modelled. Blender's exact
// numbers for convex hull collision margins, solver iteration counts or visual output are not reproduced.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <vector>

namespace dino8 {
namespace sim {

struct Vec3 {
  double x = 0, y = 0, z = 0;
  Vec3() = default;
  Vec3(double a, double b, double c) : x(a), y(b), z(c) {}
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  double Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  double Length() const { return std::sqrt(Dot(*this)); }
};

enum class RbType { Active, Passive };
enum class RbShape { Box, Sphere, Capsule, Cylinder, Cone, ConvexHull, Mesh, Compound };

// Closed-form volumes (cubic length units) of the analytic collision shapes.
//   Box:      size.x * size.y * size.z (full extents)
//   Sphere:   4/3 pi r^3
//   Cylinder: pi r^2 h            (h = full height)
//   Cone:     pi r^2 h / 3
//   Capsule:  pi r^2 h + 4/3 pi r^3 (h = height of the cylindrical part)
// Convex hull / mesh shapes have no formula: the caller gives the mesh volume (see MeshVolume).
struct ShapeDims {
  Vec3 size{1, 1, 1};  // box
  double radius = 0.5;
  double height = 1.0;
  double mesh_volume = 1.0;  // used by ConvexHull / Mesh / Compound
};

inline double ShapeVolume(RbShape s, const ShapeDims& d) {
  const double pi = 3.14159265358979323846;
  switch (s) {
    case RbShape::Box: return d.size.x * d.size.y * d.size.z;
    case RbShape::Sphere: return 4.0 / 3.0 * pi * d.radius * d.radius * d.radius;
    case RbShape::Cylinder: return pi * d.radius * d.radius * d.height;
    case RbShape::Cone: return pi * d.radius * d.radius * d.height / 3.0;
    case RbShape::Capsule:
      return pi * d.radius * d.radius * d.height + 4.0 / 3.0 * pi * d.radius * d.radius * d.radius;
    default: return d.mesh_volume;
  }
}

// Volume enclosed by a closed, consistently outward-oriented triangle mesh: sum over triangles of
// dot(a, cross(b, c)) / 6 (divergence theorem, signed tetrahedra against the origin).
inline double MeshVolume(const std::vector<Vec3>& v, const std::vector<std::size_t>& tri) {
  double vol = 0;
  for (std::size_t i = 0; i + 2 < tri.size(); i += 3) {
    const Vec3 &a = v[tri[i]], &b = v[tri[i + 1]], &c = v[tri[i + 2]];
    const Vec3 cr(b.y * c.z - b.z * c.y, b.z * c.x - b.x * c.z, b.x * c.y - b.y * c.x);
    vol += a.Dot(cr) / 6.0;
  }
  return vol;
}

struct RigidBody {
  RbType type = RbType::Active;
  double mass = 1.0;
  double friction = 0.5;
  double restitution = 0.0;
  double linear_damping = 0.04;
  double angular_damping = 0.1;
  RbShape shape = RbShape::ConvexHull;
  ShapeDims dims;
  bool enabled = true;
  bool kinematic = false;
  Vec3 vel;
};

struct SimObject {
  int id = 0;
  Vec3 loc;
  bool selected = false;
  bool has_body = false;
  RigidBody rb;
};

enum class ConstraintType { Fixed, Point, Hinge, Slider, Piston, Generic, GenericSpring, Motor };
enum class ConnectPattern { SelectedToActive, ChainDistance };
enum class PivotType { Center, Active, Selected };

struct RbConstraint {
  int id = 0;
  ConstraintType type = ConstraintType::Fixed;
  int a = -1, b = -1;  // object ids; -1 = not assigned (also the "world anchor" side of a pendulum)
  Vec3 pivot;          // world-space pivot
  bool enabled = true;
  bool disable_collisions = true;
};

struct WorldSettings {
  Vec3 gravity{0, 0, -9.81};
  int substeps_per_frame = 10;
  int solver_iterations = 10;
  double time_scale = 1.0;
  double fps = 24.0;
  int frame_start = 1;
  int frame_end = 250;
};

struct Keyframe {
  int frame;
  Vec3 loc;
};

class RigidScene {
 public:
  // ---- world ---------------------------------------------------------------------------------------
  bool HasWorld() const { return has_world_; }
  WorldSettings& World() { return world_; }
  const WorldSettings& World() const { return world_; }
  // Add Rigid Body World: false (no change) when one exists already.
  bool WorldAdd() {
    if (has_world_) return false;
    has_world_ = true;
    world_ = WorldSettings();
    return true;
  }
  // Remove Rigid Body World: false when there is none. Objects keep their body settings.
  bool WorldRemove() {
    if (!has_world_) return false;
    has_world_ = false;
    return true;
  }

  // ---- objects -------------------------------------------------------------------------------------
  int AddObject(const Vec3& loc) {
    SimObject o;
    o.id = ++next_id_;
    o.loc = loc;
    objs_.push_back(o);
    return o.id;
  }
  SimObject* Find(int id) {
    for (auto& o : objs_) if (o.id == id) return &o;
    return nullptr;
  }
  const SimObject* Find(int id) const {
    for (auto& o : objs_) if (o.id == id) return &o;
    return nullptr;
  }
  std::vector<SimObject>& Objects() { return objs_; }
  const std::vector<SimObject>& Objects() const { return objs_; }
  void SetActive(int id) { active_ = id; }
  int Active() const { return active_; }

  // Add Rigid Body: gives the object a body of `type` with default settings (and a world when none exists).
  // False when the object is missing or already has a body.
  bool ObjectAdd(int id, RbType type) {
    SimObject* o = Find(id);
    if (!o || o->has_body) return false;
    if (!has_world_) WorldAdd();
    o->has_body = true;
    o->rb = RigidBody();
    o->rb.type = type;
    return true;
  }
  bool ObjectRemove(int id) {
    SimObject* o = Find(id);
    if (!o || !o->has_body) return false;
    o->has_body = false;
    o->rb = RigidBody();
    // constraints that referenced the body are left as they are (they just stop acting): see StepFrame.
    return true;
  }
  // Add / Remove Rigid Bodies: every selected object. Returns how many objects changed.
  int ObjectsAdd(RbType type) {
    int n = 0;
    for (auto& o : objs_) if (o.selected && ObjectAdd(o.id, type)) ++n;
    return n;
  }
  int ObjectsRemove() {
    int n = 0;
    for (auto& o : objs_) if (o.selected && ObjectRemove(o.id)) ++n;
    return n;
  }
  // Copy Rigid Body Settings: from the active object onto every other selected object that has a body.
  // Location and velocity are not settings and are not copied. Returns how many were written.
  int SettingsCopy() {
    const SimObject* src = Find(active_);
    if (!src || !src->has_body) return 0;
    const RigidBody s = src->rb;
    int n = 0;
    for (auto& o : objs_) {
      if (!o.selected || o.id == active_ || !o.has_body) continue;
      const Vec3 v = o.rb.vel;
      o.rb = s;
      o.rb.vel = v;
      ++n;
    }
    return n;
  }
  // Change Collision Shape: every selected object with a body.
  int ShapeChange(RbShape shape) {
    int n = 0;
    for (auto& o : objs_) if (o.selected && o.has_body) { o.rb.shape = shape; ++n; }
    return n;
  }
  // Calculate Mass: mass = density * shape volume for every selected object with a body.
  int MassCalculate(double density) {
    int n = 0;
    for (auto& o : objs_) {
      if (!o.selected || !o.has_body) continue;
      o.rb.mass = density * ShapeVolume(o.rb.shape, o.rb.dims);
      ++n;
    }
    return n;
  }

  // ---- constraints ---------------------------------------------------------------------------------
  int ConstraintAdd(ConstraintType type, int a = -1, int b = -1, const Vec3& pivot = Vec3()) {
    RbConstraint c;
    c.id = ++next_con_;
    c.type = type;
    c.a = a;
    c.b = b;
    c.pivot = pivot;
    cons_.push_back(c);
    if (const SimObject* ob = Find(b)) rod_[c.id] = (ob->loc - pivot).Length();  // rod length fixed at creation
    return c.id;
  }
  bool ConstraintRemove(int id) {
    for (std::size_t i = 0; i < cons_.size(); ++i)
      if (cons_[i].id == id) { cons_.erase(cons_.begin() + static_cast<std::ptrdiff_t>(i)); return true; }
    return false;
  }
  const std::vector<RbConstraint>& Constraints() const { return cons_; }

  // Connect Rigid Bodies: creates constraints between the selected objects that have bodies.
  //   SelectedToActive: one constraint (active, other) per other selected object.
  //   ChainDistance:    starting at the active object, repeatedly link to the nearest not yet linked selected
  //                     object; n objects give n - 1 constraints.
  // The pivot is the midpoint (Center), the first object's location (Active) or the second's (Selected).
  // Returns the number of constraints made.
  int Connect(ConnectPattern pat, PivotType pivot, ConstraintType ctype) {
    const SimObject* act = Find(active_);
    if (!act || !act->has_body) return 0;
    std::vector<const SimObject*> others;
    for (auto& o : objs_) if (o.selected && o.has_body && o.id != active_) others.push_back(&o);
    auto make = [&](const SimObject* p, const SimObject* q) {
      Vec3 pv = (pivot == PivotType::Center) ? (p->loc + q->loc) * 0.5
                : (pivot == PivotType::Active) ? p->loc : q->loc;
      ConstraintAdd(ctype, p->id, q->id, pv);
    };
    int n = 0;
    if (pat == ConnectPattern::SelectedToActive) {
      for (auto* o : others) { make(act, o); ++n; }
    } else {
      const SimObject* cur = act;
      while (!others.empty()) {
        std::size_t best = 0;
        double bd = (others[0]->loc - cur->loc).Length();
        for (std::size_t i = 1; i < others.size(); ++i) {
          const double d = (others[i]->loc - cur->loc).Length();
          if (d < bd) { bd = d; best = i; }
        }
        make(cur, others[best]);
        ++n;
        cur = others[best];
        others.erase(others.begin() + static_cast<std::ptrdiff_t>(best));
      }
    }
    return n;
  }

  // ---- simulation ----------------------------------------------------------------------------------
  // Passive Box bodies act as the ground: their top face (loc.z + size.z / 2) is a plane for Active Spheres whose
  // centre is above it. A sphere that has penetrated and moves downwards has its normal velocity replaced by
  // -restitution * v (an impulse against an immovable body). Active spheres collide pairwise by impulse.
  //
  // Point constraints: when `a` is -1 or has no body, the pivot is fixed in the world and `b` is held at its
  // initial distance from it (a rigid massless rod): positions are projected onto the sphere around the pivot after
  // every step and the radial velocity is removed (SHAKE / RATTLE style).
  void StepSubstep(double h) {
    for (auto& o : objs_) {
      if (!o.has_body || !has_world_) continue;
      if (o.rb.type != RbType::Active || !o.rb.enabled || o.rb.kinematic) continue;
      const Vec3 g = world_.gravity;
      const double damp = std::pow(1.0 - o.rb.linear_damping, h);
      if (!StepRod(o, g, h)) {
        // constant acceleration: velocity Verlet is exact for it
        o.loc = o.loc + o.rb.vel * h + g * (0.5 * h * h);
        o.rb.vel = o.rb.vel + g * h;
      }
      o.rb.vel = o.rb.vel * damp;
      CollideGround(o);
    }
    CollidePairs();
  }
  // One frame = substeps_per_frame substeps of time_scale / (fps * substeps).
  void StepFrame() {
    const int n = std::max(1, world_.substeps_per_frame);
    const double h = world_.time_scale / (world_.fps * n);
    for (int i = 0; i < n; ++i) StepSubstep(h);
  }
  // Bake to Keyframes: steps frame_start..frame_end and records the location of every selected Active body every
  // `step` frames (the first frame included). Returns the keyframes per object id. Leaves the scene at the end state.
  std::map<int, std::vector<Keyframe>> BakeToKeyframes(int step = 1) {
    std::map<int, std::vector<Keyframe>> out;
    step = std::max(1, step);
    for (int f = world_.frame_start; f <= world_.frame_end; ++f) {
      if (f > world_.frame_start) StepFrame();
      if ((f - world_.frame_start) % step != 0) continue;
      for (auto& o : objs_)
        if (o.selected && o.has_body && o.rb.type == RbType::Active) out[o.id].push_back({f, o.loc});
    }
    return out;
  }
  // Impulse between two bodies along `n` (unit, from 1 to 2) with restitution e: j = -(1+e) vrel.n / (1/m1+1/m2).
  // Exactly conserves total momentum; kinetic energy changes by -(1-e^2) * mu * vn^2 / 2, mu = m1 m2 / (m1 + m2).
  static void Impulse(RigidBody& b1, RigidBody& b2, const Vec3& n, double e) {
    const double vn = (b2.vel - b1.vel).Dot(n);
    if (vn >= 0) return;  // separating
    const double j = -(1.0 + e) * vn / (1.0 / b1.mass + 1.0 / b2.mass);
    b1.vel = b1.vel - n * (j / b1.mass);
    b2.vel = b2.vel + n * (j / b2.mass);
  }

 private:
  // One RATTLE step (SHAKE for positions, projection for velocities; second order) for a body held at its initial
  // distance L from a world pivot by a Point constraint. The constraint acceleration acts along the old rod direction
  // d: x' = y + k d with y = x + h v + h^2 g / 2 - pivot and k chosen so |x'| = L, i.e.
  // k^2 + 2 k (y.d) + |y|^2 - L^2 = 0. Velocity: half kick with the constraint acceleration 2k/h^2 d, half kick with
  // gravity, then the component along the new rod direction is removed. Returns false when no such constraint exists.
  bool StepRod(SimObject& o, const Vec3& g, double h) {
    for (const auto& c : cons_) {
      if (!c.enabled || c.type != ConstraintType::Point || c.b != o.id) continue;
      const SimObject* a = Find(c.a);
      if (a && a->has_body) continue;  // body-to-body constraints are not modelled
      auto it = rod_.find(c.id);
      if (it == rod_.end()) it = rod_.emplace(c.id, (o.loc - c.pivot).Length()).first;
      const double L = it->second;
      Vec3 d = o.loc - c.pivot;
      const double r = d.Length();
      if (r <= 0 || L <= 0) return false;
      d = d * (1.0 / r);
      const Vec3 y = o.loc + o.rb.vel * h + g * (0.5 * h * h) - c.pivot;
      const double yd = y.Dot(d);
      const double disc = yd * yd - (y.Dot(y) - L * L);
      const double k = disc < 0 ? -yd : -yd + std::sqrt(disc);
      const Vec3 cacc = d * (2.0 * k / (h * h));
      o.loc = c.pivot + y + d * k;
      Vec3 w = o.rb.vel + (g + cacc) * (0.5 * h) + g * (0.5 * h);
      Vec3 dn = o.loc - c.pivot;
      dn = dn * (1.0 / dn.Length());
      o.rb.vel = w - dn * w.Dot(dn);
      return true;
    }
    return false;
  }
  void CollideGround(SimObject& o) {
    if (o.rb.shape != RbShape::Sphere) return;
    for (const auto& g : objs_) {
      if (!g.has_body || g.rb.type != RbType::Passive || g.rb.shape != RbShape::Box) continue;
      const double top = g.loc.z + 0.5 * g.rb.dims.size.z;
      const double r = o.rb.dims.radius;
      if (o.loc.z - r < top && o.rb.vel.z < 0) {
        o.loc.z = top + r;
        o.rb.vel.z = -o.rb.restitution * o.rb.vel.z;
      }
    }
  }
  void CollidePairs() {
    for (std::size_t i = 0; i < objs_.size(); ++i)
      for (std::size_t j = i + 1; j < objs_.size(); ++j) {
        SimObject &p = objs_[i], &q = objs_[j];
        if (!p.has_body || !q.has_body) continue;
        if (p.rb.type != RbType::Active || q.rb.type != RbType::Active) continue;
        if (p.rb.shape != RbShape::Sphere || q.rb.shape != RbShape::Sphere) continue;
        Vec3 d = q.loc - p.loc;
        const double dist = d.Length();
        if (dist <= 0 || dist >= p.rb.dims.radius + q.rb.dims.radius) continue;
        Impulse(p.rb, q.rb, d * (1.0 / dist), std::min(p.rb.restitution, q.rb.restitution));
      }
  }

  bool has_world_ = false;
  WorldSettings world_;
  std::vector<SimObject> objs_;
  std::vector<RbConstraint> cons_;
  std::map<int, double> rod_;
  int next_id_ = 0, next_con_ = 0, active_ = -1;
};

}  // namespace sim
}  // namespace dino8
