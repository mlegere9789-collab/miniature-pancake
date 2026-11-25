// Wave 4 row work, Blender simulation rows (part B): boid rule/state lists and physics point caches.
// Oracles: list algebra (permutation laws, position after m moves, order-preserving erase), ballistic closed form for
// baked frames (velocity Verlet is exact for constant gravity, 1e-9), exact counts. Blender-specific definitions that
// could not be fetched (docs.blender.org is outside the network policy) are named "model definition" or "recalled" in
// the row notes; they do not count as independent oracles.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/sim_cache.h"
#include "dino8/kernel/sim_lists.h"
#include "test_registry.h"

using namespace dino8::sim;
using d8test::Check;

namespace {
std::vector<int> Serials(const BoidState& s) {
  std::vector<int> v;
  for (auto& r : s.rules.items) v.push_back(r.serial);
  return v;
}
std::vector<std::string> Names(const BoidSettings& b) {
  std::vector<std::string> v;
  for (auto& s : b.states.items) v.push_back(s.name);
  return v;
}
BoidSettings WithRules(int n) {
  BoidSettings b;
  const BoidRuleType types[8] = {BoidRuleType::Goal, BoidRuleType::Avoid, BoidRuleType::AvoidCollision, BoidRuleType::Separate,
                                 BoidRuleType::Flock, BoidRuleType::FollowLeader, BoidRuleType::AverageSpeed, BoidRuleType::Fight};
  for (int i = 0; i < n; ++i) b.AddRule(types[i % 8]);
  return b;
}
}  // namespace

// ------------------------------------------------------------------------------------------ BOID-0001 add rule
D8_EXTRA_TEST(RowSim4BoidRuleAddAppendsToTheActiveStateAndActivatesIt) {
  BoidSettings b;
  b.AddState();  // second state, active
  b.states.active = 0;
  Check(b.AddRule(BoidRuleType::Separate) && b.AddRule(BoidRuleType::Flock) && b.AddRule(BoidRuleType::Goal),
        "add rule: three rules are accepted by the active state");
  const BoidState& s0 = b.states.items[0];
  Check(s0.rules.Size() == 3 && s0.rules.items[0].type == BoidRuleType::Separate && s0.rules.items[1].type == BoidRuleType::Flock &&
            s0.rules.items[2].type == BoidRuleType::Goal,
        "add rule: rules are appended in the order added");
  Check(s0.rules.active == 2 && Serials(s0) == std::vector<int>({1, 2, 3}), "add rule: the newest rule is the active one and the earlier ones are unchanged");
  Check(b.states.items[1].rules.Size() == 0, "add rule: the other state's list is untouched");
  b.states.active = 1;
  b.AddRule(BoidRuleType::Fight);
  Check(b.states.items[1].rules.Size() == 1 && b.states.items[0].rules.Size() == 3 && b.states.items[1].rules.items[0].type == BoidRuleType::Fight,
        "add rule: with the other state active the rule goes there and the first state keeps its 3 rules");
}

// ------------------------------------------------------------------------------------------ BOID-0002 remove rule
D8_EXTRA_TEST(RowSim4BoidRuleRemoveErasesTheActiveRuleKeepingOrder) {
  BoidSettings b = WithRules(5);  // serials 1..5, active = last
  BoidState& s = b.states.items[0];
  s.rules.active = 2;  // serial 3
  Check(b.RemoveRule() && Serials(s) == std::vector<int>({1, 2, 4, 5}), "remove rule: serial 3 is erased, the others keep their order");
  Check(s.rules.active == 2 && s.rules.items[static_cast<std::size_t>(s.rules.active)].serial == 4,
        "remove rule: the active index now points at the rule that took its place (serial 4)");
  s.rules.active = 3;
  b.RemoveRule();
  Check(Serials(s) == std::vector<int>({1, 2, 4}) && s.rules.active == 2, "remove rule: removing the last one moves the active index to the new last rule");
  while (b.RemoveRule()) {}
  Check(s.rules.Size() == 0 && s.rules.active == -1 && !b.RemoveRule(), "remove rule: an empty list stays empty and reports that nothing was removed");
}

