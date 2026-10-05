// Unit test for the dynamic-block Stretch parameter (doc/BlockInstances.h/
// .cpp): BlockSetStretchFrame names a boundary plane on a block definition
// (axis + anchor distance from its base point), and SetBlockInstanceStretch
// then moves only the part of a placed instance's geometry beyond that
// plane - leaving the near side exactly where it was - the fifth and last
// dynamic-block parameter/action type, after Visibility states, Flip,
// Array and Lookup. See PARITY_MAP.md's "Dynamic blocks" item, which this
// finally closes (Stretch was the one gap left of the four this bullet
// originally named after Flip/Array/Lookup).
//
// Also exercises, end to end, the real pre-existing Undo/Redo bug this
// same session's work on Stretch surfaced and fixed: Document::UserText()
// (the dino8.block_instances/dino8.plot_style_tables JSON every
// BlockInstance field round-trips through) was never actually part of
// Document::Capture()/Restore() or the diff-based StateDelta Undo/Redo
// path, despite BlockInstances.h's own comment claiming otherwise - so
// Undo silently left a dynamic-block instance's record (and its rebuilt
// geometry) at the post-edit state. The "Undo reverts a Stretch" check
// below is a direct before/after proof that fix actually works, not just
// that the Stretch feature's own forward path does.
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
  // --- No stretch frame configured: setting a nonzero offset must still
  // succeed and be stored, but leave the placed geometry untouched.
  {
    Document doc;
    BlockDefinition def;
    def.name = "Unconfigured";
    def.base = Point3d(0, 0, 0);
    def.states = {"A"};  // at least one named state: what makes an instance dynamic
    def.objects.push_back(SceneObject::MakePoint(Point3d(10, 0, 0)));
    doc.Blocks().push_back(def);

    const int group = InstantiateDynamicBlock(doc, "Unconfigured", Point3d(0, 0, 0));
    Check(group >= 0, "InstantiateDynamicBlock placed the instance");
    Check(SetBlockInstanceStretch(doc, group, 5), "SetBlockInstanceStretch succeeds even with no frame configured");
    BlockInstance inst;
    Check(FindBlockInstanceByGroup(doc, group, inst), "the record is still findable");
    Check(Near(inst.stretch_offset, 5), "the offset itself is stored as requested");
    const SceneObject* o = doc.Find(inst.objects[0]);
    Check(o != nullptr && Near(o->point.x, 10.0), "but the point hasn't moved - no stretch frame configured yet");
  }

  // --- A configured frame (X axis, anchor at local x=5) on a block with a
  // straight (degree-1) curve from local (0,0,0) to (10,0,0) - one control
  // point on each side of the anchor plane.
  Document doc;
  BlockDefinition def;
  def.name = "DoorJamb";
  def.base = Point3d(0, 0, 0);
  def.states = {"A"};
  def.has_stretch_frame = true;
  def.stretch_axis = Vector3d(1, 0, 0);
  def.stretch_anchor = 5;
  def.objects.push_back(SceneObject::MakeCurve(NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(10, 0, 0)}, 1)));
  doc.Blocks().push_back(def);

  const int group = InstantiateDynamicBlock(doc, "DoorJamb", Point3d(100, 0, 0));
  Check(group >= 0, "InstantiateDynamicBlock placed the instance");

  BlockInstance inst;
  Check(FindBlockInstanceByGroup(doc, group, inst), "a BlockInstance record exists for the new group");
  Check(Near(inst.stretch_offset, 0), "a freshly placed instance defaults to no stretch");
  {
    const SceneObject* o = doc.Find(inst.objects[0]);
    Check(o != nullptr && o->curve != nullptr, "the built curve is findable");
    if (o && o->curve) {
      Check(Near(o->curve->ControlPointAt(0).x, 100.0), "unstretched: near CV at insert + local 0 = 100");
      Check(Near(o->curve->ControlPointAt(1).x, 110.0), "unstretched: far CV at insert + local 10 = 110");
    }
  }

  Check(SetBlockInstanceStretch(doc, group, 3), "SetBlockInstanceStretch(3) succeeds");
  BlockInstance stretched;
  Check(FindBlockInstanceByGroup(doc, group, stretched), "the record is still findable after stretching");
  Check(Near(stretched.stretch_offset, 3), "the record's stretch offset is now 3");
  {
    const SceneObject* o = doc.Find(stretched.objects[0]);
    Check(o != nullptr && o->curve != nullptr, "the rebuilt curve is findable");
    if (o && o->curve) {
      Check(Near(o->curve->ControlPointAt(0).x, 100.0), "the near control point (local x=0, before the anchor at x=5) did NOT move");
      Check(Near(o->curve->ControlPointAt(1).x, 113.0), "the far control point (local x=10, beyond the anchor) moved by the full offset: 110 -> 113");
    }
  }

  // Persistence: the offset round-trips through the document's own
  // user-text JSON store exactly like every other BlockInstance field.
  {
    std::vector<BlockInstance> reloaded = LoadBlockInstances(doc);
    bool found = false;
    for (const BlockInstance& b : reloaded) {
      if (b.group != group) continue;
      found = true;
      Check(Near(b.stretch_offset, 3), "stretch offset survives a fresh LoadBlockInstances() parse");
    }
    Check(found, "the instance is still present in a fresh parse");
  }

  // Reversible: dropping back to 0 restores the original, unstretched curve.
  Check(SetBlockInstanceStretch(doc, group, 0), "SetBlockInstanceStretch(0) succeeds (reversible)");
  {
    BlockInstance back;
    Check(FindBlockInstanceByGroup(doc, group, back), "record found after un-stretching");
    const SceneObject* o = doc.Find(back.objects[0]);
    Check(o != nullptr && o->curve != nullptr && Near(o->curve->ControlPointAt(0).x, 100.0) && Near(o->curve->ControlPointAt(1).x, 110.0),
          "both control points are back at their original, unstretched positions");
  }

  Check(!SetBlockInstanceStretch(doc, 99999, 3), "SetBlockInstanceStretch on an unknown group fails cleanly");

  // --- Undo/Redo correctness proof (see the file comment above): before
  // the Document::UserText() capture/restore fix this session also made,
  // Undo left both the stretch_offset record and the rebuilt curve at the
  // post-stretch state, since BeginChange/FinalizePending/ApplyDelta never
  // touched user_text_ at all. SetBlockInstanceStretch itself is a
  // low-level setter that deliberately doesn't call BeginChange (same as
  // every other SetBlockInstanceXxx) - the command layer does that exactly
  // once around each edit (see cmd_drafting.cpp's BlockSetStretchCommand),
  // so this probe wraps the edit in BeginChange/Undo/Redo itself to
  // reproduce that same pairing. Re-fetches the current BlockInstance
  // first rather than reusing `inst`/`stretched`/`back` from above: every
  // RebuildBlockInstance call deletes the old objects and creates brand
  // new ones, so an id captured before the un-stretch two blocks up is
  // already stale here.
  {
    BlockInstance current;
    Check(FindBlockInstanceByGroup(doc, group, current), "record findable before the undo probe");
    const SceneObject* pre = doc.Find(current.objects[0]);
    Check(pre != nullptr && pre->curve && Near(pre->curve->ControlPointAt(1).x, 110.0), "before the probed edit: far control point at 110 (unstretched, from the reversibility check above)");

    doc.BeginChange("BlockSetStretch");
    Check(SetBlockInstanceStretch(doc, group, 7), "SetBlockInstanceStretch(7) for the undo probe succeeds");
    BlockInstance after_stretch;
    FindBlockInstanceByGroup(doc, group, after_stretch);
    const SceneObject* post = doc.Find(after_stretch.objects[0]);
    Check(Near(after_stretch.stretch_offset, 7), "right after the edit: record reads 7");
    Check(post != nullptr && post->curve && Near(post->curve->ControlPointAt(1).x, 117.0), "right after the edit: far control point at 117 (110 + 7)");

    Check(doc.Undo(), "Document::Undo() succeeds");
    BlockInstance after_undo;
    Check(FindBlockInstanceByGroup(doc, group, after_undo), "the instance is still findable after Undo");
    Check(Near(after_undo.stretch_offset, 0), "Undo reverts dino8.block_instances' own stretch_offset back to its pre-edit value (0, not 7) - the real bug this session fixed");
    const SceneObject* reverted = doc.Find(after_undo.objects[0]);
    Check(reverted != nullptr && reverted->curve, "the reverted instance's objects are findable");
    if (reverted && reverted->curve) {
      Check(Near(reverted->curve->ControlPointAt(1).x, 110.0), "Undo also reverts the actual rebuilt geometry back to the pre-edit shape (110, not 117)");
    }

    Check(doc.Redo(), "Document::Redo() succeeds");
    BlockInstance after_redo;
    Check(FindBlockInstanceByGroup(doc, group, after_redo), "the instance is still findable after Redo");
    Check(Near(after_redo.stretch_offset, 7), "Redo re-applies the edit: record back to 7");
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
