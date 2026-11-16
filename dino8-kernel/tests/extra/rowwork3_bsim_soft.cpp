// Round 3 row work, Blender simulation rows (part 2): cloth modifier (mass-spring), fluid point caches (bake,
// free, pause), hair particle edit, and the text editor line operations. Oracles are closed forms or exact counts;
// each tolerance is derived in the comment above the check. Blender-specific definitions that could not be fetched
// are called out in the row notes (this model's choice is stated in the header comment of each operation).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/sim_hair.h"
#include "dino8/kernel/sim_rigid.h"
#include "dino8/kernel/sim_soft.h"
#include "dino8/kernel/text_buffer.h"
#include "test_registry.h"

using namespace dino8::sim;
using d8test::Check;

namespace {
bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
bool NearV(const Vec3& a, const Vec3& b, double tol) { return (a - b).Length() <= tol; }

HairStrand Strand(const std::vector<Vec3>& pts, bool sel) {
  HairStrand s;
  for (const auto& p : pts) { HairKey k; k.pos = p; k.selected = sel; s.keys.push_back(k); }
  return s;
}
}  // namespace

// ---------------------------------------------------------------------------------------- MODIFIER-0007 (Cloth)
// One particle of mass m hangs from a pinned particle on a linear spring (k, rest length R) under gravity g.
//   equilibrium z_eq = -(R + m g / k);  oscillation about it has omega = sqrt(k/m), T = 2 pi sqrt(m/k).
// Velocity Verlet on a linear oscillator has the exact invariant I = m v^2 + k (1 - (omega h)^2/4) x^2, so
//   * the numerical frequency is Omega with cos(Omega h) = 1 - (omega h)^2/2, i.e. T_num = T (1 - (omega h)^2/24 ...)
//   * the true energy stays within E_osc (omega h)^2 / 4 of its start value (E = I/2 + k (omega h)^2 x^2 / 8).
// Both bounds come from the scheme, not from the run.
D8_EXTRA_TEST(RowClothModifierSpringMassMatchesLinearSpringOscillatorAndConservesMomentum) {
  const double m = 2.0, k = 200.0, R = 1.0, g = 9.81, A = 0.05, h = 1e-3;
  ClothSystem c;
  c.gravity = Vec3(0, 0, -g);
  ClothParticle pin;
  pin.pinned = true;
  ClothParticle bob;
  bob.mass = m;
  const double zeq = -(R + m * g / k);
  bob.pos = Vec3(0, 0, zeq - A);
  c.p = {pin, bob};
  ClothSpring sp;
  sp.i = 0; sp.j = 1; sp.rest = R; sp.stiffness = k;
  c.springs = {sp};

  const double w = std::sqrt(k / m), T = 2 * 3.14159265358979323846 / w;
  const double E0 = c.Energy(), Eosc = 0.5 * k * A * A;
  const double ebound = Eosc * (w * h) * (w * h) / 4.0 + 1e-10;  // + rounding of ~1e5 additions of O(20)
  double worst_de = 0;
  double prev = bob.pos.z - zeq, t = 0;
  std::vector<double> up;
  const int N = 10;
  for (long i = 0; i < 100000 && static_cast<int>(up.size()) < N + 1; ++i) {
    c.Step(h);
    t += h;
    const double x = c.p[1].pos.z - zeq;
    if (prev < 0 && x >= 0) up.push_back(t - h + h * (-prev) / (x - prev));
    prev = x;
    worst_de = std::max(worst_de, std::fabs(c.Energy() - E0));
  }
  Check(static_cast<int>(up.size()) == N + 1, "cloth: the hanging particle makes 10 full oscillations");
  const double Tm = (up.back() - up.front()) / N;
  const double tol = 1.01 * (w * h) * (w * h) / 24.0 + 2 * h * (w * h) * (w * h) / (N * T);
  std::printf("cloth: period measured %.9f exact %.9f rel err %.3e tol %.3e; energy dev %.3e bound %.3e\n", Tm, T,
              (Tm - T) / T, tol, worst_de, ebound);
  Check(std::fabs(Tm - T) / T <= tol, "cloth: period equals 2 pi sqrt(m/k) within the Verlet frequency error (omega h)^2/24");
  Check(worst_de <= ebound, "cloth: total energy stays within E_osc (omega h)^2/4 of its start value");
  Check(Near(c.p[0].pos.z, 0, 0) && c.p[0].vel.z == 0, "cloth: a pinned particle never moves");

  // Two free particles on one spring, no gravity: internal forces are equal and opposite, so momentum is conserved
  // to rounding and the centre of mass moves uniformly (x_cm(t) = x_cm(0) + V t).
  ClothSystem f;
  f.gravity = Vec3(0, 0, 0);
  ClothParticle a, b;
  a.mass = 1; b.mass = 3;
  a.pos = Vec3(0, 0, 0); b.pos = Vec3(1.3, 0, 0);
  a.vel = Vec3(0.5, 0.2, 0); b.vel = Vec3(-0.1, 0, 0.4);
  f.p = {a, b};
  ClothSpring s2; s2.i = 0; s2.j = 1; s2.rest = 1.0; s2.stiffness = 30.0;
  f.springs = {s2};
  const Vec3 P0 = f.Momentum();
  const Vec3 cm0 = (f.p[0].pos * 1.0 + f.p[1].pos * 3.0) * 0.25;
  for (int i = 0; i < 5000; ++i) f.Step(1e-3);
  const Vec3 cm1 = (f.p[0].pos * 1.0 + f.p[1].pos * 3.0) * 0.25;
  Check(NearV(f.Momentum(), P0, 1e-12), "cloth: free two-particle spring system conserves linear momentum to 1e-12");
  Check(NearV(cm1, cm0 + P0 * (1.0 / 4.0) * 5.0, 1e-9), "cloth: the centre of mass moves uniformly with P/M");
}