// ------------------------------------------------------------------------------------------ BOID-0003/0004 move rule
D8_EXTRA_TEST(RowSim4BoidRuleMoveDownSwapsWithTheNextRule) {
  BoidSettings b = WithRules(5);
  b.AddState();
  b.states.active = 0;
  BoidState& s = b.states.items[0];  // taken after the add: a reallocation would invalidate it
  s.rules.active = 1;
  const std::vector<int> before = Serials(s);
  Check(b.MoveRuleDown() && Serials(s) == std::vector<int>({1, 3, 2, 4, 5}) && s.rules.active == 2, "move rule down: rule 2 swaps with rule 3 and stays active");
  // after m moves down from index i the active rule sits at min(i + m, n - 1)
  s.rules.active = 0;
  int moves = 0;
  while (b.MoveRuleDown()) ++moves;
  Check(moves == 4 && s.rules.active == 4 && s.rules.items[4].serial == 1, "move rule down: from the top, 4 moves bring it to the bottom and the 5th does nothing");
  std::vector<int> sorted = Serials(s), orig = before;
  std::sort(sorted.begin(), sorted.end());
  std::sort(orig.begin(), orig.end());
  Check(sorted == orig, "move rule down: the list stays a permutation of the same rules");
  Check(b.states.items[1].rules.Size() == 0, "move rule down: the other state is untouched");
  s.rules.active = 1;
  const std::vector<int> pre = Serials(s);
  b.MoveRuleDown();
  b.MoveRuleUp();
  Check(Serials(s) == pre && s.rules.active == 1, "move rule down: followed by move up it restores the order and the active rule");
}

D8_EXTRA_TEST(RowSim4BoidRuleMoveUpSwapsWithThePreviousRule) {
  BoidSettings b = WithRules(5);
  BoidState& s = b.states.items[0];
  s.rules.active = 3;
  Check(b.MoveRuleUp() && Serials(s) == std::vector<int>({1, 2, 4, 3, 5}) && s.rules.active == 2, "move rule up: rule 4 swaps with rule 3 and stays active");
  int moves = 0;
  while (b.MoveRuleUp()) ++moves;
  Check(moves == 2 && s.rules.active == 0 && s.rules.items[0].serial == 4 && Serials(s) == std::vector<int>({4, 1, 2, 3, 5}),
        "move rule up: 2 more moves bring serial 4 to the top, the others shift down one place each, and the next move does nothing");
  Check(!b.MoveRuleUp() && Serials(s) == std::vector<int>({4, 1, 2, 3, 5}), "move rule up: at the top it reports no move and changes nothing");
  BoidSettings one = WithRules(1);
  Check(!one.MoveRuleUp() && !one.MoveRuleDown(), "move rule up/down: a single rule cannot move");
}

// ------------------------------------------------------------------------------------------ BOID-0005 add state
D8_EXTRA_TEST(RowSim4BoidStateAddAppendsAnActiveStateWithUniqueName) {
  BoidSettings b;
  Check(b.states.Size() == 1 && b.states.active == 0, "add state: new boid settings start with one active state");
  b.AddRule(BoidRuleType::Flock);
  for (int i = 0; i < 5; ++i) b.AddState();
  Check(b.states.Size() == 6 && b.states.active == 5, "add state: 5 adds give 6 states and the newest is active");
  std::vector<std::string> n = Names(b);
  std::sort(n.begin(), n.end());
  Check(std::adjacent_find(n.begin(), n.end()) == n.end(), "add state: all 6 names are different");
  Check(b.states.items[0].rules.Size() == 1 && b.states.items[0].rules.items[0].type == BoidRuleType::Flock && b.states.items[5].rules.Size() == 0,
        "add state: the first state keeps its rule and the new state has no rules");
}

// ------------------------------------------------------------------------------------------ BOID-0006 remove state
D8_EXTRA_TEST(RowSim4BoidStateRemoveErasesTheActiveStateAndRefusesTheLast) {
  BoidSettings b;
  b.AddRule(BoidRuleType::Goal);   // state 0: 1 rule
  b.AddState(); b.AddRule(BoidRuleType::Avoid); b.AddRule(BoidRuleType::Fight);  // state 1: 2 rules
  b.AddState(); b.AddRule(BoidRuleType::Flock);  // state 2: 1 rule
  const std::vector<std::string> names = Names(b);
  b.states.active = 1;
  Check(b.RemoveState() && b.states.Size() == 2, "remove state: the active state is erased (3 -> 2)");
  Check(Names(b) == std::vector<std::string>({names[0], names[2]}) && b.states.active == 1 && b.states.items[1].rules.items[0].type == BoidRuleType::Flock,
        "remove state: the others keep their order, names and rules; the active index points at the state that took its place");
  Check(b.states.items[0].rules.Size() == 1 && b.states.items[0].rules.items[0].type == BoidRuleType::Goal, "remove state: the first state's rule is untouched");
  Check(b.RemoveState() && b.states.Size() == 1 && b.states.active == 0, "remove state: removing the last of 2 leaves one state, active");
  Check(!b.RemoveState() && b.states.Size() == 1, "remove state: the only remaining state is not removed");
}

