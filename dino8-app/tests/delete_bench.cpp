// Standalone timing benchmark + regression guard for Document::Remove() in a
// loop vs Document::RemoveMany() on the "select k of N objects, then delete
// them" path (cmd_edit.cpp's "Delete" command - see the RemoveMany()
// comment on Document.h and its call site for the fix this measures).
//
// Build and run directly:
//   cmake --build build --target dino8_delete_bench -j$(nproc)
//   ./build/dino8_delete_bench
//
// Methodology: Document::Remove(id) is a linear O(document size) find_if()
// scan *plus* an O(document size) vector::erase() shift of every following
// element. Before this change, the "Delete" command called it once per
// selected id, so deleting a k-object selection cost up to O(k * document
// size) - and worse than a flat O(k*N), since each erase's shift cost
// depends on how much of the (shrinking) vector survives past the removed
// element, so a scattered k-of-N deletion degrades toward the vector staying
// almost as long for most of the k erases. Document::RemoveMany(ids) instead
// builds one selection-sized id set in O(k), then does a single
// erase(remove_if(...)) pass over objects_ - one shift of the survivors
// instead of one shift per removed id - for O(document size + k) total.
//
// Like tests/find_bench.cpp (same style), this asserts a same-run, relative
// speedup rather than an absolute millisecond budget, so it stays robust on
// a loaded/shared build machine while still catching a real regression.
// Unlike find_bench.cpp, Remove()/RemoveMany() mutate the document, so each
// timed cycle needs its own fresh copy of the document to delete from; to
// keep that copy cheap, the underlying SceneObjects are built once up front
// and then just copied (via Document::Add) into a fresh Document per cycle,
// so what's timed is the deletion itself, not mesh construction.
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

// Builds a fresh Document by copying `templates` into it via Add(), which
// (since Document::Add assigns ids sequentially from 1 for a freshly
// constructed Document) always yields the same id for the same template
// index, so a single precomputed `selection` of ids is valid against every
// freshly built document below.
Document BuildDocument(const std::vector<SceneObject>& templates) {
  Document doc;
  for (const SceneObject& o : templates) doc.Add(o);
  return doc;
}

}  // namespace

int main() {
  const int kDocSize = 40000;
  const int kSelectionSize = 4000;  // a "window-selected a big chunk of the model" fraction (10%)
  const int kCycles = 8;

  std::printf("Building a %d-object template (each a %d-face cylinder mesh)...\n", kDocSize,
              Mesh::Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 2.0, 8, 1).FaceCount());
  std::vector<SceneObject> templates;
  templates.reserve(kDocSize);
  Clock::time_point t0 = Clock::now();
  for (int i = 0; i < kDocSize; ++i) templates.push_back(MakeBenchObject(static_cast<double>(i)));
  std::printf("  built in %.1f ms\n\n", MillisSince(t0));

  // Selection scattered across the document (a real window/lasso select over
  // a spread-out region, not a worst/best-case clump), like find_bench.cpp's
  // selection. Ids are 1..kDocSize in template order (see BuildDocument()).
  std::vector<ObjectId> selection;
  selection.reserve(kSelectionSize);
  for (int i = 0; i < kSelectionSize; ++i) {
    const int idx = static_cast<int>(static_cast<size_t>(i) * static_cast<size_t>(kDocSize) / kSelectionSize);
    selection.push_back(static_cast<ObjectId>(idx + 1));  // ids start at 1
  }

  // ---- 1. Old path: Remove(id) once per selected id ------------------------
  double old_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) {
    Document doc = BuildDocument(templates);
    Clock::time_point cycle_t0 = Clock::now();
    for (ObjectId id : selection) doc.Remove(id);
    old_total_ms += MillisSince(cycle_t0);
  }
  const double old_avg_ms = old_total_ms / kCycles;
  std::printf("[1] Old path: Remove(id) x %d (once per selected object), %d cycles:\n", kSelectionSize, kCycles);
  std::printf("      avg time to delete %d of %d objects: %.4f ms\n\n", kSelectionSize, kDocSize, old_avg_ms);

  // ---- 2. New path: one RemoveMany(ids) call -------------------------------
  double new_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) {
    Document doc = BuildDocument(templates);
    Clock::time_point cycle_t0 = Clock::now();
    doc.RemoveMany(selection);
    new_total_ms += MillisSince(cycle_t0);
  }
  const double new_avg_ms = new_total_ms / kCycles;
  std::printf("[2] New path: one RemoveMany(ids) call, %d cycles:\n", kCycles);
  std::printf("      avg time to delete %d of %d objects: %.4f ms\n\n", kSelectionSize, kDocSize, new_avg_ms);

  const double speedup = old_avg_ms / new_avg_ms;
  std::printf("[3] Old was up to O(selection size x document size); new is O(document size + selection size):\n");
  std::printf("      %.4f ms -> %.4f ms, %.1fx less work to delete the same %d-object selection\n", old_avg_ms,
              new_avg_ms, speedup, kSelectionSize);

  // Same-run, relative regression guard (see find_bench.cpp's comment on why
  // this is a runtime check, not assert()). 8x is well below the
  // kSelectionSize/~few asymptotic win a 4000-of-40000 scattered deletion
  // should give (each old-path Remove() both scans and shifts O(document
  // size) elements), so this won't flake on a loaded machine while still
  // catching a real regression back to a Remove()-per-id loop.
  if (!(new_avg_ms * 8.0 < old_avg_ms)) {
    std::fprintf(stderr,
                  "FAIL: RemoveMany() (%.4f ms) should be >8x faster than a Remove()-per-id loop (%.4f ms) "
                  "on a %d-object document\n",
                  new_avg_ms, old_avg_ms, kDocSize);
    return 1;
  }
  std::printf("\nOK: RemoveMany() is >8x faster than the old Remove()-per-id loop on this %d-object document.\n",
              kDocSize);
  return 0;
}
