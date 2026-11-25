#pragma once
// Point cache list for the Blender physics point cache rows (bpy.ops.ptcache.*): a physics system owns an ordered
// list of caches with one active; a bake steps the simulation over the cache's frame range and stores one state per
// frame. The stepped system here is free particles under constant gravity (sim_soft.h FluidDomain::AdvanceFrame), so
// every baked frame has a closed form: p0 + v0 t + g t^2 / 2. Header-only, no openNURBS.
#include <string>
#include <utility>
#include <vector>

#include "dino8/kernel/sim_lists.h"
#include "dino8/kernel/sim_soft.h"

namespace dino8 {
namespace sim {

struct PointCache {
  std::string name;
  int frame_start = 1;
  int frame_end = 250;
  bool baked = false;
  std::vector<std::vector<FluidParticle>> frames;  // frames[k] = state at frame_start + k
};

class PointCacheSet {
 public:
  FluidDomain sim;                // gravity, fps, substeps and the initial particles
  ActiveList<PointCache> caches;  // always holds at least one cache

  PointCacheSet() { PointCache c; c.name = "Cache"; caches.Add(c); }

  PointCache& Active() { return caches.items[static_cast<std::size_t>(caches.active)]; }

  // Add New Cache: appended with a unique name and the frame range of the active one; becomes active.
  int Add() {
    PointCache c;
    c.frame_start = Active().frame_start;
    c.frame_end = Active().frame_end;
    c.name = "Cache";
    for (int i = 1; Taken(c.name); ++i) c.name = "Cache_" + std::to_string(i);
    caches.Add(c);
    return caches.active;
  }
  // Delete Current Cache: refused for the last remaining cache.
  bool Remove() { return caches.Size() > 1 && caches.Remove(); }

  // Bake Physics on the active cache: steps frame_end - frame_start + 1 states from the initial one. An empty frame
  // range is an error (message in *err, cache unchanged). Returns the number of frames stored (0 on error).
  int Bake(std::string* err = nullptr) {
    PointCache& c = Active();
    if (c.frame_end < c.frame_start) {
      if (err) *err = "frame range is empty (end before start)";
      return 0;
    }
    c.frames.clear();
    c.frames.push_back(sim.initial);
    for (int f = c.frame_start + 1; f <= c.frame_end; ++f) {
      std::vector<FluidParticle> s = c.frames.back();
      sim.AdvanceFrame(&s);
      c.frames.push_back(std::move(s));
    }
    c.baked = true;
    return static_cast<int>(c.frames.size());
  }
  // Delete Physics Bake: clears the active cache only. False when it held nothing.
  bool FreeBake() {
    PointCache& c = Active();
    const bool had = c.baked || !c.frames.empty();
    c.frames.clear();
    c.baked = false;
    return had;
  }
  // Delete All Physics Bakes: clears every cache of the set. Returns how many held data.
  int FreeBakeAll() {
    int n = 0;
    for (auto& c : caches.items) {
      if (c.baked || !c.frames.empty()) ++n;
      c.frames.clear();
      c.baked = false;
    }
    return n;
  }

 private:
  bool Taken(const std::string& n) const {
    for (auto& c : caches.items) if (c.name == n) return true;
    return false;
  }
};

}  // namespace sim
}  // namespace dino8
