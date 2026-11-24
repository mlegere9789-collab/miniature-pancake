// Wave 4 row work, Blender simulation rows (part A): rigid body plural add/remove and world remove, and the hair
// particle edit selection/visibility/delete operators. Oracles: free fall closed form (velocity Verlet is exact for
// constant gravity), morphological dilation/erosion on a line of keys computed independently in the test, binomial
// 5-sigma bounds for Select Random, exact counts. Blender-specific definitions that could not be fetched
// (docs.blender.org is outside the network policy) are stated as "recalled" in the row notes.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/sim_hair.h"
#include "dino8/kernel/sim_rigid.h"
#include "test_registry.h"

using namespace dino8::sim;
using d8test::Check;

namespace {
bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

HairStrand Line(double x, int n) {
  HairStrand s;
  for (int i = 0; i < n; ++i) { HairKey k; k.pos = Vec3(x, 0, i); s.keys.push_back(k); }
  return s;
}
std::vector<char> Sel(const HairStrand& s) {
  std::vector<char> v;
  for (auto& k : s.keys) v.push_back(k.selected);
  return v;
}
}  // namespace

// ------------------------------------------------------------------------------------------ RIGIDBOD-0009
D8_EXTRA_TEST(RowSim4RigidObjectsAddMakesSelectionFallAndLeavesTheRestStatic) {
  RigidScene s;
  std::vector<int> act, pas;
  for (int i = 0; i < 3; ++i) { int id = s.AddObject(Vec3(i, 0, 10)); s.Find(id)->selected = true; act.push_back(id); }
  const int other = s.AddObject(Vec3(9, 0, 10));  // not selected
  Check(s.ObjectsAdd(RbType::Active) == 3 && !s.Find(other)->has_body && s.HasWorld(),
        "objects add: the 3 selected objects get a body and the world appears, the unselected object gets none");
  for (int id : act) { s.Find(id)->rb.linear_damping = 0; s.Find(id)->rb.shape = RbShape::Sphere; }
  // a second batch of Passive bodies; the first three already have a body and are counted out
  for (int id : act) s.Find(id)->selected = true;
  int p = s.AddObject(Vec3(0, 5, 10)); s.Find(p)->selected = true; pas.push_back(p);
  Check(s.ObjectsAdd(RbType::Passive) == 1 && s.Find(p)->rb.type == RbType::Passive && s.Find(act[0])->rb.type == RbType::Active,
        "objects add: only objects without a body are added (1), existing Active bodies keep their type");
  for (int i = 0; i < 24; ++i) s.StepFrame();  // 1 s
  bool fell = true, still = true;
  for (int id : act) fell = fell && Near(s.Find(id)->loc.z, 10.0 - 0.5 * 9.81, 1e-9);
  still = Near(s.Find(p)->loc.z, 10, 0) && Near(s.Find(other)->loc.z, 10, 0);
  Check(fell, "objects add: Active bodies fall z = z0 - g t^2/2 = 5.095 after 1 s (1e-9)");
  Check(still, "objects add: the Passive body and the object without a body do not move");
}

// ------------------------------------------------------------------------------------------ RIGIDBOD-0010
D8_EXTRA_TEST(RowSim4RigidObjectsRemoveFreezesSelectedBodiesOnly) {
  RigidScene s;
  s.WorldAdd();
  std::vector<int> ids;
  for (int i = 0; i < 4; ++i) {
    int id = s.AddObject(Vec3(i, 0, 10));
    s.ObjectAdd(id, RbType::Active);
    s.Find(id)->rb.linear_damping = 0;
    ids.push_back(id);
  }
  s.Find(ids[0])->selected = s.Find(ids[1])->selected = true;
  Check(s.ObjectsRemove() == 2 && !s.Find(ids[0])->has_body && !s.Find(ids[1])->has_body && s.Find(ids[2])->has_body && s.Find(ids[3])->has_body,
        "objects remove: the 2 selected bodies are removed, the other 2 keep theirs");
  Check(s.ObjectsRemove() == 0, "objects remove: repeating it removes nothing");
  for (int i = 0; i < 24; ++i) s.StepFrame();
  Check(Near(s.Find(ids[0])->loc.z, 10, 0) && Near(s.Find(ids[1])->loc.z, 10, 0) &&
            Near(s.Find(ids[2])->loc.z, 10 - 0.5 * 9.81, 1e-9) && Near(s.Find(ids[3])->loc.z, 10 - 0.5 * 9.81, 1e-9),
        "objects remove: removed objects stay at z = 10, the others free-fall to 5.095 after 1 s");
}

