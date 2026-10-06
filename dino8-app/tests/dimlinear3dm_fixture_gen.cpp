// dimlinear3dm_fixture_gen - writes a small, real .3dm file containing four
// externally-authored dimension objects (ON_DimLinear x2, ON_DimRadial x2),
// built directly through OpenNURBS' own ONX_Model/ON_DimLinear/ON_DimRadial/
// ON_DimStyle API - the same API src/io/File3dm.cpp's Load3dm uses - rather
// than through Dino 8's own Open/Save round trip. Same role as
// text3dm_fixture_gen/hatch3dm_fixture_gen: Dino8 itself never writes a
// native ON_DimLinear/ON_DimRadial object (its own Dim/DimAligned/DimRadius/
// DimDiameter commands bake a group of curves plus Annotation/Dim* user_text
// instead - see commands/DimGeometry.h), so Load3dm's own ON_DimLinear/
// ON_DimRadial reader has no Dino8-authored file to prove itself against.
//
// Four objects, chosen to exercise both branches of each reader:
//   1. Rotated (non-aligned) linear: (0,0,0)-(40,0,0), horizontal dimension
//      line at y=10 -> expected measured length 40, DimHorizontal=1,
//      DimOffset=10.
//   2. Aligned linear: (0,0,0)-(30,30,0), dimension line offset 5 to the
//      "up" side -> expected measured length 30*sqrt(2), DimAligned=1.
//   3. Radius: center (100,0,0), radius point (105,0,0) [r=5], leader tail
//      out to (110,0,0) [extra=5] -> expected DimRadiusVal=5.
//   4. Diameter: center (200,0,0), radius point (203,0,0) [r=3], leader tail
//      out to (206,0,0) [extra=3] -> expected DimRadiusVal=3 (diameter 6).
// A custom ON_DimStyle (text height 1.5, distinct from ON_DimStyle::Default's
// own value) is referenced by all four, so the reader's DimStyle-table
// lookup is actually exercised, same as text3dm_fixture_gen's own style.
#include <opennurbs.h>

#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <output.3dm>\n", argv[0]);
    return 1;
  }
  const char* path = argv[1];

  ONX_Model model;
  model.m_sStartSectionComments = "dimlinear3dm_fixture_gen test fixture";

  ON_DimStyle style = ON_DimStyle::Default;
  style.SetName(L"TestDimStyle");
  style.SetTextHeight(1.5);
  const ON_ModelComponentReference style_ref = model.AddModelComponent(style, true);
  const ON_DimStyle* added_style = ON_DimStyle::Cast(style_ref.ModelComponent());
  if (!added_style) {
    std::fprintf(stderr, "dimlinear3dm_fixture_gen: could not add the custom dimstyle\n");
    return 1;
  }
  const ON_UUID style_id = added_style->Id();
  const ON_3dVector up(0.0, 0.0, 1.0);

  // 1. Rotated (horizontal) linear dimension.
  ON_DimLinear* rotated = ON_DimLinear::CreateRotated(
      ON_3dPoint(0.0, 0.0, 0.0), ON_3dPoint(40.0, 0.0, 0.0),
      ON_Line(ON_3dPoint(0.0, 10.0, 0.0), ON_3dPoint(1.0, 10.0, 0.0)), up, style_id, nullptr);
  if (!rotated) {
    std::fprintf(stderr, "dimlinear3dm_fixture_gen: ON_DimLinear::CreateRotated failed\n");
    return 1;
  }
  {
    ON_3dmObjectAttributes attr;
    model.AddModelGeometryComponent(rotated, &attr);
  }

  // 2. Aligned linear dimension.
  ON_DimLinear* aligned = ON_DimLinear::CreateAligned(
      ON_3dPoint(0.0, 0.0, 0.0), ON_3dPoint(30.0, 30.0, 0.0), ON_3dPoint(-5.0 / std::sqrt(2.0), 5.0 / std::sqrt(2.0), 0.0),
      up, style_id, nullptr);
  if (!aligned) {
    std::fprintf(stderr, "dimlinear3dm_fixture_gen: ON_DimLinear::CreateAligned failed\n");
    return 1;
  }
  {
    ON_3dmObjectAttributes attr;
    model.AddModelGeometryComponent(aligned, &attr);
  }

  // 3. Radius dimension.
  ON_DimRadial radius_dim;
  if (!radius_dim.Create(ON::AnnotationType::Radius, style_id, ON_xy_plane, ON_3dPoint(100.0, 0.0, 0.0),
                          ON_3dPoint(105.0, 0.0, 0.0), ON_3dPoint(110.0, 0.0, 0.0))) {
    std::fprintf(stderr, "dimlinear3dm_fixture_gen: ON_DimRadial::Create (radius) failed\n");
    return 1;
  }
  {
    ON_3dmObjectAttributes attr;
    model.AddModelGeometryComponent(&radius_dim, &attr);
  }

  // 4. Diameter dimension.
  ON_DimRadial diameter_dim;
  if (!diameter_dim.Create(ON::AnnotationType::Diameter, style_id, ON_xy_plane, ON_3dPoint(200.0, 0.0, 0.0),
                            ON_3dPoint(203.0, 0.0, 0.0), ON_3dPoint(206.0, 0.0, 0.0))) {
    std::fprintf(stderr, "dimlinear3dm_fixture_gen: ON_DimRadial::Create (diameter) failed\n");
    return 1;
  }
  {
    ON_3dmObjectAttributes attr;
    model.AddModelGeometryComponent(&diameter_dim, &attr);
  }

  ON_TextLog log;
  if (!model.Write(path, 0, &log)) {
    std::fprintf(stderr, "dimlinear3dm_fixture_gen: OpenNURBS could not write %s\n", path);
    return 1;
  }
  std::printf("wrote %s (ON_DimLinear x2, ON_DimRadial x2)\n", path);
  return 0;
}
