// blockinst3dm_write_check - reads a .3dm file (expected to be written by Dino 8's Save3dm) directly through OpenNURBS' own
// ONX_Model API, independent of Dino 8's own Load3dm reader, and reports every real ON_InstanceDefinition and ON_InstanceRef plus
// the loose geometry that is left (the negative control: a block written for real must not ALSO leave baked copies of its
// members in the model).
//
// The write-side mirror of the ON_InstanceRef reader in Load3dm (see instanceref3dm_fixture_gen): a block Dino 8 made, placed and
// saved must come back as a real instance definition plus one instance reference per placement to a plain OpenNURBS reader
// (Rhino), not merely to Dino 8's own Open command, which also accepts the baked-copies fallback and never notices the difference.
//
// Output:
//   idef N: name=<name> members=K
//   idefmember N.I: type=Curve|Brep|Mesh|Surface|Point|Other mode=idef_object|normal bbox=(x,y,z)-(x,y,z)
//   iref N: def=<name> translate=(x,y,z) rotation=none|other
//   world N.I: bbox=(x,y,z)-(x,y,z)
// where the idef member boxes are the stored (definition-coordinate) boxes, the iref translation is read from the reference's own
// transformation (rotation=none when its linear part is the identity), and "world" is the member box moved by that translation,
// computed here. Then a final summary line:
//   blockinst3dm_write_check: A real ON_InstanceDefinition, B real ON_InstanceRef, C loose geometry object(s) in <path>
// where C counts geometry objects in normal mode that are not instance references.
#include <opennurbs.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

static const char* KindOf(const ON_Geometry* g) {
  if (ON_Curve::Cast(g)) return "Curve";
  if (ON_Brep::Cast(g)) return "Brep";
  if (ON_Mesh::Cast(g)) return "Mesh";
  if (ON_Surface::Cast(g)) return "Surface";
  if (ON_Point::Cast(g)) return "Point";
  return "Other";
}

static double Z(double v) { return std::fabs(v) < 1e-9 ? 0.0 : v; }

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <file.3dm>\n", argv[0]);
    return 1;
  }
  const char* path = argv[1];
  ONX_Model model;
  ON_TextLog log;
  if (!model.Read(path, &log)) {
    std::fprintf(stderr, "blockinst3dm_write_check: OpenNURBS could not read %s\n", path);
    return 1;
  }

  // definition-coordinate boxes of every geometry component, by id
  std::map<ON_UUID, ON_BoundingBox, bool (*)(const ON_UUID&, const ON_UUID&)> boxes([](const ON_UUID& a, const ON_UUID& b) { return ON_UuidCompare(a, b) < 0; });
  int loose = 0, irefs = 0;
  {
    ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
    for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
      const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
      if (!mg) continue;
      const ON_Geometry* g = mg->Geometry(nullptr);
      const ON_3dmObjectAttributes* attr = mg->Attributes(nullptr);
      if (g) boxes[c->Id()] = g->BoundingBox();
      if (ON_InstanceRef::Cast(g)) continue;
      if (!attr || attr->Mode() != ON::idef_object) ++loose;
    }
  }

  int idefs = 0;
  std::map<std::string, std::map<int, ON_BoundingBox>> member_boxes;  // def name -> member index -> box
  {
    ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::InstanceDefinition);
    for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
      const ON_InstanceDefinition* idef = ON_InstanceDefinition::Cast(c);
      if (!idef) continue;
      ++idefs;
      const ON_SimpleArray<ON_UUID>& ids = idef->InstanceGeometryIdList();
      const std::string name = ON_String(idef->Name()).Array() ? ON_String(idef->Name()).Array() : "";
      std::printf("idef %d: name=%s members=%d\n", idefs, name.c_str(), ids.Count());
      for (int i = 0; i < ids.Count(); ++i) {
        const ON_ModelComponentReference ref = model.ComponentFromId(ON_ModelComponent::Type::ModelGeometry, ids[i]);
        const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(ref.ModelComponent());
        const ON_Geometry* g = mg ? mg->Geometry(nullptr) : nullptr;
        const ON_3dmObjectAttributes* attr = mg ? mg->Attributes(nullptr) : nullptr;
        if (!g) { std::printf("idefmember %d.%d: missing\n", idefs, i); continue; }
        const ON_BoundingBox b = g->BoundingBox();
        member_boxes[name][i] = b;
        std::printf("idefmember %d.%d: type=%s mode=%s bbox=(%.6g,%.6g,%.6g)-(%.6g,%.6g,%.6g)\n", idefs, i, KindOf(g),
                    attr && attr->Mode() == ON::idef_object ? "idef_object" : "normal", Z(b.m_min.x), Z(b.m_min.y), Z(b.m_min.z), Z(b.m_max.x),
                    Z(b.m_max.y), Z(b.m_max.z));
      }
    }
  }

  {
    ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
    for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
      const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
      if (!mg) continue;
      const ON_InstanceRef* iref = ON_InstanceRef::Cast(mg->Geometry(nullptr));
      if (!iref) continue;
      ++irefs;
      const ON_ModelComponentReference dref = model.ComponentFromId(ON_ModelComponent::Type::InstanceDefinition, iref->m_instance_definition_uuid);
      const ON_InstanceDefinition* idef = ON_InstanceDefinition::Cast(dref.ModelComponent());
      const std::string name = idef ? std::string(ON_String(idef->Name()).Array()) : std::string("?");
      const ON_Xform& x = iref->m_xform;
      const bool identity_linear = std::fabs(x.m_xform[0][0] - 1) < 1e-12 && std::fabs(x.m_xform[1][1] - 1) < 1e-12 && std::fabs(x.m_xform[2][2] - 1) < 1e-12 &&
                                   std::fabs(x.m_xform[0][1]) < 1e-12 && std::fabs(x.m_xform[0][2]) < 1e-12 && std::fabs(x.m_xform[1][0]) < 1e-12 &&
                                   std::fabs(x.m_xform[1][2]) < 1e-12 && std::fabs(x.m_xform[2][0]) < 1e-12 && std::fabs(x.m_xform[2][1]) < 1e-12;
      std::printf("iref %d: def=%s translate=(%.6g,%.6g,%.6g) rotation=%s\n", irefs, name.c_str(), Z(x.m_xform[0][3]), Z(x.m_xform[1][3]), Z(x.m_xform[2][3]),
                  identity_linear ? "none" : "other");
      for (const auto& kv : member_boxes[name]) {
        const ON_BoundingBox& b = kv.second;
        std::printf("world %d.%d: bbox=(%.6g,%.6g,%.6g)-(%.6g,%.6g,%.6g)\n", irefs, kv.first, Z(b.m_min.x + x.m_xform[0][3]), Z(b.m_min.y + x.m_xform[1][3]),
                    Z(b.m_min.z + x.m_xform[2][3]), Z(b.m_max.x + x.m_xform[0][3]), Z(b.m_max.y + x.m_xform[1][3]), Z(b.m_max.z + x.m_xform[2][3]));
      }
    }
  }
  std::printf("blockinst3dm_write_check: %d real ON_InstanceDefinition, %d real ON_InstanceRef, %d loose geometry object(s) in %s\n", idefs, irefs, loose, path);
  return 0;
}
