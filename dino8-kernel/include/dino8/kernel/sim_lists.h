#pragma once
// Ordered lists with an active item (boid rules and states, particle instance objects and targets, point caches):
// the data model behind the Add / Remove / Move Up / Move Down operators of the Blender simulation panels.
//
// The operations are defined by list algebra: Add appends and activates; Remove erases the active item (the order
// of the others is kept); Move Up / Move Down swap the active item with its neighbour and the active index follows;
// at the ends a move changes nothing. Header-only, no openNURBS.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace dino8 {
namespace sim {

template <class T>
struct ActiveList {
  std::vector<T> items;
  int active = -1;

  int Size() const { return static_cast<int>(items.size()); }
  T& Add(T v) {
    items.push_back(std::move(v));
    active = Size() - 1;
    return items.back();
  }
  // Removes the active item; the active index then points at the item that moved into its place (or the last one).
  bool Remove() {
    if (active < 0 || active >= Size()) return false;
    items.erase(items.begin() + active);
    active = items.empty() ? -1 : std::min(active, Size() - 1);
    return true;
  }
  bool MoveUp() {
    if (active <= 0 || active >= Size()) return false;
    std::swap(items[static_cast<std::size_t>(active)], items[static_cast<std::size_t>(active) - 1]);
    --active;
    return true;
  }
  bool MoveDown() {
    if (active < 0 || active + 1 >= Size()) return false;
    std::swap(items[static_cast<std::size_t>(active)], items[static_cast<std::size_t>(active) + 1]);
    ++active;
    return true;
  }
};

enum class BoidRuleType { Goal, Avoid, AvoidCollision, Separate, Flock, FollowLeader, AverageSpeed, Fight };

struct BoidRule {
  BoidRuleType type = BoidRuleType::Goal;
  int serial = 0;  // creation number, lets tests identify a rule independently of its position
};

struct BoidState {
  std::string name;
  ActiveList<BoidRule> rules;
};

// Boid behaviour: states, each with an ordered rule list. A new settings block has one state and no rules (the
// model's definition). Rule operators act on the active state's list. State names are kept unique with a
// ".001" style suffix; the last remaining state cannot be removed (the model's definition).
class BoidSettings {
 public:
  ActiveList<BoidState> states;
  BoidSettings() { AddState(); }

  BoidState* ActiveState() {
    return (states.active >= 0 && states.active < states.Size()) ? &states.items[static_cast<std::size_t>(states.active)] : nullptr;
  }
  int AddState() {
    BoidState s;
    s.name = UniqueName("State");
    states.Add(std::move(s));
    return states.active;
  }
  bool RemoveState() {
    if (states.Size() <= 1) return false;
    return states.Remove();
  }
  bool MoveStateUp() { return states.MoveUp(); }
  bool MoveStateDown() { return states.MoveDown(); }

  bool AddRule(BoidRuleType t) {
    BoidState* s = ActiveState();
    if (!s) return false;
    BoidRule r;
    r.type = t;
    r.serial = ++serial_;
    s->rules.Add(r);
    return true;
  }
  bool RemoveRule() { BoidState* s = ActiveState(); return s && s->rules.Remove(); }
  bool MoveRuleUp() { BoidState* s = ActiveState(); return s && s->rules.MoveUp(); }
  bool MoveRuleDown() { BoidState* s = ActiveState(); return s && s->rules.MoveDown(); }

 private:
  std::string UniqueName(const std::string& base) const {
    auto taken = [&](const std::string& n) {
      for (auto& s : states.items) if (s.name == n) return true;
      return false;
    };
    if (!taken(base)) return base;
    for (int i = 1;; ++i) {
      char buf[16];
      std::snprintf(buf, sizeof buf, ".%03d", i);
      if (!taken(base + buf)) return base + buf;
    }
  }
  int serial_ = 0;
};

}  // namespace sim
}  // namespace dino8
