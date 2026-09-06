#include "arch/ArchComponents.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "app/Application.h"
#include "dino8/kernel/boolean.h"
#include "doc/SceneObject.h"
#include "util/json_mini.h"

namespace dino8::arch {

using app::ObjectKind;
using app::SceneObject;

const char* ArchTypeName(ArchType t) {
  switch (t) {
    case ArchType::Wall: return "Wall";
    case ArchType::Door: return "Door";
    case ArchType::Window: return "Window";
    case ArchType::Slab: return "Slab";
    case ArchType::Roof: return "Roof";
    case ArchType::Stair: return "Stair";
    case ArchType::Column: return "Column";
    case ArchType::Beam: return "Beam";
  }
  return "Wall";
}

bool ParseArchType(const std::string& s, ArchType& out) {
  static const std::pair<const char*, ArchType> kTable[] = {
      {"Wall", ArchType::Wall}, {"Door", ArchType::Door}, {"Window", ArchType::Window},
      {"Slab", ArchType::Slab}, {"Roof", ArchType::Roof}, {"Stair", ArchType::Stair},
      {"Column", ArchType::Column}, {"Beam", ArchType::Beam},
  };
  for (const auto& [name, type] : kTable) if (s == name) { out = type; return true; }
  return false;
}

std::vector<std::string> ArchTypeNames() {
  return {"Wall", "Door", "Window", "Slab", "Roof", "Stair", "Column", "Beam"};
}

// ---------------------------------------------------------------------------
// Geometry: every solid here is one closed watertight mesh box (or a small
// set of them, for Stair's treads / Roof's two slopes), all built from the
// same generalization of cmd_solids.cpp's axis-aligned CubeMesh() to an
// arbitrary local frame (e_i, e_j, e_k) - a box is a box whether its edges
// run along world X/Y/Z or along a wall's own length/thickness/height, so
// one indexing scheme (k*4 + j*2 + i, exactly CubeMesh's) covers every
// component type below.
// ---------------------------------------------------------------------------
namespace {

// ON_3dVector::Unitize() normalizes in place and returns whether it could
// (false for a zero-length vector) - every call site here wants a
// normalized copy of an already-nonzero vector, so wrap that pattern once.
Vector3d UnitOf(Vector3d v) { v.Unitize(); return v; }

kernel::Mesh OrientedBox(Point3d origin, Vector3d ei, Vector3d ej, Vector3d ek,
                          double i0, double i1, double j0, double j1, double k0, double k1) {
  kernel::Mesh m;
  ON_Mesh& r = m.raw();
  const double iv[2] = {i0, i1}, jv[2] = {j0, j1}, kv[2] = {k0, k1};
  for (int k = 0; k < 2; ++k)
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i) {
        Point3d p = origin + ei * iv[i] + ej * jv[j] + ek * kv[k];
        r.SetVertex(k * 4 + j * 2 + i, ON_3dPoint(p.x, p.y, p.z));
      }
  const int f[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
  for (int i = 0; i < 6; ++i) r.SetQuad(i, f[i][0], f[i][1], f[i][2], f[i][3]);
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return m;
}

// Right-handed local frame for a horizontal run from `p0` to `p1`: forward
// along the run, `up` fixed (world Z unless the component overrides it),
// right = up x forward so that (forward, right, up) matches OrientedBox's
// (ei, ej, ek) winding (see the comment on ArchComponent::normal).
struct RunFrame {
  Vector3d forward, right, up;
  double length;
};

RunFrame MakeRunFrame(Point3d p0, Point3d p1, Vector3d up) {
  RunFrame f;
  Vector3d d = p1 - p0;
  f.length = d.Length();
  f.forward = f.length > 1e-9 ? d / f.length : Vector3d(1, 0, 0);
  f.up = up.Length() > 1e-9 ? UnitOf(up) : Vector3d(0, 0, 1);
  f.right = kernel::Vector3d::CrossProduct(f.up, f.forward);
  if (f.right.Length() < 1e-9) f.right = Vector3d(0, 1, 0);  // forward parallel to up: arbitrary but stable
  else f.right = UnitOf(f.right);
  return f;
}

kernel::Mesh BuildWallShell(const ArchComponent& c) {
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  return OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -c.thickness / 2, c.thickness / 2, 0, c.height);
}

// A cutter box for a Door/Window opening hosted on `wall`, positioned at
// the point of `opening.p0` projected onto the wall's centreline, spanning
// `opening.width` along the wall and `z0..z1` vertically, oversized in the
// thickness direction so the boolean cleanly removes material through the
// whole wall regardless of small floating-point misalignment.
kernel::Mesh BuildOpeningCutter(const ArchComponent& wall, const ArchComponent& opening, double z0, double z1) {
  RunFrame f = MakeRunFrame(wall.p0, wall.p1, wall.normal);
  Vector3d rel = opening.p0 - wall.p0;
  double along = rel * f.forward;  // distance of the opening's insertion point from the wall's start
  double half_w = opening.width / 2.0;
  double over = std::max(wall.thickness, 0.1) * 2.0;
  return OrientedBox(wall.p0, f.forward, f.right, f.up, along - half_w, along + half_w, -over / 2, over / 2, z0, z1);
}

kernel::Mesh BuildColumn(const ArchComponent& c) {
  Vector3d up = c.normal.Length() > 1e-9 ? UnitOf(c.normal) : Vector3d(0, 0, 1);
  Vector3d ref = std::fabs(up.z) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d right = UnitOf(kernel::Vector3d::CrossProduct(up, ref));
  Vector3d forward = UnitOf(kernel::Vector3d::CrossProduct(right, up));
  return OrientedBox(c.p0, forward, right, up, -c.width / 2, c.width / 2, -c.thickness / 2, c.thickness / 2, 0, c.height);
}

kernel::Mesh BuildBeam(const ArchComponent& c) {
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  // The beam's cross-section (width x thickness) sits centred on the
  // p0-p1 line, spanning from -thickness/2 to +thickness/2 so the line
  // itself is the beam's own centreline (matches how Line/Pipe place
  // their profile in the rest of the app).
  return OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -c.width / 2, c.width / 2, -c.thickness / 2, c.thickness / 2);
}

