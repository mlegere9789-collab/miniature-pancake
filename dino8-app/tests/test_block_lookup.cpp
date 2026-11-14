// Unit test for the dynamic-block Lookup parameter (doc/BlockInstances.h/
// .cpp): SetBlockInstanceLookup should match a placed instance's own input
// key against its block definition's lookup_keys/lookup_states table to
// pick which visibility state to rebuild with, fall back to the instance's
// own explicit `state` on an empty or unmatched key, persist across a
// Save/LoadBlockInstances round trip like every other BlockInstance field,
// and be a no-op (falls back to `state`) on a definition that never
// configured a lookup table - the same "not configured yet" no-op contract
// Flip/Array already have. See PARITY_MAP.md's "Dynamic blocks" item,
// which this closes the Lookup (fourth and last) half of.
#include <cmath>
#include <cstdio>

#include "doc/BlockInstances.h"
#include "doc/Document.h"

using dino8::app::BlockDefinition;
using dino8::app::BlockInstance;
using dino8::app::Document;
using dino8::app::FindBlockInstanceByGroup;
using dino8::app::InstantiateDynamicBlock;
using dino8::app::LoadBlockInstances;
using dino8::app::ResolveLookupState;
using dino8::app::SceneObject;
using dino8::app::SetBlockInstanceLookup;
using dino8::kernel::Point3d;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }
}  // namespace

