// Unit test for the dynamic-block Flip parameter (doc/BlockInstances.h/.cpp):
// SetBlockInstanceFlip should mirror a placed instance's geometry about a
// vertical world plane through its block definition's base point, keep the
// insertion point itself unmoved, persist the flag across a
// Save/LoadBlockInstances round trip (the same JSON user-text mechanism
// every other BlockInstance field already uses), and be reversible. See
// PARITY_MAP.md's "Dynamic blocks" item, which this closes the Flip half of
// (stretch/array/lookup parameters remain unattempted).
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
using dino8::app::SceneObject;
using dino8::app::SetBlockInstanceFlip;
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
  Document doc;
  BlockDefinition def;
  def.name = "FlipTest";
  def.base = Point3d(0, 0, 0);
  def.states = {"A"};  // at least one named state: what makes an instance dynamic (see BlockInstances.h)
  def.objects.push_back(SceneObject::MakePoint(Point3d(2, 0, 0)));
  doc.Blocks().push_back(def);

  const int group = InstantiateDynamicBlock(doc, "FlipTest", Point3d(10, 0, 0));
  Check(group >= 0, "InstantiateDynamicBlock placed the instance");

  BlockInstance inst;
  Check(FindBlockInstanceByGroup(doc, group, inst), "a BlockInstance record exists for the new group");
  Check(!inst.flipped, "a freshly placed instance starts unflipped");
  Check(inst.objects.size() == 1, "one object was built");
  {
    const SceneObject* o = doc.Find(inst.objects[0]);
    Check(o != nullptr, "the built object is findable");
    if (o) Check(Near(o->point.x, 12.0) && Near(o->point.y, 0.0) && Near(o->point.z, 0.0),
                 "unflipped: local (2,0,0) placed at insert (10,0,0) lands at (12,0,0)");
  }

  Check(SetBlockInstanceFlip(doc, group, true), "SetBlockInstanceFlip(true) succeeds");
  BlockInstance flipped;
  Check(FindBlockInstanceByGroup(doc, group, flipped), "the record is still findable after flipping");
  Check(flipped.flipped, "the record's flipped flag is now set");
  {
    const SceneObject* o = doc.Find(flipped.objects[0]);
    Check(o != nullptr, "the rebuilt object is findable");
    // Mirrored about the base (x=0) first: local (2,0,0) -> (-2,0,0), then
    // translated by the same (10,0,0) insert offset -> (8,0,0). The insert
    // point itself (where the base point lands) is unchanged by the flip.
    if (o) Check(Near(o->point.x, 8.0) && Near(o->point.y, 0.0) && Near(o->point.z, 0.0),
                 "flipped: local (2,0,0) mirrors about base then lands at (8,0,0)");
  }

  // Persistence: the flag round-trips through the document's own user-text
  // JSON store exactly like every other BlockInstance field.
  {
    std::vector<BlockInstance> reloaded = LoadBlockInstances(doc);
    bool found = false;
    for (const BlockInstance& b : reloaded) {
      if (b.group != group) continue;
      found = true;
      Check(b.flipped, "flip flag survives a fresh LoadBlockInstances() parse");
    }
    Check(found, "the instance is still present in a fresh parse");
  }

  Check(SetBlockInstanceFlip(doc, group, false), "SetBlockInstanceFlip(false) succeeds (reversible)");
  {
    BlockInstance back;
    Check(FindBlockInstanceByGroup(doc, group, back), "record found after un-flipping");
    Check(!back.flipped, "flag cleared after un-flipping");
    const SceneObject* o = doc.Find(back.objects[0]);
    if (o) Check(Near(o->point.x, 12.0) && Near(o->point.y, 0.0) && Near(o->point.z, 0.0),
                 "un-flipped geometry matches the original unflipped placement again");
  }

  Check(!SetBlockInstanceFlip(doc, 99999, true), "SetBlockInstanceFlip on an unknown group fails cleanly");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
