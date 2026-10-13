// text3dm_write_check - reads a .3dm file (expected to be written by Dino 8
// itself, via src/io/File3dm.cpp's Save3dm) directly through OpenNURBS' own
// ONX_Model/ON_Text/ON_DimStyle API - independent of Dino 8's own Load3dm
// reader - and reports every real ON_Text object it finds.
//
// This is the write-side mirror of text3dm_fixture_gen.cpp: that generator
// proves Load3dm's *reader* against an externally-authored ON_Text built
// directly through OpenNURBS' API; this checker proves Save3dm's own
// *writer* the same way, from the other direction - a Text annotation
// Dino 8 itself made and saved must come back as a real ON_Text object
// (ON::AnnotationType::Text) to a plain, independent OpenNURBS reader, not
// merely to Dino 8's own Open command (which would also accept the
// pre-existing baked glyph-curve-plus-user_text fallback and never notice
// the difference - Load3dm decomposes a real ON_Text back into the exact
// same baked curves either way).
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
    std::fprintf(stderr, "text3dm_write_check: OpenNURBS could not read %s\n", path);
    return 1;
  }

  int text_count = 0;
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
    if (!mg) continue;
    const ON_Annotation* ann = ON_Annotation::Cast(mg->Geometry(nullptr));
    if (!ann || ann->Type() != ON::AnnotationType::Text) continue;
    ++text_count;

    double height = -1;
    const ON_ModelComponentReference dsr = model.ComponentFromId(ON_ModelComponent::Type::DimStyle, ann->DimensionStyleId());
    if (const ON_DimStyle* ds = ON_DimStyle::Cast(dsr.ModelComponent())) height = ds->TextHeight();

    const ON_Plane& plane = ann->Plane();
    std::printf(
        "text %d: text=%ls height=%g origin=(%g,%g,%g) xaxis=(%g,%g,%g)\n",
        text_count, static_cast<const wchar_t*>(ann->PlainText()), height, plane.origin.x, plane.origin.y,
        plane.origin.z, plane.xaxis.x, plane.xaxis.y, plane.xaxis.z);
  }
  std::printf("text3dm_write_check: %d real ON_Text object(s) found in %s\n", text_count, path);
  return 0;
}