kernel::Mesh BuildSlab(const ArchComponent& c) {
  Vector3d up = c.normal.Length() > 1e-9 ? UnitOf(c.normal) : Vector3d(0, 0, 1);
  Vector3d ref = std::fabs(up.z) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d ei = UnitOf(kernel::Vector3d::CrossProduct(ref, up));
  Vector3d ej = UnitOf(kernel::Vector3d::CrossProduct(up, ei));
  Vector3d rel = c.p1 - c.p0;
  double iu = rel * ei, jv = rel * ej;
  return OrientedBox(c.p0, ei, ej, up, 0, iu, 0, jv, -c.thickness, 0);
}

// Roof over the footprint rectangle p0..p1 (in the world-XY plane): a
// gable (two sloped rectangles meeting at a ridge running parallel to the
// footprint's longer side, with triangular gable ends) or a shed (one
// sloped rectangle, full height on one eave, `ridge_height` on the other).
// Built as one open shell (a roof is a covering, not a solid Rhino has no
// equivalent primitive for either - see AUDIT.md) rather than a closed
// mesh, so BuildGeometry() below adds it as a Mesh object directly instead
// of routing it through OrientedBox/BooleanCombine.
kernel::Mesh BuildRoof(const ArchComponent& c) {
  kernel::Mesh m;
  ON_Mesh& r = m.raw();
  double x0 = std::min(c.p0.x, c.p1.x), x1 = std::max(c.p0.x, c.p1.x);
  double y0 = std::min(c.p0.y, c.p1.y), y1 = std::max(c.p0.y, c.p1.y);
  double z = c.p0.z;
  double xm = (x0 + x1) / 2.0;
  if (c.roof_style == 1) {
    // Shed: single slope from the y0 eave (z) up to the y1 eave (z + ridge_height).
    r.SetVertex(0, ON_3dPoint(x0, y0, z));
    r.SetVertex(1, ON_3dPoint(x1, y0, z));
    r.SetVertex(2, ON_3dPoint(x1, y1, z + c.ridge_height));
    r.SetVertex(3, ON_3dPoint(x0, y1, z + c.ridge_height));
    r.SetQuad(0, 0, 1, 2, 3);
  } else {
    // Gable: ridge line at x=xm, z+ridge_height, running the full y span;
    // two sloped rectangles (west eave to ridge, ridge to east eave) plus
    // two triangular gable ends closing the y0/y1 faces.
    r.SetVertex(0, ON_3dPoint(x0, y0, z));
    r.SetVertex(1, ON_3dPoint(x1, y0, z));
    r.SetVertex(2, ON_3dPoint(x1, y1, z));
    r.SetVertex(3, ON_3dPoint(x0, y1, z));
    r.SetVertex(4, ON_3dPoint(xm, y0, z + c.ridge_height));
    r.SetVertex(5, ON_3dPoint(xm, y1, z + c.ridge_height));
    r.SetQuad(0, 0, 4, 5, 3);   // west slope (x0 eave to ridge)
    r.SetQuad(1, 4, 1, 2, 5);   // east slope (ridge to x1 eave)
    r.SetTriangle(2, 0, 1, 4);  // south gable triangle
    r.SetTriangle(3, 3, 5, 2);  // north gable triangle
  }
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return m;
}

