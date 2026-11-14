// dimlinear3dm_fixture_gen - writes a small, real .3dm file containing eight
// externally-authored dimension/leader/centermark objects (ON_DimLinear x2,
// ON_DimRadial x2, ON_Leader, ON_DimAngular, ON_DimOrdinate, ON_Centermark),
// built directly through OpenNURBS' own ONX_Model/ON_DimLinear/ON_DimRadial/
// ON_Leader/ON_DimAngular/ON_DimOrdinate/ON_Centermark/ON_DimStyle API - the
// same API src/io/File3dm.cpp's Load3dm uses - rather than through Dino 8's
// own Open/Save round trip. Same role as text3dm_fixture_gen/
// hatch3dm_fixture_gen: Dino8 itself never writes any of these native
// annotation kinds (its own Dim/DimAligned/DimRadius/DimDiameter/Leader/
// DimAngle/DimOrdinate/Centermark commands bake a group of curves plus
// Annotation/Dim*/Leader*/Center* user_text instead - see commands/
// DimGeometry.h, cmd_annotate.cpp's own BuildLeaderGroup,
// cmd_annotate2.cpp's own BuildOrdinateDimGroup/BuildCentermarkGroup), so
// Load3dm's own readers for these have no Dino8-authored file to prove
// themselves against.
//
// Eight objects, chosen to exercise both branches of each dimension reader
// plus the leader, angular, ordinate and centermark readers:
//   1. Rotated (non-aligned) linear: (0,0,0)-(40,0,0), horizontal dimension
//      line at y=10 -> expected measured length 40, DimHorizontal=1,
//      DimOffset=10.
//   2. Aligned linear: (0,0,0)-(30,30,0), dimension line offset 5 to the
//      "up" side -> expected measured length 30*sqrt(2), DimAligned=1.
//   3. Radius: center (100,0,0), radius point (105,0,0) [r=5], leader tail
//      out to (110,0,0) [extra=5] -> expected DimRadiusVal=5.
//   4. Diameter: center (200,0,0), radius point (203,0,0) [r=3], leader tail
//      out to (206,0,0) [extra=3] -> expected DimRadiusVal=3 (diameter 6).
//   5. Leader: arrowhead tip (300,0,0), one bend point (305,5,0), tail/text
//      landing (315,5,0), text "LeaderText" -> expected LeaderTip=300,0,0,
//      one LeaderRest offset of 5,5,0 (bend) joined with 15,5,0 (tail).
//   6. Angular: vertex (400,0,0), extension points (410,0,0) and
//      (400,10,0) (a 90-degree angle), dimension-arc point at 45 degrees
//      -> expected DimP0=400,0,0, DimP1=410,0,0, DimP2=400,10,0, measured
//      angle 90 degrees.
//   7. Ordinate (X direction): base point (500,0,0), measured feature point
//      (520,8,0), leader point (520,15,0) -> expected DimP0=500,0,0,
//      DimP1=520,8,0, DimOrdinateDir=X, measured value 20 (520-500).
//   8. Centermark: center (600,0,0), marked-circle radius 8 (deliberately
//      NOT a multiple of the style's own CenterMark size below, so a
//      reader that wrongly derived the drawn size from a quarter of this
//      radius - 2 - instead of the real stored CenterMark field - 0.6 -
//      would be caught) -> expected CenterCenter=600,0,0, CenterSize=0.6.
// A custom ON_DimStyle (text height 1.5, distinct from ON_DimStyle::Default's
// own value, and its own CenterMark size set to 0.6, distinct from
// ON_DimStyle::Default's own 0.5) is referenced by all eight, so the
// reader's DimStyle-table lookup is actually exercised, same as
// text3dm_fixture_gen's own style.
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
  style.SetCenterMark(0.6);
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

  // 5. Leader: arrowhead tip, one bend point, tail/landing point, text.
  {
    const ON_3dPoint pts[3] = {ON_3dPoint(300.0, 0.0, 0.0), ON_3dPoint(305.0, 5.0, 0.0), ON_3dPoint(315.0, 5.0, 0.0)};
    ON_Leader leader;
    if (!leader.Create(L"LeaderText", added_style, 3, pts, ON_xy_plane, false, 0.0)) {
      std::fprintf(stderr, "dimlinear3dm_fixture_gen: ON_Leader::Create failed\n");
      return 1;
    }
    leader.SetDimensionStyleId(added_style->Id());
    ON_3dmObjectAttributes attr;
    model.AddModelGeometryComponent(&leader, &attr);
  }

  // 6. Angular dimension: vertex plus two extension points (a 90-degree
  // angle), dimension-arc point at the 45-degree bisector.
  {
    ON_DimAngular angle_dim;
    if (!angle_dim.Create(style_id, ON_xy_plane, ON_3dVector(1.0, 0.0, 0.0), ON_3dPoint(400.0, 0.0, 0.0),
                           ON_3dPoint(410.0, 0.0, 0.0), ON_3dPoint(400.0, 10.0, 0.0),
                           ON_3dPoint(400.0 + 5.0 / std::sqrt(2.0), 5.0 / std::sqrt(2.0), 0.0))) {
      std::fprintf(stderr, "dimlinear3dm_fixture_gen: ON_DimAngular::Create failed\n");
      return 1;
    }
    ON_3dmObjectAttributes attr;
    model.AddModelGeometryComponent(&angle_dim, &attr);
  }

  // 7. Ordinate dimension (X direction): base/reference point, measured
  // feature point, leader point.
  {
    ON_DimOrdinate ord_dim;
    if (!ord_dim.Create(style_id, ON_xy_plane, ON_DimOrdinate::MeasuredDirection::Xaxis, ON_3dPoint(500.0, 0.0, 0.0),
                         ON_3dPoint(520.0, 8.0, 0.0), ON_3dPoint(520.0, 15.0, 0.0), 1.0, 1.0)) {
      std::fprintf(stderr, "dimlinear3dm_fixture_gen: ON_DimOrdinate::Create failed\n");
      return 1;
    }
    ON_3dmObjectAttributes attr;
    model.AddModelGeometryComponent(&ord_dim, &attr);
  }

  // 8. Centermark: center point, marked-circle radius (the circle itself
  // isn't written - like every other dimension above, only the annotation
  // object matters to the reader under test).
  {
    ON_Centermark centermark;
    if (!centermark.Create(style_id, ON_xy_plane, ON_3dPoint(600.0, 0.0, 0.0), 8.0)) {
      std::fprintf(stderr, "dimlinear3dm_fixture_gen: ON_Centermark::Create failed\n");
      return 1;
    }
    ON_3dmObjectAttributes attr;
    model.AddModelGeometryComponent(&centermark, &attr);
  }

  ON_TextLog log;
  if (!model.Write(path, 0, &log)) {
    std::fprintf(stderr, "dimlinear3dm_fixture_gen: OpenNURBS could not write %s\n", path);
    return 1;
  }
  std::printf("wrote %s (ON_DimLinear x2, ON_DimRadial x2, ON_Leader, ON_DimAngular, ON_DimOrdinate, ON_Centermark)\n", path);
  return 0;
}