// ---------------------------------------------------------------------------------------- FLUID-0006 / 0012 / 0013
namespace {
FluidDomain MakeDomain() {
  FluidDomain d;
  d.frame_start = 1;
  d.frame_end = 25;  // 24 intervals = 1 s at 24 fps
  d.initial = {{Vec3(0, 0, 5), Vec3(1, 0, 0)}, {Vec3(2, 1, 4), Vec3(0, 1, 2)}, {Vec3(-1, 3, 9), Vec3(0.5, -2, 0)}};
  return d;
}
}  // namespace

D8_EXTRA_TEST(RowFluidBakeParticlesFollowsBallisticMotionAndKeepsParticleCount) {
  FluidDomain d = MakeDomain();
  const int got = d.Bake(FluidCacheType::Particles, 1000);
  const FluidCache* c = d.CacheIfAny(FluidCacheType::Particles);
  Check(got == 25 && c && c->baked && !c->running && c->frames.size() == 25,
        "bake particles: frames 1..25 give 25 cached frames and the cache is marked baked");
  bool count_ok = true;
  double worst = 0;
  for (std::size_t f = 0; f < c->frames.size(); ++f) {
    count_ok = count_ok && c->frames[f].size() == 3;
    const double t = static_cast<double>(f) / 24.0;
    for (std::size_t i = 0; i < 3; ++i) {
      const Vec3 want = d.initial[i].pos + d.initial[i].vel * t + d.gravity * (0.5 * t * t);
      const Vec3 wv = d.initial[i].vel + d.gravity * t;
      worst = std::max(worst, (c->frames[f][i].pos - want).Length());
      worst = std::max(worst, (c->frames[f][i].vel - wv).Length());
    }
  }
  Check(count_ok, "bake particles: the particle count (total mass) is the same in every frame");
  Check(worst <= 1e-9, "bake particles: positions and velocities equal p0 + v0 t + g t^2/2 and v0 + g t within 1e-9 (Verlet is exact for constant g)");
  Check(d.Bake(FluidCacheType::Particles, 10) == 25, "bake particles: baking a baked cache changes nothing");

  // A floor at z = 0: a particle released at 0.5 m is on the floor with zero normal speed after the contact time
  // sqrt(2 * 0.5 / g) = 0.319 s, and nothing is ever below it.
  FluidDomain f;
  f.frame_end = 25;
  f.floor_z = 0;
  f.initial = {{Vec3(0, 0, 0.5), Vec3()}};
  f.Bake(FluidCacheType::Particles, 1000);
  const auto& fr = f.CacheIfAny(FluidCacheType::Particles)->frames;
  bool above = true;
  for (const auto& s : fr) above = above && s[0].pos.z >= 0;
  Check(above && Near(fr.back()[0].pos.z, 0, 0) && fr.back()[0].vel.z == 0,
        "bake particles: with a floor no frame has a particle below it and the particle rests on it at the end");
}

