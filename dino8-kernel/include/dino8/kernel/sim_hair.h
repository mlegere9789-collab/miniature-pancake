#pragma once
// Hair (particle edit mode) data model for the Blender particle rows (bpy.ops.particle.*).
//
// A strand is an ordered list of keys; key 0 is the root. Selection and hide flags live on keys. The operations
// below work on that model and are defined by their effect on geometry and selection counts, so each can be
// checked against a closed form (counts, lengths, positions on the original polyline). Header-only.
//
// Where Blender's own definition is not known precisely (how Unify Length picks the common length, how Mirror
// pairs strands), the choice made here is documented at the function; it is not claimed to be Blender's.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "dino8/kernel/sim_rigid.h"

namespace dino8 {
namespace sim {

struct HairKey {
  Vec3 pos;
  bool selected = false;
  bool hidden = false;
  double weight = 0.0;
};
struct HairStrand {
  std::vector<HairKey> keys;
};

enum class SelectAction { Toggle, Select, Deselect, Invert };

class HairSystem {
 public:
  std::vector<HairStrand> strands;

  static double Length(const HairStrand& s) {
    double L = 0;
    for (std::size_t i = 1; i < s.keys.size(); ++i) L += (s.keys[i].pos - s.keys[i - 1].pos).Length();
    return L;
  }
  int CountSelected() const {
    int n = 0;
    for (auto& s : strands) for (auto& k : s.keys) if (k.selected && !k.hidden) ++n;
    return n;
  }

  // (De)select All. Toggle: deselect everything when any visible key is selected, else select everything.
  // Hidden keys are never changed.
  void SelectAll(SelectAction a) {
    if (a == SelectAction::Toggle) a = CountSelected() > 0 ? SelectAction::Deselect : SelectAction::Select;
    for (auto& s : strands)
      for (auto& k : s.keys) {
        if (k.hidden) continue;
        k.selected = (a == SelectAction::Select) ? true : (a == SelectAction::Deselect) ? false : !k.selected;
      }
  }
  // Select Roots: key 0 of every strand (visible ones). Select action only adds, never clears others.
  int SelectRoots() {
    int n = 0;
    for (auto& s : strands)
      if (!s.keys.empty() && !s.keys[0].hidden) { s.keys[0].selected = true; ++n; }
    return n;
  }
  // Select Tips: the last key of every strand.
  int SelectTips() {
    int n = 0;
    for (auto& s : strands)
      if (!s.keys.empty() && !s.keys.back().hidden) { s.keys.back().selected = true; ++n; }
    return n;
  }
  // Select More: every visible key adjacent (along its strand) to a selected key becomes selected.
  void SelectMore() {
    for (auto& s : strands) {
      std::vector<char> was(s.keys.size());
      for (std::size_t i = 0; i < s.keys.size(); ++i) was[i] = s.keys[i].selected;
      for (std::size_t i = 0; i < s.keys.size(); ++i) {
        if (s.keys[i].hidden) continue;
        if ((i > 0 && was[i - 1]) || (i + 1 < s.keys.size() && was[i + 1])) s.keys[i].selected = true;
      }
    }
  }
  // Select Less: a selected key stays selected only when both neighbours are selected (ends have one missing
  // neighbour and are therefore deselected whenever any selection touches them).
  void SelectLess() {
    for (auto& s : strands) {
      std::vector<char> was(s.keys.size());
      for (std::size_t i = 0; i < s.keys.size(); ++i) was[i] = s.keys[i].selected;
      for (std::size_t i = 0; i < s.keys.size(); ++i) {
        if (!was[i]) continue;
        const bool l = i > 0 && was[i - 1], r = i + 1 < s.keys.size() && was[i + 1];
        if (!(l && r)) s.keys[i].selected = false;
      }
    }
  }

  // Select Linked All: every visible key of a strand that has a selected key becomes selected.
  void SelectLinked() {
    for (auto& s : strands) {
      bool any = false;
      for (auto& k : s.keys) any = any || (k.selected && !k.hidden);
      if (!any) continue;
      for (auto& k : s.keys) if (!k.hidden) k.selected = true;
    }
  }

