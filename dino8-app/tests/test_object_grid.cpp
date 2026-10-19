// Regression test for spatial/ObjectGrid.cpp's EnsureFresh(): a non-finite
// (NaN/Infinity) object bounding box used to flow straight into the scene-
// wide extent via std::min/std::max and then into CellOf()'s
// static_cast<int>(std::floor(...)) with no check in between.
//
// Reaching a non-finite bounding box isn't hypothetical - rs.AddPoint()/
// dino8.AddPoint() (LuaEngine.cpp/PythonEngine.cpp) take their x/y/z straight
// through to SceneObject::MakePoint() with no finiteness validation, and
// SceneObject::BoundingBox() for a point object hands that point straight
// back out as both corners (SceneObject.cpp's ExpandBox, called with
// has_bbox initially false, sets box.min = box.max = p unconditionally). So
// a single `rs.AddPoint(0/0, 0/0, 0/0)`-style script call is enough to put a
// NaN-boxed object in the document.
//
// Once that happens, std::min(NaN, finite) / std::max(NaN, finite) propagate
// the NaN forward (NaN compares false against everything, so the
// accumulator - whichever argument already held it - is what each call
// returns), contaminating the *entire* scene bbox (ObjectGrid::origin_/
// extent_), not just the one degenerate object's own entry. CellOf() then
// computes (anything - NaN) = NaN for every object's cell key, and
// converting a non-finite double to int is undefined behaviour in C++ -
// unlike the already-guarded `cell_size_` a few lines below the bug, which
// has its own std::isfinite check.
//
// Build and run directly:
//   cmake --build build --target dino8_test_object_grid -j$(nproc)
//   ./build/dino8_test_object_grid
#include <cmath>
#include <cstdio>
#include <limits>

#include "doc/Document.h"
#include "viewport/Camera.h"  // Ray

using dino8::app::Document;
using dino8::app::ObjectGrid;
using dino8::app::Ray;
using dino8::app::SceneObject;
using dino8::kernel::BoundingBox;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

bool AllFinite(const BoundingBox& b) {
  return std::isfinite(b.min.x) && std::isfinite(b.min.y) && std::isfinite(b.min.z) &&
         std::isfinite(b.max.x) && std::isfinite(b.max.y) && std::isfinite(b.max.z);
}
}  // namespace

int main() {
  const double nan = std::nan("");

  // Baseline sanity check with no degenerate object at all: a grid over two
  // ordinary points should report a finite extent and find the right object
  // by both a box and a ray query. If this fails, something more basic than
  // the NaN-contamination bug below is broken.
  {
    Document doc;
    doc.Add(SceneObject::MakePoint(Point3d(1, 1, 1)));
    doc.Add(SceneObject::MakePoint(Point3d(5, 5, 5)));

    ObjectGrid& grid = doc.PickGrid();
    grid.EnsureFresh(doc);

    BoundingBox ext;
    Check(grid.Extent(ext), "baseline: Extent() reports an extent for a non-empty document");
    Check(AllFinite(ext), "baseline: Extent() is finite with no degenerate object present");

    const auto hits = grid.QueryBox(BoundingBox{Point3d(4, 4, 4), Point3d(6, 6, 6)});
    bool found_second = false;
    for (std::size_t idx : hits) if (doc.Objects()[idx].point.x == 5) found_second = true;
    Check(found_second, "baseline: QueryBox around (5,5,5) finds the point placed there");
  }

  // The actual regression: a NaN-boxed point (as rs.AddPoint(0/0,0/0,0/0)
  // would create) placed *first*, so it seeds ObjectGrid's scene bbox
  // accumulator directly, ahead of an ordinary, well-formed point object.
  {
    Document doc;
    doc.Add(SceneObject::MakePoint(Point3d(nan, nan, nan)));
    doc.Add(SceneObject::MakePoint(Point3d(5, 5, 5)));

    ObjectGrid& grid = doc.PickGrid();
    grid.EnsureFresh(doc);

    BoundingBox ext;
    const bool has_extent = grid.Extent(ext);
    Check(has_extent, "a NaN-boxed object alongside a real one still leaves the grid non-empty");
    Check(has_extent && AllFinite(ext),
          "a NaN-boxed object's bounding box does not contaminate the grid's scene-wide extent");

    // The real point must still be spatially findable - the bug's actual
    // user-visible symptom was that *every* object's cell key went bad
    // (NaN - origin_), not just the degenerate one's, breaking picking for
    // the whole document.
    const auto hits = grid.QueryBox(BoundingBox{Point3d(4, 4, 4), Point3d(6, 6, 6)});
    bool found_real_point = false;
    for (std::size_t idx : hits) if (doc.Objects()[idx].point.x == 5) found_real_point = true;
    Check(found_real_point, "QueryBox around (5,5,5) still finds the real point despite the NaN-boxed object");

    const auto ray_hits = grid.QueryRay(Ray{Point3d(5, 5, -100), Vector3d(0, 0, 1)}, 1000.0);
    bool ray_found_real_point = false;
    for (std::size_t idx : ray_hits) if (doc.Objects()[idx].point.x == 5) ray_found_real_point = true;
    Check(ray_found_real_point, "a ray toward (5,5,5) still finds the real point despite the NaN-boxed object");
  }

  // Same, but with an Infinity-boxed object instead of NaN, and with the
  // degenerate object added *second* this time (so the contamination has to
  // come from the general-case std::min/std::max path, not just the
  // objects.front() seed).
  {
    Document doc;
    doc.Add(SceneObject::MakePoint(Point3d(5, 5, 5)));
    doc.Add(SceneObject::MakePoint(Point3d(std::numeric_limits<double>::infinity(), 0, 0)));

    ObjectGrid& grid = doc.PickGrid();
    grid.EnsureFresh(doc);

    BoundingBox ext;
    const bool has_extent = grid.Extent(ext);
    Check(has_extent && AllFinite(ext),
          "an Infinity-boxed object added after a real one still leaves a finite scene extent");

    const auto hits = grid.QueryBox(BoundingBox{Point3d(4, 4, 4), Point3d(6, 6, 6)});
    bool found_real_point = false;
    for (std::size_t idx : hits) if (doc.Objects()[idx].point.x == 5) found_real_point = true;
    Check(found_real_point, "QueryBox around (5,5,5) still finds the real point despite the Infinity-boxed object");
  }

  // Degenerate case: every object in the document has a non-finite box.
  // Must behave like an empty grid (no crash, no finite-looking garbage
  // extent), not divide-by-zero or produce a bogus "finite" answer.
  {
    Document doc;
    doc.Add(SceneObject::MakePoint(Point3d(nan, nan, nan)));
    doc.Add(SceneObject::MakePoint(Point3d(nan, 0, 0)));

    ObjectGrid& grid = doc.PickGrid();
    grid.EnsureFresh(doc);

    BoundingBox ext;
    Check(!grid.Extent(ext), "a document where every object's box is non-finite reports an empty grid");
    Check(grid.Empty(), "a document where every object's box is non-finite reports Empty()");
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
