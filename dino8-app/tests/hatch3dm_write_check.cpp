// hatch3dm_write_check - reads a .3dm file (expected to be written by Dino 8
// itself, via src/io/File3dm.cpp's Save3dm) directly through OpenNURBS' own
// ONX_Model/ON_Hatch/ON_HatchPattern API - independent of Dino 8's own
// Load3dm reader - and reports every real ON_Hatch object it finds.
//
// This is the write-side mirror of hatch3dm_fixture_gen.cpp: that generator
// proves Load3dm's *reader* against an externally-authored ON_Hatch built
// directly through OpenNURBS' API; this checker proves Save3dm's own
// *writer* the same way, from the other direction - a hatch Dino 8 itself
// made and saved must come back as a real ON_Hatch object (ObjectType() ==
// ON::hatch_object) to a plain, independent OpenNURBS reader, not merely to
// Dino 8's own Open command (which would also accept the pre-existing baked
// Brep/curves-plus-user_text fallback and never notice the difference).
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
    std::fprintf(stderr, "hatch3dm_write_check: OpenNURBS could not read %s\n", path);
    return 1;
  }

  int hatch_count = 0;
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
    if (!mg) continue;
    const ON_Hatch* h = ON_Hatch::Cast(mg->Geometry(nullptr));
    if (!h) continue;
    ++hatch_count;

    ON_HatchPattern::HatchFillType fill = ON_HatchPattern::HatchFillType::Lines;
    ON_wString pattern_name;
    int hatch_line_count = -1;
    ONX_ModelComponentIterator pit(model, ON_ModelComponent::Type::HatchPattern);
    for (const ON_ModelComponent* pc = pit.FirstComponent(); pc; pc = pit.NextComponent()) {
      const ON_HatchPattern* p = ON_HatchPattern::Cast(pc);
      if (p && p->Index() == h->PatternIndex()) {
        fill = p->FillType();
        pattern_name = p->Name();
        hatch_line_count = p->HatchLineCount();
        break;
      }
    }

    ON_BoundingBox bbox;
    bool have_bbox = false;
    if (h->LoopCount() > 0) {
      if (ON_Curve* c3 = h->LoopCurve3d(0)) {
        have_bbox = c3->GetBoundingBox(bbox) ? true : false;
        delete c3;
      }
    }

    std::printf(
        "hatch %d: pattern_index=%d pattern_name=%ls fill=%s pattern_lines=%d scale=%g rotation_deg=%g loops=%d",
        hatch_count, h->PatternIndex(), static_cast<const wchar_t*>(pattern_name), fill == ON_HatchPattern::HatchFillType::Solid ? "Solid" : "Lines",
        hatch_line_count, h->PatternScale(), h->PatternRotation() * 180.0 / ON_PI, h->LoopCount());
    if (have_bbox) {
      std::printf(" bbox=(%g,%g,%g)-(%g,%g,%g)\n", bbox.Min().x, bbox.Min().y, bbox.Min().z, bbox.Max().x,
                  bbox.Max().y, bbox.Max().z);
    } else {
      std::printf(" bbox=none\n");
    }
  }
  std::printf("hatch3dm_write_check: %d real ON_Hatch object(s) found in %s\n", hatch_count, path);
  return 0;
}