D8_EXTRA_TEST(RowFluidFreeParticlesClearsOnlyThatCache) {
  FluidDomain d = MakeDomain();
  d.Bake(FluidCacheType::Particles, 1000);
  d.Bake(FluidCacheType::Data, 1000);
  d.Bake(FluidCacheType::Mesh, 5);
  Check(d.Free(FluidCacheType::Particles) && d.CacheIfAny(FluidCacheType::Particles) == nullptr,
        "free particles: the particle cache is gone");
  const FluidCache* data = d.CacheIfAny(FluidCacheType::Data);
  Check(data && data->baked && data->frames.size() == 25, "free particles: the data cache is untouched (25 frames, baked)");
  const FluidCache* mesh = d.CacheIfAny(FluidCacheType::Mesh);
  Check(mesh && !mesh->baked && mesh->frames.size() == 6, "free particles: a partial mesh cache is untouched");
  Check(!d.Free(FluidCacheType::Particles), "free particles: freeing again reports that nothing was there");
  // Re-baking after a free starts again from the initial frame and gives the same frames.
  d.Bake(FluidCacheType::Particles, 3);
  Check(d.CacheIfAny(FluidCacheType::Particles)->frames.size() == 4 &&
            NearV(d.CacheIfAny(FluidCacheType::Particles)->frames[0][0].pos, d.initial[0].pos, 0),
        "free particles: baking after the free restarts at the initial state");
  d.FreeAll();
  Check(d.CacheIfAny(FluidCacheType::Data) == nullptr && d.CacheIfAny(FluidCacheType::Mesh) == nullptr,
        "free all: every cache is cleared");
}

D8_EXTRA_TEST(RowFluidPauseBakeKeepsFramesAndResumesToTheSameResult) {
  FluidDomain ref = MakeDomain();
  ref.Bake(FluidCacheType::Particles, 1000);

  FluidDomain d = MakeDomain();
  Check(!d.PauseBake(FluidCacheType::Particles), "pause bake: nothing to pause before a bake has started");
  const int part = d.Bake(FluidCacheType::Particles, 10);
  Check(part == 11 && d.CacheIfAny(FluidCacheType::Particles)->running && !d.CacheIfAny(FluidCacheType::Particles)->baked,
        "pause bake: a bake of 10 frames is running and not finished");
  Check(d.PauseBake(FluidCacheType::Particles) && d.CacheIfAny(FluidCacheType::Particles)->paused &&
            !d.CacheIfAny(FluidCacheType::Particles)->running && d.CacheIfAny(FluidCacheType::Particles)->frames.size() == 11,
        "pause bake: the bake stops and the 11 frames made so far are kept");
  Check(d.Bake(FluidCacheType::Particles, 1000) == 25 && d.CacheIfAny(FluidCacheType::Particles)->baked &&
            !d.CacheIfAny(FluidCacheType::Particles)->paused,
        "pause bake: baking again resumes and finishes at frame 25");
  const auto& a = d.CacheIfAny(FluidCacheType::Particles)->frames;
  const auto& b = ref.CacheIfAny(FluidCacheType::Particles)->frames;
  bool same = a.size() == b.size();
  for (std::size_t f = 0; same && f < a.size(); ++f)
    for (std::size_t i = 0; i < a[f].size(); ++i)
      same = same && a[f][i].pos.x == b[f][i].pos.x && a[f][i].pos.y == b[f][i].pos.y && a[f][i].pos.z == b[f][i].pos.z &&
             a[f][i].vel.z == b[f][i].vel.z;
  Check(same, "pause bake: the paused and resumed bake equals the uninterrupted bake bit for bit");
}

