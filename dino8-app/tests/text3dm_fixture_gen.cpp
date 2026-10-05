// text3dm_fixture_gen - writes a small, real .3dm file containing a single
// ON_Text annotation object ("Hi" at (3,4,0), capital height 2, referencing
// a real custom ON_DimStyle table entry), built directly through
// OpenNURBS' own ONX_Model/ON_Text/ON_DimStyle API - the same API
// src/io/File3dm.cpp's Load3dm uses - rather than through Dino 8's own
// Open/Save round trip.
//
// Regression coverage: Dino 8 itself never writes a native ON_Annotation
// object (its own Text command bakes a group of glyph-outline curves plus
// Annotation/Text/... user_text instead - see commands/annotate_common.h),
// so Load3dm's own ON_Annotation handling - added to read a Text object
// authored by a real, independent CAD tool such as Rhino - has no
// Dino8-authored file to prove itself against. This generator exists purely
// to give that new reader code a real externally-authored ON_Text to read,
// the same role hatch3dm_fixture_gen already plays for ON_Hatch.
#include <opennurbs.h>

#include <cstdio>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <output.3dm>\n", argv[0]);
    return 1;
  }
  const char* path = argv[1];

  ONX_Model model;
  model.m_sStartSectionComments = "text3dm_fixture_gen test fixture";

  // A real custom dimension-style table entry with a distinctive text
  // height (2, not the Default style's own value) - so the reader's
  // DimStyle-table lookup (by the text object's own DimensionStyleId(),
  // not just a hardcoded ON_DimStyle::Default fallback) is actually
  // exercised and verifiable: the imported glyph curves' "TextHeight" tag
  // must read back 2, not whatever ON_DimStyle::Default happens to be.
  ON_DimStyle style = ON_DimStyle::Default;
  style.SetName(L"TestTextStyle");
  style.SetTextHeight(2.0);
  const ON_ModelComponentReference style_ref = model.AddModelComponent(style, true);
  const ON_DimStyle* added_style = ON_DimStyle::Cast(style_ref.ModelComponent());
  if (!added_style) {
    std::fprintf(stderr, "text3dm_fixture_gen: could not add the custom dimstyle\n");
    return 1;
  }

  ON_Plane plane = ON_xy_plane;
  plane.origin = ON_3dPoint(3.0, 4.0, 0.0);

  ON_Text text;
  if (!text.Create(L"Hi", added_style, plane)) {
    std::fprintf(stderr, "text3dm_fixture_gen: ON_Text::Create failed\n");
    return 1;
  }
  text.SetDimensionStyleId(added_style->Id());

  ON_3dmObjectAttributes attr;
  model.AddModelGeometryComponent(&text, &attr);

  ON_TextLog log;
  if (!model.Write(path, 0, &log)) {
    std::fprintf(stderr, "text3dm_fixture_gen: OpenNURBS could not write %s\n", path);
    return 1;
  }
  std::printf("wrote %s (ON_Text \"Hi\" at 3,4,0, height 2)\n", path);
  return 0;
}