// ------------------------------------------------------------------------------------------ RIGIDBOD-0013
D8_EXTRA_TEST(RowSim4RigidWorldRemoveStopsMotionAndKeepsBodiesAndConstraints) {
  RigidScene s;
  s.WorldAdd();
  const int a = s.AddObject(Vec3(0, 0, 10));
  s.ObjectAdd(a, RbType::Active);
  s.Find(a)->rb.linear_damping = 0;
  s.Find(a)->rb.mass = 3.5;
  const int c = s.ConstraintAdd(ConstraintType::Fixed, a, -1, Vec3());
  for (int i = 0; i < 12; ++i) s.StepFrame();  // 0.5 s of free fall
  const double z_half = 10 - 0.5 * 9.81 * 0.25;
  Check(Near(s.Find(a)->loc.z, z_half, 1e-9), "world remove: before removal the body fell z0 - g t^2/2 for 0.5 s");
  const double z_before = s.Find(a)->loc.z;
  Check(s.WorldRemove() && !s.HasWorld(), "world remove: the world is removed");
  for (int i = 0; i < 24; ++i) s.StepFrame();
  Check(s.Find(a)->loc.z == z_before && s.Find(a)->rb.vel.z < 0, "world remove: nothing moves afterwards (z unchanged, 1 s of stepping)");
  Check(s.Find(a)->has_body && s.Find(a)->rb.mass == 3.5 && s.Constraints().size() == 1 && s.Constraints()[0].id == c,
        "world remove: the body settings and the constraint are kept");
  const double vz = s.Find(a)->rb.vel.z;
  s.WorldAdd();
  for (int i = 0; i < 24; ++i) s.StepFrame();
  const double t = 1.0;
  Check(Near(s.Find(a)->loc.z, z_half + vz * t - 0.5 * 9.81 * t * t, 1e-9),
        "world remove: after a new world the body resumes with its stored velocity: z = z + v t - g t^2/2");
}

// ------------------------------------------------------------------------------------------ PARTICLE-0027 select more
// Dilation of a selection on a line of keys: key i is selected after m applications iff some originally selected key
// j has |i - j| <= m. The expected set is computed by that definition in the test, not by repeating the operator.
D8_EXTRA_TEST(RowSim4ParticleSelectMoreDilatesAlongEachStrand) {
  HairSystem h;
  h.strands.push_back(Line(0, 10));
  h.strands.push_back(Line(1, 10));
  h.strands.push_back(Line(2, 10));  // nothing selected: stays so
  for (int i : {3, 4}) h.strands[0].keys[static_cast<std::size_t>(i)].selected = true;
  for (int i : {0, 8}) h.strands[1].keys[static_cast<std::size_t>(i)].selected = true;
  const HairSystem before = h;
  for (int m = 1; m <= 3; ++m) {
    h.SelectMore();
    bool ok = true;
    for (std::size_t si = 0; si < 2; ++si)
      for (int i = 0; i < 10; ++i) {
        bool want = false;  // dilation by m: some originally selected key within m positions
        for (int j = 0; j < 10; ++j)
          if (before.strands[si].keys[static_cast<std::size_t>(j)].selected && std::abs(i - j) <= m) want = true;
        ok = ok && (h.strands[si].keys[static_cast<std::size_t>(i)].selected == want);
      }
    char buf[120];
    std::snprintf(buf, sizeof buf, "select more: after %d application(s) key i is selected iff an original selected key is within %d positions", m, m);
    Check(ok, buf);
  }
  bool s0 = true;
  for (int i = 0; i < 10; ++i) s0 = s0 && (h.strands[0].keys[static_cast<std::size_t>(i)].selected == (i <= 7));
  Check(s0, "select more: keys 3, 4 selected become keys 0..7 after 3 applications (clamped at the root, 3 per side otherwise)");
  bool none = true, moved = false;
  for (auto& k : h.strands[2].keys) none = none && !k.selected;
  for (std::size_t si = 0; si < 3; ++si)
    for (std::size_t i = 0; i < 10; ++i)
      moved = moved || h.strands[si].keys[i].pos.x != before.strands[si].keys[i].pos.x || h.strands[si].keys[i].pos.z != before.strands[si].keys[i].pos.z;
  Check(none && !moved, "select more: a strand without selection stays unselected and no key position changes");
  HairSystem g;
  g.strands.push_back(Line(0, 6));
  g.strands[0].keys[2].selected = true;
  g.strands[0].keys[3].hidden = true;
  g.SelectMore();
  Check(g.strands[0].keys[1].selected && !g.strands[0].keys[3].selected && g.strands[0].keys[3].hidden, "select more: a hidden neighbour is not selected");
}

