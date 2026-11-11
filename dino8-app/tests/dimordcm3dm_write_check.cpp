// dimordcm3dm_write_check - reads a .3dm file (expected to be written by Dino 8's Save3dm) directly through OpenNURBS' own
// ONX_Model/ON_DimOrdinate/ON_Centermark API, independent of Dino 8's own Load3dm reader, and reports every real
// ON_DimOrdinate and ON_Centermark object plus the number of loose ON_Curve objects in the file.
//
// The write-side mirror of the ON_DimOrdinate / ON_Centermark halves of Load3dm (see dimlinear3dm_fixture_gen): a DimOrdinate or
// Centermark Dino 8 made and saved must come back as a real native annotation object to a plain OpenNURBS reader, not merely to
// Dino 8's own Open command (which also accepts the baked leader-line / glyph / crossing-line fallback and never notices the
// difference). The loose-curve count is the negative control: an annotation written for real must not ALSO leave its baked
// curves in the file.
//
// Output, one line per object:
//   dimord N: dir=X|Y base=(x,y,z) feature=(x,y,z) leader=(x,y,z) value=V texth=H
//   centermark N: center=(x,y,z) size=S radius=R
// where the points are the world points OpenNURBS itself resolves (Get3dBasePoint / Get3dDefPt / Get3dLeaderPt, the plane-mapped
// centre), V is the measured coordinate difference computed here from the two points (not read from OpenNURBS), H is the
// TextHeight of the dimension style the object references, and S is that style's CenterMark size, then a final summary line.
#include <opennurbs.h>

#include <cmath>
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
    std::fprintf(stderr, "dimordcm3dm_write_check: OpenNURBS could not read %s\n", path);
    return 1;
  }

  int ord_count = 0, cm_count = 0, curve_count = 0, geometry_count = 0;
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
    if (!mg) continue;
    ++geometry_count;
    const ON_Geometry* g = mg->Geometry(nullptr);
    if (ON_Curve::Cast(g)) ++curve_count;
    if (const ON_DimOrdinate* dim = ON_DimOrdinate::Cast(g)) {
      ++ord_count;
      const ON_DimStyle* ds = ON_DimStyle::Cast(model.ComponentFromId(ON_ModelComponent::Type::DimStyle, dim->DimensionStyleId()).ModelComponent());
      const bool is_x = dim->GetMeasuredDirection() == ON_DimOrdinate::MeasuredDirection::Xaxis;
      const ON_3dPoint base = dim->Get3dBasePoint(), feat = dim->Get3dDefPt(), ldr = dim->Get3dLeaderPt();
      const ON_Plane& plane = dim->Plane();
      double u0, v0, u1, v1;
      plane.ClosestPointTo(base, &u0, &v0);
      plane.ClosestPointTo(feat, &u1, &v1);
      std::printf("dimord %d: dir=%s base=(%.6g,%.6g,%.6g) feature=(%.6g,%.6g,%.6g) leader=(%.6g,%.6g,%.6g) value=%.6g texth=%.6g\n", ord_count,
                  is_x ? "X" : "Y", base.x, base.y, base.z, feat.x, feat.y, feat.z, ldr.x, ldr.y, ldr.z, is_x ? u1 - u0 : v1 - v0,
                  ds ? ds->TextHeight() : -1.0);
    }
    if (const ON_Centermark* cm = ON_Centermark::Cast(g)) {
      ++cm_count;
      const ON_DimStyle* ds = ON_DimStyle::Cast(model.ComponentFromId(ON_ModelComponent::Type::DimStyle, cm->DimensionStyleId()).ModelComponent());
      const ON_2dPoint c2 = cm->CenterPoint();
      const ON_3dPoint ctr = cm->Plane().PointAt(c2.x, c2.y);
      std::printf("centermark %d: center=(%.6g,%.6g,%.6g) size=%.6g radius=%.6g\n", cm_count, ctr.x, ctr.y, ctr.z, ds ? ds->CenterMark() : -1.0,
                  cm->Radius());
    }
  }
  std::printf("dimordcm3dm_write_check: %d real ON_DimOrdinate object(s), %d real ON_Centermark object(s), %d loose curve(s), %d geometry object(s) in %s\n",
              ord_count, cm_count, curve_count, geometry_count, path);
  return 0;
}
