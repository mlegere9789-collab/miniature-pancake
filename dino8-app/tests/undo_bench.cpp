// Standalone timing benchmark for Document's diff-based undo/redo history.
// Not wired into smoke.sh (it has nothing to assert - see
// undo_scaling_notes.md for the numbers this produces and how to read
// them) - build and run it directly:
//   cmake --build build --target dino8_undo_bench -j$(nproc)
//   ./build/dino8_undo_bench
//
// Methodology, spelled out because a benchmark that isn't explicit about
// what it's actually timing is worse than no benchmark:
//  - "Full snapshot" numbers come from Document::SaveNamedSnapshot /
//    RestoreNamedSnapshot, which call the exact same, unmodified
//    Capture()/Restore() the pre-existing undo/redo model used for every
//    single BeginChange/Undo/Redo - so timing them on a real Document is a
//    faithful, non-simulated stand-in for "what the old full-snapshot
//    Undo() cost", without needing to keep a second copy of the deleted
//    code path around.
//  - "General path" numbers come from Document::BeginChange +
//    Document::Undo() - the always-safe default every unmigrated call
//    site still uses.
//  - "Fast path" numbers come from Document::BeginChangeForObjects +
//    Document::Undo() - what ApplyXform (Move/Rotate/Scale/Mirror/Orient/
//    Nudge/ProjectToCPlane, copy=false) now uses.
// All three exercise the *same* document and the *same* kind of edit
// (transforming some objects), so the comparison is apples-to-apples.
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "doc/Document.h"

using dino8::app::Document;
using dino8::app::ObjectId;
using dino8::app::SceneObject;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;
using Clock = std::chrono::steady_clock;

namespace {

double MillisSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

long VmRssKb() {
  FILE* f = std::fopen("/proc/self/status", "r");
  if (!f) return -1;
  long kb = -1;
  char line[256];
  while (std::fgets(line, sizeof(line), f)) {
    if (std::sscanf(line, "VmRSS: %ld kB", &kb) == 1) break;
  }
  std::fclose(f);
  return kb;
}

// A modest per-object mesh payload (a low-poly cylinder: 8-gon caps, one
// ring) - representative of "a real solid," not a bare point, without
// making the benchmark itself slow to set up.
SceneObject MakeBenchObject(double x) {
  Mesh m = Mesh::Cylinder(Point3d(x, 0, 0), Vector3d(0, 0, 1), 1.0, 2.0, 8, 1);
  return SceneObject::MakeMesh(m);
}

}  // namespace