// ---------------------------------------------------------------------------------------- PARTICLE-0023 select all
D8_EXTRA_TEST(RowParticleSelectAllActionsCountVisibleKeysOnly) {
  HairSystem h;
  for (int i = 0; i < 3; ++i) h.strands.push_back(Strand({Vec3(i, 0, 0), Vec3(i, 0, 1), Vec3(i, 0, 2), Vec3(i, 0, 3)}, false));
  h.strands[1].keys[2].hidden = true;  // 12 keys, 11 visible
  h.SelectAll(SelectAction::Toggle);
  Check(h.CountSelected() == 11 && !h.strands[1].keys[2].selected, "select all: toggle with nothing selected selects the 11 visible keys, not the hidden one");
  h.SelectAll(SelectAction::Toggle);
  Check(h.CountSelected() == 0, "select all: toggle with a selection deselects everything");
  h.strands[0].keys[0].selected = true;
  h.strands[0].keys[1].selected = true;
  h.SelectAll(SelectAction::Invert);
  Check(h.CountSelected() == 9 && !h.strands[0].keys[0].selected && h.strands[2].keys[3].selected,
        "select all: invert flips the 2 selected keys to 9 selected of 11 visible");
  h.SelectAll(SelectAction::Select);
  Check(h.CountSelected() == 11, "select all: select makes all 11 visible keys selected");
  h.SelectAll(SelectAction::Deselect);
  Check(h.CountSelected() == 0, "select all: deselect clears them");
}

// ---------------------------------------------------------------------------------------- PARTICLE-0029 select roots
D8_EXTRA_TEST(RowParticleSelectRootsSelectsKeyZeroOfEachVisibleStrand) {
  HairSystem h;
  for (int i = 0; i < 4; ++i) h.strands.push_back(Strand({Vec3(i, 0, 0), Vec3(i, 0, 1), Vec3(i, 0, 2)}, false));
  h.strands[3].keys[0].hidden = true;
  const int n = h.SelectRoots();
  bool only_roots = true;
  for (auto& s : h.strands) only_roots = only_roots && !s.keys[1].selected && !s.keys[2].selected;
  Check(n == 3 && h.CountSelected() == 3 && only_roots && h.strands[0].keys[0].selected && !h.strands[3].keys[0].selected,
        "select roots: key 0 of each of the 3 strands with a visible root, no other key");
  const int t = h.SelectTips();
  Check(t == 4 && h.CountSelected() == 7 && h.strands[3].keys[2].selected, "select tips: the 4 last keys are added to the selection");
}

// ---------------------------------------------------------------------------------------- PARTICLE-0020 rekey
D8_EXTRA_TEST(RowParticleRekeyResamplesAlongTheOriginalPolylineEvenly) {
  HairSystem h;
  h.strands.push_back(Strand({Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(0, 0, 3)}, true));  // straight, uneven keys
  h.strands.push_back(Strand({Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0)}, true));  // L shape, length 2
  h.strands.push_back(Strand({Vec3(5, 5, 0), Vec3(5, 5, 1)}, false));                // not selected
  HairSystem h2;
  h2.strands.push_back(h.strands[1]);  // the L shape alone, so it is resampled once from its original keys
  h.strands.erase(h.strands.begin() + 1);
  h.Rekey(4);
  const auto& a = h.strands[0].keys;
  bool ok = a.size() == 4;
  for (int i = 0; ok && i < 4; ++i) ok = NearV(a[static_cast<std::size_t>(i)].pos, Vec3(0, 0, i), 1e-12);
  Check(ok, "rekey: a straight strand of length 3 with keys at 0, 1, 3 becomes 4 keys at z = 0, 1, 2, 3");
  h2.Rekey(5);
  const auto& b = h2.strands[0].keys;
  const Vec3 want[5] = {Vec3(0, 0, 0), Vec3(0.5, 0, 0), Vec3(1, 0, 0), Vec3(1, 0.5, 0), Vec3(1, 1, 0)};
  bool ok2 = b.size() == 5;
  for (int i = 0; ok2 && i < 5; ++i) ok2 = NearV(b[static_cast<std::size_t>(i)].pos, want[i], 1e-12);
  Check(ok2, "rekey: the L-shaped strand of length 2 gets 5 keys 0.5 apart along its path, corner included");
  Check(Near(HairSystem::Length(h2.strands[0]), 2.0, 1e-12), "rekey: the polyline length is unchanged when the corner is a new key");
  Check(h.strands[1].keys.size() == 2 && NearV(h.strands[1].keys[1].pos, Vec3(5, 5, 1), 0), "rekey: a strand with no selected key is untouched");
  h.Rekey(1);
  Check(h.strands[0].keys.size() == 2 && NearV(h.strands[0].keys[1].pos, Vec3(0, 0, 3), 1e-12),
        "rekey: fewer than 2 keys is raised to 2 (root and tip)");
}