// ------------------------------------------------------------------------------------------ BOID-0007/0008 move state
D8_EXTRA_TEST(RowSim4BoidStateMoveDownSwapsWithTheNextState) {
  BoidSettings b;
  for (int i = 0; i < 3; ++i) b.AddState();  // 4 states
  const std::vector<std::string> n = Names(b);
  b.states.active = 1;
  b.AddRule(BoidRuleType::Avoid);
  Check(b.MoveStateDown() && Names(b) == std::vector<std::string>({n[0], n[2], n[1], n[3]}) && b.states.active == 2,
        "move state down: state 1 swaps with state 2 and stays active");
  Check(b.states.items[2].rules.Size() == 1 && b.states.items[1].rules.Size() == 0, "move state down: a state's rules travel with it");
  Check(b.MoveStateDown() && b.states.active == 3 && !b.MoveStateDown() && b.states.items[3].name == n[1],
        "move state down: one more move reaches the bottom, the next does nothing");
  b.MoveStateUp(); b.MoveStateUp(); b.MoveStateUp();
  b.MoveStateDown(); b.MoveStateDown(); b.MoveStateDown();
  std::vector<std::string> s = Names(b), o = n;
  std::sort(s.begin(), s.end());
  std::sort(o.begin(), o.end());
  Check(s == o && b.states.items[3].name == n[1], "move state down: up and down moves keep the same set of states");
}

D8_EXTRA_TEST(RowSim4BoidStateMoveUpSwapsWithThePreviousState) {
  BoidSettings b;
  for (int i = 0; i < 3; ++i) b.AddState();
  const std::vector<std::string> n = Names(b);
  b.states.active = 2;
  Check(b.MoveStateUp() && Names(b) == std::vector<std::string>({n[0], n[2], n[1], n[3]}) && b.states.active == 1, "move state up: state 2 swaps with state 1 and stays active");
  Check(b.MoveStateUp() && b.states.active == 0 && b.states.items[0].name == n[2] && !b.MoveStateUp(),
        "move state up: a second move reaches the top, a third does nothing");
  Check(Names(b) == std::vector<std::string>({n[2], n[0], n[1], n[3]}), "move state up: the shifted states keep their relative order");
  BoidSettings one;
  Check(!one.MoveStateUp() && !one.MoveStateDown(), "move state: a single state cannot move");
}

// ------------------------------------------------------------------------------------------ PTCACHE helpers
namespace {
void SetupSim(PointCacheSet& s) {
  s.sim.frame_start = 1;
  s.sim.initial = {{Vec3(0, 0, 5), Vec3(1, 0, 0)}, {Vec3(2, 1, 4), Vec3(0, 1, 2)}};
  s.Active().frame_start = 1;
  s.Active().frame_end = 25;
}
}  // namespace

// ------------------------------------------------------------------------------------------ PTCACHE-0001 add
D8_EXTRA_TEST(RowSim4PointCacheAddAppendsAnActiveCacheWithUniqueName) {
  PointCacheSet s;
  SetupSim(s);
  s.Active().frame_start = 10;
  s.Active().frame_end = 40;
  Check(s.caches.Size() == 1 && s.caches.active == 0, "ptcache add: a physics system starts with one cache");
  Check(s.Add() == 1 && s.caches.Size() == 2 && s.caches.active == 1, "ptcache add: the new cache is appended and becomes active");
  Check(s.Active().frame_start == 10 && s.Active().frame_end == 40 && !s.Active().baked && s.Active().frames.empty(),
        "ptcache add: the new cache has the frame range of the previous active one and holds no data");
  for (int i = 0; i < 4; ++i) s.Add();
  std::vector<std::string> n;
  for (auto& c : s.caches.items) n.push_back(c.name);
  std::sort(n.begin(), n.end());
  Check(s.caches.Size() == 6 && std::adjacent_find(n.begin(), n.end()) == n.end(), "ptcache add: 5 adds give 6 caches with 6 different names");
  Check(s.caches.items[0].name == "Cache" && s.caches.items[0].frame_end == 40, "ptcache add: the first cache is unchanged");
}

