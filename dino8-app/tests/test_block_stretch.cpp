// Unit test for the dynamic-block Stretch parameter (doc/BlockInstances.h/
// .cpp): SetBlockInstanceStretch should move only the objects tagged
// kBlockStretchKey (BlockSetStretchGroup) along their block definition's
// stretch_axis (BlockSetStretchAxis), leave every untagged object exactly
// where Flip/Array alone would put it, compose correctly with an already-
// flipped instance, persist across a Save/LoadBlockInstances round trip
// like every other BlockInstance field, and be a no-op (zero displacement)
// both when the amount is 0 (the default) and when no object is tagged at
// all - the same "not configured yet" no-op contract Flip/Array/Lookup
// already have. See PARITY_MAP.md's "Dynamic blocks" item, which this
// closes the fifth and last named gap of (Flip/Array/Lookup already
// closed).
#include <cmath>
#include <cstdio>

#include "doc/BlockInstances.h"
#include "doc/Document.h"

using dino8::app::BlockDefinition;
using dino8::app::BlockInstance;
using dino8::app::Document;
using dino8::app::FindBlockInstanceByGroup;
using dino8::app::InstantiateDynamicBlock;
using dino8::app::kBlockStretchKey;
using dino8::app::LoadBlockInstances;
using dino8::app::SceneObject;
using dino8::app::SetBlockInstanceFlip;
using dino8::app::SetBlockInstanceStretch;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }
}  // namespace