// ---------------------------------------------------------------------------------------- PARTICLE-0032 subdivide
D8_EXTRA_TEST(RowParticleSubdivideInsertsMidpointsBetweenSelectedNeighbours) {
  HairSystem h;
  h.strands.push_back(Strand({Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 2, 0), Vec3(1, 2, 4)}, true));
  const double L0 = HairSystem::Length(h.strands[0]);
  Check(h.Subdivide() == 3 && h.strands[0].keys.size() == 7, "subdivide: 4 selected keys become 2*4 - 1 = 7");
  const auto& k = h.strands[0].keys;
  Check(NearV(k[1].pos, Vec3(0.5, 0, 0), 1e-15) && NearV(k[3].pos, Vec3(1, 1, 0), 1e-15) && NearV(k[5].pos, Vec3(1, 2, 2), 1e-15),
        "subdivide: the new keys are the midpoints (0.5,0,0), (1,1,0), (1,2,2)");
  Check(Near(HairSystem::Length(h.strands[0]), L0, 1e-12) && L0 == 7.0, "subdivide: the polyline length (7) is unchanged");
  bool all_sel = true;
  for (auto& key : k) all_sel = all_sel && key.selected;
  Check(all_sel, "subdivide: the new keys are selected");
  HairSystem p;
  HairStrand s = Strand({Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(0, 0, 2), Vec3(0, 0, 3)}, false);
  s.keys[1].selected = s.keys[2].selected = true;
  p.strands.push_back(s);
  Check(p.Subdivide() == 1 && p.strands[0].keys.size() == 5 && NearV(p.strands[0].keys[2].pos, Vec3(0, 0, 1.5), 1e-15),
        "subdivide: only the segment between the 2 selected keys gets a midpoint");
}

// ---------------------------------------------------------------------------------------- PARTICLE-0036 unify length
D8_EXTRA_TEST(RowParticleUnifyLengthScalesSelectedStrandsToTheirMeanLength) {
  HairSystem h;
  h.strands.push_back(Strand({Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(0, 0, 2)}, true));            // length 2
  h.strands.push_back(Strand({Vec3(1, 0, 0), Vec3(1, 2, 0), Vec3(1, 4, 0)}, true));            // length 4
  h.strands.push_back(Strand({Vec3(2, 0, 0), Vec3(2, 3, 3), Vec3(2, 6, 6)}, true));            // length 6 sqrt 2
  h.strands.push_back(Strand({Vec3(9, 9, 9), Vec3(9, 9, 10)}, false));                         // not selected
  const double L2 = 6.0 * std::sqrt(2.0);
  const double avg = (2.0 + 4.0 + L2) / 3.0;
  h.UnifyLength();
  bool lens = true, roots = true;
  for (int i = 0; i < 3; ++i) lens = lens && Near(HairSystem::Length(h.strands[static_cast<std::size_t>(i)]), avg, 1e-12);
  roots = NearV(h.strands[0].keys[0].pos, Vec3(0, 0, 0), 0) && NearV(h.strands[1].keys[0].pos, Vec3(1, 0, 0), 0) &&
          NearV(h.strands[2].keys[0].pos, Vec3(2, 0, 0), 0);
  Check(lens, "unify length: every selected strand has the mean length (2 + 4 + 6 sqrt2) / 3");
  Check(roots, "unify length: roots do not move");
  const Vec3 d1 = h.strands[2].keys[2].pos - h.strands[2].keys[0].pos;
  Check(Near(d1.y, d1.z, 1e-12) && Near(d1.x, 0, 0) && Near(h.strands[1].keys[1].pos.y * 2, h.strands[1].keys[2].pos.y, 1e-12),
        "unify length: directions and key spacing ratios are preserved (uniform scale about the root)");
  Check(NearV(h.strands[3].keys[1].pos, Vec3(9, 9, 10), 0), "unify length: a strand with no selected key is untouched");
}

