// instanceref3dm_fixture_gen - writes a small, real .3dm file containing a
// real ON_InstanceDefinition (one member: a straight line from (0,0,0) to
// (1,0,0), tagged ON::idef_object the way a real block-definition member
// always is) and two ON_InstanceRef placements of it - one a plain
// translation, one a translation combined with a uniform scale, so the
// placements are only distinguishable if the reader applies the full
// ON_InstanceRef::m_xform rather than just an insertion-point translation.
// Built directly through OpenNURBS' own ONX_Model/ON_InstanceDefinition/
// ON_InstanceRef API - the same API src/io/File3dm.cpp's Load3dm uses -
// rather than through Dino 8's own Open/Save round trip.
//
// Regression coverage: Dino 8's own blocks (doc/BlockInstances.h) persist
// through .3dm as tagged ordinary geometry plus a private
// "Dino8.BlocksMeta" document user string (see File3dm.cpp's
// EncodeBlocksMeta/DecodeBlocksMeta), never as a real ON_InstanceDefinition/
// ON_InstanceRef pair - so Load3dm's own handling of that pair, added to
// read a block instance authored by a real, independent CAD tool such as
// Rhino, has no Dino8-authored file to prove itself against. This
// generator exists purely to give that new reader code a real
// externally-authored instance definition/reference to read, the same
// role hatch3dm_fixture_gen already plays for ON_Hatch.
#include <opennurbs.h>

#include <cstdio>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <output.3dm>\n", argv[0]);
    return 1;
  }
  const char* path = argv[1];

  ONX_Model model;
  model.m_sStartSectionComments = "instanceref3dm_fixture_gen test fixture";

  // The instance definition's one member object: a unit-length line on the
  // world X axis, written with ON::idef_object mode - the mode every real
  // CAD tool marks a block-definition member with, and the mode Load3dm's
  // own new reader code relies on to keep this object from also being
  // added as an ordinary, un-transformed, ungrouped scene object (see its
  // comment in File3dm.cpp).
  ON_LineCurve member(ON_3dPoint(0, 0, 0), ON_3dPoint(1, 0, 0));
  ON_3dmObjectAttributes member_attr;
  member_attr.SetMode(ON::idef_object);
  const ON_ModelComponentReference member_ref = model.AddModelGeometryComponent(&member, &member_attr, true);
  const ON_ModelGeometryComponent* member_geom = ON_ModelGeometryComponent::Cast(member_ref.ModelComponent());
  const ON_3dmObjectAttributes* added_member_attr = member_geom ? member_geom->Attributes(nullptr) : nullptr;
  if (!added_member_attr) {
    std::fprintf(stderr, "instanceref3dm_fixture_gen: could not add the idef member line\n");
    return 1;
  }

  ON_InstanceDefinition idef;
  idef.SetName(L"TestBlock");
  idef.AddInstanceGeometryId(added_member_attr->m_uuid);
  const ON_ModelComponentReference idef_ref = model.AddModelComponent(idef, true);
  const ON_InstanceDefinition* added_idef = ON_InstanceDefinition::Cast(idef_ref.ModelComponent());
  if (!added_idef) {
    std::fprintf(stderr, "instanceref3dm_fixture_gen: could not add the instance definition\n");
    return 1;
  }

  // Placement 1: plain translation to (2,3,0) - the member line lands at
  // (2,3,0)-(3,3,0), length 1, insertion point (2,3,0).
  {
    ON_InstanceRef iref;
    iref.m_instance_definition_uuid = added_idef->Id();
    iref.m_xform = ON_Xform::TranslationTransformation(ON_3dVector(2, 3, 0));
    ON_3dmObjectAttributes attr;
    model.AddModelGeometryComponent(new ON_InstanceRef(iref), &attr);
  }
  // Placement 2: uniform scale by 3 about the world origin, then translate
  // to (10,0,0) - the member line lands at (10,0,0)-(13,0,0), length 3 (not
  // 1), insertion point (10,0,0). Only distinguishable from placement 1 if
  // the reader applies the ref's full m_xform, not just a translation.
  {
    ON_Xform scale = ON_Xform::ScaleTransformation(ON_3dPoint::Origin, 3, 3, 3);
    ON_Xform translate = ON_Xform::TranslationTransformation(ON_3dVector(10, 0, 0));
    ON_InstanceRef iref;
    iref.m_instance_definition_uuid = added_idef->Id();
    iref.m_xform = translate * scale;
    ON_3dmObjectAttributes attr;
    model.AddModelGeometryComponent(new ON_InstanceRef(iref), &attr);
  }

  ON_TextLog log;
  if (!model.Write(path, 0, &log)) {
    std::fprintf(stderr, "instanceref3dm_fixture_gen: OpenNURBS could not write %s\n", path);
    return 1;
  }
  std::printf("wrote %s (1 idef, 2 instance refs)\n", path);
  return 0;
}