int main() {
  const int kDocSize = 12000;
  const int kSelectionSize = 12;   // a typical "selected a handful of parts" edit
  const int kCycles = 200;         // repeated edit+undo+redo, as the task asks for

  Document doc;
  std::printf("Building a %d-object document (each a %d-face cylinder mesh)...\n", kDocSize,
              Mesh::Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 2.0, 8, 1).FaceCount());
  std::vector<ObjectId> ids;
  ids.reserve(kDocSize);
  Clock::time_point t0 = Clock::now();
  for (int i = 0; i < kDocSize; ++i) ids.push_back(doc.Add(MakeBenchObject(static_cast<double>(i))));
  std::printf("  built in %.1f ms, RSS now %ld kB\n\n", MillisSince(t0), VmRssKb());

  // ---- 1. Full snapshot (Capture/Restore, via the named-snapshot API) ----
  {
    long rss0 = VmRssKb();
    t0 = Clock::now();
    doc.SaveNamedSnapshot("bench");
    const double save_ms = MillisSince(t0);
    long rss1 = VmRssKb();
    t0 = Clock::now();
    doc.RestoreNamedSnapshot("bench");
    const double restore_ms = MillisSince(t0);
    doc.DeleteNamedSnapshot("bench");
    std::printf("[1] Full snapshot of the whole %d-object document:\n", kDocSize);
    std::printf("      Capture (SaveNamedSnapshot):  %.2f ms\n", save_ms);
    std::printf("      Restore (RestoreNamedSnapshot): %.2f ms\n", restore_ms);
    std::printf("      RSS delta while the extra copy was held: ~%ld kB\n", rss1 - rss0);
    std::printf("      -> old model's per-Undo() cost was ~Capture+Restore = %.2f ms, EVERY step,\n",
                save_ms + restore_ms);
    std::printf("         regardless of how small the edit was.\n\n");
  }

  // ---- 2. General/default BeginChange path (unmigrated call sites) ----
  {
    std::mt19937 rng(1);
    std::uniform_int_distribution<int> pick(0, kDocSize - 1);
    double total_begin_ms = 0, total_undo_ms = 0, total_redo_ms = 0;
    for (int c = 0; c < kCycles; ++c) {
      const ObjectId id = ids[static_cast<size_t>(pick(rng))];
      t0 = Clock::now();
      doc.BeginChange("Bench-General");  // finalizes the previous cycle's pending entry too
      total_begin_ms += MillisSince(t0);
      SceneObject* o = doc.Find(id);
      o->Transform(ON_Xform::TranslationTransformation(ON_3dVector(1, 0, 0)));
      t0 = Clock::now();
      doc.Undo();
      total_undo_ms += MillisSince(t0);
      t0 = Clock::now();
      doc.Redo();
      total_redo_ms += MillisSince(t0);
      doc.Undo();  // leave the document as found for the next cycle
    }
    doc.ClearUndo();
    std::printf("[2] General BeginChange(label) path, %d x (move 1 of %d objects, Undo, Redo):\n", kCycles,
                kDocSize);
    std::printf("      avg BeginChange (incl. finalizing the previous entry): %.3f ms\n",
                total_begin_ms / kCycles);
    std::printf("      avg Undo(): %.3f ms   avg Redo(): %.3f ms\n", total_undo_ms / kCycles,
                total_redo_ms / kCycles);
    std::printf(
        "      -> id-set is unchanged (a pure in-place move), so every object is a\n"
        "         \"modified candidate\" under this path - no smaller than the old full\n"
        "         snapshot for this case. This is exactly why BeginChangeForObjects exists.\n\n");
  }

  // ---- 3. Fast path: BeginChangeForObjects on a small selection ----
  {
    std::vector<ObjectId> selection(ids.begin(), ids.begin() + kSelectionSize);
    double total_begin_ms = 0, total_undo_ms = 0, total_redo_ms = 0;
    for (int c = 0; c < kCycles; ++c) {
      t0 = Clock::now();
      doc.BeginChangeForObjects("Bench-FastPath", selection);
      total_begin_ms += MillisSince(t0);
      for (ObjectId id : selection) {
        doc.Find(id)->Transform(ON_Xform::TranslationTransformation(ON_3dVector(1, 0, 0)));
      }
      t0 = Clock::now();
      doc.Undo();
      total_undo_ms += MillisSince(t0);
      t0 = Clock::now();
      doc.Redo();
      total_redo_ms += MillisSince(t0);
      doc.Undo();
    }
    doc.ClearUndo();
    std::printf("[3] BeginChangeForObjects fast path, %d x (move %d of %d objects, Undo, Redo):\n", kCycles,
                kSelectionSize, kDocSize);
    std::printf("      avg BeginChangeForObjects: %.3f ms\n", total_begin_ms / kCycles);
    std::printf("      avg Undo(): %.3f ms   avg Redo(): %.3f ms\n", total_undo_ms / kCycles,
                total_redo_ms / kCycles);
    std::printf("\n");
  }

  // ---- 4. Add/remove-dominant edit under the general path (no fast path needed) ----
  {
    double total_add_ms = 0, total_undo_ms = 0;
    for (int c = 0; c < kCycles; ++c) {
      t0 = Clock::now();
      doc.BeginChange("Bench-Add");
      const ObjectId id = doc.Add(MakeBenchObject(99999));
      doc.Undo();  // finalizes immediately (Undo() calls FinalizePending() first)
      (void)id;
      total_add_ms += MillisSince(t0);
    }
    doc.ClearUndo();
    std::printf(
        "[4] General BeginChange(label) path, %d x (add 1 object to the %d-object\n"
        "    document, then Undo) - the general path's *other* real win, no fast path\n"
        "    needed: avg BeginChange+Add+Undo: %.3f ms\n",
        kCycles, kDocSize, total_add_ms / kCycles);
  }

  return 0;
}