// ---------------------------------------------------------------------------------------- PARTICLE-0015 mirror
D8_EXTRA_TEST(RowParticleMirrorWritesReflectedKeysOntoThePairedStrand) {
  HairSystem h;
  h.strands.push_back(Strand({Vec3(1, 0, 0), Vec3(1.5, 0.2, 1), Vec3(2, 0.5, 2)}, true));    // source
  h.strands.push_back(Strand({Vec3(-1, 0, 0), Vec3(-1, 0, 1), Vec3(-1, 0, 2)}, false));       // partner
  h.strands.push_back(Strand({Vec3(0, 5, 0), Vec3(0, 5, 1), Vec3(0, 5, 2)}, false));          // unrelated
  h.strands.push_back(Strand({Vec3(1, 3, 0), Vec3(1, 3, 1), Vec3(1, 3, 2)}, true));           // selected, no partner at (-1,3,0)
  const HairStrand src = h.strands[0];
  const HairStrand other = h.strands[2];
  Check(h.MirrorX(1e-6) == 1, "mirror: exactly one strand has a partner and is written");
  bool ok = true;
  for (std::size_t i = 0; i < 3; ++i)
    ok = ok && NearV(h.strands[1].keys[i].pos, Vec3(-src.keys[i].pos.x, src.keys[i].pos.y, src.keys[i].pos.z), 1e-15);
  Check(ok, "mirror: the partner's keys are the source keys with x negated");
  bool same = true;
  for (std::size_t i = 0; i < 3; ++i) same = same && NearV(h.strands[0].keys[i].pos, src.keys[i].pos, 0);
  Check(same, "mirror: the source strand is unchanged");
  bool un = true;
  for (std::size_t i = 0; i < 3; ++i) un = un && NearV(h.strands[2].keys[i].pos, other.keys[i].pos, 0);
  Check(un && NearV(h.strands[3].keys[1].pos, Vec3(1, 3, 1), 0), "mirror: unrelated strands and a source without a partner are unchanged");
}

// ---------------------------------------------------------------------------------------- TEXT-0008 / 0011 / 0041 / 0002 / 0020
D8_EXTRA_TEST(RowTextDuplicateLineCopiesTheCursorLineBelow) {
  TextBuffer t({"import bpy", "x = 1", "y = 2"});
  t.cursor_line = 1;
  t.DuplicateLine();
  Check(t.lines.size() == 4 && t.lines[1] == "x = 1" && t.lines[2] == "x = 1" && t.lines[3] == "y = 2" && t.lines[0] == "import bpy",
        "duplicate line: line 1 appears twice, the other lines keep their order");
  Check(t.cursor_line == 2, "duplicate line: the cursor moves onto the copy");
  TextBuffer one;
  one.lines[0] = "only";
  one.DuplicateLine();
  Check(one.lines.size() == 2 && one.lines[0] == "only" && one.lines[1] == "only", "duplicate line: a one-line text becomes two identical lines");
  TextBuffer e({"", "a"});
  e.cursor_line = 0;
  e.DuplicateLine();
  Check(e.lines.size() == 3 && e.lines[0].empty() && e.lines[1].empty() && e.lines[2] == "a", "duplicate line: an empty line is duplicated too");
}

D8_EXTRA_TEST(RowTextIndentAddsOneUnitToEverySelectedLine) {
  TextBuffer t({"a", "b", "", "c", "d"});
  t.SelectLines(1, 3);
  t.Indent();
  Check(t.lines[0] == "a" && t.lines[1] == "    b" && t.lines[2] == "    " && t.lines[3] == "    c" && t.lines[4] == "d",
        "indent: lines 1..3 each gain 4 spaces (the empty line too), lines 0 and 4 are unchanged");
  TextBuffer u({"x", "y"});
  u.tabs_as_spaces = false;
  u.SelectLines(0, 1);
  u.Indent();
  Check(u.lines[0] == "\tx" && u.lines[1] == "\ty", "indent: with tabs instead of spaces one tab is added");
  t.Indent();
  Check(t.lines[1] == "        b", "indent: indenting twice adds two units (8 spaces)");
  Check(t.Joined() == std::string("a\n        b\n        \n        c\nd"), "indent: the joined text has the same line count and order");
}