std::vector<kernel::Mesh> BuildStairTreads(const ArchComponent& c) {
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  std::vector<kernel::Mesh> treads;
  for (int i = 0; i < std::max(1, c.step_count); ++i) {
    double along0 = i * c.run, along1 = (i + 1) * c.run;
    double z1 = (i + 1) * c.rise;
    treads.push_back(OrientedBox(c.p0, f.forward, f.right, f.up, along0, along1, -c.stair_width / 2, c.stair_width / 2, 0, z1));
  }
  return treads;
}

}  // namespace

// ---------------------------------------------------------------------------
// Building/rebuilding: places (or replaces) `c`'s objects in `doc` and
// returns their ids. Door/Window add nothing of their own - their only
// effect is the cut they contribute to their host Wall's shell.
// ---------------------------------------------------------------------------
namespace {

std::vector<ObjectId> BuildGeometry(Document& doc, ArchComponent& c, const std::vector<ArchComponent>& all) {
  std::vector<ObjectId> ids;
  auto add_mesh = [&](kernel::Mesh m, const std::string& name) {
    SceneObject o = SceneObject::MakeMesh(m);
    o.name = name;
    ids.push_back(doc.Add(std::move(o)));
  };
  switch (c.type) {
    case ArchType::Wall: {
      kernel::Mesh shell = BuildWallShell(c);
      for (const ArchComponent& other : all) {
        if (other.host != static_cast<ObjectId>(c.id)) continue;
        if (other.type != ArchType::Door && other.type != ArchType::Window) continue;
        double z0 = other.type == ArchType::Window ? other.sill_height : 0.0;
        double z1 = other.type == ArchType::Window ? other.sill_height + other.height : other.height;
        z1 = std::min(z1, c.height);
        if (z1 <= z0) continue;
        try {
          kernel::Mesh cutter = BuildOpeningCutter(c, other, z0, z1);
          shell = kernel::BooleanCombine(shell, cutter, kernel::BooleanOp::Difference);
        } catch (const std::exception&) {
          // Opening geometry degenerate (e.g. wall rebuilt shorter than the
          // opening's position) - leave the shell uncut rather than fail
          // the whole Wall rebuild.
        }
      }
      add_mesh(shell, "Wall");
      break;
    }
    case ArchType::Door:
    case ArchType::Window:
      break;  // no geometry of their own; see the host Wall's case above
    case ArchType::Slab: add_mesh(BuildSlab(c), "Slab"); break;
    case ArchType::Roof: add_mesh(BuildRoof(c), "Roof"); break;
    case ArchType::Column: add_mesh(BuildColumn(c), "Column"); break;
    case ArchType::Beam: add_mesh(BuildBeam(c), "Beam"); break;
    case ArchType::Stair: {
      int i = 0;
      for (kernel::Mesh& tread : BuildStairTreads(c)) add_mesh(tread, "Stair tread " + std::to_string(++i));
      break;
    }
  }
  return ids;
}

}  // namespace