int main() {
  // --- Pure ResolveLookupState: no table at all, a table with a matching
  // row, and a table with no matching row.
  {
    BlockDefinition def;
    Check(ResolveLookupState(def, "S", "Fallback") == "Fallback", "no table at all: always the fallback, regardless of key");
    Check(ResolveLookupState(def, "", "Fallback") == "Fallback", "empty key with no table: still the fallback");

    def.lookup_keys = {"S", "M", "L"};
    def.lookup_states = {"Small", "Medium", "Large"};
    Check(ResolveLookupState(def, "M", "Fallback") == "Medium", "a matching key resolves to its row's state, not the fallback");
    Check(ResolveLookupState(def, "Z", "Fallback") == "Fallback", "an unmatched key falls back");
    Check(ResolveLookupState(def, "", "Fallback") == "Fallback", "an empty key falls back even when a table exists");
  }

  // --- No lookup table configured on the definition: setting a key must
  // still place the instance's own explicit state (a pure no-op, like
  // Array's count on a definition with no configured spacing).
  {
    Document doc;
    BlockDefinition def;
    def.name = "Unconfigured";
    def.base = Point3d(0, 0, 0);
    def.states = {"A"};
    def.objects.push_back(SceneObject::MakePoint(Point3d(1, 0, 0)));
    doc.Blocks().push_back(def);

    const int group = InstantiateDynamicBlock(doc, "Unconfigured", Point3d(0, 0, 0));
    Check(group >= 0, "InstantiateDynamicBlock placed the instance");
    Check(SetBlockInstanceLookup(doc, group, "AnyKey"), "SetBlockInstanceLookup succeeds even with no table configured");
    BlockInstance inst;
    Check(FindBlockInstanceByGroup(doc, group, inst), "the record is still findable");
    Check(inst.lookup_key == "AnyKey", "the key itself is stored as requested");
    Check(inst.objects.size() == 1, "exactly one object still placed - the instance's own state (A), unaffected");
  }

  // --- A configured lookup table over two states, each with distinct
  // geometry (so which one got placed is directly observable).
  Document doc;
  BlockDefinition def;
  def.name = "LookTest";
  def.base = Point3d(0, 0, 0);
  def.states = {"Small", "Large"};
  def.lookup_keys = {"S", "L"};
  def.lookup_states = {"Small", "Large"};
  SceneObject small_pt = SceneObject::MakePoint(Point3d(1, 0, 0));
  small_pt.user_text[dino8::app::kBlockVisStatesKey] = "Small";
  SceneObject large_pt = SceneObject::MakePoint(Point3d(2, 0, 0));
  large_pt.user_text[dino8::app::kBlockVisStatesKey] = "Large";
  def.objects.push_back(small_pt);
  def.objects.push_back(large_pt);
  doc.Blocks().push_back(def);

  const int group = InstantiateDynamicBlock(doc, "LookTest", Point3d(10, 0, 0));
  Check(group >= 0, "InstantiateDynamicBlock placed the instance");

  BlockInstance inst;
  Check(FindBlockInstanceByGroup(doc, group, inst), "a BlockInstance record exists for the new group");
  Check(inst.state == "Small", "a freshly placed instance defaults to the definition's first state");
  Check(inst.objects.size() == 1, "only the Small-tagged point was placed");
  {
    const SceneObject* o = doc.Find(inst.objects[0]);
    Check(o != nullptr, "the built object is findable");
    if (o) Check(Near(o->point.x, 11.0), "local Small point (1,0,0) + insert (10,0,0) lands at (11,0,0)");
  }

  Check(SetBlockInstanceLookup(doc, group, "L"), "SetBlockInstanceLookup(\"L\") succeeds");
  BlockInstance looked;
  Check(FindBlockInstanceByGroup(doc, group, looked), "the record is still findable after looking up");
  Check(looked.lookup_key == "L", "the record's lookup key is now 'L'");
  Check(looked.state == "Small", "the instance's own explicit state field is untouched by a lookup switch");
  Check(looked.objects.size() == 1, "still exactly one object - the Large-tagged point, not both or neither");
  if (looked.objects.size() == 1) {
    const SceneObject* o = doc.Find(looked.objects[0]);
    Check(o != nullptr, "the rebuilt object is findable");
    if (o) Check(Near(o->point.x, 12.0), "key 'L' resolved to state Large: local (2,0,0) + insert (10,0,0) lands at (12,0,0), not the old (11,0,0)");
  }

  // Persistence: the lookup key round-trips through the document's own
  // user-text JSON store exactly like every other BlockInstance field.
  {
    std::vector<BlockInstance> reloaded = LoadBlockInstances(doc);
    bool found = false;
    for (const BlockInstance& b : reloaded) {
      if (b.group != group) continue;
      found = true;
      Check(b.lookup_key == "L", "lookup key survives a fresh LoadBlockInstances() parse");
    }
    Check(found, "the instance is still present in a fresh parse");
  }

  // An unmatched key falls back to the instance's own explicit state
  // (Small), not to no geometry or to the previous Large geometry.
  Check(SetBlockInstanceLookup(doc, group, "NoSuchKey"), "SetBlockInstanceLookup with an unmatched key still succeeds");
  {
    BlockInstance back;
    Check(FindBlockInstanceByGroup(doc, group, back), "record found after an unmatched key");
    Check(back.objects.size() == 1, "exactly one object still placed");
    if (back.objects.size() == 1) {
      const SceneObject* o = doc.Find(back.objects[0]);
      if (o) Check(Near(o->point.x, 11.0), "an unmatched key falls back to the instance's own state (Small): back to (11,0,0)");
    }
  }

  // An empty key is the same as never having looked anything up.
  Check(SetBlockInstanceLookup(doc, group, ""), "SetBlockInstanceLookup(\"\") succeeds");
  {
    BlockInstance cleared;
    Check(FindBlockInstanceByGroup(doc, group, cleared), "record found after clearing the key");
    Check(cleared.lookup_key.empty(), "the key is now empty");
    Check(cleared.objects.size() == 1, "still exactly one object");
    if (cleared.objects.size() == 1) {
      const SceneObject* o = doc.Find(cleared.objects[0]);
      if (o) Check(Near(o->point.x, 11.0), "an empty key falls back to the instance's own state (Small)");
    }
  }

  Check(!SetBlockInstanceLookup(doc, 99999, "S"), "SetBlockInstanceLookup on an unknown group fails cleanly");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
