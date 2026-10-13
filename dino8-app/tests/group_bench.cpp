// Standalone timing benchmark + regression guard for Document::Ungroup()
// (backing the Ungroup/RemoveFromGroup commands) and Document::
// RemoveEmptyGroups() (the Purge command, cmd_layer.cpp), which share a
// private PruneEmptyGroups() helper.
//
// Build and run directly:
//   cmake --build build --target dino8_group_bench -j$(nproc)
//   ./build/dino8_group_bench
//
// Methodology: pruning empty groups used to call Document::GroupMembers(id)
// - an O(document size) linear scan - once per *existing* group, to test
// whether that group had any members left. So on a document with many
// groups (routine after grouping lots of individually-tagged annotation/
// table/block/dimension objects - every one of those commands calls
// CreateGroup() to tag its own output), the prune cost O(group count x
// document size) - independent of how many ids Ungroup() was actually
// given, so even ungrouping a single object could be this slow. Counting
// every object's group_id in one O(document size) pass and testing each
// group against that count map in O(1) makes the whole prune O(document
// size + group count).
//
// Same style as properties_bench.cpp/layer_bench.cpp (same author, same
// codebase): asserts a same-run relative speedup rather than an absolute
// millisecond budget, so it stays robust on a loaded/shared build machine
// while still catching a real regression. Document::Ungroup()/
// RemoveEmptyGroups() are already the fixed (PruneEmptyGroups()-based)
// versions, so the "old" side below reimplements the pre-fix
// GroupMembers()-per-group loop shape locally for comparison.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "doc/Document.h"

using dino8::app::Document;
using dino8::app::Group;
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

// The pre-fix call pattern: Find(id) once per id being ungrouped (already
// benchmarked in isolation by properties_bench.cpp/layer_bench.cpp), then
// GroupMembers(g.id) - an O(document size) scan - once per *existing*
// group to test whether it's now empty.
double TimeOldUngroup(Document& doc, const std::vector<ObjectId>& ids, long& checksum) {
  Clock::time_point t0 = Clock::now();
  for (ObjectId id : ids) {
    if (SceneObject* o = doc.Find(id)) o->group_id = -1;
  }
  std::vector<Group>& groups = doc.Groups();
  groups.erase(std::remove_if(groups.begin(), groups.end(),
                               [&doc](const Group& g) { return doc.GroupMembers(g.id).empty(); }),
               groups.end());
  const double ms = MillisSince(t0);
  checksum += static_cast<long>(groups.size());
  return ms;
}

// The post-fix call pattern: Document's real (fixed) Ungroup(), which uses
// FindMany() plus the single-pass PruneEmptyGroups() count map.
double TimeNewUngroup(Document& doc, const std::vector<ObjectId>& ids, long& checksum) {
  Clock::time_point t0 = Clock::now();
  doc.Ungroup(ids);
  const double ms = MillisSince(t0);
  checksum += static_cast<long>(doc.Groups().size());
  return ms;
}

}  // namespace

