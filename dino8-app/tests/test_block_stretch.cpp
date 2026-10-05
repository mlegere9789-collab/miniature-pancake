// Unit test for the dynamic-block Stretch parameter (doc/BlockInstances.h/.cpp):
// SetBlockInstanceStretch should re-map a placed instance's own geometry to
// a target length along its block definition's stretch axis/frame using the
// exact StretchMap point-map formula (unmoved before the frame, graded
// in-between, fully translated past it), leave geometry built from an
// unconfigured definition (stretch_length == 0) untouched, persist the
// target length across a Save/LoadBlockInstances round trip like every
// other BlockInstance field, and be reversible. See PARITY_MAP.md's
// "Dynamic blocks" item, which this closes the fifth and last parameter/
// action type of (Visibility states, Flip, Array and Lookup already
// closed).
#include <cmath>
#include <cstdio>

#include "doc/BlockInstances.h"
#include "doc/Document.h"
#include "dino8/kernel/curve.h"

using dino8::app::BlockDefinition;
using dino8::app::BlockInstance;
using dino8::app::Document;
using dino8::app::FindBlockInstanceByGroup;
using dino8::app::InstantiateDynamicBlock;
using dino8::app::LoadBlockInstances;
using dino8::app::SceneObject;
using dino8::app::SetBlockInstanceStretch;
using dino8::kernel::NurbsCurve;
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
  // --- No stretch parameter configured (stretch_length == 0, the default):
  // setting a target length must leave the built geometry untouched.
  {
    Document doc;
    BlockDefinition def;
    def.name = "Unconfigured";
    def.base = Point3d(0, 0, 0);
    def.states = {"A"};  // at least one named state: what makes an instance dynamic
    def.objects.push_back(SceneObject::MakePoint(Point3d(5, 0, 0)));
    doc.Blocks().push_back(def);

    const int group = InstantiateDynamicBlock(doc, "Unconfigured", Point3d(0, 0, 0));
    Check(group >= 0, "InstantiateDynamicBlock placed the instance");
    Check(SetBlockInstanceStretch(doc, group, 20), "SetBlockInstanceStretch succeeds even with no frame configured");
    BlockInstance inst;
    Check(FindBlockInstanceByGroup(doc, group, inst), "the record is still findable");
    Check(Near(inst.stretch_length, 20), "the target length itself is stored as requested");
    Check(inst.objects.size() == 1, "still exactly one object");
    const SceneObject* o = doc.Find(inst.objects[0]);
    Check(o != nullptr && Near(o->point.x, 5.0), "but the point is unmoved - stretch_length is 0 on the definition, so the parameter has no effect yet");
  }

  // --- A configured stretch frame: X axis, 10 units long from the base.
  Document doc;
  BlockDefinition def;
  def.name = "StretchTest";
  def.base = Point3d(0, 0, 0);
  def.states = {"A"};
  def.stretch_axis = Vector3d(1, 0, 0);
  def.stretch_length = 10;
  def.objects.push_back(SceneObject::MakePoint(Point3d(-2, 0, 0)));  // before the frame
  def.objects.push_back(SceneObject::MakePoint(Point3d(5, 0, 0)));   // inside the frame (graded)
  def.objects.push_back(SceneObject::MakePoint(Point3d(15, 0, 0)));  // past the frame (full translate)
  def.objects.push_back(SceneObject::MakeCurve(NurbsCurve::FromControlPoints({Point3d(2, 1, 0), Point3d(8, 1, 0)}, 1)));
  doc.Blocks().push_back(def);

  const int group = InstantiateDynamicBlock(doc, "StretchTest", Point3d(100, 0, 0));
  Check(group >= 0, "InstantiateDynamicBlock placed the instance");

  BlockInstance inst;
  Check(FindBlockInstanceByGroup(doc, group, inst), "a BlockInstance record exists for the new group");
  Check(Near(inst.stretch_length, 0), "a freshly placed instance defaults to no override (unstretched)");
  Check(inst.objects.size() == 4, "all four objects were built by default");
  {
    const SceneObject* before = doc.Find(inst.objects[0]);
    const SceneObject* graded = doc.Find(inst.objects[1]);
    const SceneObject* past = doc.Find(inst.objects[2]);
    Check(before && Near(before->point.x, 98.0), "unstretched: local -2 + insert 100 = 98");
    Check(graded && Near(graded->point.x, 105.0), "unstretched: local 5 + insert 100 = 105");
    Check(past && Near(past->point.x, 115.0), "unstretched: local 15 + insert 100 = 115");
  }

  Check(SetBlockInstanceStretch(doc, group, 20), "SetBlockInstanceStretch(20) succeeds");
  BlockInstance stretched;
  Check(FindBlockInstanceByGroup(doc, group, stretched), "the record is still findable after stretching");
  Check(Near(stretched.stretch_length, 20), "the record's target length is now 20");
  if (stretched.objects.size() == 4) {
    const SceneObject* before = doc.Find(stretched.objects[0]);
    const SceneObject* graded = doc.Find(stretched.objects[1]);
    const SceneObject* past = doc.Find(stretched.objects[2]);
    const SceneObject* crv = doc.Find(stretched.objects[3]);
    // s=-2 <= 0: unmoved by the stretch itself, only the insert translation applies: 100 + -2 = 98.
    Check(before && Near(before->point.x, 98.0), "before the frame: still 98, unmoved by the stretch");
    // s=5, 0<len=10: p + u*(s*new_len/len - s) = 5 + (5*20/10 - 5) = 5 + 5 = 10; + insert 100 = 110.
    Check(graded && Near(graded->point.x, 110.0), "inside the frame: graded from 105 to 110 (doubled stretch)");
    // s=15 >= len=10: p + u*(new_len-len) = 15 + (20-10) = 25; + insert 100 = 125.
    Check(past && Near(past->point.x, 125.0), "past the frame: fully translated from 115 to 125 (+10, the full length change)");
    // Curve control points at local x=2 (s=2, graded: 2 + (2*2-2)=4) and x=8 (s=8, graded: 8 + (8*2-8)=16).
    Check(crv != nullptr, "the curve object survived the stretch");
    if (crv) {
      Check(Near(crv->curve->ControlPointAt(0).x, 104.0), "curve control point 0: local 2 graded to 4, + insert 100 = 104");
      Check(Near(crv->curve->ControlPointAt(1).x, 116.0), "curve control point 1: local 8 graded to 16, + insert 100 = 116");
    }
  } else {
    Check(false, "four copies remained after stretching");
  }

  // Persistence: the target length round-trips through the document's own
  // user-text JSON store exactly like every other BlockInstance field.
  {
    std::vector<BlockInstance> reloaded = LoadBlockInstances(doc);
    bool found = false;
    for (const BlockInstance& b : reloaded) {
      if (b.group != group) continue;
      found = true;
      Check(Near(b.stretch_length, 20), "target length survives a fresh LoadBlockInstances() parse");
    }
    Check(found, "the instance is still present in a fresh parse");
  }

  // Reversible: a length of 0 clears the override, falling back to the
  // definition's own stretch_length (10) - the identity case, so geometry
  // returns exactly to its unstretched positions.
  Check(SetBlockInstanceStretch(doc, group, 0), "SetBlockInstanceStretch(0) succeeds (reversible)");
  {
    BlockInstance back;
    Check(FindBlockInstanceByGroup(doc, group, back), "record found after clearing the override");
    Check(Near(back.stretch_length, 0), "target length back to 0 (no override)");
    if (back.objects.size() == 4) {
      const SceneObject* graded = doc.Find(back.objects[1]);
      Check(graded && Near(graded->point.x, 105.0), "geometry back to its original unstretched position");
    }
  }

  // A negative length is clamped to 0 (no override), not stored negative.
  Check(SetBlockInstanceStretch(doc, group, -5), "SetBlockInstanceStretch(-5) succeeds");
  {
    BlockInstance clamped;
    Check(FindBlockInstanceByGroup(doc, group, clamped), "record found after a negative length");
    Check(Near(clamped.stretch_length, 0), "a negative length is clamped to 0 (no override), not stored negative");
  }

  Check(!SetBlockInstanceStretch(doc, 99999, 20), "SetBlockInstanceStretch on an unknown group fails cleanly");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
