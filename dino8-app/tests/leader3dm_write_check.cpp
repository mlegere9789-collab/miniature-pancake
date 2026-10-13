// leader3dm_write_check - reads a .3dm file (expected to be written by
// Dino 8 itself, via src/io/File3dm.cpp's Save3dm) directly through
// OpenNURBS' own ONX_Model/ON_Leader API - independent of Dino 8's own
// Load3dm reader - and reports every real ON_Leader object it finds.
//
// The write-side mirror of the ON_Leader half of Load3dm's own reader
// (see File3dm.cpp's ON_Leader::Cast branch): a Leader annotation Dino 8
// itself made and saved must come back as a real ON_Leader object to a
// plain, independent OpenNURBS reader, not merely to Dino 8's own Open
// command (which would also accept the pre-existing baked polyline/
// arrowhead/glyph-curve fallback and never notice the difference).
#include <opennurbs.h>

#include <cstdio>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <file.3dm>\n", argv[0]);
    return 1;
  }
  const char* path = argv[1];

  ONX_Model model;
  ON_TextLog log;
  if (!model.Read(path, &log)) {
    std::fprintf(stderr, "leader3dm_write_check: OpenNURBS could not read %s\n", path);
    return 1;
  }

  int leader_count = 0;
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
    if (!mg) continue;
    const ON_Leader* leader = ON_Leader::Cast(mg->Geometry(nullptr));
    if (!leader) continue;
    ++leader_count;

    const ON_Plane& plane = leader->Plane();
    const ON__UINT32 n = leader->PointCount();
    std::printf("leader %d: text=%ls points=%u", leader_count, static_cast<const wchar_t*>(leader->PlainText()), n);
    for (ON__UINT32 i = 0; i < n; ++i) {
      ON_2dPoint p2;
      if (!leader->Point2d(static_cast<int>(i), p2)) continue;
      const ON_3dPoint p3 = plane.PointAt(p2.x, p2.y);
      std::printf(" pt[%u]=(%g,%g,%g)", i, p3.x, p3.y, p3.z);
    }
    std::printf("\n");
  }
  std::printf("leader3dm_write_check: %d real ON_Leader object(s) found in %s\n", leader_count, path);
  return 0;
}