D8_EXTRA_TEST(RowTextUnindentRemovesAtMostOneUnitAndInvertsIndent) {
  TextBuffer t({"a", "  b", "      c", "\td", "e"});
  t.SelectLines(1, 3);
  t.Unindent();
  Check(t.lines[0] == "a" && t.lines[1] == "b" && t.lines[2] == "  c" && t.lines[3] == "d" && t.lines[4] == "e",
        "unindent: 2 spaces go, 6 spaces lose 4, a tab goes, lines outside the selection are unchanged");
  TextBuffer r({"p", "q", "  r", "s"});
  const std::string before = r.Joined();
  r.SelectLines(0, 3);
  r.Indent();
  r.Unindent();
  Check(r.Joined() == before, "unindent: Unindent after Indent restores the exact text");
  TextBuffer z({"abc"});
  z.SelectLines(0, 0);
  z.Unindent();
  Check(z.lines[0] == "abc", "unindent: a line without leading whitespace is unchanged");
}

D8_EXTRA_TEST(RowTextToggleCommentsAddsAndRemovesAHashAndIsAnInvolution) {
  TextBuffer t({"a = 1", "b = 2", "c = 3"});
  const std::string before = t.Joined();
  t.SelectLines(0, 1);
  t.CommentToggle();
  Check(t.lines[0] == "#a = 1" && t.lines[1] == "#b = 2" && t.lines[2] == "c = 3",
        "toggle comments: the 2 selected lines get a leading # and the third is unchanged");
  t.CommentToggle();
  Check(t.Joined() == before, "toggle comments: toggling twice restores the text");
  t.lines[1] = "b = 2";
  t.lines[0] = "#a = 1";
  t.SelectLines(0, 1);
  t.CommentToggle();
  Check(t.lines[0] == "##a = 1" && t.lines[1] == "#b = 2", "toggle comments: with a mix of commented and plain lines every selected line gains one #");
  // A line starting with '#' is a Python comment and no statement: the property that makes the marker meaningful.
  bool all_hash = true;
  for (int i = 0; i <= 1; ++i) all_hash = all_hash && t.lines[static_cast<std::size_t>(i)][0] == '#';
  Check(all_hash, "toggle comments: every commented line starts with '#', which Python reads as a comment");
  t.CommentToggle();
  Check(t.lines[0] == "#a = 1" && t.lines[1] == "b = 2", "toggle comments: the toggle is an involution on mixed selections too");
}

D8_EXTRA_TEST(RowTextMoveLinesShiftsTheSelectedBlockByOne) {
  TextBuffer t({"l0", "l1", "l2", "l3", "l4"});
  t.SelectLines(1, 2);
  t.cursor_line = 1;
  Check(t.MoveLines(true) && t.lines == std::vector<std::string>({"l1", "l2", "l0", "l3", "l4"}) && t.sel_first == 0 && t.sel_last == 1 && t.cursor_line == 0,
        "move lines: moving lines 1..2 up gives l1 l2 l0 l3 l4 and the selection follows");
  Check(!t.MoveLines(true) && t.lines[0] == "l1", "move lines: at the top moving up does nothing");
  Check(t.MoveLines(false) && t.lines == std::vector<std::string>({"l0", "l1", "l2", "l3", "l4"}),
        "move lines: moving down again restores the original order");
  t.SelectLines(3, 4);
  Check(!t.MoveLines(false) && t.lines[4] == "l4", "move lines: at the bottom moving down does nothing");
  t.SelectLines(3, 3);
  t.cursor_line = 3;
  Check(t.MoveLines(true) && t.lines == std::vector<std::string>({"l0", "l1", "l3", "l2", "l4"}) && t.cursor_line == 2,
        "move lines: a single line moves up past its neighbour");
}
