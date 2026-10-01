// Unit test for the dynamic-block Array parameter (doc/BlockInstances.h/.cpp):
// SetBlockInstanceArrayCount should repeat a placed instance's geometry
// along its block definition's array axis/spacing, leave the insertion
// point of the first copy unmoved, persist the count across a Save/
// LoadBlockInstances round trip like every other BlockInstance field, and
// be a no-op (always exactly one copy) on a definition that never
// configured a nonzero array_spacing - the same "not configured yet" no-op
// contract Flip has on a block with no named states. See PARITY_MAP.md's
// "Dynamic blocks" item, which this closes the Array half of (Flip already
// closed; Stretch/Lookup parameters remain unattempted).
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
using dino8::app::SetBlockInstanceArrayCount;
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
  // --- No array parameter configured (array_spacing == 0, the default):
  // setting a count > 1 must still place exactly one copy.
  {
    Document doc;
    BlockDefinition def;
    def.name = "Unconfigured";
    def.base = Point3d(0, 0, 0);
    def.states = {"A"};  // at least one named state: what makes an instance dynamic
    def.objects.push_back(SceneObject::MakePoint(Point3d(1, 0, 0)));
    doc.Blocks().push_back(def);

    const int group = InstantiateDynamicBlock(doc, "Unconfigured", Point3d(0, 0, 0));
    Check(group >= 0, "InstantiateDynamicBlock placed the instance");
    Check(SetBlockInstanceArrayCount(doc, group, 5), "SetBlockInstanceArrayCount succeeds even with no spacing configured");
    BlockInstance inst;
    Check(FindBlockInstanceByGroup(doc, group, inst), "the record is still findable");
    Check(inst.array_count == 5, "the count itself is stored as requested");
    Check(inst.objects.size() == 1, "but exactly one copy is actually placed - array_spacing is 0, so the parameter has no effect yet");
  }

  // --- A configured array (X axis, 5 units apart), three copies.
  Document doc;
  BlockDefinition def;
  def.name = "ArrTest";
  def.base = Point3d(0, 0, 0);
  def.states = {"A"};
  def.array_axis = Vector3d(1, 0, 0);
  def.array_spacing = 5;
  def.objects.push_back(SceneObject::MakePoint(Point3d(1, 0, 0)));
  doc.Blocks().push_back(def);

  const int group = InstantiateDynamicBlock(doc, "ArrTest", Point3d(10, 0, 0));
  Check(group >= 0, "InstantiateDynamicBlock placed the instance");

  BlockInstance inst;
  Check(FindBlockInstanceByGroup(doc, group, inst), "a BlockInstance record exists for the new group");
  Check(inst.array_count == 1, "a freshly placed instance defaults to a single copy");
  Check(inst.objects.size() == 1, "one object was built by default");
  {
    const SceneObject* o = doc.Find(inst.objects[0]);
    Check(o != nullptr, "the built object is findable");
    if (o) Check(Near(o->point.x, 11.0) && Near(o->point.y, 0.0) && Near(o->point.z, 0.0),
                 "local (1,0,0) placed at insert (10,0,0) lands at (11,0,0)");
  }

  Check(SetBlockInstanceArrayCount(doc, group, 3), "SetBlockInstanceArrayCount(3) succeeds");
  BlockInstance arrayed;
  Check(FindBlockInstanceByGroup(doc, group, arrayed), "the record is still findable after arraying");
  Check(arrayed.array_count == 3, "the record's array count is now 3");
  Check(arrayed.objects.size() == 3, "three copies were built");
  if (arrayed.objects.size() == 3) {
    const SceneObject* o0 = doc.Find(arrayed.objects[0]);
    const SceneObject* o1 = doc.Find(arrayed.objects[1]);
    const SceneObject* o2 = doc.Find(arrayed.objects[2]);
    Check(o0 && o1 && o2, "all three copies are findable");
    if (o0) Check(Near(o0->point.x, 11.0) && Near(o0->point.y, 0.0), "copy 0 unchanged: (11,0,0)");
    if (o1) Check(Near(o1->point.x, 16.0) && Near(o1->point.y, 0.0), "copy 1 stepped 5 units along X: (16,0,0)");
    if (o2) Check(Near(o2->point.x, 21.0) && Near(o2->point.y, 0.0), "copy 2 stepped 10 units along X: (21,0,0)");
  }

  // Persistence: the count round-trips through the document's own user-text
  // JSON store exactly like every other BlockInstance field.
  {
    std::vector<BlockInstance> reloaded = LoadBlockInstances(doc);
    bool found = false;
    for (const BlockInstance& b : reloaded) {
      if (b.group != group) continue;
      found = true;
      Check(b.array_count == 3, "array count survives a fresh LoadBlockInstances() parse");
    }
    Check(found, "the instance is still present in a fresh parse");
  }

  // Reversible: dropping back to 1 removes the extra copies.
  Check(SetBlockInstanceArrayCount(doc, group, 1), "SetBlockInstanceArrayCount(1) succeeds (reversible)");
  {
    BlockInstance back;
    Check(FindBlockInstanceByGroup(doc, group, back), "record found after un-arraying");
    Check(back.array_count == 1, "count back to 1");
    Check(back.objects.size() == 1, "extra copies removed, exactly one object remains");
  }

  // A count of 0 (or negative) is clamped to 1, not zero copies.
  Check(SetBlockInstanceArrayCount(doc, group, 0), "SetBlockInstanceArrayCount(0) succeeds");
  {
    BlockInstance clamped;
    Check(FindBlockInstanceByGroup(doc, group, clamped), "record found after a 0 count");
    Check(clamped.array_count == 1, "a 0 count is clamped up to 1, not stored as 0 (no zero-copy instance)");
  }

  // An absurdly large count (as could come from a loaded .3dm's own
  // untrusted embedded JSON, not just this setter) must not make
  // PlaceFiltered try to doc.Add() billions of objects - it's capped well
  // below any plausible real use, rather than only clamped at the bottom.
  Check(SetBlockInstanceArrayCount(doc, group, 50000000), "SetBlockInstanceArrayCount with a huge count still succeeds");
  {
    BlockInstance huge;
    Check(FindBlockInstanceByGroup(doc, group, huge), "record found after a huge count");
    Check(huge.objects.size() <= 2000, "the actually-placed object count is capped, not literally 50 million");
    Check(huge.objects.size() > 1, "...but the array is still genuinely in effect, not silently dropped to 1");
  }
  Check(SetBlockInstanceArrayCount(doc, group, 1), "SetBlockInstanceArrayCount(1) after the huge-count test succeeds (cleanup)");

  // The same hardening against a crafted/corrupt .3dm, exercised directly
  // through the wire format LoadBlockInstances actually parses (not just
  // this module's own setter): a JSON "array" value past INT_MAX is
  // undefined behavior to static_cast straight to int, so it must be
  // clamped to something well-defined (and still bounded) before the cast.
  {
    Document doc2;
    doc2.UserText()["dino8.block_instances"] =
        "[{\"group\":1,\"block\":\"X\",\"state\":\"\",\"ix\":0,\"iy\":0,\"iz\":0,\"flip\":0,"
        "\"array\":1e18,\"objects\":[]}]";
    std::vector<BlockInstance> parsed = LoadBlockInstances(doc2);
    Check(parsed.size() == 1, "a single crafted record parses");
    if (!parsed.empty()) {
      Check(parsed[0].array_count > 0, "a JSON array count past INT_MAX still lands on a well-defined positive int");
      Check(parsed[0].array_count <= 1000000000, "...and stays within the same sane clamp used elsewhere, not UB-dependent garbage");
    }
  }

  Check(!SetBlockInstanceArrayCount(doc, 99999, 3), "SetBlockInstanceArrayCount on an unknown group fails cleanly");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