// ------------------------------------------------------------------------------------------ PARTICLE-0024 select less
// Erosion with the strand ends counted as unselected: key i stays selected iff keys i-1, i, i+1 all exist and are selected.
D8_EXTRA_TEST(RowSim4ParticleSelectLessErodesAlongEachStrand) {
  HairSystem h;
  h.strands.push_back(Line(0, 12));
  const int pattern[12] = {0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1};  // runs 1..4, 6, 8..11 (touches the tip)
  for (int i = 0; i < 12; ++i) h.strands[0].keys[static_cast<std::size_t>(i)].selected = pattern[i] != 0;
  const std::vector<char> before = Sel(h.strands[0]);
  h.SelectLess();
  bool ok = true;
  for (int i = 0; i < 12; ++i) {
    const bool want = i > 0 && i < 11 && before[static_cast<std::size_t>(i - 1)] && before[static_cast<std::size_t>(i)] && before[static_cast<std::size_t>(i + 1)];
    ok = ok && (h.strands[0].keys[static_cast<std::size_t>(i)].selected == want);
  }
  Check(ok, "select less: key i stays iff both neighbours exist and are selected (runs 1..4 -> 2..3, single key 6 gone, 8..11 -> 9..10)");
  Check(h.CountSelected() == 4, "select less: 4 keys remain of the 9 that were selected");
  // Closing law for an interval away from the strand ends: Less(More([a, b])) == [a, b].
  HairSystem g;
  g.strands.push_back(Line(0, 12));
  for (int i = 4; i <= 7; ++i) g.strands[0].keys[static_cast<std::size_t>(i)].selected = true;
  const std::vector<char> iv = Sel(g.strands[0]);
  g.SelectMore();
  g.SelectLess();
  Check(Sel(g.strands[0]) == iv, "select less: undoes select more on an interval that does not touch the strand ends");
  // Erosion only removes: no data modified, selection only shrinks.
  Check(h.strands[0].keys[5].pos.z == 5 && h.strands[0].keys.size() == 12, "select less: no key is created, removed or moved");
}

// ------------------------------------------------------------------------------------------ PARTICLE-0025 select linked
D8_EXTRA_TEST(RowSim4ParticleSelectLinkedAllSelectsWholeStrands) {
  HairSystem h;
  for (int i = 0; i < 5; ++i) h.strands.push_back(Line(i, 6));
  h.strands[1].keys[2].selected = true;
  h.strands[3].keys[5].selected = true;
  h.strands[3].keys[0].hidden = true;
  h.SelectLinked();
  int per[5] = {};
  for (int i = 0; i < 5; ++i) for (auto& k : h.strands[static_cast<std::size_t>(i)].keys) per[i] += k.selected;
  Check(per[0] == 0 && per[1] == 6 && per[2] == 0 && per[3] == 5 && per[4] == 0,
        "select linked: strands 1 and 3 become fully selected (6 and 5 visible keys), the other 3 strands none");
  Check(!h.strands[3].keys[0].selected, "select linked: a hidden key stays unselected");
  h.SelectLinked();
  Check(h.CountSelected() == 11, "select linked: applying it again changes nothing (11 keys)");
}

