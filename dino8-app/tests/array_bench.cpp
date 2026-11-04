// Standalone timing benchmark + regression guard for the transform
// commands that duplicate a selection several times under different
// transforms - Array/ArrayLinear/ArrayPolar, and ApplyXform's Copy=Yes
// path (cmd_transform.cpp) - resolving `Document::Find(id)` once per
// (copy x selected id) pair vs resolving the selection's object *data*
// once up front and reusing it for every copy.
//
// Build and run directly:
//   cmake --build build --target dino8_array_bench -j$(nproc)
//   ./build/dino8_array_bench
//
// Methodology: Document::Find(id) is a linear O(document size) scan; before
// this change, Array/ArrayLinear/ArrayPolar called it once per selected id,
// *inside* their loop over the array's copy count, so producing a C-copy
// array of a k-object selection cost O(C * k * document size) - the same
// Find()-per-id bug already fixed elsewhere (see find_bench.cpp), except
// here the copy count multiplies it on top of the selection size, making it
// the worse of the two nested-loop shapes (like isolate_bench.cpp's fix,
// but compounding rather than inverting the loop order). The fix resolves
// the source selection's object values once via Document::FindMany() -
// values, not pointers, since Document::Add() may reallocate the objects_
// vector and invalidate a pointer resolved before an earlier Add() in the
// same command - then reuses those cached values for every copy, for
// O(document size + C * k) total.
//
// Like find_bench.cpp/isolate_bench.cpp/delete_bench.cpp (same style), this
// asserts a same-run, relative speedup rather than an absolute millisecond
// budget, so it stays robust on a loaded/shared build machine while still
// catching a real regression.
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

// The pre-fix call pattern: one Document::Find(id) call per (copy x
// selected id) pair, exactly like ArrayCommand/ArrayLinearCommand/
// ArrayPolarCommand's old inner loop. Doesn't call Document::Add() (that
// cost is identical either way and not what the fix changes); instead it
// accumulates a value out of each duplicate so the loop can't be optimized
// away as dead code.
double TimeFindLoopArray(const Document& doc, const std::vector<ObjectId>& ids, const std::vector<ON_Xform>& xforms,
                          double& checksum) {
  Clock::time_point t0 = Clock::now();
  double sum = 0;
  for (const ON_Xform& xf : xforms) {
    for (ObjectId id : ids) {
      const SceneObject* o = doc.Find(id);
      if (!o) continue;
      SceneObject dup = *o;
      dup.selected = false;
      dup.Transform(xf);
      sum += dup.BoundingBox().min.x;
    }
  }
  checksum += sum;
  return MillisSince(t0);
}

// The post-fix call pattern: one Document::FindMany(ids) call resolving the
// selection's object *values* up front, then reused for every copy.
double TimeResolveOnceArray(const Document& doc, const std::vector<ObjectId>& ids,
                             const std::vector<ON_Xform>& xforms, double& checksum) {
  Clock::time_point t0 = Clock::now();
  std::vector<SceneObject> sources;
  sources.reserve(ids.size());
  for (const SceneObject* o : doc.FindMany(ids)) {
    if (o) sources.push_back(*o);
  }
  double sum = 0;
  for (const ON_Xform& xf : xforms) {
    for (const SceneObject& src : sources) {
      SceneObject dup = src;
      dup.selected = false;
      dup.Transform(xf);
      sum += dup.BoundingBox().min.x;
    }
  }
  checksum += sum;
  return MillisSince(t0);
}

}  // namespace

