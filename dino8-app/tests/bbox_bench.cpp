// Standalone timing benchmark + regression guard for Document::BoundingBoxOf()
// (src/doc/Document.cpp), the function behind Gumball::Update()'s per-frame
// "where's the selection's centre" query (src/ui/Gumball.cpp): resolving the
// selection via `Document::Find(id)` once per id vs one `Document::FindMany()`
// call.
//
// Build and run directly:
//   cmake --build build --target dino8_bbox_bench -j$(nproc)
//   ./build/dino8_bbox_bench
//
// Methodology: Document::Find(id) is a linear O(document size) scan; before
// this change, BoundingBoxOf(ids, ...) called it once per id in `ids`, so
// computing a k-object selection's bounding box cost O(k * document size) -
// the same Find()-per-id bug already fixed for the property/transform/
// layer/group commands (see find_bench.cpp/array_bench.cpp/layer_bench.cpp/
// group_bench.cpp). Unlike those, though, this call site isn't a one-shot
// command: Gumball::Update() calls doc.BoundingBoxOf(doc.SelectedIds(), ...)
// once per rendered frame for every viewport, for as long as anything stays
// selected and the gumball isn't mid-drag - so on a large document with even
// a modest selection, the quadratic cost ran continuously at the UI frame
// rate rather than once per user action. The fix resolves the whole
// selection in one Document::FindMany(ids) call, which builds a
// selection-sized hash map and fills it in a single O(document size +
// selection size) pass.
//
// Like find_bench.cpp/array_bench.cpp/layer_bench.cpp (same style), this
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
using dino8::kernel::BoundingBox;
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
// exactly like BoundingBoxOf()'s old loop.
double TimeFindLoopBoundingBox(const Document& doc, const std::vector<ObjectId>& ids, long& checksum) {
  Clock::time_point t0 = Clock::now();
  bool has = false;
  BoundingBox out;
  for (ObjectId id : ids) {
    const SceneObject* o = doc.Find(id);
    if (!o) continue;
    const BoundingBox b = o->BoundingBox();
    if (!has) {
      out = b;
      has = true;
    } else {
      out.min.x = std::min(out.min.x, b.min.x);
      out.min.y = std::min(out.min.y, b.min.y);
      out.min.z = std::min(out.min.z, b.min.z);
      out.max.x = std::max(out.max.x, b.max.x);
      out.max.y = std::max(out.max.y, b.max.y);
      out.max.z = std::max(out.max.z, b.max.z);
    }
  }
  checksum += has ? static_cast<long>(out.min.x + out.max.x) : 0;
  return MillisSince(t0);
}

// The post-fix call pattern: Document's real (fixed) BoundingBoxOf(), which
// uses FindMany().
double TimeFindManyBoundingBox(const Document& doc, const std::vector<ObjectId>& ids, long& checksum) {
  Clock::time_point t0 = Clock::now();
  BoundingBox out;
  const bool has = doc.BoundingBoxOf(ids, out);
  const double ms = MillisSince(t0);
  checksum += has ? static_cast<long>(out.min.x + out.max.x) : 0;
  return ms;
}

}  // namespace

int main() {
  const int kDocSize = 100000;
  const int kSelectionSize = 1000;  // "selected 1000 parts, gumball tracks their centre every frame"
  const int kCycles = 20;           // simulated rendered frames

  Document doc;
  std::printf("Building a %d-object document (each a %d-face cylinder mesh)...\n", kDocSize,
              Mesh::Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 2.0, 8, 1).FaceCount());
  std::vector<ObjectId> ids;
  ids.reserve(kDocSize);
  Clock::time_point t0 = Clock::now();
  for (int i = 0; i < kDocSize; ++i) ids.push_back(doc.Add(MakeBenchObject(static_cast<double>(i))));
  std::printf("  built in %.1f ms\n\n", MillisSince(t0));

  // Selection scattered across the document, like a real window/lasso select
  // over a spread-out region (same style as array_bench.cpp/layer_bench.cpp's
  // selection).
  std::vector<ObjectId> selection;
  selection.reserve(kSelectionSize);
  for (int i = 0; i < kSelectionSize; ++i) {
    selection.push_back(ids[static_cast<size_t>(i) * static_cast<size_t>(kDocSize) / kSelectionSize]);
  }

  long checksum = 0;

  // ---- 1. Old path: Find(id) once per selected id, per simulated frame -----
  double old_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) old_total_ms += TimeFindLoopBoundingBox(doc, selection, checksum);
  const double old_avg_ms = old_total_ms / kCycles;
  std::printf("[1] Old path: Find(id) x %d selected ids, %d simulated frames:\n", kSelectionSize, kCycles);
  std::printf("      avg time to bound %d of %d objects: %.4f ms\n\n", kSelectionSize, kDocSize, old_avg_ms);

  // ---- 2. New path: one FindMany(ids) call, per simulated frame ------------
  double new_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) new_total_ms += TimeFindManyBoundingBox(doc, selection, checksum);
  const double new_avg_ms = new_total_ms / kCycles;
  std::printf("[2] New path: Document::BoundingBoxOf() (one FindMany(ids) call), %d simulated frames:\n", kCycles);
  std::printf("      avg time to bound %d of %d objects: %.4f ms\n\n", kSelectionSize, kDocSize, new_avg_ms);

  const double speedup = old_avg_ms / new_avg_ms;
  std::printf("[3] Old was O(selection size x document size); new is O(document size + selection size):\n");
  std::printf("      %.4f ms -> %.4f ms, %.1fx less work per frame to bound the same %d-object selection\n",
              old_avg_ms, new_avg_ms, speedup, kSelectionSize);
  std::printf("      (checksum %ld, only to keep the compiler from discarding the work above)\n", checksum);

  // Same-run, relative regression guard (see find_bench.cpp's comment on why
  // this is a runtime check, not assert()).
  if (!(new_avg_ms * 5.0 < old_avg_ms)) {
    std::fprintf(stderr,
                  "FAIL: FindMany()-based BoundingBoxOf() (%.4f ms) should be >5x faster than a Find()-per-id loop "
                  "(%.4f ms) on a %d-object document\n",
                  new_avg_ms, old_avg_ms, kDocSize);
    return 1;
  }
  std::printf(
      "\nOK: Document::BoundingBoxOf() is >5x faster than the old Find()-per-id loop on this %d-object "
      "document - and this one runs every frame, not once per command.\n",
      kDocSize);
  return 0;
}
