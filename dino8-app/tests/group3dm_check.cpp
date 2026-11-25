// group3dm_check - the native ON_Group table of a .3dm file, through OpenNURBS' own API and independent of Dino 8's Save3dm /
// Load3dm.
//
//   group3dm_check fixture OUT.3dm   writes a file the way Rhino would: groups "Alpha" (points 1 and 2) and "Beta" (point 3 and a
//                                    line), one ungrouped point, and a point in BOTH groups ("Nested": Alpha then Beta, outermost
//                                    last). No Dino8.* user strings anywhere - Dino 8 never wrote this file.
//   group3dm_check dump FILE.3dm     reads FILE with ONX_Model and prints
//                                       groups: N
//                                       group <index> name=<name>
//                                       object <kind> at=(x,y,z) groups=<names, comma separated, in the attribute's own order>
//                                    one "object" line per geometry object, in the file's order.
#include <opennurbs.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string Narrow(const ON_wString& s) {
  std::string out;
  const wchar_t* p = static_cast<const wchar_t*>(s);
  for (; p && *p; ++p) out.push_back(*p < 128 ? static_cast<char>(*p) : '?');
  return out;
}

int AddGroup(ONX_Model& model, const wchar_t* name) {
  ON_Group g;
  g.SetName(name);
  const ON_ModelComponentReference ref = model.AddModelComponent(g, true);
  const ON_Group* stored = ON_Group::FromModelComponentRef(ref, nullptr);
  return stored ? stored->Index() : -1;
}

void AddPoint(ONX_Model& model, double x, double y, double z, int layer, const int* groups, int group_count) {
  ON_3dmObjectAttributes attr;
  attr.m_layer_index = layer;
  for (int i = 0; i < group_count; ++i) attr.AddToGroup(groups[i]);
  model.AddModelGeometryComponent(new ON_Point(ON_3dPoint(x, y, z)), &attr);
}

int Fixture(const char* path) {
  ONX_Model model;
  model.m_sStartSectionComments = "group3dm_check fixture";
  const int layer = model.AddLayer(L"Default", ON_Color(0, 0, 0));
  const int alpha = AddGroup(model, L"Alpha");
  const int beta = AddGroup(model, L"Beta");
  if (alpha < 0 || beta < 0) {
    std::fprintf(stderr, "group3dm_check: could not add the groups\n");
    return 1;
  }
  AddPoint(model, 1, 0, 0, layer, &alpha, 1);
  AddPoint(model, 2, 0, 0, layer, &alpha, 1);
  AddPoint(model, 3, 0, 0, layer, &beta, 1);
  {
    ON_3dmObjectAttributes attr;
    attr.m_layer_index = layer;
    attr.AddToGroup(beta);
    model.AddModelGeometryComponent(new ON_LineCurve(ON_3dPoint(0, 5, 0), ON_3dPoint(10, 5, 0)), &attr);
  }
  AddPoint(model, 4, 0, 0, layer, nullptr, 0);
  const int nested[2] = {alpha, beta};  // innermost first, outermost (TopGroup) last
  AddPoint(model, 5, 0, 0, layer, nested, 2);
  ON_TextLog log;
  if (!model.Write(path, 0, &log)) {
    std::fprintf(stderr, "group3dm_check: OpenNURBS could not write %s\n", path);
    return 1;
  }
  std::printf("wrote %s (2 groups, 5 points and a line)\n", path);
  return 0;
}

int Dump(const char* path) {
  ONX_Model model;
  ON_TextLog log;
  if (!model.Read(path, &log)) {
    std::fprintf(stderr, "group3dm_check: OpenNURBS could not read %s\n", path);
    return 1;
  }
  int groups = 0;
  {
    ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::Group);
    for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) ++groups;
  }
  std::printf("groups: %d\n", groups);
  {
    ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::Group);
    for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
      std::printf("group %d name=%s\n", c->Index(), Narrow(c->Name()).c_str());
    }
  }
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
    if (!mg) continue;
    const ON_Geometry* geo = mg->Geometry(nullptr);
    const ON_3dmObjectAttributes* attr = mg->Attributes(nullptr);
    if (!geo || !attr) continue;
    const char* kind = "other";
    ON_3dPoint at(0, 0, 0);
    if (const ON_Point* p = ON_Point::Cast(geo)) { kind = "point"; at = p->point; }
    else if (const ON_Curve* cv = ON_Curve::Cast(geo)) { kind = "curve"; at = cv->PointAtStart(); }
    std::string names;
    ON_SimpleArray<int> list;
    attr->GetGroupList(list);
    for (int i = 0; i < list.Count(); ++i) {
      const ON_ModelComponentReference ref = model.ComponentFromIndex(ON_ModelComponent::Type::Group, list[i]);
      const ON_Group* g = ON_Group::FromModelComponentRef(ref, nullptr);
      names += (i ? "," : "") + (g ? Narrow(g->Name()) : std::string("?"));
    }
    std::printf("object %s at=(%g,%g,%g) groups=%s\n", kind, at.x, at.y, at.z, names.c_str());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s fixture|dump <file.3dm>\n", argv[0]);
    return 1;
  }
  if (!std::strcmp(argv[1], "fixture")) return Fixture(argv[2]);
  if (!std::strcmp(argv[1], "dump")) return Dump(argv[2]);
  std::fprintf(stderr, "group3dm_check: unknown mode %s\n", argv[1]);
  return 1;
}