// ---------------------------------------------------------------------------
// Persistence (Document::UserText()["dino8.arch"] as a JSON array) -
// same trick as sketch/Constraints.cpp.
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kUserTextKey = "dino8.arch";
}

std::vector<ArchComponent> LoadArch(const Document& doc) {
  std::vector<ArchComponent> out;
  auto it = const_cast<Document&>(doc).UserText().find(kUserTextKey);
  if (it == const_cast<Document&>(doc).UserText().end() || it->second.empty()) return out;
  json::Value root;
  std::string err;
  if (!json::Parse(it->second, root, err) || !root.IsArray()) return out;
  for (size_t i = 0; i < root.Size(); ++i) {
    const json::Value& v = root[i];
    ArchComponent c;
    c.id = static_cast<int>(v["id"].number);
    ArchType t;
    if (!ParseArchType(v["type"].AsString(), t)) continue;
    c.type = t;
    c.p0 = Point3d(v["p0x"].number, v["p0y"].number, v["p0z"].number);
    c.p1 = Point3d(v["p1x"].number, v["p1y"].number, v["p1z"].number);
    c.normal = Vector3d(v["nx"].number, v["ny"].number, v["nz"].number);
    if (c.normal.Length() < 1e-9) c.normal = Vector3d(0, 0, 1);
    c.height = v["height"].number;
    c.thickness = v["thickness"].number;
    c.width = v["width"].number;
    c.sill_height = v["sill"].number;
    c.ridge_height = v["ridge"].number;
    c.roof_style = static_cast<int>(v["roof_style"].number);
    c.step_count = static_cast<int>(v["steps"].number);
    c.rise = v["rise"].number;
    c.run = v["run"].number;
    c.stair_width = v["stair_width"].number;
    c.host = static_cast<ObjectId>(v["host"].number);
    const json::Value& objs = v["objects"];
    for (size_t j = 0; j < objs.Size(); ++j) c.objects.push_back(static_cast<ObjectId>(objs[j].number));
    out.push_back(c);
  }
  return out;
}

void SaveArch(Document& doc, const std::vector<ArchComponent>& list) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < list.size(); ++i) {
    const ArchComponent& c = list[i];
    out << (i ? "," : "") << "{\"id\":" << c.id << ",\"type\":\"" << ArchTypeName(c.type) << "\""
        << ",\"p0x\":" << c.p0.x << ",\"p0y\":" << c.p0.y << ",\"p0z\":" << c.p0.z
        << ",\"p1x\":" << c.p1.x << ",\"p1y\":" << c.p1.y << ",\"p1z\":" << c.p1.z
        << ",\"nx\":" << c.normal.x << ",\"ny\":" << c.normal.y << ",\"nz\":" << c.normal.z
        << ",\"height\":" << c.height << ",\"thickness\":" << c.thickness << ",\"width\":" << c.width
        << ",\"sill\":" << c.sill_height << ",\"ridge\":" << c.ridge_height << ",\"roof_style\":" << c.roof_style
        << ",\"steps\":" << c.step_count << ",\"rise\":" << c.rise << ",\"run\":" << c.run << ",\"stair_width\":" << c.stair_width
        << ",\"host\":" << c.host << ",\"objects\":[";
    for (size_t j = 0; j < c.objects.size(); ++j) out << (j ? "," : "") << c.objects[j];
    out << "]}";
  }
  out << "]";
  doc.UserText()[kUserTextKey] = out.str();
}

