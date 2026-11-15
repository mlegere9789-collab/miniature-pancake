// hatch3dm_fixture_gen - writes a small, real .3dm file containing a single
// solid-filled ON_Hatch (a 10x10 square boundary on the world XY plane,
// referencing a real "Solid" ON_HatchPattern table entry), built directly
// through OpenNURBS' own ONX_Model/ON_Hatch/ON_HatchPattern API - the same
// API src/io/File3dm.cpp's Load3dm/Save3dm use - rather than through Dino 8's
// own Open/Save round trip.
//
// Regression coverage: Dino 8 itself never writes a native ON_Hatch object
// (a hatch made in-app, or read from DXF/DWG, is always baked to a trimmed
// Brep or pattern-line curves plus Hatch/... user_text metadata - see
// drafting/HatchBuild.h), so Load3dm's own ON_Hatch handling - added to read
// a hatch authored by a real, independent CAD tool such as Rhino - has no
// Dino8-authored file to prove itself against. This generator exists purely
// to give that new reader code a real externally-authored ON_Hatch to read.
#include <opennurbs.h>

#include <cstdio>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <output.3dm>\n", argv[0]);
    return 1;
  }
  const char* path = argv[1];

  ONX_Model model;
  model.m_sStartSectionComments = "hatch3dm_fixture_gen test fixture";

  // A real "Solid" hatch-pattern table entry (index assigned by the model),
  // the way a real CAD tool's own default pattern table always has one -
  // ON_Hatch::Create() itself refuses a negative pattern_index, so a real
  // file's hatch always references an actual table entry like this one.
  ON_HatchPattern pattern;
  pattern.SetName(L"Solid");
  pattern.SetFillType(ON_HatchPattern::HatchFillType::Solid);
  const ON_ModelComponentReference pattern_ref = model.AddModelComponent(pattern, true);
  const ON_HatchPattern* added_pattern = ON_HatchPattern::Cast(pattern_ref.ModelComponent());
  if (!added_pattern) {
    std::fprintf(stderr, "hatch3dm_fixture_gen: could not add the Solid hatch pattern\n");
    return 1;
  }

  // A closed 10x10 square boundary, world XY plane - same boundary the
  // existing dxf_hatch_fixture.dxf/dwg_hatch_fixture.dwg tests already use
  // (Area = 100 square), so the same smoke-test expectations apply here.
  ON_3dPointArray pts;
  pts.Append(ON_3dPoint(0.0, 0.0, 0.0));
  pts.Append(ON_3dPoint(10.0, 0.0, 0.0));
  pts.Append(ON_3dPoint(10.0, 10.0, 0.0));
  pts.Append(ON_3dPoint(0.0, 10.0, 0.0));
  pts.Append(ON_3dPoint(0.0, 0.0, 0.0));
  ON_PolylineCurve boundary(pts);

  ON_SimpleArray<const ON_Curve*> loops;
  loops.Append(&boundary);

  ON_Hatch hatch;
  if (!hatch.Create(ON_xy_plane, loops, added_pattern->Index(), 0.0, 1.0)) {
    std::fprintf(stderr, "hatch3dm_fixture_gen: ON_Hatch::Create failed\n");
    return 1;
  }

  ON_3dmObjectAttributes attr;
  model.AddModelGeometryComponent(new ON_Hatch(hatch), &attr);

  ON_TextLog log;
  if (!model.Write(path, 0, &log)) {
    std::fprintf(stderr, "hatch3dm_fixture_gen: OpenNURBS could not write %s\n", path);
    return 1;
  }
  std::printf("wrote %s (solid hatch, pattern index %d)\n", path, added_pattern->Index());
  return 0;
}