int main() {
  const int kDocSize = 30000;
  const int kBackgroundGroups = 3000;   // groups that stay put across every cycle
  const int kBackgroundGroupSize = 4;   // each covers this many objects
  const int kVictimSize = 200;          // objects actually passed to Ungroup() each cycle
  const int kCycles = 8;

  Document doc;
  std::printf("Building a %d-object document (each a %d-face cylinder mesh)...\n", kDocSize,
              Mesh::Cylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 2.0, 8, 1).FaceCount());
  std::vector<ObjectId> ids;
  ids.reserve(kDocSize);
  Clock::time_point t0 = Clock::now();
  for (int i = 0; i < kDocSize; ++i) ids.push_back(doc.Add(MakeBenchObject(static_cast<double>(i))));
  std::printf("  built in %.1f ms\n", MillisSince(t0));

  // kBackgroundGroups small groups, scattered across the document, that
  // are never touched by the Ungroup() calls below - like a document that
  // accumulated groups from lots of earlier Text/Hatch/Table/Dimension/
  // Block commands (every one of those tags its own output via
  // CreateGroup()). These are what makes the old GroupMembers()-per-group
  // prune expensive, independent of the (small) selection Ungroup() is
  // actually given.
  std::printf("Creating %d background groups of %d objects each...\n", kBackgroundGroups, kBackgroundGroupSize);
  for (int g = 0; g < kBackgroundGroups; ++g) {
    std::vector<ObjectId> members;
    members.reserve(kBackgroundGroupSize);
    for (int m = 0; m < kBackgroundGroupSize; ++m) {
      const size_t idx = (static_cast<size_t>(g) * kBackgroundGroupSize + m) % ids.size();
      members.push_back(ids[idx]);
    }
    doc.CreateGroup(members, "Background");
  }
  std::printf("  document now has %zu group(s)\n\n", doc.Groups().size());

  // A small "victim" selection, scattered across the document like a real
  // window/lasso select, that gets grouped then ungrouped once per timed
  // cycle - the background groups above are the ones that make the old
  // path's cost balloon, not this selection's size.
  std::vector<ObjectId> victim;
  victim.reserve(kVictimSize);
  for (int i = 0; i < kVictimSize; ++i) {
    victim.push_back(ids[static_cast<size_t>(i) * static_cast<size_t>(kDocSize) / kVictimSize]);
  }

  long checksum = 0;

  // ---- 1. Old path: Find(id) per id + GroupMembers(id) per group -----------
  double old_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) {
    doc.CreateGroup(victim, "Victim");  // untimed setup: recreate what the previous cycle ungrouped
    old_total_ms += TimeOldUngroup(doc, victim, checksum);
  }
  const double old_avg_ms = old_total_ms / kCycles;
  std::printf("[1] Old path: Find(id) x %d + GroupMembers(id) x %zu groups, %d cycles:\n", kVictimSize,
              doc.Groups().size() + 1, kCycles);
  std::printf("      avg time to ungroup %d objects on a %d-object document: %.4f ms\n\n", kVictimSize, kDocSize,
              old_avg_ms);

  // ---- 2. New path: FindMany() + single-pass PruneEmptyGroups() ------------
  double new_total_ms = 0;
  for (int c = 0; c < kCycles; ++c) {
    doc.CreateGroup(victim, "Victim");  // untimed setup
    new_total_ms += TimeNewUngroup(doc, victim, checksum);
  }
  const double new_avg_ms = new_total_ms / kCycles;
  std::printf("[2] New path: Document::Ungroup() (FindMany() + PruneEmptyGroups()), %d cycles:\n", kCycles);
  std::printf("      avg time to ungroup %d objects on a %d-object document: %.4f ms\n\n", kVictimSize, kDocSize,
              new_avg_ms);

  const double speedup = old_avg_ms / new_avg_ms;
  std::printf("[3] Old was O(group count x document size); new is O(document size + group count):\n");
  std::printf("      %.4f ms -> %.4f ms, %.1fx less work to ungroup %d objects on a document with ~%d groups\n",
              old_avg_ms, new_avg_ms, speedup, kVictimSize, kBackgroundGroups);
  std::printf("      (checksum %ld, only to keep the compiler from discarding the work above)\n", checksum);

  // Same-run, relative regression guard (see find_bench.cpp's comment on
  // why this is a runtime check, not assert()).
  if (!(new_avg_ms * 20.0 < old_avg_ms)) {
    std::fprintf(stderr,
                  "FAIL: PruneEmptyGroups() Ungroup() (%.4f ms) should be >20x faster than the old "
                  "GroupMembers()-per-group prune (%.4f ms) on a %d-object document with ~%d groups\n",
                  new_avg_ms, old_avg_ms, kDocSize, kBackgroundGroups);
    return 1;
  }
  std::printf(
      "\nOK: Document::Ungroup()'s single-pass PruneEmptyGroups() is >20x faster than the old "
      "GroupMembers()-per-group prune on this %d-object document with ~%d groups.\n",
      kDocSize, kBackgroundGroups);
  return 0;
}