// ------------------------------------------------------------------------------------------ PARTICLE-0028 select random
// Each key is picked independently with probability p: the count is Binomial(n, p). Accepted within 5 sigma
// (two-sided tail < 6e-7) of the mean n p, sigma = sqrt(n p (1 - p)).
D8_EXTRA_TEST(RowSim4ParticleSelectRandomFollowsTheRatioAndIsReproducible) {
  HairSystem h;
  for (int i = 0; i < 400; ++i) h.strands.push_back(Line(i, 50));
  h.strands[7].keys[3].hidden = true;
  const int visible = 400 * 50 - 1;
  using RT = HairSystem::RandomType;
  HairSystem none = h;
  Check(none.SelectRandom(0.0, 1, RT::Points) == 0 && none.CountSelected() == 0, "select random: ratio 0 selects nothing");
  HairSystem all = h;
  all.SelectRandom(1.0, 1, RT::Points);
  Check(all.CountSelected() == visible && !all.strands[7].keys[3].selected, "select random: ratio 1 selects every visible key and never a hidden one");
  const double p = 0.3, n = visible, sigma = std::sqrt(n * p * (1 - p));
  HairSystem a = h;
  a.SelectRandom(p, 12345, RT::Points);
  std::printf("select random: %d selected, mean %.1f sigma %.1f\n", a.CountSelected(), n * p, sigma);
  Check(std::fabs(a.CountSelected() - n * p) <= 5 * sigma, "select random: ratio 0.3 over 19999 keys selects n p within 5 sigma");
  HairSystem b = h;
  b.SelectRandom(p, 12345, RT::Points);
  bool same = true, differs = false;
  HairSystem c = h;
  c.SelectRandom(p, 54321, RT::Points);
  for (std::size_t si = 0; si < 400; ++si)
    for (std::size_t ki = 0; ki < 50; ++ki) {
      same = same && a.strands[si].keys[ki].selected == b.strands[si].keys[ki].selected;
      differs = differs || a.strands[si].keys[ki].selected != c.strands[si].keys[ki].selected;
    }
  Check(same && differs, "select random: the same seed gives the same selection, another seed a different one");
  // Hair type: whole strands, count of strands Binomial(400, 0.25).
  HairSystem d = h;
  d.SelectRandom(0.25, 7, RT::Hair);
  int strands_sel = 0;
  bool whole = true;
  for (auto& s : d.strands) {
    int c2 = 0, vis = 0;
    for (auto& k : s.keys) { c2 += k.selected; vis += !k.hidden; }
    if (c2 > 0) ++strands_sel;
    whole = whole && (c2 == 0 || c2 == vis);
  }
  const double ms = 400 * 0.25, ss = std::sqrt(400 * 0.25 * 0.75);
  Check(whole && std::fabs(strands_sel - ms) <= 5 * ss, "select random: type Hair selects whole strands, their count within 5 sigma of 400 * 0.25");
  // Deselect action removes exactly the picked keys from a full selection.
  HairSystem e = h;
  e.SelectAll(SelectAction::Select);
  const int picked = e.SelectRandom(0.5, 99, RT::Points, false);
  Check(e.CountSelected() == visible - picked, "select random: with action deselect the picked keys leave the selection (n - picked remain)");
}

