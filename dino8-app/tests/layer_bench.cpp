// Standalone timing benchmark + regression guard for the layer commands
// that bulk-retarget a selection - ChangeLayer, ChangeToCurrentLayer and
// MatchLayer (cmd_layer.cpp) - resolving `Document::Find(id)` once per
// selected id vs resolving the whole selection in one `Document::FindMany()`
// call.
//
// Build and run directly:
//   cmake --build build --target dino8_layer_bench -j$(nproc)
//   ./build/dino8_layer_bench
//
// Methodology: Document::Find(id) is a linear O(document size) scan; before
// this change, ChangeLayer/ChangeToCurrentLayer/MatchLayer (and the
// selection-driven LayerOff/LayerLock fallbacks) each called it once per
// selected id, so retargeting a k-object selection cost O(k * document
// size) - the same Find()-per-id bug already fixed elsewhere (see
// find_bench.cpp/array_bench.cpp). The fix resolves the whole selection in
// one Document::FindMany(ids) call, which builds a selection-sized hash map
// and fills it in a single O(document size + selection size) pass.
//
// Like find_bench.cpp/isolate_bench.cpp/delete_bench.cpp/array_bench.cpp
// (same style), this asserts a same-run, relative speedup rather than an
// absolute millisecond budget, so it stays robust on a loaded/shared build
// machine while still catching a real regression.
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

SceneObject MakeBenchObject(double x) {
  Mesh m = Mesh::Cylinder(Point3d(x, 0, 0), Vector3d(0, 0, 1), 1.0, 2.0, 8, 1);
  return SceneObject::MakeMesh(m);
}

// The pre-fix call pattern: one Document::Find(id) call per selected id,
// exactly like ChangeLayerCommand's old loop.
double TimeFindLoopChangeLayer(Document& doc, const std::vector<ObjectId>& ids, int target_layer, long& checksum) {
  Clock::time_point t0 = Clock::now();
  long touched = 0;
  for (ObjectId id : ids) {
    if (SceneObject* o = doc.Find(id)) {
      o->layer_index = target_layer;
      ++touched;
    }
  }
  checksum += touched;
  return MillisSince(t0);
}

// The post-fix call pattern: one Document::FindMany(ids) call resolving the
// whole selection up front.
double TimeFindManyChangeLayer(Document& doc, const std::vector<ObjectId>& ids, int target_layer, long& checksum) {
  Clock::time_point t0 = Clock::now();
  long touched = 0;
  for (SceneObject* o : doc.FindMany(ids)) {
    if (o) {
      o->layer_index = target_layer;
      ++touched;
    }
  }
  checksum += touched;
  return MillisSince(t0);
}

}  // namespace

int main() {
  const int kDocSize = 100000;
  const int kSelectionSize = 1000;  // "selected 1000 parts, then ChangeLayer'd them"
  const int kCycles = 20;

  Document doc;
  std::printf("Building a %d-object document (each a %d-face cylinder mesh)...\n", kDocSize,
              Mesh::Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 2.0, 8, 1).FaceCount());
  std::vector<ObjectId> ids;
  ids.reserve(kDocSize);
  Clock::time_point t0 = Clock::now();
  for (int i = 0; i < kDocSize; ++i) ids.push_back(doc.Add(MakeBenchObject(static_cast<double>(i))));
  std::printf("  built in %.1f ms\n\n", MillisSince(t0));

  // Selection scattered across the document, like a real window/lasso select
  // over a spread-out region (same style as array_bench.cpp's selection).
  std::vector<ObjectId> selection;
  selection.reserve(kSelectionSize);
  for (int i = 0; i < kSelectionSize; ++i) {
    selection.push_back(ids[static_cast<size_t>(i) * static_cast<size_t>(kDocSize) / kSelectionSize]);
  }

  long checksum = 0;

  // ---- 1. Old path: Find(id) once per selected id --------------------------
  double old_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) old_total_ms += TimeFindLoopChangeLayer(doc, selection, c % 2, checksum);
  const double old_avg_ms = old_total_ms / kCycles;
  std::printf("[1] Old path: Find(id) x %d selected ids, %d cycles:\n", kSelectionSize, kCycles);
  std::printf("      avg time to retarget %d of %d objects: %.4f ms\n\n", kSelectionSize, kDocSize, old_avg_ms);

  // ---- 2. New path: one FindMany(ids) call ----------------------------------
  double new_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) new_total_ms += TimeFindManyChangeLayer(doc, selection, c % 2, checksum);
  const double new_avg_ms = new_total_ms / kCycles;
  std::printf("[2] New path: one FindMany(ids) call, %d cycles:\n", kCycles);
  std::printf("      avg time to retarget %d of %d objects: %.4f ms\n\n", kSelectionSize, kDocSize, new_avg_ms);

  const double speedup = old_avg_ms / new_avg_ms;
  std::printf("[3] Old was O(selection size x document size); new is O(document size + selection size):\n");
  std::printf("      %.4f ms -> %.4f ms, %.1fx less work to retarget the same %d-object selection\n", old_avg_ms,
              new_avg_ms, speedup, kSelectionSize);
  std::printf("      (checksum %ld, only to keep the compiler from discarding the work above)\n", checksum);

  // Same-run, relative regression guard (see find_bench.cpp's comment on why
  // this is a runtime check, not assert()).
  if (!(new_avg_ms * 5.0 < old_avg_ms)) {
    std::fprintf(stderr,
                  "FAIL: FindMany() layer retarget (%.4f ms) should be >5x faster than a Find()-per-id loop (%.4f "
                  "ms) on a %d-object document\n",
                  new_avg_ms, old_avg_ms, kDocSize);
    return 1;
  }
  std::printf("\nOK: FindMany() layer retarget is >5x faster than the old Find()-per-id loop on this %d-object document.\n",
              kDocSize);
  return 0;
}
