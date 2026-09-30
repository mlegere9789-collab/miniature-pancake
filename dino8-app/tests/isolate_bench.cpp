// Standalone timing benchmark + regression guard for the "Isolate" command's
// selection-membership test (cmd_edit.cpp) - std::find(ids...) inside a loop
// over every document object vs hashing the selection once first.
//
// Build and run directly:
//   cmake --build build --target dino8_isolate_bench -j$(nproc)
//   ./build/dino8_isolate_bench
//
// Methodology: before this change, Isolate set each object's visibility with
// `std::find(ids.begin(), ids.end(), o.id) != ids.end()` inside a loop over
// every one of the document's N objects - an O(document size) linear scan of
// the k-object selection, run once per object, for O(N * k) total. That's
// worse than the already-fixed Find()-per-id loops elsewhere in this file
// (Hide/Lock, see HideShow's FindMany() comment): those are O(k * N) with the
// smaller factor (k) as the outer loop, while Isolate's nested scan runs the
// *larger* factor (N) as the outer loop over the *smaller*
// factor (k) each time, so scaling either the document or the selection
// degrades it identically. Hashing the selection into an unordered_set once
// (O(k)) turns each per-object membership test into O(1), for O(N + k)
// total - the same shape win Document::FindMany() gives a Find()-per-id
// loop, just via std::unordered_set instead of Document's own index.
//
// Like tests/find_bench.cpp and tests/delete_bench.cpp (same style), this
// asserts a same-run, relative speedup rather than an absolute millisecond
// budget, so it stays robust on a loaded/shared build machine while still
// catching a real regression.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
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

// The pre-fix call pattern: std::find(ids...) once per document object.
double TimeStdFindIsolate(Document& doc, const std::vector<ObjectId>& ids) {
  Clock::time_point t0 = Clock::now();
  for (SceneObject& o : doc.Objects()) {
    o.visible = std::find(ids.begin(), ids.end(), o.id) != ids.end();
  }
  return MillisSince(t0);
}

// The post-fix call pattern: hash the selection once, then O(1) lookups.
double TimeHashSetIsolate(Document& doc, const std::vector<ObjectId>& ids) {
  Clock::time_point t0 = Clock::now();
  const std::unordered_set<ObjectId> sel(ids.begin(), ids.end());
  for (SceneObject& o : doc.Objects()) {
    o.visible = sel.count(o.id) != 0;
  }
  return MillisSince(t0);
}

}  // namespace

int main() {
  const int kDocSize = 40000;
  const int kSelectionSize = 4000;  // a "window-selected a big chunk of the model" fraction (10%)
  const int kCycles = 30;

  Document doc;
  std::printf("Building a %d-object document (each a %d-face cylinder mesh)...\n", kDocSize,
              Mesh::Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 2.0, 8, 1).FaceCount());
  std::vector<ObjectId> ids;
  ids.reserve(kDocSize);
  Clock::time_point t0 = Clock::now();
  for (int i = 0; i < kDocSize; ++i) ids.push_back(doc.Add(MakeBenchObject(static_cast<double>(i))));
  std::printf("  built in %.1f ms\n\n", MillisSince(t0));

  // Selection scattered across the document, like a real window/lasso select
  // over a spread-out region (same style as delete_bench.cpp's selection).
  std::vector<ObjectId> selection;
  selection.reserve(kSelectionSize);
  for (int i = 0; i < kSelectionSize; ++i) {
    selection.push_back(ids[static_cast<size_t>(i) * static_cast<size_t>(kDocSize) / kSelectionSize]);
  }

  // ---- 1. Old path: std::find(ids...) once per document object ------------
  double old_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) old_total_ms += TimeStdFindIsolate(doc, selection);
  const double old_avg_ms = old_total_ms / kCycles;
  std::printf("[1] Old path: std::find(ids...) x %d (once per document object), %d cycles:\n", kDocSize, kCycles);
  std::printf("      avg time to isolate %d of %d objects: %.4f ms\n\n", kSelectionSize, kDocSize, old_avg_ms);

  // ---- 2. New path: hash the selection once, then O(1) lookups ------------
  double new_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) new_total_ms += TimeHashSetIsolate(doc, selection);
  const double new_avg_ms = new_total_ms / kCycles;
  std::printf("[2] New path: one unordered_set build + O(1) lookups, %d cycles:\n", kCycles);
  std::printf("      avg time to isolate %d of %d objects: %.4f ms\n\n", kSelectionSize, kDocSize, new_avg_ms);

  const double speedup = old_avg_ms / new_avg_ms;
  std::printf("[3] Old was O(document size x selection size); new is O(document size + selection size):\n");
  std::printf("      %.4f ms -> %.4f ms, %.1fx less work to isolate the same %d-object selection\n", old_avg_ms,
              new_avg_ms, speedup, kSelectionSize);

  // Same-run, relative regression guard (see find_bench.cpp's comment on why
  // this is a runtime check, not assert()). 8x is well below the
  // ~kSelectionSize/~few asymptotic win a 4000-of-40000 scattered isolate
  // should give, so this won't flake on a loaded machine while still
  // catching a real regression back to a std::find-per-object scan.
  if (!(new_avg_ms * 8.0 < old_avg_ms)) {
    std::fprintf(stderr,
                  "FAIL: unordered_set isolate (%.4f ms) should be >8x faster than a std::find-per-object scan "
                  "(%.4f ms) on a %d-object document\n",
                  new_avg_ms, old_avg_ms, kDocSize);
    return 1;
  }
  std::printf("\nOK: unordered_set isolate is >8x faster than the old std::find-per-object scan on this %d-object "
              "document.\n",
              kDocSize);
  return 0;
}