  // Select Random: every visible key (type Points) or strand (type Hair: all its visible keys) is selected
  // independently with probability `ratio`, drawn from a counter-based hash of (seed, strand, key), so the same seed
  // gives the same selection. select == false deselects the chosen ones instead. Returns the number of keys changed
  // to the requested state by the draw (chosen keys, whether or not they already were in that state).
  enum class RandomType { Points, Hair };
  static unsigned long long Mix(unsigned long long x) {  // splitmix64 finaliser
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
  }
  static double Unit(unsigned long long seed, unsigned long long a, unsigned long long b) {
    const unsigned long long h = Mix(Mix(seed * 0x100000001B3ull + a) + b);
    return static_cast<double>(h >> 11) / 9007199254740992.0;  // 53 bits -> [0, 1)
  }
  int SelectRandom(double ratio, unsigned long long seed, RandomType type, bool select = true) {
    int n = 0;
    for (std::size_t si = 0; si < strands.size(); ++si) {
      auto& s = strands[si];
      const bool strand_pick = Unit(seed, si, 0xFFFFFFFFull) < ratio;
      for (std::size_t ki = 0; ki < s.keys.size(); ++ki) {
        auto& k = s.keys[ki];
        if (k.hidden) continue;
        const bool pick = (type == RandomType::Hair) ? strand_pick : Unit(seed, si, ki) < ratio;
        if (pick) { k.selected = select; ++n; }
      }
    }
    return n;
  }

  // Hide Selected (unselected == false) or Hide Unselected: matching visible keys become hidden and deselected.
  int Hide(bool unselected = false) {
    int n = 0;
    for (auto& s : strands)
      for (auto& k : s.keys) {
        if (k.hidden || k.selected == unselected) continue;
        k.hidden = true;
        k.selected = false;
        ++n;
      }
    return n;
  }
  // Reveal: every hidden key becomes visible; with `select` the revealed keys are selected as well.
  int Reveal(bool select = true) {
    int n = 0;
    for (auto& s : strands)
      for (auto& k : s.keys)
        if (k.hidden) { k.hidden = false; if (select) k.selected = true; ++n; }
    return n;
  }

  // Delete: type Particle removes every strand that has a selected key; type Key removes the selected keys and then
  // every strand left with fewer than 2 keys (a strand needs a root and one more key; this rule is the model's
  // definition). Returns the number of keys removed in total (keys of removed strands included).
  enum class DeleteType { Particle, Key };
  int Delete(DeleteType type) {
    int removed = 0;
    std::vector<HairStrand> out;
    for (auto& s : strands) {
      bool any = false;
      for (auto& k : s.keys) any = any || (k.selected && !k.hidden);
      if (!any) { out.push_back(s); continue; }
      if (type == DeleteType::Particle) { removed += static_cast<int>(s.keys.size()); continue; }
      HairStrand kept;
      for (auto& k : s.keys) if (!(k.selected && !k.hidden)) kept.keys.push_back(k);
      removed += static_cast<int>(s.keys.size() - kept.keys.size());
      if (kept.keys.size() >= 2) out.push_back(std::move(kept));
      else removed += static_cast<int>(kept.keys.size());
    }
    strands = std::move(out);
    return removed;
  }

  // Remove Doubles: a strand with a selected key is removed when the root of an earlier strand that survives is
  // closer than `threshold` to its root (earlier = lower index; the first of a cluster is kept). Returns the number
  // of strands removed.
  int RemoveDoubles(double threshold) {
    std::vector<HairStrand> out;
    int removed = 0;
    for (auto& s : strands) {
      bool any = false;
      for (auto& k : s.keys) any = any || (k.selected && !k.hidden);
      bool dup = false;
      if (any && !s.keys.empty())
        for (auto& o : out)
          if (!o.keys.empty() && (o.keys[0].pos - s.keys[0].pos).Length() < threshold) { dup = true; break; }
      if (dup) ++removed; else out.push_back(s);
    }
    strands = std::move(out);
    return removed;
  }