namespace {
void RemoveObjects(Document& doc, const std::vector<ObjectId>& ids) {
  for (ObjectId id : ids) doc.Remove(id);
}
}  // namespace

int AddArchComponent(Document& doc, ArchComponent c) {
  std::vector<ArchComponent> list = LoadArch(doc);
  int next_id = 1;
  for (const ArchComponent& e : list) next_id = std::max(next_id, e.id + 1);
  c.id = next_id;
  list.push_back(c);
  // Doors/Windows contribute a cut to their host Wall but never own
  // geometry themselves; a freshly added Wall (or one whose opening list
  // just grew) needs its shell rebuilt with every current opening.
  ArchComponent& stored = list.back();
  stored.objects = BuildGeometry(doc, stored, list);
  if ((c.type == ArchType::Door || c.type == ArchType::Window) && c.host != 0) {
    for (ArchComponent& wall : list) {
      if (wall.id == static_cast<int>(c.host) && wall.type == ArchType::Wall) {
        RemoveObjects(doc, wall.objects);
        wall.objects = BuildGeometry(doc, wall, list);
      }
    }
  }
  SaveArch(doc, list);
  return stored.id;
}

bool DeleteArchComponent(Document& doc, int id) {
  std::vector<ArchComponent> list = LoadArch(doc);
  auto it = std::find_if(list.begin(), list.end(), [id](const ArchComponent& c) { return c.id == id; });
  if (it == list.end()) return false;
  RemoveObjects(doc, it->objects);
  const bool was_wall = it->type == ArchType::Wall;
  list.erase(it);
  if (was_wall) {
    // Any Door/Window hosted on the deleted wall no longer has a shell to
    // cut into; drop them too rather than leaving orphaned records.
    list.erase(std::remove_if(list.begin(), list.end(), [id](const ArchComponent& c) {
                 return (c.type == ArchType::Door || c.type == ArchType::Window) && c.host == static_cast<ObjectId>(id);
               }),
               list.end());
  }
  SaveArch(doc, list);
  return true;
}

bool FindArchComponent(const Document& doc, int id, ArchComponent& out) {
  for (const ArchComponent& c : LoadArch(doc)) if (c.id == id) { out = c; return true; }
  return false;
}

bool FindArchComponentByObject(const Document& doc, ObjectId object, ArchComponent& out) {
  for (const ArchComponent& c : LoadArch(doc)) {
    if (std::find(c.objects.begin(), c.objects.end(), object) != c.objects.end()) { out = c; return true; }
  }
  return false;
}

int RebuildArchComponent(Document& doc, const ArchComponent& updated) {
  std::vector<ArchComponent> list = LoadArch(doc);
  auto it = std::find_if(list.begin(), list.end(), [&](const ArchComponent& c) { return c.id == updated.id; });
  if (it == list.end()) return -1;
  RemoveObjects(doc, it->objects);
  *it = updated;
  it->objects = BuildGeometry(doc, *it, list);
  if (it->type == ArchType::Wall) {
    for (ArchComponent& other : list) {
      if (other.id == it->id) continue;
      if ((other.type == ArchType::Door || other.type == ArchType::Window) && other.host == static_cast<ObjectId>(it->id)) {
        // The wall's own rebuild already cut every current opening into
        // its fresh shell (BuildGeometry looks at the whole `list`), so
        // openings need no rebuild of their own here - they own no
        // geometry to begin with.
      }
    }
  }
  SaveArch(doc, list);
  return it->id;
}

void RebuildAll(Document& doc) {
  std::vector<ArchComponent> list = LoadArch(doc);
  for (ArchComponent& c : list) RemoveObjects(doc, c.objects);
  for (ArchComponent& c : list) c.objects = BuildGeometry(doc, c, list);
  SaveArch(doc, list);
}

}  // namespace dino8::arch