int main() {
  // --- No object ever tagged kBlockStretchKey: a nonzero amount is stored
  // but moves nothing, same as Array's count on a definition with no
  // configured spacing.
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
    Check(SetBlockInstanceStretch(doc, group, 7.0), "SetBlockInstanceStretch succeeds even with no stretch group tagged");
    BlockInstance inst;
    Check(FindBlockInstanceByGroup(doc, group, inst), "the record is still findable");
    Check(Near(inst.stretch_amount, 7.0), "the amount itself is stored as requested");
    Check(inst.objects.size() == 1, "still exactly one object");
    const SceneObject* o = doc.Find(inst.objects[0]);
    Check(o != nullptr && Near(o->point.x, 1.0), "the object did not move - no object is tagged, so the parameter has no effect yet");
  }

  // --- A configured stretch: one tagged object moves along X, one
  // untagged object stays put.
  Document doc;
  BlockDefinition def;
  def.name = "StretchTest";
  def.base = Point3d(0, 0, 0);
  def.states = {"A"};
  def.stretch_axis = Vector3d(1, 0, 0);
  SceneObject moving = SceneObject::MakePoint(Point3d(5, 0, 0));
  moving.user_text[kBlockStretchKey] = "1";
  def.objects.push_back(moving);
  def.objects.push_back(SceneObject::MakePoint(Point3d(-5, 0, 0)));  // untagged: stays fixed
  doc.Blocks().push_back(def);

  const int group = InstantiateDynamicBlock(doc, "StretchTest", Point3d(10, 0, 0));
  Check(group >= 0, "InstantiateDynamicBlock placed the instance");

  BlockInstance inst;
  Check(FindBlockInstanceByGroup(doc, group, inst), "a BlockInstance record exists for the new group");
  Check(Near(inst.stretch_amount, 0.0), "a freshly placed instance defaults to zero stretch");
  Check(inst.objects.size() == 2, "both objects were built");
  {
    const SceneObject* moved = doc.Find(inst.objects[0]);
    const SceneObject* fixed = doc.Find(inst.objects[1]);
    Check(moved && fixed, "both built objects are findable");
    if (moved) Check(Near(moved->point.x, 15.0), "tagged object at local (5,0,0), insert (10,0,0): lands at (15,0,0) with zero stretch");
    if (fixed) Check(Near(fixed->point.x, 5.0), "untagged object at local (-5,0,0), insert (10,0,0): lands at (5,0,0)");
  }

  Check(SetBlockInstanceStretch(doc, group, 3.0), "SetBlockInstanceStretch(3) succeeds");
  BlockInstance stretched;
  Check(FindBlockInstanceByGroup(doc, group, stretched), "the record is still findable after stretching");
  Check(Near(stretched.stretch_amount, 3.0), "the record's stretch amount is now 3");
  {
    const SceneObject* moved = doc.Find(stretched.objects[0]);
    const SceneObject* fixed = doc.Find(stretched.objects[1]);
    Check(moved && fixed, "both objects still findable");
    if (moved) Check(Near(moved->point.x, 18.0), "tagged object moved 3 units along the stretch axis: (18,0,0)");
    if (fixed) Check(Near(fixed->point.x, 5.0), "untagged object unaffected by the same stretch: still (5,0,0)");
  }

  // A negative amount moves the tagged object the other way along the axis.
  Check(SetBlockInstanceStretch(doc, group, -2.0), "a negative stretch amount succeeds");
  {
    BlockInstance neg;
    Check(FindBlockInstanceByGroup(doc, group, neg), "record found after a negative stretch");
    const SceneObject* moved = doc.Find(neg.objects[0]);
    Check(moved && Near(moved->point.x, 13.0), "tagged object moved -2 units along X: (13,0,0)");
  }

  // Composes correctly with Flip: the stretch displacement is itself
  // mirrored, not applied in world space after the flip.
  Check(SetBlockInstanceFlip(doc, group, true), "flipping the instance succeeds");
  {
    BlockInstance flipped;
    Check(FindBlockInstanceByGroup(doc, group, flipped), "record found after flipping");
    Check(Near(flipped.stretch_amount, -2.0), "the stretch amount survives flipping unchanged");
    const SceneObject* moved = doc.Find(flipped.objects[0]);
    const SceneObject* fixed = doc.Find(flipped.objects[1]);
    // Local (5,0,0) mirrored about the vertical plane through base (0,0,0)
    // becomes (-5,0,0); the stretch (-2 along local +X) is applied before
    // the mirror, so it also flips sign in world space: local (5-2,0,0) =
    // (3,0,0) mirrors to (-3,0,0), then the insert translation (+10) lands
    // at (7,0,0).
    Check(moved && Near(moved->point.x, 7.0), "tagged object under flip+stretch lands at (7,0,0), not a naive (18,0,0)-mirrored-alone (-8,0,0)");
    Check(fixed && Near(fixed->point.x, 15.0), "untagged object under flip alone: local (-5,0,0) mirrors to (5,0,0), insert +10 = (15,0,0)");
  }
  Check(SetBlockInstanceFlip(doc, group, false), "un-flipping for the rest of the test succeeds");
  Check(SetBlockInstanceStretch(doc, group, 0.0), "resetting stretch to 0 succeeds");

  // Persistence: the amount round-trips through the document's own
  // user-text JSON store exactly like every other BlockInstance field.
  Check(SetBlockInstanceStretch(doc, group, 4.5), "setting a fresh amount for the persistence check succeeds");
  {
    std::vector<BlockInstance> reloaded = LoadBlockInstances(doc);
    bool found = false;
    for (const BlockInstance& b : reloaded) {
      if (b.group != group) continue;
      found = true;
      Check(Near(b.stretch_amount, 4.5), "stretch amount survives a fresh LoadBlockInstances() parse");
    }
    Check(found, "the instance is still present in a fresh parse");
  }

  // Reversible: back to 0 removes the displacement.
  Check(SetBlockInstanceStretch(doc, group, 0.0), "SetBlockInstanceStretch(0) succeeds (reversible)");
  {
    BlockInstance back;
    Check(FindBlockInstanceByGroup(doc, group, back), "record found after resetting to 0");
    const SceneObject* moved = doc.Find(back.objects[0]);
    Check(moved && Near(moved->point.x, 15.0), "tagged object back at its zero-stretch position (15,0,0)");
  }

  Check(!SetBlockInstanceStretch(doc, 99999, 3.0), "SetBlockInstanceStretch on an unknown group fails cleanly");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