// ------------------------------------------------------------------------------------------ PARTICLE-0014 hide / 0022 reveal
D8_EXTRA_TEST(RowSim4ParticleHideSelectedAndUnselectedHidesAndDeselects) {
  HairSystem h;
  for (int i = 0; i < 3; ++i) h.strands.push_back(Line(i, 5));
  for (int i : {0, 1}) h.strands[0].keys[static_cast<std::size_t>(i)].selected = true;
  h.strands[2].keys[4].selected = true;  // 3 selected of 15
  HairSystem u = h;
  Check(h.Hide(false) == 3 && h.CountSelected() == 0, "hide selected: the 3 selected keys are hidden and no longer selected");
  int hidden = 0;
  for (auto& s : h.strands) for (auto& k : s.keys) hidden += k.hidden;
  Check(hidden == 3 && h.strands[0].keys[0].hidden && h.strands[2].keys[4].hidden && !h.strands[1].keys[0].hidden,
        "hide selected: exactly those 3 keys are hidden");
  h.SelectAll(SelectAction::Select);
  Check(h.CountSelected() == 12 && !h.strands[0].keys[0].selected, "hide selected: select all skips the hidden keys (12 of 15)");
  Check(u.Hide(true) == 12 && u.CountSelected() == 3 && !u.strands[0].keys[0].hidden && !u.strands[2].keys[4].hidden && u.strands[1].keys[2].hidden,
        "hide unselected: the 12 unselected keys are hidden, the 3 selected stay visible and selected");
  const double z = h.strands[1].keys[3].pos.z;
  Check(z == 3, "hide: key positions are not changed");
}

D8_EXTRA_TEST(RowSim4ParticleRevealShowsHiddenKeysAndOptionallySelectsThem) {
  HairSystem h;
  for (int i = 0; i < 4; ++i) h.strands.push_back(Line(i, 5));
  for (int i : {1, 2}) { h.strands[1].keys[static_cast<std::size_t>(i)].selected = true; }
  h.strands[3].keys[0].selected = true;
  const int hid = h.Hide(false);  // 3 hidden
  h.strands[0].keys[4].selected = true;  // a visible selected key
  HairSystem g = h;
  Check(hid == 3 && h.Reveal(false) == 3, "reveal: the 3 hidden keys are revealed");
  int hidden = 0;
  for (auto& s : h.strands) for (auto& k : s.keys) hidden += k.hidden;
  Check(hidden == 0 && h.CountSelected() == 1, "reveal: nothing is hidden afterwards and, without select, only the earlier visible selection (1 key) is selected");
  Check(g.Reveal(true) == 3 && g.CountSelected() == 4 && g.strands[1].keys[1].selected, "reveal with select: the revealed keys are selected too (1 + 3 = 4)");
  Check(h.Reveal(true) == 0, "reveal: with nothing hidden it changes nothing");
  Check(h.strands[3].keys[0].pos.x == 3 && h.strands[1].keys[2].pos.z == 2, "reveal: key positions are unchanged by hide and reveal");
}

// ------------------------------------------------------------------------------------------ PARTICLE-0004 delete
D8_EXTRA_TEST(RowSim4ParticleDeleteRemovesStrandsOrSelectedKeys) {
  auto make = [] {
    HairSystem h;
    for (int i = 0; i < 4; ++i) h.strands.push_back(Line(i, 6));
    h.strands[1].keys[2].selected = true;                     // one key of strand 1
    h.strands[2].keys[0].selected = h.strands[2].keys[1].selected = true;  // 2 keys of strand 2
    for (int i = 0; i < 5; ++i) h.strands[3].keys[static_cast<std::size_t>(i)].selected = true;  // 5 of 6 keys: 1 key would remain
    return h;
  };
  HairSystem p = make();
  const int rp = p.Delete(HairSystem::DeleteType::Particle);
  Check(p.strands.size() == 1 && p.strands[0].keys.size() == 6 && p.strands[0].keys[0].pos.x == 0 && rp == 18,
        "delete particle: the 3 strands that have a selected key are gone (18 keys), strand 0 is untouched");
  HairSystem k = make();
  const int rk = k.Delete(HairSystem::DeleteType::Key);
  Check(k.strands.size() == 3, "delete keys: strands 0, 1, 2 survive, strand 3 (1 key left) is removed");
  bool order = k.strands[1].keys.size() == 5 && k.strands[2].keys.size() == 4;
  const int want1[5] = {0, 1, 3, 4, 5};
  for (int i = 0; order && i < 5; ++i) order = k.strands[1].keys[static_cast<std::size_t>(i)].pos.z == want1[i];
  for (int i = 0; order && i < 4; ++i) order = k.strands[2].keys[static_cast<std::size_t>(i)].pos.z == i + 2;
  Check(order, "delete keys: strand 1 keeps keys 0, 1, 3, 4, 5 and strand 2 keeps keys 2..5, in their original order");
  Check(rk == 1 + 2 + 6 && k.strands[0].keys.size() == 6, "delete keys: 1 + 2 selected keys and the 6 keys of the dissolved strand are removed (9), strand 0 untouched");
  HairSystem none = make();
  none.SelectAll(SelectAction::Deselect);
  Check(none.Delete(HairSystem::DeleteType::Particle) == 0 && none.strands.size() == 4, "delete: with nothing selected nothing is removed");
}

