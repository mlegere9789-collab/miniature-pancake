// Unit test for Document::UserText() actually being captured by Undo/Redo
// and by named snapshots (doc/Document.h/.cpp's Snapshot::user_text /
// StateDelta::user_text_before/user_text_after / PendingChange::user_text,
// wired through Capture/Restore/CaptureSnapshotAsDocument/
// AdoptNamedSnapshotFromDocument/BeginChange/BeginChangeForObjects/
// FinalizePending/ApplyDelta).
//
// A real, independently-confirmed pre-existing bug, found while testing a
// dynamic-block Stretch parameter's own Undo path (a separate, concurrent
// session on this branch closed that feature itself - see PARITY_MAP.md's
// "Dynamic blocks" item): BlockInstances.h's own comment claims a dynamic
// block's instance record (persisted as JSON in UserText()["dino8.
// block_instances"]) "survives Undo/Redo for free (document user text is
// part of the snapshot Document::BeginChange captures)" - but before this
// fix, neither Capture()/Restore() nor the diff-based StateDelta path ever
// actually read or wrote user_text_ at all, so Undo silently left it at
// whatever it was when the undo entry was popped, regardless of which side
// of the edit was being applied. This test is deliberately independent of
// BlockInstances (or any other UserText() consumer) - it exercises
// Document's own Undo/Redo/named-snapshot contract directly against a
// plain UserText() key, so it proves the fix at the layer it was actually
// made, not just one feature built on top of it.
#include <cstdio>
#include <string>

#include "doc/Document.h"

using dino8::app::Document;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}  // namespace

int main() {
  // --- Diff-based Undo/Redo path (BeginChange/FinalizePending/ApplyDelta).
  Document doc;
  doc.UserText()["k"] = "v0";
  Check(doc.UserText()["k"] == "v0", "initial value set before any BeginChange");

  doc.BeginChange("edit1");
  doc.UserText()["k"] = "v1";
  Check(doc.UserText()["k"] == "v1", "live value right after the first edit");

  // A second BeginChange finalizes the first edit into a real StateDelta
  // (v0 -> v1) and starts tracking the next one.
  doc.BeginChange("edit2");
  doc.UserText()["k"] = "v2";
  Check(doc.UserText()["k"] == "v2", "live value right after the second edit");

  Check(doc.Undo(), "Undo() of edit2 succeeds");
  Check(doc.UserText()["k"] == "v1", "Undo reverts the SECOND edit: back to v1, not left at v2 or jumping straight to v0");

  Check(doc.Undo(), "Undo() of edit1 succeeds");
  Check(doc.UserText()["k"] == "v0", "Undo reverts the FIRST edit too: back to v0, the true pre-edit value");

  Check(doc.Redo(), "Redo() re-applies edit1");
  Check(doc.UserText()["k"] == "v1", "Redo moves forward one step at a time: v0 -> v1, not straight to v2");

  Check(doc.Redo(), "Redo() re-applies edit2");
  Check(doc.UserText()["k"] == "v2", "Redo reaches the final edited value: v2");

  // A key that only ever existed on one side of an edit (added/removed by
  // the edit itself, not just changed) must also round-trip correctly.
  Document doc2;
  doc2.BeginChange("add-key");
  doc2.UserText()["new_key"] = "present";
  Check(doc2.UserText().count("new_key") == 1, "a key added during a tracked edit is live immediately after it");
  // Undo() itself calls FinalizePending() first, which is what turns this
  // still-open "add-key" edit into a real StateDelta - no separate flush
  // needed (and a redundant BeginChange here would instead push its own
  // no-op entry on top, so the next Undo() would revert THAT one first).
  Check(doc2.Undo(), "Undo() of the key-adding edit succeeds");
  Check(doc2.UserText().count("new_key") == 0, "Undo removes a key that didn't exist before the edit that added it, not just leaving a stale value");
  Check(doc2.Redo(), "Redo() re-adds the key");
  Check(doc2.UserText()["new_key"] == "present", "Redo brings the key back with its original value");

  // --- Named-snapshot path (Capture/Restore), independent of the
  // diff-based Undo/Redo stack above.
  Document doc3;
  doc3.UserText()["k"] = "A";
  Check(doc3.SaveNamedSnapshot("snap1"), "SaveNamedSnapshot succeeds");
  doc3.UserText()["k"] = "B";
  Check(doc3.UserText()["k"] == "B", "live value after changing it post-snapshot");
  Check(doc3.RestoreNamedSnapshot("snap1"), "RestoreNamedSnapshot succeeds");
  Check(doc3.UserText()["k"] == "A", "RestoreNamedSnapshot reverts UserText() back to the snapshot's own value (A, not the later B)");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