int main() {
  const int kDocSize = 100000;
  const int kSelectionSize = 100;  // "selected 100 parts, then arrayed them"
  const int kCopyCount = 30;       // e.g. a 30-item linear array, or a 5x6x1 grid
  const int kCycles = 5;

  Document doc;
  std::printf("Building a %d-object document (each a %d-face cylinder mesh)...\n", kDocSize,
              Mesh::Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 2.0, 8, 1).FaceCount());
  std::vector<ObjectId> ids;
  ids.reserve(kDocSize);
  Clock::time_point t0 = Clock::now();
  for (int i = 0; i < kDocSize; ++i) ids.push_back(doc.Add(MakeBenchObject(static_cast<double>(i))));
  std::printf("  built in %.1f ms\n\n", MillisSince(t0));

  // Selection scattered across the document, like a real window/lasso select
  // over a spread-out region (same style as isolate_bench.cpp's selection).
  std::vector<ObjectId> selection;
  selection.reserve(kSelectionSize);
  for (int i = 0; i < kSelectionSize; ++i) {
    selection.push_back(ids[static_cast<size_t>(i) * static_cast<size_t>(kDocSize) / kSelectionSize]);
  }

  std::vector<ON_Xform> xforms;
  xforms.reserve(kCopyCount);
  for (int i = 1; i <= kCopyCount; ++i) {
    xforms.push_back(ON_Xform::TranslationTransformation(ON_3dVector(static_cast<double>(i), 0, 0)));
  }

  double checksum = 0;

  // ---- 1. Old path: Find(id) once per (copy x selected id) pair -----------
  double old_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) old_total_ms += TimeFindLoopArray(doc, selection, xforms, checksum);
  const double old_avg_ms = old_total_ms / kCycles;
  std::printf("[1] Old path: Find(id) x %d copies x %d selected ids, %d cycles:\n", kCopyCount, kSelectionSize,
              kCycles);
  std::printf("      avg time to build a %d-copy array of %d of %d objects: %.4f ms\n\n", kCopyCount,
              kSelectionSize, kDocSize, old_avg_ms);

  // ---- 2. New path: one FindMany(ids) call, reused for every copy ---------
  double new_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) new_total_ms += TimeResolveOnceArray(doc, selection, xforms, checksum);
  const double new_avg_ms = new_total_ms / kCycles;
  std::printf("[2] New path: one FindMany(ids) call + reuse, %d cycles:\n", kCycles);
  std::printf("      avg time to build a %d-copy array of %d of %d objects: %.4f ms\n\n", kCopyCount,
              kSelectionSize, kDocSize, new_avg_ms);

  const double speedup = old_avg_ms / new_avg_ms;
  std::printf(
      "[3] Old was O(copies x selection size x document size); new is O(document size + copies x selection "
      "size):\n");
  std::printf("      %.4f ms -> %.4f ms, %.1fx less work to build the same %d-copy array\n", old_avg_ms, new_avg_ms,
              speedup, kCopyCount);
  std::printf("      (checksum %.6f, only to keep the compiler from discarding the work above)\n", checksum);

  // Same-run, relative regression guard (see find_bench.cpp's comment on why
  // this is a runtime check, not assert()). kCopyCount=30 selected-id
  // rescans per old-path cycle vs one new-path resolve means the old path
  // does on the order of 30x the index-building work the new path does, so
  // an 8x margin looked wide on paper - but unlike find_bench.cpp/
  // isolate_bench.cpp's pointer-only resolution cost, every copy here also
  // pays a real, identical-either-way Transform()+BoundingBox() cost on an
  // 8-face cylinder mesh, which dilutes the measured ratio well below the
  // index-scan theory. Confirmed flaky in practice, not just in theory: CI
  // (Windows, shared runner) measured 5.7x and 7.0x on two separate recent
  // runs, both genuine resolve-once wins (never anywhere near a 1x no-op
  // regression) but below the old 8x bar. 4x keeps a wide margin below both
  // observed values while still failing hard on an actual regression back
  // to a Find()-per-pair loop.
  if (!(new_avg_ms * 4.0 < old_avg_ms)) {
    std::fprintf(stderr,
                  "FAIL: resolve-once array build (%.4f ms) should be >4x faster than a Find()-per-(copy,id) loop "
                  "(%.4f ms) on a %d-object document\n",
                  new_avg_ms, old_avg_ms, kDocSize);
    return 1;
  }
  std::printf(
      "\nOK: resolve-once array build is >4x faster than the old Find()-per-(copy,id) loop on this %d-object "
      "document.\n",
      kDocSize);
  return 0;
}