  // Subdivide: between every pair of adjacent selected keys insert a new selected key at their midpoint.
  // A strand whose n keys are all selected becomes 2n - 1 keys; the polyline (shape and length) is unchanged.
  int Subdivide() {
    int added = 0;
    for (auto& s : strands) {
      std::vector<HairKey> out;
      for (std::size_t i = 0; i < s.keys.size(); ++i) {
        out.push_back(s.keys[i]);
        if (i + 1 < s.keys.size() && s.keys[i].selected && s.keys[i + 1].selected) {
          HairKey m;
          m.pos = (s.keys[i].pos + s.keys[i + 1].pos) * 0.5;
          m.selected = true;
          m.weight = 0.5 * (s.keys[i].weight + s.keys[i + 1].weight);
          out.push_back(m);
          ++added;
        }
      }
      s.keys = std::move(out);
    }
    return added;
  }

  // Rekey: resample every strand that has a selected key to `n` (>= 2) keys equally spaced in arc length along its
  // original polyline; root and tip are kept. The new keys lie on the original polyline.
  void Rekey(int n) {
    if (n < 2) n = 2;
    for (auto& s : strands) {
      bool any = false;
      for (auto& k : s.keys) any = any || k.selected;
      if (!any || s.keys.size() < 2) continue;
      const double L = Length(s);
      std::vector<HairKey> out(static_cast<std::size_t>(n));
      std::size_t seg = 1;
      double acc = 0;  // arc length at keys[seg-1]
      for (int k = 0; k < n; ++k) {
        const double target = L * k / (n - 1);
        while (seg + 1 < s.keys.size() && acc + (s.keys[seg].pos - s.keys[seg - 1].pos).Length() < target) {
          acc += (s.keys[seg].pos - s.keys[seg - 1].pos).Length();
          ++seg;
        }
        const double sl = (s.keys[seg].pos - s.keys[seg - 1].pos).Length();
        const double t = sl > 0 ? std::min(1.0, std::max(0.0, (target - acc) / sl)) : 0.0;
        out[static_cast<std::size_t>(k)].pos = s.keys[seg - 1].pos + (s.keys[seg].pos - s.keys[seg - 1].pos) * t;
        out[static_cast<std::size_t>(k)].selected = true;
      }
      s.keys = std::move(out);
    }
  }

  // Unify Length: every strand with a selected key is scaled about its root so that its length equals the AVERAGE
  // length of those strands (uniform scaling: shape and direction are preserved, roots do not move). The choice
  // of the average is this model's definition, not a verified statement about Blender.
  void UnifyLength() {
    double sum = 0;
    int n = 0;
    for (auto& s : strands) {
      bool any = false;
      for (auto& k : s.keys) any = any || k.selected;
      if (any) { sum += Length(s); ++n; }
    }
    if (n == 0) return;
    const double avg = sum / n;
    for (auto& s : strands) {
      bool any = false;
      for (auto& k : s.keys) any = any || k.selected;
      const double L = Length(s);
      if (!any || L <= 0) continue;
      const double f = avg / L;
      const Vec3 root = s.keys[0].pos;
      for (std::size_t i = 1; i < s.keys.size(); ++i) s.keys[i].pos = root + (s.keys[i].pos - root) * f;
    }
  }

  // Mirror (across the plane x = 0): for each strand with all keys selected, find another strand with the same key
  // count whose root lies within `tol` of the mirrored root and whose keys are not selected; overwrite that
  // strand's keys with the mirrored keys (x -> -x). The source is unchanged. Strands without a partner are left
  // alone. Returns the number of strands written. (Pairing rule is this model's definition.)
  int MirrorX(double tol) {
    int written = 0;
    for (std::size_t a = 0; a < strands.size(); ++a) {
      const HairStrand& src = strands[a];
      if (src.keys.empty()) continue;
      bool all = true;
      for (auto& k : src.keys) all = all && k.selected;
      if (!all) continue;
      const Vec3 mr(-src.keys[0].pos.x, src.keys[0].pos.y, src.keys[0].pos.z);
      for (std::size_t b = 0; b < strands.size(); ++b) {
        if (b == a || strands[b].keys.size() != src.keys.size()) continue;
        bool sel = false;
        for (auto& k : strands[b].keys) sel = sel || k.selected;
        if (sel || (strands[b].keys[0].pos - mr).Length() > tol) continue;
        for (std::size_t i = 0; i < src.keys.size(); ++i)
          strands[b].keys[i].pos = Vec3(-src.keys[i].pos.x, src.keys[i].pos.y, src.keys[i].pos.z);
        ++written;
        break;
      }
    }
    return written;
  }
};

}  // namespace sim
}  // namespace dino8