// ------------------------------------------------------------------------------------------ PARTICLE-0021 remove doubles
D8_EXTRA_TEST(RowSim4ParticleRemoveDoublesLeavesOneStrandPerRootCluster) {
  HairSystem h;
  std::vector<std::pair<int, int>> grid;
  for (int ix = 0; ix < 5; ++ix) for (int iy = 0; iy < 5; ++iy) {
    HairStrand s = Line(0, 3);
    for (auto& k : s.keys) { k.pos.x = ix; k.pos.y = iy; k.selected = true; }
    h.strands.push_back(s);
  }
  // 7 duplicates of grid roots, each displaced by < 0.01 (a quarter of the 0.05 threshold at most)
  for (int d = 0; d < 7; ++d) {
    HairStrand s = Line(0, 3);
    for (auto& k : s.keys) { k.pos.x = (d % 5) + 0.004 * d; k.pos.y = (d * 3 % 5) - 0.003; k.selected = true; }
    h.strands.push_back(s);
  }
  HairStrand lone = Line(0, 3);  // unselected duplicate of root (2,2): must stay
  for (auto& k : lone.keys) { k.pos.x = 2.001; k.pos.y = 2; }
  h.strands.push_back(lone);
  HairSystem tight = h;
  Check(tight.RemoveDoubles(0.001) == 0 && tight.strands.size() == 33, "remove doubles: a threshold below every displacement removes nothing");
  Check(h.RemoveDoubles(0.05) == 7 && h.strands.size() == 26, "remove doubles: the 7 selected near-duplicates are removed, 25 grid strands + 1 unselected duplicate remain");
  bool first = true;
  for (int i = 0; i < 25; ++i) first = first && h.strands[static_cast<std::size_t>(i)].keys[0].pos.x == i / 5 && h.strands[static_cast<std::size_t>(i)].keys[0].pos.y == i % 5;
  Check(first, "remove doubles: of each cluster the earliest strand is the one kept (the exact grid points, in order)");
  Check(h.strands[25].keys[0].pos.x == 2.001, "remove doubles: an unselected strand is never removed");
}

// ------------------------------------------------------------------------------------------ PARTICLE-0030 select tips
D8_EXTRA_TEST(RowSim4ParticleSelectTipsSelectsLastKeyOfEachVisibleStrand) {
  HairSystem h;
  h.strands.push_back(Line(0, 5));
  h.strands.push_back(Line(1, 2));
  h.strands.push_back(Line(2, 1));  // root == tip
  h.strands.push_back(Line(3, 4));
  h.strands[3].keys[3].hidden = true;
  h.strands[0].keys[1].selected = true;  // existing selection is kept
  Check(h.SelectTips() == 3 && h.CountSelected() == 4, "select tips: 3 visible tips are selected on top of the 1 existing key");
  Check(h.strands[0].keys[4].selected && h.strands[1].keys[1].selected && h.strands[2].keys[0].selected && !h.strands[3].keys[3].selected,
        "select tips: the last key of strands 0, 1, 2; the hidden tip of strand 3 is not selected");
  Check(h.strands[0].keys[0].selected == false && h.strands[1].keys[0].selected == false && h.strands[0].keys[1].selected,
        "select tips: roots are not selected and the earlier selection is kept");
}