// ------------------------------------------------------------------------------------------ PTCACHE-0002 bake
D8_EXTRA_TEST(RowSim4PointCacheBakeStoresBallisticFramesAndReportsAnEmptyRange) {
  PointCacheSet s;
  SetupSim(s);
  s.Add();
  s.caches.active = 0;
  std::string err;
  const int n = s.Bake(&err);
  const PointCache& c = s.caches.items[0];
  Check(n == 25 && c.baked && c.frames.size() == 25 && err.empty(), "ptcache bake: frames 1..25 store 25 states and mark the cache baked");
  double worst = 0;
  for (std::size_t f = 0; f < c.frames.size(); ++f) {
    const double t = static_cast<double>(f) / s.sim.fps;
    for (std::size_t i = 0; i < 2; ++i) {
      const Vec3 want = s.sim.initial[i].pos + s.sim.initial[i].vel * t + s.sim.gravity * (0.5 * t * t);
      worst = std::max(worst, (c.frames[f][i].pos - want).Length());
    }
  }
  Check(worst <= 1e-9, "ptcache bake: every stored position equals p0 + v0 t + g t^2/2 within 1e-9 (exact for constant gravity)");
  Check(!s.caches.items[1].baked && s.caches.items[1].frames.empty(), "ptcache bake: only the active cache is baked");
  const auto first = c.frames;
  s.Bake();
  bool same = first.size() == c.frames.size();
  for (std::size_t f = 0; same && f < first.size(); ++f) same = first[f][0].pos.z == c.frames[f][0].pos.z && first[f][1].vel.y == c.frames[f][1].vel.y;
  Check(same, "ptcache bake: baking again gives bit-identical frames");
  s.Active().frame_end = 0;  // empty range: end before start
  std::string e2;
  Check(s.Bake(&e2) == 0 && !e2.empty() && s.caches.items[0].baked && s.caches.items[0].frames.size() == 25,
        "ptcache bake: an empty frame range reports an error and leaves the cache as it was");
}

// ------------------------------------------------------------------------------------------ PTCACHE-0005 free bake
D8_EXTRA_TEST(RowSim4PointCacheFreeBakeClearsOnlyTheActiveCache) {
  PointCacheSet s;
  SetupSim(s);
  s.Add();
  s.caches.items[1].frame_start = 1;
  s.caches.items[1].frame_end = 12;
  s.Bake();            // cache 1 (active)
  s.caches.active = 0;
  s.Bake();            // cache 0
  Check(s.caches.items[0].frames.size() == 25 && s.caches.items[1].frames.size() == 12, "ptcache free bake: setup baked 25 and 12 frames");
  Check(s.FreeBake() && s.caches.items[0].frames.empty() && !s.caches.items[0].baked, "ptcache free bake: the active cache is emptied and no longer baked");
  Check(s.caches.items[1].baked && s.caches.items[1].frames.size() == 12, "ptcache free bake: the other cache keeps its 12 baked frames");
  Check(!s.FreeBake(), "ptcache free bake: freeing an empty cache reports that nothing was there");
  Check(s.caches.items[0].frame_end == 25 && s.caches.Size() == 2, "ptcache free bake: the cache itself and its frame range stay");
}

// ------------------------------------------------------------------------------------------ PTCACHE-0006 free all
D8_EXTRA_TEST(RowSim4PointCacheFreeAllBakesClearsEveryCache) {
  PointCacheSet s;
  SetupSim(s);
  for (int i = 0; i < 3; ++i) s.Add();  // 4 caches
  for (int i = 0; i < 3; ++i) { s.caches.active = i; s.Active().frame_end = 5 + i; s.Bake(); }  // caches 0..2 baked, 3 empty
  Check(s.FreeBakeAll() == 3, "ptcache free all: 3 of the 4 caches held data and are reported");
  bool clear = true;
  for (auto& c : s.caches.items) clear = clear && c.frames.empty() && !c.baked;
  Check(clear && s.caches.Size() == 4, "ptcache free all: every cache is empty and all 4 caches remain");
  Check(s.FreeBakeAll() == 0, "ptcache free all: a second call finds nothing");
  Check(s.caches.items[2].frame_end == 7 && s.caches.items[0].frame_end == 5, "ptcache free all: frame ranges are kept");
}

// ------------------------------------------------------------------------------------------ PTCACHE-0007 remove
D8_EXTRA_TEST(RowSim4PointCacheRemoveDeletesTheActiveCacheAndKeepsOne) {
  PointCacheSet s;
  SetupSim(s);
  for (int i = 0; i < 2; ++i) s.Add();  // 3 caches: Cache, Cache_1, Cache_2
  for (int i = 0; i < 3; ++i) { s.caches.active = i; s.Active().frame_end = 3 + i; s.Bake(); }
  const std::vector<std::string> n = {s.caches.items[0].name, s.caches.items[1].name, s.caches.items[2].name};
  s.caches.active = 1;
  Check(s.Remove() && s.caches.Size() == 2, "ptcache remove: the active cache is deleted (3 -> 2)");
  Check(s.caches.items[0].name == n[0] && s.caches.items[1].name == n[2] && s.caches.active == 1,
        "ptcache remove: the others keep their order, and the active index points at the cache that took its place");
  Check(s.caches.items[0].frames.size() == 3 && s.caches.items[1].frames.size() == 5, "ptcache remove: the remaining caches keep their baked frames (3 and 5)");
  Check(s.Remove() && s.caches.Size() == 1 && s.caches.active == 0, "ptcache remove: deleting down to one cache works");
  Check(!s.Remove() && s.caches.Size() == 1, "ptcache remove: the last cache is not deleted");
}
