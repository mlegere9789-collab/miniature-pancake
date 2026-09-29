// Standalone timing benchmark + regression guard for Document::Find() vs
// Document::FindMany() on the "select k of N objects, then transform them"
// path (cmd_transform.cpp's ApplyXform/PreviewXform - see the FindMany()
// comment on Document.h and its call sites for the fix this measures).
//
// Build and run directly:
//   cmake --build build --target dino8_find_bench -j$(nproc)
//   ./build/dino8_find_bench
//
// Methodology: Document::Find(id) is a linear O(document size) scan; before
// this change, ApplyXform/PreviewXform called it once per selected id, so
// resolving a k-object selection cost O(k * document size) - worse than the
// O(document size) a single full scan would cost, on the exact "select a
// handful of objects out of a large document, then transform them" scenario
// select/transform/undo benchmarks like this one are meant to exercise.
// Document::FindMany(ids) instead builds one id->pointer index in a single
// O(document size) pass and then resolves all k ids against it in O(k), i.e.
// O(document size + k) total - both primitives are still present on
// Document (FindMany is new; Find(id) remains exactly as before and is still
// used everywhere a single id is resolved), so timing them side by side on
// the same document is a faithful, non-simulated comparison, exactly the
// style tests/undo_bench.cpp uses for BeginChange vs BeginChangeForObjects.
//
// This one DOES assert (unlike undo_bench.cpp, which is numbers-only by
// design - see its header comment): the ratio asserted on is a same-run,
// same-machine relative comparison (new path vs old path back to back, many
// times), not an absolute millisecond budget, so it stays robust under a
// loaded/shared build machine while still catching a real regression.
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

// The pre-fix call pattern: one Document::Find(id) call per selected id.
// Kept here, calling the still-present Find(id) API, so this benchmark
// times the real old code path rather than a re-simulation of deleted code.
double TimeFindLoopAndTransform(Document& doc, const std::vector<ObjectId>& ids, const ON_Xform& xf) {
  Clock::time_point t0 = Clock::now();
  for (ObjectId id : ids) {
    SceneObject* o = doc.Find(id);
    if (o) o->Transform(xf);
  }
  return MillisSince(t0);
}

// The post-fix call pattern: one Document::FindMany(ids) call.
double TimeFindManyAndTransform(Document& doc, const std::vector<ObjectId>& ids, const ON_Xform& xf) {
  Clock::time_point t0 = Clock::now();
  for (SceneObject* o : doc.FindMany(ids)) {
    if (o) o->Transform(xf);
  }
  return MillisSince(t0);
}

}  // namespace

int main() {
  const int kDocSize = 40000;
  const int kSelectionSize = 25;  // "selected a handful of parts", like undo_bench.cpp
  const int kCycles = 600;

  Document doc;
  std::printf("Building a %d-object document (each a %d-face cylinder mesh)...\n", kDocSize,
              Mesh::Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 2.0, 8, 1).FaceCount());
  std::vector<ObjectId> ids;
  ids.reserve(kDocSize);
  Clock::time_point t0 = Clock::now();
  for (int i = 0; i < kDocSize; ++i) ids.push_back(doc.Add(MakeBenchObject(static_cast<double>(i))));
  std::printf("  built in %.1f ms\n\n", MillisSince(t0));

  // Selection scattered across the document (worst case for a linear scan:
  // no id is consistently near the front), like a real click/window select
  // would produce, not the doc's own insertion order.
  std::vector<ObjectId> selection;
  selection.reserve(kSelectionSize);
  for (int i = 0; i < kSelectionSize; ++i) {
    selection.push_back(ids[static_cast<size_t>(i) * static_cast<size_t>(kDocSize) / kSelectionSize]);
  }
  const ON_Xform step = ON_Xform::TranslationTransformation(ON_3dVector(1, 0, 0));
  const ON_Xform back = ON_Xform::TranslationTransformation(ON_3dVector(-1, 0, 0));

  // ---- 1. Old path: Find(id) once per selected id -------------------------
  double old_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) {
    old_total_ms += TimeFindLoopAndTransform(doc, selection, step);
    TimeFindLoopAndTransform(doc, selection, back);  // move back, untimed
  }
  const double old_avg_ms = old_total_ms / kCycles;
  std::printf("[1] Old path: Find(id) x %d (once per selected object), %d cycles:\n", kSelectionSize, kCycles);
  std::printf("      avg resolve+transform %d of %d objects: %.4f ms\n\n", kSelectionSize, kDocSize, old_avg_ms);

  // ---- 2. New path: one FindMany(ids) call ---------------------------------
  double new_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) {
    new_total_ms += TimeFindManyAndTransform(doc, selection, step);
    TimeFindManyAndTransform(doc, selection, back);  // move back, untimed
  }
  const double new_avg_ms = new_total_ms / kCycles;
  std::printf("[2] New path: one FindMany(ids) call, %d cycles:\n", kCycles);
  std::printf("      avg resolve+transform %d of %d objects: %.4f ms\n\n", kSelectionSize, kDocSize, new_avg_ms);

  const double speedup = old_avg_ms / new_avg_ms;
  std::printf("[3] Old was O(selection size x document size); new is O(document size + selection size):\n");
  std::printf("      %.4f ms -> %.4f ms, %.1fx less work to resolve+transform the same %d-object selection\n",
              old_avg_ms, new_avg_ms, speedup, kSelectionSize);

  // Same-run, relative regression guard: FindMany must be meaningfully
  // faster, not just noise-level different. A plain runtime check, not
  // assert() - this benchmark is built/run in Release (NDEBUG), which
  // compiles asserts out entirely, and a "benchmark test" that can't
  // actually fail isn't proving anything. 3x is a wide margin below the
  // ~kSelectionSize/~few asymptotic win this document size/selection size
  // should give (kSelectionSize=25 selected ids means the old path does 25x
  // the index-building work the new path does), so this won't flake on a
  // loaded machine while still catching a real regression back to an O(k*N)
  // lookup loop.
  if (!(new_avg_ms * 3.0 < old_avg_ms)) {
    std::fprintf(stderr,
                  "FAIL: FindMany() (%.4f ms) should be >3x faster than a Find()-per-id loop (%.4f ms) "
                  "on a %d-object document\n",
                  new_avg_ms, old_avg_ms, kDocSize);
    return 1;
  }
  std::printf("\nOK: FindMany() is >3x faster than the old Find()-per-id loop on this %d-object document.\n",
              kDocSize);
  return 0;
}
