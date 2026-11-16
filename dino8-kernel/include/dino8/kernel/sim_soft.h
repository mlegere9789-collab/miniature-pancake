#pragma once
// Mass-spring cloth and a particle fluid domain with point caches, for the Blender cloth / soft body / fluid rows.
//
// Header-only, no openNURBS. Integrator: velocity Verlet (second order, symplectic). Spring damping is a
// velocity-dependent force and makes the step only first order for that term, so every oracle in the tests
// uses damping 0 unless it says otherwise.
//
// Not modelled: Blender's cloth solver (quality steps, bending, shear, collision margins), FLIP / Mantaflow
// grids, noise and mesh generation. The fluid domain advances free particles (gravity, a floor); its caches
// exist to model what a bake produces and what Free / Pause do to it.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

#include "dino8/kernel/sim_rigid.h"

namespace dino8 {
namespace sim {

// ------------------------------------------------------------------------------------------------ cloth
struct ClothParticle {
  Vec3 pos, vel;
  double mass = 1.0;
  bool pinned = false;
};
struct ClothSpring {
  int i = 0, j = 0;
  double rest = 1.0;
  double stiffness = 1.0;  // spring constant k (force per unit stretch)
  double damping = 0.0;    // force per unit relative speed along the spring
};

class ClothSystem {
 public:
  Vec3 gravity{0, 0, -9.81};
  std::vector<ClothParticle> p;
  std::vector<ClothSpring> springs;

  // Linear spring law force on every particle from the springs and gravity (pinned particles: zero).
  void Accel(std::vector<Vec3>* a) const {
    a->assign(p.size(), Vec3());
    std::vector<Vec3> f(p.size());
    for (std::size_t k = 0; k < p.size(); ++k) f[k] = gravity * p[k].mass;
    for (const auto& s : springs) {
      Vec3 d = p[s.j].pos - p[s.i].pos;
      const double len = d.Length();
      if (len <= 0) continue;
      d = d * (1.0 / len);
      const double rel = (p[s.j].vel - p[s.i].vel).Dot(d);
      const double mag = s.stiffness * (len - s.rest) + s.damping * rel;
      f[s.i] += d * mag;
      f[s.j] += d * (-mag);
    }
    for (std::size_t k = 0; k < p.size(); ++k)
      if (!p[k].pinned) (*a)[k] = f[k] * (1.0 / p[k].mass);
  }
  void Step(double h) {
    std::vector<Vec3> a0, a1;
    Accel(&a0);
    for (std::size_t k = 0; k < p.size(); ++k)
      if (!p[k].pinned) p[k].pos = p[k].pos + p[k].vel * h + a0[k] * (0.5 * h * h);
    Accel(&a1);
    for (std::size_t k = 0; k < p.size(); ++k)
      if (!p[k].pinned) p[k].vel = p[k].vel + (a0[k] + a1[k]) * (0.5 * h);
  }
  // Total energy: kinetic + spring (1/2 k (len - rest)^2) + gravitational (-m g . x, for constant g).
  double Energy() const {
    double e = 0;
    for (const auto& q : p) {
      e += 0.5 * q.mass * q.vel.Dot(q.vel);
      e -= q.mass * gravity.Dot(q.pos);
    }
    for (const auto& s : springs) {
      const double ext = (p[s.j].pos - p[s.i].pos).Length() - s.rest;
      e += 0.5 * s.stiffness * ext * ext;
    }
    return e;
  }
  Vec3 Momentum() const {
    Vec3 m;
    for (const auto& q : p) m += q.vel * q.mass;
    return m;
  }
};

// ------------------------------------------------------------------------------------------------ fluid
enum class FluidCacheType { Data, Noise, Mesh, Particles, Guides };

struct FluidParticle {
  Vec3 pos, vel;
};

struct FluidCache {
  std::vector<std::vector<FluidParticle>> frames;  // frames[k] = state at frame_start + k
  bool running = false;
  bool paused = false;
  bool baked = false;
};

class FluidDomain {
 public:
  Vec3 gravity{0, 0, -9.81};
  double fps = 24.0;
  int substeps = 10;
  int frame_start = 1, frame_end = 24;
  double floor_z = -1e300;
  double particle_mass = 1.0;
  std::vector<FluidParticle> initial;

  FluidCache& Cache(FluidCacheType t) { return caches_[t]; }
  const FluidCache* CacheIfAny(FluidCacheType t) const {
    auto it = caches_.find(t);
    return it == caches_.end() ? nullptr : &it->second;
  }
  int TotalFrames() const { return frame_end - frame_start + 1; }

  // Advance one frame of free particles: velocity Verlet for constant gravity (exact), then the floor
  // (a particle below it is placed on it and its normal velocity is zeroed: restitution 0).
  void AdvanceFrame(std::vector<FluidParticle>* s) const {
    const double h = 1.0 / (fps * substeps);
    for (int k = 0; k < substeps; ++k)
      for (auto& q : *s) {
        q.pos = q.pos + q.vel * h + gravity * (0.5 * h * h);
        q.vel = q.vel + gravity * h;
        if (q.pos.z < floor_z) { q.pos.z = floor_z; if (q.vel.z < 0) q.vel.z = 0; }
      }
  }
  // Start (or resume after Pause) baking cache `t`, then run up to `frames` frames; returns frames baked so far.
  // The cache is `baked` once frame_end is reached.
  int Bake(FluidCacheType t, int frames) {
    FluidCache& c = caches_[t];
    if (c.baked) return static_cast<int>(c.frames.size());
    c.running = true;
    c.paused = false;
    if (c.frames.empty()) c.frames.push_back(initial);
    while (frames-- > 0 && static_cast<int>(c.frames.size()) < TotalFrames()) {
      std::vector<FluidParticle> s = c.frames.back();
      AdvanceFrame(&s);
      c.frames.push_back(std::move(s));
    }
    if (static_cast<int>(c.frames.size()) >= TotalFrames()) { c.baked = true; c.running = false; }
    return static_cast<int>(c.frames.size());
  }
  // Pause Bake: stops a running bake and keeps the frames made so far. False when nothing is running.
  bool PauseBake(FluidCacheType t) {
    auto it = caches_.find(t);
    if (it == caches_.end() || !it->second.running) return false;
    it->second.running = false;
    it->second.paused = true;
    return true;
  }
  // Free Particles / Free Data / ... : clears one cache and leaves the others. False when it held nothing.
  bool Free(FluidCacheType t) {
    auto it = caches_.find(t);
    if (it == caches_.end()) return false;
    const bool had = !it->second.frames.empty();
    caches_.erase(it);
    return had;
  }
  void FreeAll() { caches_.clear(); }

 private:
  std::map<FluidCacheType, FluidCache> caches_;
};

}  // namespace sim
}  // namespace dino8
