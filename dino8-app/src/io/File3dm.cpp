#include "io/File3dm.h"

#include "geom/BrepMesher.h"

#include <opennurbs.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>

#include "commands/DimGeometry.h"
#include "drafting/HatchBuild.h"
#include "drafting/HatchLibrary.h"
#include "geom/TextOutline.h"
#include "util/json_mini.h"

namespace dino8::app {

namespace {

// Block definitions (see doc/BlockInstances.h): `Dino8.BlocksMeta` document
// user text holds the small stuff (name/base/description/states) as one
// JSON array, same trick as dino8.arch/dino8.constraints; each definition's
// *objects* persist as ordinary geometry components (reusing the exact
// read/write code the main object loop below already has for every curve/
// brep/mesh/SubD kind), tagged `Dino8.BlockDefOf`=<name> and
// `Dino8.BlockDefIndex`=<position> so Load3dm can pull them back out of
// Document::Objects() into their BlockDefinition instead of leaving them as
// ordinary (if invisible) scene objects.
std::string JsonEscapeBlock(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
        else out += c;
    }
  }
  return out;
}

std::string EncodeBlocksMeta(const std::vector<BlockDefinition>& blocks) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < blocks.size(); ++i) {
    const BlockDefinition& b = blocks[i];
    out << (i ? "," : "") << "{\"name\":\"" << JsonEscapeBlock(b.name) << "\",\"desc\":\"" << JsonEscapeBlock(b.description) << "\""
        << ",\"bx\":" << b.base.x << ",\"by\":" << b.base.y << ",\"bz\":" << b.base.z << ",\"states\":[";
    for (size_t j = 0; j < b.states.size(); ++j) out << (j ? "," : "") << "\"" << JsonEscapeBlock(b.states[j]) << "\"";
    out << "],\"aax\":" << b.array_axis.x << ",\"aay\":" << b.array_axis.y << ",\"aaz\":" << b.array_axis.z
        << ",\"aspc\":" << b.array_spacing
        << ",\"stx\":" << b.stretch_axis.x << ",\"sty\":" << b.stretch_axis.y << ",\"stz\":" << b.stretch_axis.z
        << ",\"lupk\":[";
    for (size_t j = 0; j < b.lookup_keys.size(); ++j) out << (j ? "," : "") << "\"" << JsonEscapeBlock(b.lookup_keys[j]) << "\"";
    out << "],\"lups\":[";
    for (size_t j = 0; j < b.lookup_states.size(); ++j) out << (j ? "," : "") << "\"" << JsonEscapeBlock(b.lookup_states[j]) << "\"";
    out << "]}";
  }
  out << "]";
  return out.str();
}

std::map<std::string, BlockDefinition> DecodeBlocksMeta(const std::string& text) {
  std::map<std::string, BlockDefinition> out;
  if (text.empty()) return out;
  json::Value root;
  std::string err;
  if (!json::Parse(text, root, err) || !root.IsArray()) return out;
  for (size_t i = 0; i < root.Size(); ++i) {
    const json::Value& v = root[i];
    BlockDefinition b;
    b.name = v["name"].AsString();
    b.description = v["desc"].AsString();
    b.base = kernel::Point3d(v["bx"].number, v["by"].number, v["bz"].number);
    const json::Value& st = v["states"];
    for (size_t j = 0; j < st.Size(); ++j) b.states.push_back(st[j].AsString());
    // Missing (a file saved before the Array parameter existed) reads back
    // as 0/0/0 axis with 0 spacing - harmless, since PlaceFiltered treats
    // array_spacing == 0 as "no array parameter defined" regardless of axis.
    b.array_axis = kernel::Vector3d(v["aax"].number, v["aay"].number, v["aaz"].number);
    b.array_spacing = v["aspc"].number;
    // Missing (a file saved before the Stretch parameter existed) reads back
    // as a 0/0/0 axis - harmless, since PlaceFiltered falls back to +X
    // whenever Unitize() fails on a zero vector, the same fallback the
    // mirror-flip transform's own normal already uses.
    b.stretch_axis = kernel::Vector3d(v["stx"].number, v["sty"].number, v["stz"].number);
    // Missing (a file saved before the Lookup parameter existed) reads back
    // as empty tables - harmless, since ResolveLookupState treats an empty
    // lookup_keys as "no lookup parameter defined" regardless of a key.
    const json::Value& lupk = v["lupk"];
    for (size_t j = 0; j < lupk.Size(); ++j) b.lookup_keys.push_back(lupk[j].AsString());
    const json::Value& lups = v["lups"];
    for (size_t j = 0; j < lups.Size(); ++j) b.lookup_states.push_back(lups[j].AsString());
    out[b.name] = b;
  }
  return out;
}

// Cage-editing captive originals (see doc/Document.h's comment on
// CageBinding): one entry per cage, holding just the small per-cage numbers
// (nx/ny/nz and the lattice positions the captives were last evaluated
// with) - every captive's own `local`/`original` travels as an extra
// hidden geometry component instead (see the write/read loops in Save3dm/
// Load3dm below, same "real geometry component + small JSON sidecar"
// pattern as EncodeBlocksMeta above). `cage_uuid` is the cage object's
// stable file uuid, resolved back to a live ObjectId on read the same way
// DimRefObj1/2/3 are.
struct CageBindingMeta {
  std::string cage_uuid;
  int nx = 2, ny = 2, nz = 2;
  std::vector<kernel::Point3d> lattice;
};

std::string EncodeCageBindingsMeta(const std::vector<CageBindingMeta>& bindings) {
  std::ostringstream out;
  out << std::setprecision(17) << "[";
  for (size_t i = 0; i < bindings.size(); ++i) {
    const CageBindingMeta& b = bindings[i];
    out << (i ? "," : "") << "{\"cage\":\"" << b.cage_uuid << "\",\"nx\":" << b.nx << ",\"ny\":" << b.ny << ",\"nz\":" << b.nz << ",\"lat\":[";
    for (size_t j = 0; j < b.lattice.size(); ++j) {
      const kernel::Point3d& p = b.lattice[j];
      out << (j ? "," : "") << "[" << p.x << "," << p.y << "," << p.z << "]";
    }
    out << "]}";
  }
  out << "]";
  return out.str();
}

std::vector<CageBindingMeta> DecodeCageBindingsMeta(const std::string& text) {
  std::vector<CageBindingMeta> out;
  if (text.empty()) return out;
  json::Value root;
  std::string err;
  if (!json::Parse(text, root, err) || !root.IsArray()) return out;
  for (size_t i = 0; i < root.Size(); ++i) {
    const json::Value& v = root[i];
    CageBindingMeta b;
    b.cage_uuid = v["cage"].AsString();
    b.nx = static_cast<int>(v["nx"].number);
    b.ny = static_cast<int>(v["ny"].number);
    b.nz = static_cast<int>(v["nz"].number);
    const json::Value& lat = v["lat"];
    for (size_t j = 0; j < lat.Size(); ++j) {
      const json::Value& p = lat[j];
      b.lattice.emplace_back(p[static_cast<size_t>(0)].number, p[static_cast<size_t>(1)].number, p[static_cast<size_t>(2)].number);
    }
    out.push_back(std::move(b));
  }
  return out;
}

// Parses the ';'-separated "x,y,z" triples Save3dm writes to a captive
// original's "Dino8.CaptiveLocal" user string back into per-point lattice
// coordinates.
std::vector<kernel::Vector3d> DecodeCaptiveLocal(const std::string& text) {
  std::vector<kernel::Vector3d> out;
  std::istringstream in(text);
  std::string tok;
  while (std::getline(in, tok, ';')) {
    if (tok.empty()) continue;
    double x = 0, y = 0, z = 0;
    if (std::sscanf(tok.c_str(), "%lf,%lf,%lf", &x, &y, &z) == 3) out.emplace_back(x, y, z);
  }
  return out;
}

std::string LowerExt(const std::string& path) {
  std::string e = std::filesystem::path(path).extension().string();
  for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

std::string FromWide(const ON_wString& w) {
  ON_String s(w);
  return std::string(static_cast<const char*>(s));
}

ON_Color ToOnColor(const Color& c) {
  return ON_Color(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255));
}

Color FromOnColor(const ON_Color& c) { return Color::FromBytes(c.Red(), c.Green(), c.Blue()); }

std::string ColorKey(const Color& c) {
  return std::to_string(static_cast<int>(c.r * 255 + 0.5f)) + "," + std::to_string(static_cast<int>(c.g * 255 + 0.5f)) + "," + std::to_string(static_cast<int>(c.b * 255 + 0.5f));
}

bool ColorFromKey(const std::string& t, Color& out) {
  int r, g, b;
  if (std::sscanf(t.c_str(), "%d,%d,%d", &r, &g, &b) != 3) return false;
  out = Color::FromBytes(r, g, b);
  return true;
}

// Render settings travel as document user strings (Dino8.Render.*).
void WriteRenderSettings(ONX_Model& model, const RenderSettings& r) {
  auto set = [&](const char* key, const std::string& v) { model.SetDocumentUserString(ON_wString(key), ON_wString(v.c_str())); };
  set("Dino8.Render.Background", std::to_string(static_cast<int>(r.background)));
  set("Dino8.Render.BackgroundColor", ColorKey(r.background_color));
  set("Dino8.Render.GradientTop", ColorKey(r.gradient_top));
  set("Dino8.Render.GradientBottom", ColorKey(r.gradient_bottom));
  set("Dino8.Render.GradientView", r.gradient_view ? "1" : "0");
  set("Dino8.Render.GroundPlane", r.ground_plane ? "1" : "0");
  set("Dino8.Render.GroundAutoHeight", r.ground_auto_height ? "1" : "0");
  set("Dino8.Render.GroundHeight", std::to_string(r.ground_height));
  set("Dino8.Render.GroundColor", ColorKey(r.ground_color));
  set("Dino8.Render.GroundShadows", r.ground_shadows ? "1" : "0");
  set("Dino8.Render.Sun", r.sun ? "1" : "0");
  set("Dino8.Render.SunAzimuth", std::to_string(r.sun_azimuth));
  set("Dino8.Render.SunAltitude", std::to_string(r.sun_altitude));
  set("Dino8.Render.SunIntensity", std::to_string(r.sun_intensity));
  set("Dino8.Render.SunColor", ColorKey(r.sun_color));
  set("Dino8.Render.Skylight", r.skylight ? "1" : "0");
  set("Dino8.Render.Width", std::to_string(r.render_width));
  set("Dino8.Render.Height", std::to_string(r.render_height));
  set("Dino8.Render.Quality", std::to_string(r.render_quality));
  if (!r.environment_image.empty()) set("Dino8.Render.EnvironmentImage", r.environment_image);
}

void ReadRenderSettings(const std::map<std::string, std::string>& strings, RenderSettings& r) {
  auto get = [&](const char* key) -> const std::string* { auto it = strings.find(key); return it == strings.end() ? nullptr : &it->second; };
  auto num = [&](const char* key, double& v) { if (const std::string* t = get(key)) v = std::atof(t->c_str()); };
  auto flag = [&](const char* key, bool& v) { if (const std::string* t = get(key)) v = *t == "1"; };
  auto col = [&](const char* key, Color& v) { if (const std::string* t = get(key)) ColorFromKey(*t, v); };
  if (const std::string* t = get("Dino8.Render.Background")) r.background = static_cast<RenderSettings::Background>(std::clamp(std::atoi(t->c_str()), 0, 2));
  col("Dino8.Render.BackgroundColor", r.background_color);
  col("Dino8.Render.GradientTop", r.gradient_top);
  col("Dino8.Render.GradientBottom", r.gradient_bottom);
  flag("Dino8.Render.GradientView", r.gradient_view);
  flag("Dino8.Render.GroundPlane", r.ground_plane);
  flag("Dino8.Render.GroundAutoHeight", r.ground_auto_height);
  num("Dino8.Render.GroundHeight", r.ground_height);
  col("Dino8.Render.GroundColor", r.ground_color);
  flag("Dino8.Render.GroundShadows", r.ground_shadows);
  flag("Dino8.Render.Sun", r.sun);
  num("Dino8.Render.SunAzimuth", r.sun_azimuth);
  num("Dino8.Render.SunAltitude", r.sun_altitude);
  double d = r.sun_intensity; num("Dino8.Render.SunIntensity", d); r.sun_intensity = static_cast<float>(d);
  col("Dino8.Render.SunColor", r.sun_color);
  flag("Dino8.Render.Skylight", r.skylight);
  d = r.render_width; num("Dino8.Render.Width", d); r.render_width = std::clamp(static_cast<int>(d), 16, 8192);
  d = r.render_height; num("Dino8.Render.Height", d); r.render_height = std::clamp(static_cast<int>(d), 16, 8192);
  d = r.render_quality; num("Dino8.Render.Quality", d); r.render_quality = std::clamp(static_cast<int>(d), 1, 4);
  if (const std::string* t = get("Dino8.Render.EnvironmentImage")) r.environment_image = *t;
}

void AddLightFromOn(Document& doc, const ON_Light& light_ref, const ON_3dmObjectAttributes* attr) {
  const ON_Light* light = &light_ref;
  Light L;
  L.name = FromWide(light->LightName());
  if (L.name.empty() && attr) L.name = FromWide(attr->Name());
  switch (light->Style()) {
    case ON::world_spot_light: case ON::camera_spot_light: L.type = LightType::Spot; break;
    case ON::world_directional_light: case ON::camera_directional_light: L.type = LightType::Directional; break;
    case ON::world_linear_light: L.type = LightType::Linear; break;
    case ON::world_rectangular_light: L.type = LightType::Rectangular; break;
    default: L.type = LightType::Point; break;
  }
  L.position = light->Location();
  L.direction = light->Direction();
  if (!L.direction.Unitize()) L.direction = kernel::Vector3d(0, 0, -1);
  L.color = FromOnColor(light->Diffuse());
  L.intensity = static_cast<float>(light->Intensity());
  L.spot_angle = static_cast<float>(std::clamp(light->SpotAngleDegrees(), 1.0, 89.0));
  L.enabled = light->IsEnabled();
  if (L.type == LightType::Spot) { L.length = light->Direction().Length(); if (L.length <= 0) L.length = 10; }
  if (L.type == LightType::Rectangular || L.type == LightType::Linear) {
    L.x_axis = light->Length();
    L.length = L.x_axis.Length();
    if (!L.x_axis.Unitize()) L.x_axis = kernel::Vector3d(1, 0, 0);
    L.width = light->Width().Length();
    if (L.type == LightType::Rectangular) { L.position = light->Location() + light->Length() * 0.5 + light->Width() * 0.5; }
  }
  doc.AddLight(L);
}

bool UuidLess(const ON_UUID& a, const ON_UUID& b) { return ON_UuidCompare(a, b) < 0; }
using UuidMap = std::map<ON_UUID, int, bool (*)(const ON_UUID&, const ON_UUID&)>;

// "x,y,z" tag format a real Text command's own glyph curves carry (see
// annotate_common.h's PointTag/TagGlyph) - reimplemented locally, not via
// that header, since it pulls in commands/cmd_common.h and from there
// app/Application.h, the same heavier chain this file's DXF-writer sibling
// (FileExchange.cpp's DxfTextGlyphSpecOf) already keeps the I/O layer away
// from. Read back by the exact same annotate_common.h::ParsePointTag
// (plain sscanf("%lf,%lf,%lf", ...)), so the format only has to match, not
// come from the same function.
std::string PointTagLocal(double x, double y, double z) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.10g,%.10g,%.10g", x, y, z);
  return buf;
}

// Matching plain-number counterpart for a single-value tag ("TextHeight") -
// annotate_common.h's GlyphSpecOf reads it back with plain std::atof, so
// (unlike the comma-joined PointTag format above) any %g-style text works.
std::string NumberTagLocal(double v) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.10g", v);
  return buf;
}

// Same curve+label+tag-group assembly as io/FileExchange.cpp's own (file-
// local, anonymous-namespace) AddDimensionGroupToDoc, for the exact same
// reason PointTagLocal/NumberTagLocal above duplicate annotate_common.h's
// tag codecs rather than pull it in: that helper is itself file-local to
// FileExchange.cpp, consistent with this codebase's convention of keeping
// these Document-dependent dimension builders un-shared across translation
// units (only the Document-independent math in commands/DimGeometry.h is
// actually shared). Reconstructs a native-.3dm ON_DimLinear/ON_DimRadial
// read below into the identical tagged curve group a live Dim/DimRadius
// command or a DXF/DWG DIMENSION import already produces, so it is
// selectable (SelDim) and rebuildable (UpdateDimensions) exactly the same
// way.
bool AddDimensionGroupToDocLocal(Document& doc, const std::string& kind, int layer,
                                  const std::vector<kernel::NurbsCurve>& curves, const DimGlyphSpec& text,
                                  const std::map<std::string, std::string>& tags) {
  std::vector<ObjectId> ids;
  for (const kernel::NurbsCurve& c : curves) {
    SceneObject o = SceneObject::MakeCurve(c);
    o.layer_index = layer;
    o.user_text["Annotation"] = kind;
    o.user_text["Style"] = "Standard";
    for (const auto& [k, v] : tags) o.user_text[k] = v;
    ids.push_back(doc.Add(std::move(o)));
  }
  if (!text.text.empty()) {
    std::vector<kernel::NurbsCurve> glyphs;
    std::string font_used;
    double width = 0;
    if (TextToCurves(text.text, text.height, text.plane, glyphs, font_used, &width)) {
      const ON_Xform shift = ON_Xform::TranslationTransformation(-text.plane.xaxis * (text.center ? width / 2 : 0));
      for (kernel::NurbsCurve gc : glyphs) {
        if (text.center) gc.raw().Transform(shift);
        SceneObject o = SceneObject::MakeCurve(gc);
        o.layer_index = layer;
        o.user_text["Annotation"] = kind;
        o.user_text["Style"] = "Standard";
        ids.push_back(doc.Add(std::move(o)));
      }
    }
  }
  if (ids.empty()) return false;
  doc.CreateGroup(ids, kind);
  return true;
}

void CameraToViewport(const CameraState& c, ON_Viewport& vp) {
  vp.SetProjection(c.perspective ? ON::perspective_view : ON::parallel_view);
  vp.SetCameraLocation(c.eye);
  vp.SetCameraDirection(c.target - c.eye);
  vp.SetCameraUp(c.up);
  vp.SetTargetPoint(c.target);
  if (!c.perspective) vp.SetFrustum(-c.ortho_height / 2, c.ortho_height / 2, -c.ortho_height / 2, c.ortho_height / 2, 1, 1e6);
}

CameraState ViewportToCamera(const ON_Viewport& vp) {
  CameraState c;
  c.eye = vp.CameraLocation();
  c.target = vp.TargetPoint();
  c.up = vp.CameraUp();
  c.perspective = vp.IsPerspectiveProjection();
  double l, r, b, t;
  if (vp.GetFrustum(&l, &r, &b, &t) && !c.perspective && t - b > 0) c.ortho_height = t - b;
  return c;
}

std::string UuidString(const ON_UUID& id) { char buf[64] = {}; ON_UuidToString(id, buf); return buf; }

// Animation frames <-> one document user string.
std::string AnimationToString(const Animation& a) {
  std::ostringstream out;
  out << a.kind << "|" << a.viewport << "|";
  for (size_t i = 0; i < a.frames.size(); ++i) {
    const CameraState& c = a.frames[i];
    if (i) out << ";";
    out << c.eye.x << "," << c.eye.y << "," << c.eye.z << "," << c.target.x << "," << c.target.y << "," << c.target.z << ","
        << c.up.x << "," << c.up.y << "," << c.up.z << "," << (c.perspective ? 1 : 0) << "," << c.ortho_height << "," << c.lens_mm;
  }
  return out.str();
}

Animation AnimationFromString(const std::string& text) {
  Animation a;
  const size_t p1 = text.find('|');
  if (p1 == std::string::npos) return a;
  const size_t p2 = text.find('|', p1 + 1);
  if (p2 == std::string::npos) return a;
  a.kind = text.substr(0, p1);
  a.viewport = text.substr(p1 + 1, p2 - p1 - 1);
  std::istringstream in(text.substr(p2 + 1));
  std::string frame;
  while (std::getline(in, frame, ';')) {
    double v[12] = {};
    if (std::sscanf(frame.c_str(), "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11]) != 12) continue;
    CameraState c;
    c.eye = kernel::Point3d(v[0], v[1], v[2]); c.target = kernel::Point3d(v[3], v[4], v[5]); c.up = kernel::Vector3d(v[6], v[7], v[8]);
    c.perspective = v[9] != 0; c.ortho_height = v[10]; c.lens_mm = v[11];
    a.frames.push_back(c);
  }
  return a;
}

// Range check for a mesh read off disk: see the comment at its use in
// Load3dm(). Same rule as the kernel's own Model::Load() (file_io.cpp):
// every face index inside [0, VertexCount()), nothing stricter.
bool MeshFaceIndicesInRange(const ON_Mesh& mesh) {
  const int vertex_count = mesh.m_V.Count();
  for (int i = 0; i < mesh.m_F.Count(); ++i) {
    const ON_MeshFace& face = mesh.m_F[i];
    for (int k = 0; k < 4; ++k) {
      if (face.vi[k] < 0 || face.vi[k] >= vertex_count) return false;
    }
  }
  return true;
}

// Looks up an ON_Hatch's referenced ON_HatchPattern in the file's pattern
// table by index, the same way the layer/material/linetype lookups above do.
// A negative index with no matching table entry is one of OpenNURBS' own
// built-in stock patterns (ON_HatchPattern::Solid is index -1); only -1 is
// treated as solid fill here (the rest are stock line patterns), matching
// what real Rhino files actually emit for an unreferenced/default hatch.
ON_HatchPattern::HatchFillType HatchFillTypeFor(const ONX_Model& model, int pattern_index) {
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::HatchPattern);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_HatchPattern* p = ON_HatchPattern::Cast(c);
    if (p && p->Index() == pattern_index) return p->FillType();
  }
  return pattern_index == -1 ? ON_HatchPattern::HatchFillType::Solid : ON_HatchPattern::HatchFillType::Lines;
}

std::string HatchPatternNameFor(const ONX_Model& model, int pattern_index) {
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::HatchPattern);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_HatchPattern* p = ON_HatchPattern::Cast(c);
    if (p && p->Index() == pattern_index) return FromWide(p->Name());
  }
  return std::string();
}

// Resolves a real ON_InstanceDefinition's member geometry (its
// InstanceGeometryIdList(), each looked up directly off the model by
// ONX_Model::ModelGeometryComponentFromId - no prepass needed, since the
// main Load3dm loop below skips idef-member objects by ON::idef_object
// mode rather than collecting them itself) into Dino8's own BlockDefinition
// shape - the same conversions (Point/Curve/Brep/Surface/Mesh/SubD/
// Extrusion) the main loop already applies to ordinary document objects,
// factored out so this function and that loop share one copy. A member
// object whose geometry kind has no Dino8 scene-object equivalent (nested
// instance refs, annotations, hatches, point clouds inside a block - none
// of which Rhino itself actually allows inside a block definition anyway
// except nested instance refs) is silently dropped from the definition,
// the same per-object "no made flag" convention the main loop already
// uses for a whole-object skip.
BlockDefinition BuildBlockDefinitionFromIdef(const ONX_Model& model, const ON_InstanceDefinition& idef,
                                              const std::map<int, int>& layer_map) {
  BlockDefinition def;
  def.name = FromWide(idef.Name());
  if (def.name.empty()) def.name = "Block";
  def.description = FromWide(idef.Description());
  const ON_SimpleArray<ON_UUID>& member_ids = idef.InstanceGeometryIdList();
  for (int i = 0; i < member_ids.Count(); ++i) {
    const ON_ModelGeometryComponent& mg = model.ModelGeometryComponentFromId(member_ids[i]);
    const ON_Geometry* g = mg.Geometry(nullptr);
    if (!g) continue;
    SceneObject obj;
    bool made = false;
    if (const ON_Point* p = ON_Point::Cast(g)) {
      obj = SceneObject::MakePoint(p->point);
      made = true;
    } else if (const ON_Curve* cv = ON_Curve::Cast(g)) {
      ON_NurbsCurve nc;
      if (cv->GetNurbForm(nc) > 0) {
        kernel::NurbsCurve k;
        k.raw() = nc;
        obj = SceneObject::MakeCurve(k);
        made = true;
      }
    } else if (const ON_Brep* b = ON_Brep::Cast(g)) {
      kernel::Brep k;
      k.raw() = *b;
      obj = SceneObject::MakeBrep(k);
      made = true;
    } else if (const ON_Surface* s = ON_Surface::Cast(g)) {
      ON_NurbsSurface ns;
      if (s->GetNurbForm(ns) > 0) {
        kernel::NurbsSurface k;
        k.raw() = ns;
        obj = SceneObject::MakeSurface(k);
        made = true;
      }
    } else if (const ON_Mesh* m = ON_Mesh::Cast(g)) {
      if (MeshFaceIndicesInRange(*m)) {
        kernel::Mesh k;
        k.raw() = *m;
        obj = SceneObject::MakeMesh(k);
        made = true;
      }
    } else if (const ON_SubD* sd = ON_SubD::Cast(g)) {
      kernel::SubD k;
      k.raw() = *sd;
      obj = SceneObject::MakeSubD(k);
      made = true;
    } else if (const ON_Extrusion* ex = ON_Extrusion::Cast(g)) {
      ON_Brep* b = ex->BrepForm(nullptr);
      if (b) {
        kernel::Brep k;
        k.raw() = *b;
        delete b;
        obj = SceneObject::MakeBrep(k);
        made = true;
      }
    }
    if (!made) continue;
    if (const ON_3dmObjectAttributes* attr = mg.Attributes(nullptr)) {
      auto lm = layer_map.find(attr->m_layer_index);
      if (lm != layer_map.end()) obj.layer_index = lm->second;
      if (attr->ColorSource() == ON::color_from_object) {
        obj.color_by_layer = false;
        obj.color = FromOnColor(attr->m_color);
      }
    }
    def.objects.push_back(std::move(obj));
  }
  return def;
}

}  // namespace

// ---------------------------------------------------------------------------
// .3dm
// ---------------------------------------------------------------------------

bool Load3dm(Document& doc, const std::string& path, std::string& error) {
  ONX_Model model;
  ON_TextLog log;
  if (!model.Read(path.c_str(), &log)) {
    error = "OpenNURBS could not read " + path;
    return false;
  }
  doc.Clear();

  // Layers: map layer index in the file -> layer index in the document.
  std::map<int, int> layer_map;
  std::map<ON_UUID, int, bool (*)(const ON_UUID&, const ON_UUID&)> layer_by_id(
      [](const ON_UUID& a, const ON_UUID& b) { return ON_UuidCompare(a, b) < 0; });
  bool first_layer = true;
  {
    ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::Layer);
    for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
      const ON_Layer* layer = ON_Layer::Cast(c);
      if (!layer) continue;
      const std::string name = FromWide(layer->Name());
      int idx;
      if (first_layer) {
        idx = 0;
        doc.Layers()[0].name = name.empty() ? "Default" : name;
        first_layer = false;
      } else {
        idx = doc.AddLayer(name.empty() ? "Layer" : name);
      }
      Layer& L = doc.Layers()[static_cast<size_t>(idx)];
      L.color = FromOnColor(layer->Color());
      L.visible = layer->IsVisible();
      L.locked = layer->IsLocked();
      layer_map[layer->Index()] = idx;
      layer_by_id[layer->Id()] = idx;
    }
    // Resolve parents.
    it = ONX_ModelComponentIterator(model, ON_ModelComponent::Type::Layer);
    for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
      const ON_Layer* layer = ON_Layer::Cast(c);
      if (!layer) continue;
      const ON_UUID pid = layer->ParentLayerId();
      if (ON_UuidIsNil(pid)) continue;
      auto me = layer_by_id.find(layer->Id());
      auto parent = layer_by_id.find(pid);
      if (me != layer_by_id.end() && parent != layer_by_id.end()) {
        doc.Layers()[static_cast<size_t>(me->second)].parent = parent->second;
      }
    }
  }

  // Materials: file index -> document material name.
  std::map<int, std::string> material_map;
  {
    ONX_ModelComponentIterator mit(model, ON_ModelComponent::Type::RenderMaterial);
    for (const ON_ModelComponent* c = mit.FirstComponent(); c; c = mit.NextComponent()) {
      const ON_Material* om = ON_Material::Cast(c);
      if (!om) continue;
      Material m;
      m.name = FromWide(om->Name());
      if (m.name.empty()) m.name = "Material " + std::to_string(om->Index());
      m.diffuse = FromOnColor(om->Diffuse());
      m.specular = FromOnColor(om->Specular());
      m.emission = FromOnColor(om->Emission());
      m.gloss = static_cast<float>(std::clamp(om->Shine() / ON_Material::MaxShine, 0.0, 1.0));
      m.transparency = static_cast<float>(std::clamp(om->Transparency(), 0.0, 1.0));
      m.reflectivity = static_cast<float>(std::clamp(om->Reflectivity(), 0.0, 1.0));
      for (int t = 0; t < om->m_textures.Count(); ++t) {
        const ON_Texture& tx = om->m_textures[t];
        if (tx.m_type != ON_Texture::TYPE::bitmap_texture && tx.m_type != ON_Texture::TYPE::pbr_base_color_texture) continue;
        m.texture_path = FromWide(tx.m_image_file_reference.FullPath());
        if (m.texture_path.empty()) m.texture_path = FromWide(tx.m_image_file_reference.RelativePath());
        break;
      }
      ON_wString v;
      if (om->GetUserString(L"Dino8.Mapping", v)) ParseTextureMapping(FromWide(v), m.mapping);
      if (om->GetUserString(L"Dino8.MappingScale", v)) m.mapping_scale = static_cast<float>(std::atof(FromWide(v).c_str()));
      if (m.mapping == TextureMapping::Default) m.mapping = TextureMapping::Surface;
      // Names collide? Keep the first; later ones get a suffix.
      std::string base = m.name;
      for (int k = 2; doc.FindMaterial(m.name); ++k) m.name = base + " " + std::to_string(k);
      material_map[om->Index()] = doc.AddMaterial(m);
    }
  }
  // Layer render materials.
  {
    ONX_ModelComponentIterator lit(model, ON_ModelComponent::Type::Layer);
    for (const ON_ModelComponent* c = lit.FirstComponent(); c; c = lit.NextComponent()) {
      const ON_Layer* layer = ON_Layer::Cast(c);
      if (!layer) continue;
      auto lm = layer_map.find(layer->Index());
      auto mm = material_map.find(layer->RenderMaterialIndex());
      if (lm != layer_map.end() && mm != material_map.end()) doc.Layers()[static_cast<size_t>(lm->second)].material = mm->second;
    }
  }
  // Linetypes: the file's table (index -> name) merged into the document's.
  std::map<int, std::string> linetype_by_index;
  {
    ONX_ModelComponentIterator lit(model, ON_ModelComponent::Type::LinePattern);
    for (const ON_ModelComponent* c = lit.FirstComponent(); c; c = lit.NextComponent()) {
      const ON_Linetype* lt = ON_Linetype::Cast(c);
      if (!lt) continue;
      std::string name = FromWide(lt->Name());
      if (name.empty()) continue;
      std::vector<double> pattern;
      for (int i = 0; i < lt->SegmentCount(); ++i) pattern.push_back(lt->Segment(i).m_length);
      // A pattern of a single dash is continuous.
      bool any_gap = false;
      for (int i = 0; i < lt->SegmentCount(); ++i) if (lt->Segment(i).m_seg_type == ON_LinetypeSegment::eSegType::stSpace) any_gap = true;
      if (!any_gap) pattern.clear();
      if (!doc.FindLinetype(name)) doc.SetLinetype(name, pattern);
      else if (!pattern.empty()) doc.FindLinetype(name)->pattern = pattern;
      linetype_by_index[lt->Index()] = name;
    }
    ONX_ModelComponentIterator lyr(model, ON_ModelComponent::Type::Layer);
    for (const ON_ModelComponent* c = lyr.FirstComponent(); c; c = lyr.NextComponent()) {
      const ON_Layer* layer = ON_Layer::Cast(c);
      if (!layer) continue;
      auto me = layer_by_id.find(layer->Id());
      auto lt = linetype_by_index.find(layer->LinetypeIndex());
      if (me != layer_by_id.end() && lt != linetype_by_index.end()) doc.Layers()[static_cast<size_t>(me->second)].linetype = lt->second;
      if (me != layer_by_id.end()) {
        Layer& L = doc.Layers()[static_cast<size_t>(me->second)];
        L.print_width_mm = layer->PlotWeight();
        // m_plot_color (not the PlotColor() getter, which already resolves
        // ON_UNSET_COLOR back to the layer's display color) is the real
        // "is there an override at all" signal - see Layer::has_plot_color's
        // own comment in doc/Document.h.
        if (layer->m_plot_color != ON_Color::UnsetColor) {
          L.has_plot_color = true;
          L.plot_color = FromOnColor(layer->m_plot_color);
        }
        // Named PlotStyle assignment (LayerPlotStyle, PlotStyle below): no
        // native ON_Layer field for this (unlike PlotWeight/PlotColor above),
        // so it rides as a plain per-layer user string, the same mechanism
        // Dino8.DetailLocked/DetailMode use on ON_3dmObjectAttributes above.
        ON_wString ps;
        if (layer->GetUserString(L"Dino8.PlotStyle", ps)) L.plot_style = FromWide(ps);
      }
    }
  }
  // Viewports and layout pages: model views give clipping planes their
  // viewport names, page views become layouts.
  std::map<ON_UUID, std::string, bool (*)(const ON_UUID&, const ON_UUID&)> view_names(UuidLess);
  UuidMap page_layout(UuidLess);
  for (int i = 0; i < model.m_settings.m_views.Count(); ++i) {
    const ON_3dmView& v = model.m_settings.m_views[i];
    if (v.m_view_type == ON::page_view_type) {
      Layout L;
      L.name = FromWide(v.m_name);
      if (L.name.empty()) L.name = "Layout " + std::to_string(doc.Layouts().size() + 1);
      if (v.m_page_settings.m_width_mm > 0) L.width_mm = v.m_page_settings.m_width_mm;
      if (v.m_page_settings.m_height_mm > 0) L.height_mm = v.m_page_settings.m_height_mm;
      page_layout[v.m_vp.ViewportId()] = static_cast<int>(doc.Layouts().size());
      doc.Layouts().push_back(L);
    } else {
      view_names[v.m_vp.ViewportId()] = FromWide(v.m_name);
    }
  }
  struct PendingDetail { int layout; size_t detail; std::string hidden_objects; };
  std::vector<PendingDetail> pending_details;
  UuidMap object_ids(UuidLess);  // object uuid -> document id (as int)
  std::map<int, std::vector<ObjectId>> restore_groups;  // file group_id -> new object ids (see Document::CreateGroup below)
  // Instance-definition uuid -> the Dino8 block name already built for it
  // this load (see the ON_InstanceRef branch below) - an idef referenced by
  // several ON_InstanceRef placements is converted to a BlockDefinition
  // only once.
  std::map<ON_UUID, std::string, bool (*)(const ON_UUID&, const ON_UUID&)> idef_block_name(UuidLess);

  int skipped = 0;
  int corrupt_meshes = 0;  // see the ON_Mesh branch below
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
    if (!mg) continue;
    const ON_Geometry* g = mg->Geometry(nullptr);
    const ON_3dmObjectAttributes* attr = mg->Attributes(nullptr);
    if (!g) continue;
    // Instance-definition member geometry (ON::idef_object mode) exists
    // only to be read back out through ON_InstanceDefinition's own
    // InstanceGeometryIdList(), via BuildBlockDefinitionFromIdef above when
    // an ON_InstanceRef below first names it - it is never itself a
    // document-visible object (a real Rhino file's own reader treats it
    // the same way). Previously unhandled: with no idef_object check at
    // all, this file's loop added every idef member as an ordinary visible
    // scene object too, so opening a real Rhino file with even one block
    // defined duplicated that block's geometry into plain, ungrouped
    // objects on top of the (also previously unhandled - see the
    // ON_InstanceRef branch below) instance placements themselves.
    if (attr && attr->Mode() == ON::idef_object) continue;
    SceneObject obj;
    bool made = false;
    int file_group_id = -1;
    if (const ON_Light* light = ON_Light::Cast(g)) {
      AddLightFromOn(doc, *light, attr);
      continue;
    }
    if (const ON_ClippingPlaneSurface* cps = ON_ClippingPlaneSurface::Cast(g)) {
      ClippingPlane cp;
      cp.origin = cps->m_plane.origin;
      cp.x_axis = cps->m_plane.xaxis;
      cp.y_axis = cps->m_plane.yaxis;
      cp.width = std::max(cps->Extents(0).Length(), 0.1);
      cp.height = std::max(cps->Extents(1).Length(), 0.1);
      cp.enabled = cps->m_clipping_plane.m_bEnabled;
      for (int i = 0; i < cps->m_clipping_plane.m_viewport_ids.Count(); ++i) {
        auto vn = view_names.find(cps->m_clipping_plane.m_viewport_ids.Array()[i]);
        if (vn != view_names.end() && !vn->second.empty()) cp.viewports.push_back(vn->second);
      }
      if (attr) cp.name = FromWide(attr->Name());
      doc.AddClippingPlane(cp);
      continue;
    }
    if (const ON_Hatch* h = ON_Hatch::Cast(g)) {
      // Reconstructed via the same builders DXF/DWG HATCH import and the
      // live Hatch command already share (drafting::BuildSolidHatch/
      // BuildPatternHatch, see HatchBuild.h), so a hatch read from a real
      // Rhino-written .3dm - previously silently skipped here entirely, no
      // ON_Hatch handling existed at all - is indistinguishable from one
      // made in-app: same object kind(s), same Hatch/HatchSpacing/
      // HatchRotation/HatchBoundary user_text tags, selectable via SelHatch.
      bool built = false;
      if (h->LoopCount() > 0) {
        if (ON_Curve* c3 = h->LoopCurve3d(0)) {
          ON_NurbsCurve nc;
          if (c3->GetNurbForm(nc) > 0) {
            kernel::NurbsCurve boundary;
            boundary.raw() = nc;
            int layer_idx = 0;
            Color color;
            bool has_color = false;
            if (attr) {
              auto lm = layer_map.find(attr->m_layer_index);
              if (lm != layer_map.end()) layer_idx = lm->second;
              if (attr->ColorSource() == ON::color_from_object) { color = FromOnColor(attr->m_color); has_color = true; }
            }
            const double tol = doc.Settings().absolute_tolerance > 0 ? doc.Settings().absolute_tolerance : 0.001;
            const Color* color_ptr = has_color ? &color : nullptr;
            if (HatchFillTypeFor(model, h->PatternIndex()) == ON_HatchPattern::HatchFillType::Solid) {
              built = drafting::BuildSolidHatch(doc, boundary, kNoObject, layer_idx, tol, color_ptr);
            } else {
              const drafting::HatchPattern* pat = drafting::HatchLibrary::Instance().Find(HatchPatternNameFor(model, h->PatternIndex()));
              if (!pat) pat = drafting::HatchLibrary::Instance().Find("ANSI31");
              if (pat) {
                built = drafting::BuildPatternHatch(doc, *pat, boundary, kNoObject, layer_idx, tol, h->PatternScale(),
                                                     h->PatternRotation() * 180.0 / ON_PI, doc.Settings().hatch_base, color_ptr);
              }
            }
          }
          delete c3;
        }
      }
      if (!built) ++skipped;
      continue;
    }
    if (const ON_DimLinear* dim = ON_DimLinear::Cast(g)) {
      // Real ON_DimLinear (Rhino's Aligned/Rotated linear dimension),
      // checked ahead of the plain ON_Annotation branch below (which would
      // also match - ON_DimLinear derives from ON_Annotation - but only
      // knows PlainText()/Plane(), not a dimension's own def-points) since
      // this closes part of that branch's own disclosed "Dim*/Leader... still
      // unhandled" gap. Rebuilt via BuildLinearDimensionGeometry
      // (commands/DimGeometry.h), the exact math a live Dim/DimAligned
      // command and DXF/DWG DIMENSION import already share, fed the file's
      // own three plane-local points (DefPoint1/DefPoint2/DimlinePoint)
      // instead of a third live pick. Horizontal-vs-vertical for a Rotated
      // dimension is decided by the identical "offset point lies farther
      // outside the points' vertical span than their horizontal span" rule
      // cmd_annotate.cpp's own DimLinearCommand::Build uses for a live pick.
      const ON_Plane plane = dim->Plane();
      const ON_2dPoint p0_2d = dim->DefPoint1();
      const ON_2dPoint p1_2d = dim->DefPoint2();
      const ON_2dPoint dl_2d = dim->DimlinePoint();
      const bool aligned = dim->Type() == ON::AnnotationType::Aligned;
      LinearDimLayout L;
      L.plane = plane;
      L.aligned = aligned;
      const kernel::Point3d p0 = plane.PointAt(p0_2d.x, p0_2d.y);
      const kernel::Point3d p1 = plane.PointAt(p1_2d.x, p1_2d.y);
      const kernel::Point3d dl = plane.PointAt(dl_2d.x, dl_2d.y);
      if (!aligned) {
        const double dx_out = std::max(0.0, std::fabs(dl_2d.x - (p0_2d.x + p1_2d.x) / 2) - std::fabs(p1_2d.x - p0_2d.x) / 2);
        const double dy_out = std::max(0.0, std::fabs(dl_2d.y - (p0_2d.y + p1_2d.y) / 2) - std::fabs(p1_2d.y - p0_2d.y) / 2);
        bool horizontal = dy_out >= dx_out;
        if (horizontal && std::fabs(p1_2d.x - p0_2d.x) < 1e-9) horizontal = false;
        if (!horizontal && std::fabs(p1_2d.y - p0_2d.y) < 1e-9) horizontal = true;
        L.horizontal = horizontal;
        L.offset = horizontal ? dl_2d.y : dl_2d.x;
      } else {
        kernel::Vector3d n = ON_CrossProduct(plane.zaxis, kernel::Vector3d(p1 - p0));
        n.Unitize();
        L.offset = ON_DotProduct(kernel::Vector3d(dl - p0), n);
      }
      const ON_ModelComponentReference dimstyle_ref =
          model.ComponentFromId(ON_ModelComponent::Type::DimStyle, dim->DimensionStyleId());
      const ON_DimStyle* dimstyle = ON_DimStyle::Cast(dimstyle_ref.ModelComponent());
      double text_h = (dimstyle ? *dimstyle : ON_DimStyle::Default).TextHeight();
      if (text_h <= 0) {
        const AnnotationStyle& ast = doc.CurrentAnnotationStyle();
        text_h = ast.text_height > 0 ? ast.text_height : std::max(doc.Settings().grid_spacing * 2.0, 1e-6);
      }
      int layer_idx = 0;
      if (attr) {
        auto lm = layer_map.find(attr->m_layer_index);
        if (lm != layer_map.end()) layer_idx = lm->second;
      }
      std::vector<kernel::NurbsCurve> curves;
      DimGlyphSpec dim_text;
      std::map<std::string, std::string> tags;
      if (!BuildLinearDimensionGeometry(p0, p1, L, text_h, curves, dim_text, tags) ||
          !AddDimensionGroupToDocLocal(doc, aligned ? "DimAligned" : "DimLinear", layer_idx, curves, dim_text, tags)) {
        ++skipped;
      }
      continue;
    }
    if (const ON_DimRadial* dim = ON_DimRadial::Cast(g)) {
      // Real ON_DimRadial (Rhino's Radius/Diameter dimension) - same
      // rationale and gap-closing as the ON_DimLinear branch above, via
      // BuildRadiusDimensionGeometry instead. `extra` (how far the tag point
      // sits beyond the measured radius point, RadiusDimLayout's own field)
      // is recovered from the file's own leader-tail DimlinePoint, projected
      // onto the center->radius-point direction.
      const bool diameter = dim->Type() == ON::AnnotationType::Diameter;
      const ON_Plane plane = dim->Plane();
      const ON_2dPoint c_2d = dim->CenterPoint();
      const ON_2dPoint r_2d = dim->RadiusPoint();
      const ON_2dPoint dl_2d = dim->DimlinePoint();
      const kernel::Point3d center = plane.PointAt(c_2d.x, c_2d.y);
      const kernel::Point3d radius_pt = plane.PointAt(r_2d.x, r_2d.y);
      const kernel::Point3d dl_pt = plane.PointAt(dl_2d.x, dl_2d.y);
      kernel::Vector3d dir = radius_pt - center;
      const double radius = dir.Length();
      if (radius <= 0 || !dir.Unitize()) { ++skipped; continue; }
      const double extra = ON_DotProduct(kernel::Vector3d(dl_pt - center), dir) - radius;
      RadiusDimLayout L;
      L.diameter = diameter;
      L.plane = plane;
      L.dir = dir;
      L.extra = extra;
      const ON_ModelComponentReference dimstyle_ref =
          model.ComponentFromId(ON_ModelComponent::Type::DimStyle, dim->DimensionStyleId());
      const ON_DimStyle* dimstyle = ON_DimStyle::Cast(dimstyle_ref.ModelComponent());
      double text_h = (dimstyle ? *dimstyle : ON_DimStyle::Default).TextHeight();
      if (text_h <= 0) {
        const AnnotationStyle& ast = doc.CurrentAnnotationStyle();
        text_h = ast.text_height > 0 ? ast.text_height : std::max(doc.Settings().grid_spacing * 2.0, 1e-6);
      }
      int layer_idx = 0;
      if (attr) {
        auto lm = layer_map.find(attr->m_layer_index);
        if (lm != layer_map.end()) layer_idx = lm->second;
      }
      std::vector<kernel::NurbsCurve> curves;
      DimGlyphSpec dim_text;
      std::map<std::string, std::string> tags;
      if (!BuildRadiusDimensionGeometry(center, radius, L, text_h, curves, dim_text, tags) ||
          !AddDimensionGroupToDocLocal(doc, diameter ? "DimDiameter" : "DimRadius", layer_idx, curves, dim_text, tags)) {
        ++skipped;
      }
      continue;
    }
    if (const ON_Leader* leader = ON_Leader::Cast(g)) {
      // Real ON_Leader (Rhino's arrowhead-plus-bend-points-plus-text
      // annotation) - checked ahead of the plain ON_Annotation branch below
      // for the same reason as ON_DimLinear/ON_DimRadial above, closing the
      // "Leader... still have no reader at all" half of that branch's own
      // disclosed gap. Rebuilt via the exact same curve/tag shape a live
      // `Leader` command bakes (`BuildLeaderGroup`, `commands/cmd_annotate.cpp`:
      // a polyline through the leader's own points, an arrowhead at the
      // first point pointing away from the second, and left-aligned glyph
      // text near the last point) - reusing `dim_geom_detail::MakePolyline`/
      // `AddArrow` (`commands/DimGeometry.h`, a named, not anonymous,
      // namespace, so already reachable here) and `AddDimensionGroupToDocLocal`
      // above for the text-glyph-plus-group assembly, rather than
      // reimplementing either. `ON_Leader`'s own point order (arrowhead
      // first, tail/landing last - confirmed against its own `TailDirection`/
      // `LandingLine` doc comments naming the *last* point the tail) matches
      // `BuildLeaderGroup`'s `pts_[0]` = arrowhead convention exactly, so no
      // reordering is needed. `LeaderTip`/`LeaderRest` tags are written the
      // same way so `UpdateDimensions`/`SelLeader` both work on it exactly
      // like a live leader (no `DimRefObj1`/`DimRefEnd1` association, since
      // there is no Dino8 object for an externally-authored point to
      // reference - it behaves like a live leader whose arrowhead wasn't on
      // any object either, the same documented fallback).
      const ON_Plane plane = leader->Plane();
      const ON__UINT32 n = leader->PointCount();
      if (n < 2) { ++skipped; continue; }
      std::vector<kernel::Point3d> pts;
      for (ON__UINT32 i = 0; i < n; ++i) {
        ON_2dPoint p2;
        if (!leader->Point2d(static_cast<int>(i), p2)) { pts.clear(); break; }
        pts.push_back(plane.PointAt(p2.x, p2.y));
      }
      if (pts.size() < 2) { ++skipped; continue; }
      const ON_ModelComponentReference dimstyle_ref =
          model.ComponentFromId(ON_ModelComponent::Type::DimStyle, leader->DimensionStyleId());
      const ON_DimStyle* dimstyle = ON_DimStyle::Cast(dimstyle_ref.ModelComponent());
      double text_h = (dimstyle ? *dimstyle : ON_DimStyle::Default).TextHeight();
      if (text_h <= 0) {
        const AnnotationStyle& ast = doc.CurrentAnnotationStyle();
        text_h = ast.text_height > 0 ? ast.text_height : std::max(doc.Settings().grid_spacing * 2.0, 1e-6);
      }
      int layer_idx = 0;
      if (attr) {
        auto lm = layer_map.find(attr->m_layer_index);
        if (lm != layer_map.end()) layer_idx = lm->second;
      }
      std::vector<kernel::NurbsCurve> curves;
      curves.push_back(dim_geom_detail::MakePolyline(pts));
      dim_geom_detail::AddArrow(curves, pts[0], kernel::Vector3d(pts[0] - pts[1]), text_h, plane);
      DimGlyphSpec leader_text;
      leader_text.text = FromWide(leader->PlainText());
      leader_text.height = text_h;
      leader_text.plane = plane;
      leader_text.plane.SetOrigin(pts.back() + plane.xaxis * (text_h * 0.4) - plane.yaxis * (text_h * 0.5));
      leader_text.center = false;
      std::map<std::string, std::string> tags;
      tags["DimPlaneOrigin"] = DimPointTag(plane.origin);
      tags["DimPlaneX"] = DimPointTag(kernel::Point3d(plane.xaxis));
      tags["DimPlaneY"] = DimPointTag(kernel::Point3d(plane.yaxis));
      tags["LeaderTip"] = DimPointTag(pts[0]);
      {
        std::string s;
        for (size_t i = 1; i < pts.size(); ++i) {
          if (!s.empty()) s += ";";
          s += DimPointTag(kernel::Point3d(pts[i] - pts[0]));
        }
        tags["LeaderRest"] = s;
      }
      if (!AddDimensionGroupToDocLocal(doc, "Leader", layer_idx, curves, leader_text, tags)) ++skipped;
      continue;
    }
    if (const ON_DimAngular* dim = ON_DimAngular::Cast(g)) {
      // Real ON_DimAngular (Rhino's Angular/Angular3pt dimension) - checked
      // ahead of the plain ON_Annotation branch below for the same reason as
      // ON_DimLinear/ON_DimRadial/ON_Leader above. Rebuilt via
      // `BuildAngleDimensionGeometry` (`commands/DimGeometry.h`), extracted
      // from `cmd_annotate.cpp`'s own `BuildAngleDimensionGroup` for exactly
      // this kind of sharing (the DXF/DWG `DIMENSION_ANG3PT` reader, added
      // this same day, already reuses it the same way - see
      // `FileExchange.cpp`'s own `DxfImporter::Dimension` type==5 case),
      // from the dimension's own `CenterPoint`/`DefPoint1`/`DefPoint2` (the
      // vertex and a point out along each leg - the same shape a live
      // `DimAngle` command's own three picked points already are, not unit
      // direction vectors) rather than `ExtDir1`/`ExtDir2`, so the real
      // extension-point distances (not an arbitrary unit length) drive the
      // rebuilt arc's own radius exactly like a live dimension would.
      // `BuildAngleDimensionGeometry` always measures the <=180-degree angle
      // between the two legs (Dino8's own `DimAngle` has no way to draw the
      // complementary reflex angle) - a real reflex `ON_DimAngular` reads
      // back as the acute/obtuse complement instead of being declined
      // outright, an honestly narrower contract than the DXF/DWG reader's
      // own explicit def_pt-based reflex detection (that importer has a
      // third, independent `def_pt` field to check against; `ON_DimAngular`
      // has no equivalent third point here, only the two already-used
      // extension points, so there is nothing left to disambiguate with).
      const ON_Plane plane = dim->Plane();
      const ON_2dPoint v2 = dim->CenterPoint();
      const ON_2dPoint p1_2 = dim->DefPoint1();
      const ON_2dPoint p2_2 = dim->DefPoint2();
      const kernel::Point3d vertex = plane.PointAt(v2.x, v2.y);
      const kernel::Point3d p1 = plane.PointAt(p1_2.x, p1_2.y);
      const kernel::Point3d p2 = plane.PointAt(p2_2.x, p2_2.y);
      const ON_ModelComponentReference dimstyle_ref =
          model.ComponentFromId(ON_ModelComponent::Type::DimStyle, dim->DimensionStyleId());
      const ON_DimStyle* dimstyle = ON_DimStyle::Cast(dimstyle_ref.ModelComponent());
      double text_h = (dimstyle ? *dimstyle : ON_DimStyle::Default).TextHeight();
      if (text_h <= 0) {
        const AnnotationStyle& ast = doc.CurrentAnnotationStyle();
        text_h = ast.text_height > 0 ? ast.text_height : std::max(doc.Settings().grid_spacing * 2.0, 1e-6);
      }
      int layer_idx = 0;
      if (attr) {
        auto lm = layer_map.find(attr->m_layer_index);
        if (lm != layer_map.end()) layer_idx = lm->second;
      }
      std::vector<kernel::NurbsCurve> curves;
      DimGlyphSpec angle_text;
      std::map<std::string, std::string> tags;
      if (!BuildAngleDimensionGeometry(vertex, p1, p2, plane, text_h, curves, angle_text, tags) ||
          !AddDimensionGroupToDocLocal(doc, "DimAngle", layer_idx, curves, angle_text, tags)) {
        ++skipped;
      }
      continue;
    }
    if (const ON_Annotation* ann = ON_Annotation::Cast(g)) {
      // Previously entirely unhandled, like ON_Hatch/ON_InstanceRef above:
      // a real ON_Annotation (Rhino's Text/Dim*/Leader object kind) fell
      // through to "no made flag" below and counted as a skipped object no
      // matter its type, the one case this bullet's own top-level note
      // ("real ON_Annotation text/dimension objects are still silently
      // skipped on open") still named after the ON_Hatch pass above closed
      // every other remaining externally-authored-object gap in this
      // function. Scoped to plain ON::AnnotationType::Text only - the
      // same single case DXF's own TEXT writer/reader pair already covers
      // (see FileExchange.cpp's WriteDxfTextIfPlanarXY) - converted into
      // the identical baked glyph-curve group a live Text command itself
      // produces (TagGlyph's tags, reimplemented locally above as
      // PointTagLocal rather than via annotate_common.h - see its own
      // comment) so it round-trips, is selectable (FindText) and editable
      // (TextProperties) exactly like one made in-app. Linear/aligned,
      // radius/diameter and angular/3-point-angular dimensions
      // (ON_DimLinear/ON_DimRadial/ON_DimAngular) and ON_Leader are all
      // handled in their own branches above, ahead of this one - only the
      // remaining Ordinate/ArcLen/CenterMark annotation kinds still have no
      // reader at all (no Dino8 command produces one of those to rebuild
      // against), so they still count as skipped, same as before.
      if (ann->Type() != ON::AnnotationType::Text) { ++skipped; continue; }
      const std::string text = FromWide(ann->PlainText());
      const ON_Plane& plane = ann->Plane();
      const ON_ModelComponentReference dimstyle_ref =
          model.ComponentFromId(ON_ModelComponent::Type::DimStyle, ann->DimensionStyleId());
      const ON_DimStyle* dimstyle = ON_DimStyle::Cast(dimstyle_ref.ModelComponent());
      const double height = (dimstyle ? *dimstyle : ON_DimStyle::Default).TextHeight();
      std::vector<kernel::NurbsCurve> glyphs;
      std::string font_used;
      if (text.empty() || height <= 0 || !TextToCurves(text, height, plane, glyphs, font_used) || glyphs.empty()) {
        ++skipped;
        continue;
      }
      int layer_idx = 0;
      if (attr) {
        auto lm = layer_map.find(attr->m_layer_index);
        if (lm != layer_map.end()) layer_idx = lm->second;
      }
      std::vector<ObjectId> ids;
      for (kernel::NurbsCurve& glyph : glyphs) {
        SceneObject s = SceneObject::MakeCurve(glyph);
        s.layer_index = layer_idx;
        s.user_text["Annotation"] = "Text";
        s.user_text["Glyph"] = "1";
        s.user_text["Text"] = text;
        s.user_text["TextHeight"] = NumberTagLocal(height);
        s.user_text["TextOrigin"] = PointTagLocal(plane.origin.x, plane.origin.y, plane.origin.z);
        s.user_text["TextX"] = PointTagLocal(plane.xaxis.x, plane.xaxis.y, plane.xaxis.z);
        s.user_text["TextY"] = PointTagLocal(plane.yaxis.x, plane.yaxis.y, plane.yaxis.z);
        s.user_text["TextAlign"] = "Left";
        ids.push_back(doc.Add(std::move(s)));
      }
      doc.CreateGroup(ids, "Text");
      continue;
    }
    if (const ON_InstanceRef* iref = ON_InstanceRef::Cast(g)) {
      // Previously entirely unhandled, like ON_Hatch before its own pass
      // above: with no ON_InstanceRef case at all, a real Rhino block
      // instance just fell through to "no made flag" below and counted as
      // a skipped object, while its member geometry (see the
      // ON::idef_object skip above) was silently added as ordinary loose
      // objects instead - so a file with blocks lost the placements
      // entirely and kept only one un-transformed, ungrouped copy of each
      // block's geometry at the origin. Reconstructed via
      // BuildBlockDefinitionFromIdef (above), which converts the
      // referenced ON_InstanceDefinition's member geometry into a real
      // Dino8 BlockDefinition the first time any ON_InstanceRef names it -
      // every further reference to the same idef in this file reuses it,
      // same "build once, place many" relationship InstantiateBlockInDocument
      // already has for a Dino8-authored block. Each placement then becomes
      // a tagged Block/BlockInsert group exactly like one made with the
      // live Block/Insert commands, so it is just as selectable
      // (SelBlockInstance), explodable (ExplodeBlock) and re-insertable.
      std::string name;
      auto cached = idef_block_name.find(iref->m_instance_definition_uuid);
      if (cached != idef_block_name.end()) {
        name = cached->second;
      } else {
        const ON_ModelComponentReference idef_ref =
            model.ComponentFromId(ON_ModelComponent::Type::InstanceDefinition, iref->m_instance_definition_uuid);
        const ON_InstanceDefinition* idef = ON_InstanceDefinition::Cast(idef_ref.ModelComponent());
        if (!idef) { ++skipped; continue; }
        BlockDefinition def = BuildBlockDefinitionFromIdef(model, *idef, layer_map);
        // Name collision with a block already in the document (e.g. a
        // Dino8-authored block of the same name): keep the first, give
        // this one a suffix - same convention AddMaterial's own caller
        // uses above for a material-name collision.
        std::string base = def.name;
        for (int k = 2; doc.FindBlock(def.name); ++k) def.name = base + " " + std::to_string(k);
        name = def.name;
        idef_block_name[iref->m_instance_definition_uuid] = name;
        doc.Blocks().push_back(std::move(def));
      }
      const BlockDefinition* def = doc.FindBlock(name);
      if (!def || def->objects.empty()) { ++skipped; continue; }
      int layer_idx = -1;
      if (attr) {
        auto lm = layer_map.find(attr->m_layer_index);
        if (lm != layer_map.end()) layer_idx = lm->second;
      }
      std::vector<ObjectId> ids;
      for (const SceneObject& member : def->objects) {
        SceneObject copy = member;
        copy.id = kNoObject;
        copy.selected = false;
        copy.group_id = -1;
        copy.Transform(iref->m_xform);
        if (layer_idx >= 0) copy.layer_index = layer_idx;
        copy.user_text["Block"] = name;
        const kernel::Point3d insert = iref->m_xform * kernel::Point3d(0, 0, 0);
        copy.user_text["BlockInsert"] =
            std::to_string(insert.x) + "," + std::to_string(insert.y) + "," + std::to_string(insert.z);
        ids.push_back(doc.Add(std::move(copy)));
      }
      for (size_t i = 1; i < ids.size(); ++i) doc.SetProvenance(ids[i], ids[0], ProvenanceKind::BlockInstanceMember);
      doc.CreateGroup(ids, name);
      continue;
    }
    if (const ON_DetailView* dv = ON_DetailView::Cast(g)) {
      int layout = -1;
      if (attr) {
        auto pl = page_layout.find(attr->m_viewport_id);
        if (pl != page_layout.end()) layout = pl->second;
      }
      if (layout < 0 && !doc.Layouts().empty()) layout = 0;
      if (layout < 0) { ++skipped; continue; }
      Layout& L = doc.Layouts()[static_cast<size_t>(layout)];
      LayoutDetail d;
      d.name = FromWide(dv->m_view.m_name);
      if (d.name.empty() && attr) d.name = FromWide(attr->Name());
      if (d.name.empty()) d.name = "Detail " + std::to_string(L.details.size() + 1);
      d.camera = ViewportToCamera(dv->m_view.m_vp);
      ON_BoundingBox bb;
      if (dv->m_boundary.GetBoundingBox(bb)) { d.x = bb.m_min.x; d.y = bb.m_min.y; d.width = std::max(bb.m_max.x - bb.m_min.x, 1.0); d.height = std::max(bb.m_max.y - bb.m_min.y, 1.0); }
      d.scale = dv->m_page_per_model_ratio > 0 ? dv->m_page_per_model_ratio : 0;
      std::string hidden;
      if (attr) {
        ON_wString v;
        if (attr->GetUserString(L"Dino8.DetailLocked", v)) d.locked = FromWide(v) == "1";
        if (attr->GetUserString(L"Dino8.DetailMode", v)) d.display_mode = FromWide(v);
        if (attr->GetUserString(L"Dino8.DetailView", v)) d.standard_view = FromWide(v);
        if (attr->GetUserString(L"Dino8.HiddenObjects", v)) hidden = FromWide(v);
      }
      const ON_UUID detail_id = dv->m_view.m_vp.ViewportId();
      ONX_ModelComponentIterator lit(model, ON_ModelComponent::Type::Layer);
      for (const ON_ModelComponent* lc = lit.FirstComponent(); lc; lc = lit.NextComponent()) {
        const ON_Layer* layer = ON_Layer::Cast(lc);
        if (!layer || layer->PerViewportIsVisible(detail_id)) continue;
        auto lm = layer_map.find(layer->Index());
        if (lm != layer_map.end()) d.hidden_layers.push_back(lm->second);
      }
      L.details.push_back(d);
      pending_details.push_back({layout, L.details.size() - 1, hidden});
      continue;
    }
    if (const ON_Point* p = ON_Point::Cast(g)) {
      obj = SceneObject::MakePoint(p->point);
      made = true;
    } else if (const ON_Curve* cv = ON_Curve::Cast(g)) {
      ON_NurbsCurve nc;
      if (cv->GetNurbForm(nc) > 0) {
        kernel::NurbsCurve k;
        k.raw() = nc;
        obj = SceneObject::MakeCurve(k);
        made = true;
      }
    } else if (const ON_Brep* b = ON_Brep::Cast(g)) {
      kernel::Brep k;
      k.raw() = *b;
      obj = SceneObject::MakeBrep(k);
      made = true;
    } else if (const ON_Surface* s = ON_Surface::Cast(g)) {
      ON_NurbsSurface ns;
      if (s->GetNurbForm(ns) > 0) {
        kernel::NurbsSurface k;
        k.raw() = ns;
        obj = SceneObject::MakeSurface(k);
        made = true;
      }
    } else if (const ON_Mesh* m = ON_Mesh::Cast(g)) {
      // ONX_Model::Read() (ON_Mesh::ReadFaceArray() under it) copies each
      // face's vertex indices straight off the disk with no check against
      // the vertex count it read a moment earlier, and never runs
      // ON_Mesh::IsValid(). A corrupt or hostile file's mesh whose faces
      // index past its own vertex array therefore used to be copied into
      // the document verbatim, after which every mesh query (Area, Volume,
      // tessellation, booleans...) read memory past m_V - silently, no
      // diagnostic. Skip such a mesh, counted separately below so the
      // reader summary names the real reason. Range check only, not
      // ON_MeshFace::IsValid()'s stricter no-repeated-index rule: a
      // degenerate in-range face is something other exporters legitimately
      // write, and indexing it is safe (same contract as the kernel's own
      // Model::Load() and Mesh::LoadObj()).
      if (!MeshFaceIndicesInRange(*m)) {
        ++corrupt_meshes;
        continue;
      }
      kernel::Mesh k;
      k.raw() = *m;
      obj = SceneObject::MakeMesh(k);
      made = true;
    } else if (const ON_SubD* sd = ON_SubD::Cast(g)) {
      kernel::SubD k;
      k.raw() = *sd;
      obj = SceneObject::MakeSubD(k);
      made = true;
    } else if (const ON_Extrusion* ex = ON_Extrusion::Cast(g)) {
      ON_Brep* b = ex->BrepForm(nullptr);
      if (b) {
        kernel::Brep k;
        k.raw() = *b;
        delete b;
        obj = SceneObject::MakeBrep(k);
        made = true;
      }
    } else if (const ON_PointCloud* pc = ON_PointCloud::Cast(g)) {
      kernel::PointCloud k;
      k.raw() = *pc;
      obj = SceneObject::MakePointCloud(k);
      made = true;
    }
    if (!made) {
      ++skipped;
      continue;
    }
    if (attr) {
      obj.name = FromWide(attr->Name());
      auto lm = layer_map.find(attr->m_layer_index);
      if (lm != layer_map.end()) obj.layer_index = lm->second;
      if (attr->ColorSource() == ON::color_from_object) {
        obj.color_by_layer = false;
        obj.color = FromOnColor(attr->m_color);
      }
      obj.visible = attr->IsVisible();
      obj.locked = attr->Mode() == ON::locked_object;
      if (attr->MaterialSource() == ON::material_from_object) {
        auto mm = material_map.find(attr->m_material_index);
        if (mm != material_map.end()) obj.material_name = mm->second;
      }
      if (attr->LinetypeSource() == ON::linetype_from_object) {
        auto lt = linetype_by_index.find(attr->m_linetype_index);
        if (lt != linetype_by_index.end()) obj.linetype = lt->second;
      }
      // Attribute user strings.
      ON_ClassArray<ON_UserString> strings;
      attr->GetUserStrings(strings);
      for (int i = 0; i < strings.Count(); ++i) {
        const std::string key = FromWide(strings[i].m_key), val = FromWide(strings[i].m_string_value);
        if (key == "Dino8.Mapping") { ParseTextureMapping(val, obj.mapping); continue; }
        if (key == "Dino8.MappingScale") { obj.mapping_scale = static_cast<float>(std::atof(val.c_str())); continue; }
        if (key == "Dino8.GroupId") { file_group_id = std::atoi(val.c_str()); continue; }
        obj.user_text[key] = val;
      }
    }
    const ObjectId added = doc.Add(std::move(obj));
    if (attr) object_ids[attr->m_uuid] = static_cast<int>(added);
    if (file_group_id >= 0) restore_groups[file_group_id].push_back(added);
  }
  // Restore real Document::Group() membership (see Save3dm's "Dino8.GroupId"
  // note) via the same CreateGroup() path the Group command itself uses, so
  // group_id values can't collide with anything the document allocates
  // later, and UpdateDimensions' own use of group_id to tie a dimension's
  // baked objects together survives the round trip too.
  for (auto& [file_gid, ids] : restore_groups) doc.CreateGroup(ids);
  // Block definitions: pull the tagged member objects the loop above just
  // added as ordinary geometry back out into Document::Blocks() (see the
  // comment on EncodeBlocksMeta above).
  {
    ON_wString v;
    std::map<std::string, BlockDefinition> by_name;
    if (model.GetDocumentUserString(L"Dino8.BlocksMeta", v)) by_name = DecodeBlocksMeta(FromWide(v));
    std::map<std::string, std::vector<std::pair<int, SceneObject>>> members;
    std::vector<ObjectId> to_remove;
    for (const SceneObject& o : doc.Objects()) {
      auto it = o.user_text.find("Dino8.BlockDefOf");
      if (it == o.user_text.end()) continue;
      SceneObject c = o;
      c.id = kNoObject;
      c.selected = false;
      c.group_id = -1;
      c.user_text.erase("Dino8.BlockDefOf");
      int idx = static_cast<int>(members[it->second].size());
      auto ii = c.user_text.find("Dino8.BlockDefIndex");
      if (ii != c.user_text.end()) { idx = std::atoi(ii->second.c_str()); c.user_text.erase(ii); }
      members[it->second].push_back({idx, std::move(c)});
      to_remove.push_back(o.id);
    }
    for (auto& [name, list] : members) {
      std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      BlockDefinition& b = by_name[name];  // default-constructs one if meta was somehow missing
      b.name = name;
      b.objects.clear();
      b.objects.reserve(list.size());
      for (auto& [idx, obj] : list) b.objects.push_back(std::move(obj));
    }
    for (ObjectId id : to_remove) doc.Remove(id);
    for (auto& [name, b] : by_name) doc.Blocks().push_back(std::move(b));
  }
  // Cage-editing captive originals (see CageBindingMeta/CageBinding above
  // and the comment on CageBinding in doc/Document.h): the hidden geometry
  // components the loop above just added, tagged "Dino8.CaptiveOriginalOf"/
  // "Dino8.CaptiveCageOf", are pulled back out into Document::CageBindings()
  // the same way block-definition member objects were pulled out just
  // above - real Document objects, Save3dm, ObjectCount() and the object
  // list stay untouched by any of it once this is done.
  {
    ON_wString v;
    std::vector<CageBindingMeta> metas;
    if (model.GetDocumentUserString(L"Dino8.CageBindingsMeta", v)) metas = DecodeCageBindingsMeta(FromWide(v));
    std::map<std::string, CageBinding> by_cage_uuid;
    for (const CageBindingMeta& m : metas) {
      auto cage_it = object_ids.find(ON_UuidFromString(m.cage_uuid.c_str()));
      if (cage_it == object_ids.end()) continue;  // cage object didn't survive (e.g. excluded reference)
      CageBinding& b = by_cage_uuid[m.cage_uuid];
      b.cage = static_cast<ObjectId>(cage_it->second);
      b.nx = m.nx;
      b.ny = m.ny;
      b.nz = m.nz;
      b.lattice = m.lattice;
    }
    std::vector<ObjectId> to_remove;
    for (const SceneObject& o : doc.Objects()) {
      auto it = o.user_text.find("Dino8.CaptiveOriginalOf");
      if (it == o.user_text.end()) continue;
      to_remove.push_back(o.id);
      auto captive_it = object_ids.find(ON_UuidFromString(it->second.c_str()));
      auto cage_tag = o.user_text.find("Dino8.CaptiveCageOf");
      if (captive_it == object_ids.end() || cage_tag == o.user_text.end()) continue;
      auto binding_it = by_cage_uuid.find(cage_tag->second);
      if (binding_it == by_cage_uuid.end()) continue;
      Captive c;
      c.id = static_cast<ObjectId>(captive_it->second);
      auto local_tag = o.user_text.find("Dino8.CaptiveLocal");
      if (local_tag != o.user_text.end()) c.local = DecodeCaptiveLocal(local_tag->second);
      c.original = o;
      c.original.id = kNoObject;
      c.original.selected = false;
      c.original.user_text.erase("Dino8.CaptiveOriginalOf");
      c.original.user_text.erase("Dino8.CaptiveCageOf");
      c.original.user_text.erase("Dino8.CaptiveLocal");
      binding_it->second.captives.push_back(std::move(c));
    }
    for (ObjectId id : to_remove) doc.Remove(id);
    for (auto& [uuid, b] : by_cage_uuid) doc.CageBindings()[b.cage] = std::move(b);
  }
  // DimRefObj1/2/3 (see Save3dm) were rewritten to the referenced anchor
  // object's stable uuid at save time, since Open reassigns every object a
  // fresh numeric id - resolve them back to the *new* numeric id now that
  // every object has been added and object_ids is complete. A reference
  // whose anchor didn't survive (e.g. excluded as a reference object) is
  // dropped so UpdateDimensions falls back to its normal "skipped" handling
  // instead of misinterpreting a stale uuid string as a bogus object id.
  {
    static const char* kDimRefPairs[][2] = {{"DimRefObj1", "DimRefEnd1"}, {"DimRefObj2", "DimRefEnd2"}, {"DimRefObj3", "DimRefEnd3"}};
    for (SceneObject& o : doc.Objects()) {
      for (const char** pair : kDimRefPairs) {
        auto it = o.user_text.find(pair[0]);
        if (it == o.user_text.end()) continue;
        auto oi = object_ids.find(ON_UuidFromString(it->second.c_str()));
        if (oi != object_ids.end()) it->second = std::to_string(oi->second);
        else { o.user_text.erase(it); o.user_text.erase(pair[1]); }
      }
    }
  }
  // Per-detail hidden objects (saved as object uuids).
  for (const PendingDetail& pd : pending_details) {
    LayoutDetail& d = doc.Layouts()[static_cast<size_t>(pd.layout)].details[pd.detail];
    std::istringstream in(pd.hidden_objects);
    std::string tok;
    while (std::getline(in, tok, ';')) {
      if (tok.empty()) continue;
      auto oi = object_ids.find(ON_UuidFromString(tok.c_str()));
      if (oi != object_ids.end()) d.hidden_objects.push_back(static_cast<ObjectId>(oi->second));
    }
  }
  // Named construction planes.
  for (int i = 0; i < model.m_settings.m_named_cplanes.Count(); ++i) {
    const ON_3dmConstructionPlane& c = model.m_settings.m_named_cplanes[i];
    NamedCPlane n;
    n.name = FromWide(c.m_name);
    if (n.name.empty()) n.name = "CPlane " + std::to_string(i + 1);
    n.origin = c.m_plane.origin; n.x_axis = c.m_plane.xaxis; n.y_axis = c.m_plane.yaxis;
    doc.NamedCPlanes().push_back(n);
  }

  // Render lights live in their own table.
  {
    ONX_ModelComponentIterator lit(model, ON_ModelComponent::Type::RenderLight);
    for (const ON_ModelComponent* c = lit.FirstComponent(); c; c = lit.NextComponent()) {
      const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
      if (!mg) continue;
      if (const ON_Light* light = ON_Light::Cast(mg->Geometry(nullptr))) AddLightFromOn(doc, *light, mg->Attributes(nullptr));
    }
  }

  // Document notes, metadata and user text.
  doc.Notes() = FromWide(model.m_properties.m_Notes.m_notes);
  doc.Settings().author = FromWide(model.m_properties.m_RevisionHistory.m_sCreatedBy);
  {
    ON_wString v;
    if (model.GetDocumentUserString(L"Dino8.Title", v)) doc.Settings().title = FromWide(v);
    if (model.GetDocumentUserString(L"Dino8.Comments", v)) doc.Settings().comments = FromWide(v);
    if (model.GetDocumentUserString(L"Dino8.LinetypeScale", v)) doc.Settings().linetype_scale = std::max(1e-6, std::atof(FromWide(v).c_str()));
    if (model.GetDocumentUserString(L"Dino8.LinetypeDisplay", v)) doc.Settings().linetype_display = FromWide(v) != "0";
    if (model.GetDocumentUserString(L"Dino8.DimensionLayer", v)) doc.Settings().dimension_layer = FromWide(v);
    if (model.GetDocumentUserString(L"Dino8.CenterLayer", v)) doc.Settings().center_layer = FromWide(v);
    if (model.GetDocumentUserString(L"Dino8.AnnotationStyle", v)) doc.Settings().annotation_style = FromWide(v);
    if (model.GetDocumentUserString(L"Dino8.DwgExportScheme", v)) doc.Settings().dwg_export_scheme = FromWide(v);
    if (model.GetDocumentUserString(L"Dino8.HatchBase", v)) {
      double x = 0, y = 0, z = 0;
      if (std::sscanf(FromWide(v).c_str(), "%lf,%lf,%lf", &x, &y, &z) == 3) doc.Settings().hatch_base = kernel::Point3d(x, y, z);
    }
    if (model.GetDocumentUserString(L"Dino8.Animation", v)) doc.GetAnimation() = AnimationFromString(FromWide(v));
  }
  {
    ON_ClassArray<ON_UserString> strings;
    model.GetDocumentUserStrings(strings);
    std::map<std::string, std::string> render_strings;
    // Deferred, not applied inline: "Dino8.AnnotationStylePrecision.<name>"
    // (see below) can appear before its style's own "Dino8.AnnotationStyle.
    // <name>" entry in model.GetDocumentUserStrings()'s order, which is
    // whatever order OpenNURBS happened to store them in, not necessarily
    // write order - so every precision override is collected here and
    // applied only after the loop below has finished loading every style.
    std::map<std::string, int> pending_precision;
    for (int i = 0; i < strings.Count(); ++i) {
      const std::string key = FromWide(strings[i].m_key);
      if (key.compare(0, 13, "Dino8.Render.") == 0) { render_strings[key] = FromWide(strings[i].m_string_value); continue; }
      const std::string value = FromWide(strings[i].m_string_value);
      // Checked before the plain style_prefix below: without the distinct
      // "...StylePrecision." spelling (no "." right after "AnnotationStyle"
      // in style_prefix, so the prefixes cannot collide either way) this
      // would itself match style_prefix and get loaded as a bogus style
      // literally named "Precision.<name>".
      const std::string precision_prefix = "Dino8.AnnotationStylePrecision.";
      if (key.compare(0, precision_prefix.size(), precision_prefix) == 0) {
        pending_precision[key.substr(precision_prefix.size())] = std::atoi(value.c_str());
        continue;
      }
      const std::string style_prefix = "Dino8.AnnotationStyle.";
      if (key.compare(0, style_prefix.size(), style_prefix) == 0) {
        // New format (this window on): "height;arrow;precision;angprec;
        // suffix;extoffset;extext;placement;tolmode;tolvalue;tolupper;
        // tollower;font" - font last and un-delimited (takes the rest of
        // the string verbatim) since it's the one field that could itself
        // contain a literal ';' in principle. A file saved before this
        // window (old 2-semicolon "height;arrow;font" format) is still
        // read correctly by falling back below - the new fields simply
        // stay at their AnnotationStyle{} defaults for it.
        AnnotationStyle st;
        st.name = key.substr(style_prefix.size());
        std::vector<std::string> parts;
        size_t start = 0;
        bool new_format = true;
        for (int i = 0; i < 12; ++i) {
          const size_t semi = value.find(';', start);
          if (semi == std::string::npos) { new_format = false; break; }
          parts.push_back(value.substr(start, semi - start));
          start = semi + 1;
        }
        if (new_format) {
          st.text_height = std::atof(parts[0].c_str());
          st.arrow_size = std::atof(parts[1].c_str());
          st.precision = std::atoi(parts[2].c_str());
          st.angular_precision = std::atoi(parts[3].c_str());
          st.unit_suffix = parts[4];
          st.ext_offset = std::atof(parts[5].c_str());
          st.ext_extension = std::atof(parts[6].c_str());
          st.text_placement = parts[7].empty() ? "Above" : parts[7];
          st.tol_mode = parts[8];
          st.tol_value = parts[9];
          st.tol_upper = parts[10];
          st.tol_lower = parts[11];
          st.font = value.substr(start);
        } else {
          char font[256] = "";
          std::sscanf(value.c_str(), "%lf;%lf;%255[^\n]", &st.text_height, &st.arrow_size, font);
          st.font = font;
        }
        if (AnnotationStyle* existing = doc.FindAnnotationStyle(st.name)) *existing = st; else doc.AnnotationStyles().push_back(st);
        continue;
      }
      const std::string layer_state_prefix = "Dino8.LayerState.";
      if (key.compare(0, layer_state_prefix.size(), layer_state_prefix) == 0) {
        // "layerA,1,0|layerB,0,1|..." (name,visible,locked per layer, '|'-separated)
        LayerState ls;
        ls.name = key.substr(layer_state_prefix.size());
        std::istringstream entries(value);
        std::string entry;
        while (std::getline(entries, entry, '|')) {
          if (entry.empty()) continue;
          const std::size_t c1 = entry.rfind(',');
          if (c1 == std::string::npos || c1 == 0) continue;
          const std::size_t c0 = entry.rfind(',', c1 - 1);
          if (c0 == std::string::npos) continue;
          const std::string lname = entry.substr(0, c0);
          const bool vis = entry.substr(c0 + 1, c1 - c0 - 1) == "1";
          const bool locked = entry.substr(c1 + 1) == "1";
          ls.layers.push_back({lname, {vis, locked}});
        }
        if (LayerState* existing = doc.FindLayerState(ls.name)) *existing = ls; else doc.LayerStates().push_back(ls);
        continue;
      }
      const std::string plot_style_prefix = "Dino8.PlotStyle.";
      if (key.compare(0, plot_style_prefix.size(), plot_style_prefix) == 0) {
        // "has_color;r;g;b;width_mm;transparency" - the same flat two-column
        // shape Layer::has_plot_color/plot_color/print_width_mm already
        // have, just named and stored once per row instead of once per
        // layer, plus a third transparency column with no flat per-layer
        // equivalent. A file saved before transparency existed has only the
        // first 5 fields; sscanf's own return count tells the two apart, and
        // a missing field reads back as 0 (fully opaque), the same harmless
        // no-op default a fresh PlotStyle already has.
        PlotStyle st;
        st.name = key.substr(plot_style_prefix.size());
        int has_color = 0, r = 0, g = 0, b = 0;
        double width = 0, transparency = 0;
        const int n = std::sscanf(value.c_str(), "%d;%d;%d;%d;%lf;%lf", &has_color, &r, &g, &b, &width, &transparency);
        if (n >= 5) {
          st.has_color = has_color != 0;
          st.color = Color::FromBytes(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
          st.width_mm = width;
          st.transparency = n >= 6 ? std::clamp(transparency, 0.0, 100.0) : 0.0;
        }
        if (PlotStyle* existing = doc.FindPlotStyle(st.name)) *existing = st; else doc.PlotStyles().push_back(st);
        continue;
      }
      if (key.compare(0, 6, "Dino8.") == 0) continue;  // settings, handled above
      doc.UserText()[key] = value;
    }
    // Backward-compat fallback only: a style already carrying a precision
    // from the new unified "height;arrow;precision;..." format (below) is
    // left alone; this only fills in `precision` for a style whose
    // "Dino8.AnnotationStyle.<name>" entry was still the old 3-field
    // "height;arrow;font" line (pre-dates *both* precision mechanisms) but
    // which somehow also carries this separate key (e.g. a file written by
    // a build that used the short-lived separate-key format).
    for (const auto& [style_name, precision] : pending_precision) {
      if (AnnotationStyle* st = doc.FindAnnotationStyle(style_name)) {
        if (st->precision < 0) st->precision = precision;
      }
    }
    ReadRenderSettings(render_strings, doc.Render());
  }
  // Named views.
  for (int i = 0; i < model.m_settings.m_named_views.Count(); ++i) {
    const ON_3dmView& v = model.m_settings.m_named_views[i];
    NamedView nv;
    nv.name = FromWide(v.m_name);
    nv.camera.eye = v.m_vp.CameraLocation();
    nv.camera.target = v.m_vp.TargetPoint();
    nv.camera.up = v.m_vp.CameraUp();
    nv.camera.perspective = v.m_vp.IsPerspectiveProjection();
    double l, r, b, t;
    if (v.m_vp.GetFrustum(&l, &r, &b, &t) && !nv.camera.perspective) nv.camera.ortho_height = t - b;
    doc.NamedViews().push_back(nv);
  }
  // Units / tolerances.
  switch (model.m_settings.m_ModelUnitsAndTolerances.m_unit_system.UnitSystem()) {
    case ON::LengthUnitSystem::Inches: doc.Settings().unit_system = "Inches"; break;
    case ON::LengthUnitSystem::Feet: doc.Settings().unit_system = "Feet"; break;
    case ON::LengthUnitSystem::Centimeters: doc.Settings().unit_system = "Centimeters"; break;
    case ON::LengthUnitSystem::Meters: doc.Settings().unit_system = "Meters"; break;
    default: doc.Settings().unit_system = "Millimeters"; break;
  }
  if (model.m_settings.m_ModelUnitsAndTolerances.m_absolute_tolerance > 0) {
    doc.Settings().absolute_tolerance = model.m_settings.m_ModelUnitsAndTolerances.m_absolute_tolerance;
  }
  if (skipped > 0) error = std::to_string(skipped) + " unsupported object(s) were skipped";
  if (corrupt_meshes > 0) {
    if (!error.empty()) error += "; ";
    error += std::to_string(corrupt_meshes) + " corrupt mesh(es) skipped (face vertex indices outside the mesh's own vertex array)";
  }
  doc.ClearUndo();
  return true;
}

bool Save3dm(const Document& doc, const std::string& path, std::string& error, bool include_reference_objects) {
  ONX_Model model;
  model.m_sStartSectionComments = "Dino 8 - free NURBS modeler";
  model.m_properties.m_Application.m_application_name = L"Dino 8";
  model.m_properties.m_Application.m_application_URL = L"https://github.com/mlegere9789-collab/miniature-pancake";
  model.m_properties.m_Notes.m_notes = ON_wString(const_cast<Document&>(doc).Notes().c_str());
  model.m_properties.m_Notes.m_bVisible = !const_cast<Document&>(doc).Notes().empty();
  for (const auto& [k, v] : const_cast<Document&>(doc).UserText()) {
    model.SetDocumentUserString(ON_wString(k.c_str()), ON_wString(v.c_str()));
  }
  if (!doc.Settings().title.empty()) model.SetDocumentUserString(L"Dino8.Title", ON_wString(doc.Settings().title.c_str()));
  if (!doc.Settings().comments.empty()) model.SetDocumentUserString(L"Dino8.Comments", ON_wString(doc.Settings().comments.c_str()));
  WriteRenderSettings(model, doc.Render());
  {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%g", doc.Settings().linetype_scale);
    model.SetDocumentUserString(L"Dino8.LinetypeScale", ON_wString(buf));
    model.SetDocumentUserString(L"Dino8.LinetypeDisplay", doc.Settings().linetype_display ? L"1" : L"0");
    if (!doc.Settings().dimension_layer.empty()) model.SetDocumentUserString(L"Dino8.DimensionLayer", ON_wString(doc.Settings().dimension_layer.c_str()));
    if (!doc.Settings().center_layer.empty()) model.SetDocumentUserString(L"Dino8.CenterLayer", ON_wString(doc.Settings().center_layer.c_str()));
    model.SetDocumentUserString(L"Dino8.AnnotationStyle", ON_wString(doc.Settings().annotation_style.c_str()));
    model.SetDocumentUserString(L"Dino8.DwgExportScheme", ON_wString(doc.Settings().dwg_export_scheme.c_str()));
    const kernel::Point3d hb = doc.Settings().hatch_base;
    std::snprintf(buf, sizeof(buf), "%g,%g,%g", hb.x, hb.y, hb.z);
    model.SetDocumentUserString(L"Dino8.HatchBase", ON_wString(buf));
    for (const AnnotationStyle& st : doc.AnnotationStyles()) {
      // "height;arrow;precision;angprec;suffix;extoffset;extext;placement;
      // tolmode;tolvalue;tolupper;tollower;font" - see the reader's own
      // comment on this format and its old-file fallback. Precision now
      // lives in this one unified line (st.precision), so no separate
      // "Dino8.AnnotationStylePrecision.<name>" key is ever written any
      // more - the reader still accepts one on load, as a fallback, for a
      // file that predates this unified format.
      char style_buf[1024];
      std::snprintf(style_buf, sizeof(style_buf), "%g;%g;%d;%d;%s;%g;%g;%s;%s;%s;%s;%s;%s",
                    st.text_height, st.arrow_size, st.precision, st.angular_precision, st.unit_suffix.c_str(),
                    st.ext_offset, st.ext_extension, st.text_placement.c_str(), st.tol_mode.c_str(), st.tol_value.c_str(),
                    st.tol_upper.c_str(), st.tol_lower.c_str(), st.font.c_str());
      model.SetDocumentUserString(ON_wString(("Dino8.AnnotationStyle." + st.name).c_str()), ON_wString(style_buf));
    }
    for (const LayerState& ls : doc.LayerStates()) {
      std::string packed;
      for (const auto& [lname, vis_lock] : ls.layers) {
        if (!packed.empty()) packed += '|';
        packed += lname + "," + (vis_lock.first ? "1" : "0") + "," + (vis_lock.second ? "1" : "0");
      }
      model.SetDocumentUserString(ON_wString(("Dino8.LayerState." + ls.name).c_str()), ON_wString(packed.c_str()));
    }
    for (const PlotStyle& st : doc.PlotStyles()) {
      char style_buf[160];
      std::snprintf(style_buf, sizeof(style_buf), "%d;%d;%d;%d;%g;%g", st.has_color ? 1 : 0,
                    static_cast<int>(st.color.r * 255 + 0.5f), static_cast<int>(st.color.g * 255 + 0.5f),
                    static_cast<int>(st.color.b * 255 + 0.5f), st.width_mm, st.transparency);
      model.SetDocumentUserString(ON_wString(("Dino8.PlotStyle." + st.name).c_str()), ON_wString(style_buf));
    }
  }

  // Linetype table (name -> index in the file).
  std::map<std::string, int> file_linetype_index;
  for (const Linetype& lt : doc.Linetypes()) {
    ON_Linetype olt;
    olt.SetName(ON_wString(lt.name.c_str()));
    if (lt.pattern.empty()) {
      olt.AppendSegment(ON_LinetypeSegment(1.0, ON_LinetypeSegment::eSegType::stLine));
    } else {
      for (size_t i = 0; i < lt.pattern.size(); ++i) {
        olt.AppendSegment(ON_LinetypeSegment(lt.pattern[i], i % 2 == 0 ? ON_LinetypeSegment::eSegType::stLine : ON_LinetypeSegment::eSegType::stSpace));
      }
    }
    ON_ModelComponentReference ref = model.AddModelComponent(olt, true);
    if (const ON_ModelComponent* mc = ref.ModelComponent()) file_linetype_index[lt.name] = mc->Index();
  }
  auto linetype_index = [&](const std::string& name) {
    auto it = file_linetype_index.find(name);
    return it == file_linetype_index.end() ? -1 : it->second;
  };
  model.m_properties.m_RevisionHistory.m_sCreatedBy = ON_wString(doc.Settings().author.c_str());
  model.m_properties.m_RevisionHistory.m_sLastEditedBy = ON_wString(doc.Settings().author.c_str());
  model.m_properties.m_RevisionHistory.m_revision_count += 1;
  if (!doc.GetAnimation().frames.empty()) model.SetDocumentUserString(L"Dino8.Animation", ON_wString(AnimationToString(doc.GetAnimation()).c_str()));

  // Viewport ids: model views referenced by clipping planes, one page view
  // per layout and one viewport id per detail (used for per-detail layer
  // visibility and as the detail's own viewport id).
  std::map<std::string, ON_UUID> model_view_ids;
  for (const char* n : {"Top", "Perspective", "Front", "Right"}) { ON_UUID id; ON_CreateUuid(id); model_view_ids[n] = id; }
  for (const ClippingPlane& cp : doc.ClippingPlanes()) {
    for (const std::string& v : cp.viewports) if (!model_view_ids.count(v)) { ON_UUID id; ON_CreateUuid(id); model_view_ids[v] = id; }
  }
  for (const auto& [name, id] : model_view_ids) {
    ON_3dmView v;
    v.m_name = ON_wString(name.c_str());
    v.m_view_type = ON::model_view_type;
    v.m_vp.SetViewportId(id);
    if (name == "Top") { v.m_vp.SetProjection(ON::parallel_view); v.m_vp.SetCameraLocation(ON_3dPoint(0, 0, 100)); v.m_vp.SetCameraDirection(ON_3dVector(0, 0, -1)); v.m_vp.SetCameraUp(ON_3dVector(0, 1, 0)); }
    model.m_settings.m_views.Append(v);
  }
  std::vector<ON_UUID> page_ids;
  std::vector<std::vector<ON_UUID>> detail_ids;
  for (const Layout& L : doc.Layouts()) {
    ON_UUID id; ON_CreateUuid(id);
    page_ids.push_back(id);
    ON_3dmView v;
    v.m_name = ON_wString(L.name.c_str());
    v.m_view_type = ON::page_view_type;
    v.m_page_settings.m_width_mm = L.width_mm;
    v.m_page_settings.m_height_mm = L.height_mm;
    v.m_page_settings.m_page_number = static_cast<int>(page_ids.size());
    v.m_vp.SetViewportId(id);
    v.m_vp.SetProjection(ON::parallel_view);
    v.m_vp.SetCameraLocation(ON_3dPoint(L.width_mm / 2, L.height_mm / 2, 100));
    v.m_vp.SetCameraDirection(ON_3dVector(0, 0, -1));
    v.m_vp.SetCameraUp(ON_3dVector(0, 1, 0));
    model.m_settings.m_views.Append(v);
    detail_ids.emplace_back();
    for (size_t i = 0; i < L.details.size(); ++i) { ON_UUID d; ON_CreateUuid(d); detail_ids.back().push_back(d); }
  }

  // Units.
  ON::LengthUnitSystem us = ON::LengthUnitSystem::Millimeters;
  const std::string& u = doc.Settings().unit_system;
  if (u == "Inches") us = ON::LengthUnitSystem::Inches;
  else if (u == "Feet") us = ON::LengthUnitSystem::Feet;
  else if (u == "Centimeters") us = ON::LengthUnitSystem::Centimeters;
  else if (u == "Meters") us = ON::LengthUnitSystem::Meters;
  model.m_settings.m_ModelUnitsAndTolerances.m_unit_system = ON_UnitSystem(us);
  model.m_settings.m_ModelUnitsAndTolerances.m_absolute_tolerance = doc.Settings().absolute_tolerance;
  model.m_settings.m_ModelUnitsAndTolerances.m_angle_tolerance = doc.Settings().angle_tolerance_degrees * ON_PI / 180.0;

  // Materials.
  std::map<std::string, int> material_index;
  for (const Material& m : doc.Materials()) {
    ON_Material om;
    om.SetName(ON_wString(m.name.c_str()));
    om.SetDiffuse(ToOnColor(m.diffuse));
    om.SetSpecular(ToOnColor(m.specular));
    om.SetEmission(ToOnColor(m.emission));
    om.SetShine(std::clamp(static_cast<double>(m.gloss), 0.0, 1.0) * ON_Material::MaxShine);
    om.SetTransparency(std::clamp(static_cast<double>(m.transparency), 0.0, 1.0));
    om.SetReflectivity(std::clamp(static_cast<double>(m.reflectivity), 0.0, 1.0));
    if (!m.texture_path.empty()) {
      ON_Texture tx;
      tx.m_image_file_reference.SetFullPath(m.texture_path.c_str(), false);
      tx.m_type = ON_Texture::TYPE::bitmap_texture;
      om.AddTexture(tx);
    }
    om.SetUserString(L"Dino8.Mapping", ON_wString(TextureMappingName(m.mapping)));
    om.SetUserString(L"Dino8.MappingScale", ON_wString(std::to_string(m.mapping_scale).c_str()));
    ON_ModelComponentReference ref = model.AddModelComponent(om, true);
    if (const ON_Material* stored = ON_Material::Cast(ref.ModelComponent())) material_index[m.name] = stored->Index();
  }

  // Layers.
  std::vector<int> file_layer_index(doc.Layers().size(), 0);
  std::vector<ON_UUID> layer_ids(doc.Layers().size(), ON_nil_uuid);
  for (size_t i = 0; i < doc.Layers().size(); ++i) {
    const Layer& L = doc.Layers()[i];
    ON_Layer layer;
    layer.SetName(ON_wString(L.name.c_str()));
    layer.SetColor(ToOnColor(L.color));
    layer.SetVisible(L.visible);
    layer.SetLocked(L.locked);
    if (L.parent >= 0 && static_cast<size_t>(L.parent) < i) layer.SetParentLayerId(layer_ids[static_cast<size_t>(L.parent)]);
    ON_CreateUuid(layer_ids[i]);
    layer.SetId(layer_ids[i]);
    file_layer_index[i] = model.AddLayer(ON_wString(L.name.c_str()), ToOnColor(L.color));
    // AddLayer created a fresh layer; update its properties in place.
    ON_ModelComponentReference ref = model.LayerFromIndex(file_layer_index[i]);
    if (ON_Layer* stored = const_cast<ON_Layer*>(ON_Layer::Cast(ref.ModelComponent()))) {
      stored->SetVisible(L.visible);
      stored->SetLocked(L.locked);
      if (!L.material.empty() && material_index.count(L.material)) stored->SetRenderMaterialIndex(material_index[L.material]);
      if (linetype_index(L.linetype) >= 0) stored->SetLinetypeIndex(linetype_index(L.linetype));
      stored->SetPlotWeight(L.print_width_mm);  // real .3dm field, same 0/>0/<0 convention as Layer::print_width_mm
      if (L.has_plot_color) stored->SetPlotColor(ToOnColor(L.plot_color));  // real .3dm field; unset (ON_UNSET_COLOR) is ON_Layer's own default
      if (!L.plot_style.empty()) stored->SetUserString(L"Dino8.PlotStyle", ON_wString(L.plot_style.c_str()));  // no native field for this - see the Load3dm read above
      for (size_t li = 0; li < doc.Layouts().size(); ++li) {
        const Layout& lay = doc.Layouts()[li];
        for (size_t di = 0; di < lay.details.size(); ++di) {
          const std::vector<int>& hidden = lay.details[di].hidden_layers;
          if (std::find(hidden.begin(), hidden.end(), static_cast<int>(i)) != hidden.end()) stored->SetPerViewportVisible(detail_ids[li][di], false);
        }
      }
      if (L.parent >= 0 && static_cast<size_t>(L.parent) < i) {
        ON_ModelComponentReference pref = model.LayerFromIndex(file_layer_index[static_cast<size_t>(L.parent)]);
        if (const ON_Layer* pl = ON_Layer::Cast(pref.ModelComponent())) stored->SetParentLayerId(pl->Id());
      }
    }
  }
  if (!doc.Layers().empty()) {
    ON_ModelComponentReference cur = model.LayerFromIndex(file_layer_index[static_cast<size_t>(std::max(0, doc.CurrentLayer()))]);
    if (const ON_Layer* cl = ON_Layer::Cast(cur.ModelComponent())) model.m_settings.SetCurrentLayerId(cl->Id());
  }

  // Named views.
  for (const NamedView& nv : const_cast<Document&>(doc).NamedViews()) {
    ON_3dmView v;
    v.m_name = ON_wString(nv.name.c_str());
    v.m_vp.SetProjection(nv.camera.perspective ? ON::perspective_view : ON::parallel_view);
    v.m_vp.SetCameraLocation(nv.camera.eye);
    v.m_vp.SetCameraDirection(nv.camera.target - nv.camera.eye);
    v.m_vp.SetCameraUp(nv.camera.up);
    v.m_vp.SetTargetPoint(nv.camera.target);
    model.m_settings.m_named_views.Append(v);
  }

  // Named construction planes.
  for (const NamedCPlane& n : doc.NamedCPlanes()) {
    ON_3dmConstructionPlane c;
    c.m_plane = ON_Plane(n.origin, n.x_axis, n.y_axis);
    c.m_name = ON_wString(n.name.c_str());
    model.m_settings.m_named_cplanes.Append(c);
  }

  int written = 0;
  std::map<ObjectId, ON_UUID> object_uuids;
  // Pre-generate every written object's uuid before writing any attributes,
  // so DimRefObj1/2/3 (see below) can always resolve the *referenced*
  // object's uuid regardless of which one is written first.
  for (const SceneObject& o : doc.Objects()) {
    if (!include_reference_objects && o.user_text.count("Dino8.Reference")) continue;
    ON_UUID uuid;
    ON_CreateUuid(uuid);
    object_uuids[o.id] = uuid;
  }
  for (const SceneObject& o : doc.Objects()) {
    if (!include_reference_objects && o.user_text.count("Dino8.Reference")) continue;
    ON_3dmObjectAttributes attr;
    attr.m_uuid = object_uuids[o.id];
    attr.SetName(ON_wString(o.name.c_str()), true);
    attr.m_layer_index = file_layer_index[static_cast<size_t>(std::clamp(o.layer_index, 0, static_cast<int>(doc.Layers().size()) - 1))];
    if (!o.color_by_layer) {
      attr.SetColorSource(ON::color_from_object);
      attr.m_color = ToOnColor(o.color);
    }
    attr.SetVisible(o.visible);
    attr.SetMode(o.locked ? ON::locked_object : ON::normal_object);
    if (o.linetype != "ByLayer" && linetype_index(o.linetype) >= 0) {
      attr.SetLinetypeSource(ON::linetype_from_object);
      attr.m_linetype_index = linetype_index(o.linetype);
    }
    // DimRefObj1/2/3 (see cmd_annotate.cpp) carry the *referenced* anchor
    // object's live numeric ObjectId, which Open reassigns on every load -
    // write the anchor's stable uuid instead so UpdateDimensions can still
    // resolve the same logical anchor after a .3dm round trip (fixed up
    // back into a numeric id again on read, below).
    static const char* kDimRefKeys[] = {"DimRefObj1", "DimRefObj2", "DimRefObj3"};
    for (const auto& [k, v] : o.user_text) {
      bool remapped = false;
      for (const char* rk : kDimRefKeys) {
        if (k != rk) continue;
        const ObjectId ref = static_cast<ObjectId>(std::strtoull(v.c_str(), nullptr, 10));
        auto ru = object_uuids.find(ref);
        if (ru != object_uuids.end()) attr.SetUserString(ON_wString(k.c_str()), ON_wString(UuidString(ru->second).c_str()));
        remapped = true;
        break;
      }
      if (!remapped) attr.SetUserString(ON_wString(k.c_str()), ON_wString(v.c_str()));
    }
    // group_id (Document::CreateGroup/Ungroup) isn't part of any opennurbs
    // table this app populates - persist it as a plain user string so real
    // "Group" membership and UpdateDimensions' own use of group_id to tie a
    // dimension's line/arrow/text objects together both survive Save/Open
    // (restored via Document::CreateGroup on read, below).
    if (o.group_id >= 0) attr.SetUserString(L"Dino8.GroupId", ON_wString(std::to_string(o.group_id).c_str()));
    if (!o.material_name.empty() && material_index.count(o.material_name)) {
      attr.m_material_index = material_index[o.material_name];
      attr.SetMaterialSource(ON::material_from_object);
    }
    if (o.mapping != TextureMapping::Default) {
      attr.SetUserString(L"Dino8.Mapping", ON_wString(TextureMappingName(o.mapping)));
      attr.SetUserString(L"Dino8.MappingScale", ON_wString(std::to_string(o.mapping_scale).c_str()));
    }

    ON_Geometry* g = nullptr;
    switch (o.kind) {
      case ObjectKind::Point: g = new ON_Point(o.point); break;
      case ObjectKind::Curve: if (o.curve) g = new ON_NurbsCurve(o.curve->raw()); break;
      case ObjectKind::Surface: if (o.surface) g = new ON_NurbsSurface(o.surface->raw()); break;
      case ObjectKind::Brep: if (o.brep) g = new ON_Brep(o.brep->raw()); break;
      case ObjectKind::Mesh: if (o.mesh) g = new ON_Mesh(o.mesh->raw()); break;
      case ObjectKind::SubD: if (o.subd) g = new ON_SubD(o.subd->raw()); break;
      case ObjectKind::PointCloud: if (o.point_cloud) g = new ON_PointCloud(o.point_cloud->raw()); break;
    }
    if (!g) continue;
    model.AddModelGeometryComponent(g, &attr);
    ++written;
  }

  // Block definitions (see the comment on EncodeBlocksMeta above): metadata
  // as one small JSON blob, each definition's objects as ordinary tagged
  // geometry components reusing the same write code as the main loop above.
  model.SetDocumentUserString(L"Dino8.BlocksMeta", ON_wString(EncodeBlocksMeta(doc.Blocks()).c_str()));
  for (const BlockDefinition& def : doc.Blocks()) {
    for (size_t bi = 0; bi < def.objects.size(); ++bi) {
      const SceneObject& o = def.objects[bi];
      ON_3dmObjectAttributes attr;
      ON_CreateUuid(attr.m_uuid);
      attr.SetName(ON_wString(o.name.c_str()), true);
      attr.SetVisible(o.visible);
      for (const auto& [k, v] : o.user_text) attr.SetUserString(ON_wString(k.c_str()), ON_wString(v.c_str()));
      attr.SetUserString(L"Dino8.BlockDefOf", ON_wString(def.name.c_str()));
      attr.SetUserString(L"Dino8.BlockDefIndex", ON_wString(std::to_string(bi).c_str()));
      ON_Geometry* g = nullptr;
      switch (o.kind) {
        case ObjectKind::Point: g = new ON_Point(o.point); break;
        case ObjectKind::Curve: if (o.curve) g = new ON_NurbsCurve(o.curve->raw()); break;
        case ObjectKind::Surface: if (o.surface) g = new ON_NurbsSurface(o.surface->raw()); break;
        case ObjectKind::Brep: if (o.brep) g = new ON_Brep(o.brep->raw()); break;
        case ObjectKind::Mesh: if (o.mesh) g = new ON_Mesh(o.mesh->raw()); break;
        case ObjectKind::SubD: if (o.subd) g = new ON_SubD(o.subd->raw()); break;
        case ObjectKind::PointCloud: if (o.point_cloud) g = new ON_PointCloud(o.point_cloud->raw()); break;
      }
      if (!g) continue;
      model.AddModelGeometryComponent(g, &attr);
    }
  }

  // Cage-editing captive originals (see the CageBindingMeta/CageBinding
  // comments above and in doc/Document.h): the same "real geometry
  // component + small JSON sidecar" pattern as block definitions just
  // above, so ExtractOriginalCaptives can still restore an original after
  // a Save/Open round trip. A binding whose cage (or a given captive) isn't
  // itself being written (e.g. excluded reference objects) is silently
  // skipped for that entry - same tolerance as the DimRefObj1/2/3 handling
  // above.
  {
    std::vector<CageBindingMeta> metas;
    for (const auto& [cage_id, b] : doc.CageBindings()) {
      auto cage_uuid = object_uuids.find(b.cage);
      if (cage_uuid == object_uuids.end()) continue;
      CageBindingMeta m;
      m.cage_uuid = UuidString(cage_uuid->second);
      m.nx = b.nx;
      m.ny = b.ny;
      m.nz = b.nz;
      m.lattice = b.lattice;
      metas.push_back(m);
      for (const Captive& c : b.captives) {
        auto captive_uuid = object_uuids.find(c.id);
        if (captive_uuid == object_uuids.end()) continue;
        const SceneObject& o = c.original;
        ON_3dmObjectAttributes attr;
        ON_CreateUuid(attr.m_uuid);
        attr.SetName(ON_wString(o.name.c_str()), true);
        // The captured original's own visibility (not "false" - unlike the
        // block-definition write loop just above, which also does this,
        // this hidden component's attribute is read straight back into
        // Captive::original's own `visible` field on Load, see below, and
        // ExtractOriginalCaptives later duplicates that field verbatim onto
        // the restored object; forcing it false here would make every
        // Save/Open-restored original invisible even when it was visible
        // when captured).
        attr.SetVisible(o.visible);
        for (const auto& [k, v] : o.user_text) attr.SetUserString(ON_wString(k.c_str()), ON_wString(v.c_str()));
        attr.SetUserString(L"Dino8.CaptiveOriginalOf", ON_wString(UuidString(captive_uuid->second).c_str()));
        attr.SetUserString(L"Dino8.CaptiveCageOf", ON_wString(m.cage_uuid.c_str()));
        std::ostringstream local;
        local << std::setprecision(17);
        for (size_t i = 0; i < c.local.size(); ++i) {
          if (i) local << ";";
          local << c.local[i].x << "," << c.local[i].y << "," << c.local[i].z;
        }
        attr.SetUserString(L"Dino8.CaptiveLocal", ON_wString(local.str().c_str()));
        ON_Geometry* g = nullptr;
        switch (o.kind) {
          case ObjectKind::Point: g = new ON_Point(o.point); break;
          case ObjectKind::Curve: if (o.curve) g = new ON_NurbsCurve(o.curve->raw()); break;
          case ObjectKind::Surface: if (o.surface) g = new ON_NurbsSurface(o.surface->raw()); break;
          case ObjectKind::Brep: if (o.brep) g = new ON_Brep(o.brep->raw()); break;
          case ObjectKind::Mesh: if (o.mesh) g = new ON_Mesh(o.mesh->raw()); break;
          case ObjectKind::SubD: if (o.subd) g = new ON_SubD(o.subd->raw()); break;
          case ObjectKind::PointCloud: if (o.point_cloud) g = new ON_PointCloud(o.point_cloud->raw()); break;
        }
        if (!g) continue;
        model.AddModelGeometryComponent(g, &attr);
      }
    }
    model.SetDocumentUserString(L"Dino8.CageBindingsMeta", ON_wString(EncodeCageBindingsMeta(metas).c_str()));
  }

  // Lights are geometry components in the 3dm.
  for (const Light& L : doc.Lights()) {
    ON_Light* light = new ON_Light();
    light->SetLightName(L.name.c_str());
    switch (L.type) {
      case LightType::Point: light->SetStyle(ON::world_point_light); light->SetLocation(L.position); break;
      case LightType::Spot:
        light->SetStyle(ON::world_spot_light);
        light->SetLocation(L.position);
        light->SetDirection(L.direction * (L.length > 0 ? L.length : 10.0));
        light->SetSpotAngleDegrees(L.spot_angle);
        break;
      case LightType::Directional: light->SetStyle(ON::world_directional_light); light->SetLocation(L.position); light->SetDirection(L.direction); break;
      case LightType::Rectangular: {
        light->SetStyle(ON::world_rectangular_light);
        kernel::Vector3d y = ON_CrossProduct(L.direction, L.x_axis);
        y.Unitize();
        light->SetLocation(L.position - L.x_axis * (L.length / 2) - y * (L.width / 2));
        light->SetDirection(L.direction);
        light->SetLength(L.x_axis * L.length);
        light->SetWidth(y * L.width);
        break;
      }
      case LightType::Linear:
        light->SetStyle(ON::world_linear_light);
        light->SetLocation(L.position);
        light->SetDirection(L.direction);
        light->SetLength(L.x_axis * L.length);
        break;
    }
    light->SetDiffuse(ToOnColor(L.color));
    light->SetIntensity(L.intensity);
    light->Enable(L.enabled);
    ON_3dmObjectAttributes attr;
    ON_CreateUuid(attr.m_uuid);
    attr.SetName(ON_wString(L.name.c_str()), true);
    model.AddModelGeometryComponent(light, &attr);
  }
  // Clipping planes.
  for (const ClippingPlane& cp : doc.ClippingPlanes()) {
    ON_Plane plane(cp.origin, cp.x_axis, cp.y_axis);
    ON_ClippingPlaneSurface* cps = new ON_ClippingPlaneSurface(plane);
    cps->SetExtents(0, ON_Interval(-cp.width / 2, cp.width / 2), true);
    cps->SetExtents(1, ON_Interval(-cp.height / 2, cp.height / 2), true);
    cps->m_clipping_plane.m_plane = plane;
    cps->m_clipping_plane.m_bEnabled = cp.enabled;
    ON_CreateUuid(cps->m_clipping_plane.m_plane_id);
    for (const std::string& v : cp.viewports) {
      auto id = model_view_ids.find(v);
      if (id != model_view_ids.end()) cps->m_clipping_plane.m_viewport_ids.AddUuid(id->second);
    }
    ON_3dmObjectAttributes attr;
    ON_CreateUuid(attr.m_uuid);
    attr.SetName(ON_wString(cp.name.c_str()), true);
    attr.m_layer_index = file_layer_index.empty() ? 0 : file_layer_index[0];
    model.AddModelGeometryComponent(cps, &attr);
  }

  // Layout details (page views were written with the settings above).
  for (size_t li = 0; li < doc.Layouts().size(); ++li) {
    const Layout& L = doc.Layouts()[li];
    for (size_t di = 0; di < L.details.size(); ++di) {
      const LayoutDetail& d = L.details[di];
      ON_DetailView* dv = new ON_DetailView();
      dv->m_view.m_name = ON_wString(d.name.c_str());
      dv->m_view.m_view_type = ON::nested_view_type;
      CameraToViewport(d.camera, dv->m_view.m_vp);
      dv->m_view.m_vp.SetViewportId(detail_ids[li][di]);
      dv->m_page_per_model_ratio = d.scale;
      ON_Polyline rect;
      rect.Append(ON_3dPoint(d.x, d.y, 0)); rect.Append(ON_3dPoint(d.x + d.width, d.y, 0));
      rect.Append(ON_3dPoint(d.x + d.width, d.y + d.height, 0)); rect.Append(ON_3dPoint(d.x, d.y + d.height, 0)); rect.Append(ON_3dPoint(d.x, d.y, 0));
      ON_PolylineCurve pc(rect);
      pc.GetNurbForm(dv->m_boundary);
      ON_3dmObjectAttributes attr;
      ON_CreateUuid(attr.m_uuid);
      attr.SetName(ON_wString(d.name.c_str()), true);
      attr.m_viewport_id = page_ids[li];
      attr.m_layer_index = file_layer_index.empty() ? 0 : file_layer_index[0];
      attr.SetUserString(L"Dino8.DetailLocked", d.locked ? L"1" : L"0");
      attr.SetUserString(L"Dino8.DetailMode", ON_wString(d.display_mode.c_str()));
      attr.SetUserString(L"Dino8.DetailView", ON_wString(d.standard_view.c_str()));
      std::string hidden;
      for (ObjectId id : d.hidden_objects) {
        auto u = object_uuids.find(id);
        if (u != object_uuids.end()) hidden += UuidString(u->second) + ";";
      }
      if (!hidden.empty()) attr.SetUserString(L"Dino8.HiddenObjects", ON_wString(hidden.c_str()));
      model.AddModelGeometryComponent(dv, &attr);
    }
  }

  ON_TextLog log;
  if (!model.Write(path.c_str(), 0, &log)) {
    error = "OpenNURBS could not write " + path;
    return false;
  }
  (void)written;
  return true;
}

// ---------------------------------------------------------------------------
// OBJ / STL
// ---------------------------------------------------------------------------

namespace {

// App-level multi-object .obj support: kernel::Mesh::LoadObj/SaveObj (used
// below for .stl and as the single-mesh fallback) only ever produce/consume
// one merged, welded ON_Mesh - by design, the kernel has no notion of
// "document objects" at all. The functions in this block sit above that,
// splitting/reassembling on .obj's own "o"/"g"/"usemtl" directives so
// ImportObjMulti/ExportObjMulti below can round-trip real per-object
// identity and a sidecar .mtl material file, closing the app_interop OBJ
// gap ("the importer loads the whole file as one mesh with no per-group/
// per-object split and no .mtl; the exporter... merg[es] everything into a
// single welded mesh, losing object identity and writing no materials").

bool ParseObjIndexField(const std::string& field, int& value) {
  if (field.empty()) return false;
  size_t consumed = 0;
  int parsed = 0;
  try {
    parsed = std::stoi(field, &consumed);
  } catch (const std::exception&) {
    return false;
  }
  if (consumed != field.size() || parsed == 0) return false;
  value = parsed;
  return true;
}

// Same token grammar as the kernel's own ParseObjFaceIndex (mesh.cpp): the
// plain "3" form, "3/4" (vertex/texture), and "3/4/5"/"3//5"
// (vertex[/texture]/normal, normal parsed away and discarded - this
// importer, like the kernel's, has no per-face-corner normal storage).
bool ParseObjFaceToken(const std::string& token, int& v_index, int& vt_index, bool& has_vt) {
  has_vt = false;
  const size_t first_slash = token.find('/');
  const std::string first = (first_slash == std::string::npos) ? token : token.substr(0, first_slash);
  if (!ParseObjIndexField(first, v_index)) return false;
  if (first_slash == std::string::npos) return true;
  const size_t second_slash = token.find('/', first_slash + 1);
  const std::string second =
      (second_slash == std::string::npos) ? token.substr(first_slash + 1) : token.substr(first_slash + 1, second_slash - first_slash - 1);
  if (second.empty()) return true;  // "v//vn" form
  if (!ParseObjIndexField(second, vt_index)) return false;
  has_vt = true;
  return true;
}

struct ObjCorner {
  int v = 0;
  int vt = -1;
};
struct ObjFace {
  std::vector<ObjCorner> corners;
};
struct ObjGroup {
  std::string name;
  std::string material;
  std::vector<ObjFace> faces;
};

// Builds one group's own compact ON_Mesh: each referenced global vertex
// gets a local index the first time this group sees it, so a per-object
// group carries only the vertices it actually uses, not the whole file's
// table. UV coverage uses each local vertex's first-seen `vt` (the same
// "no correct answer" simplification kernel::Mesh::LoadObj's own doc
// comment discloses for an ambiguous corner) - a real UV seam within one
// object is not split into duplicate vertices here, unlike the kernel's
// single-mesh loader; every vertex needs a `vt` for the group to carry
// texture coordinates at all (ON_Mesh's own all-or-nothing convention).
bool BuildMeshFromObjGroup(const std::vector<ON_3fPoint>& positions, const std::vector<kernel::Point2d>& texcoords,
                            const ObjGroup& group, kernel::Mesh& out_mesh) {
  std::map<int, int> local_of_global;
  std::vector<int> vt_of_local;
  ON_Mesh raw;
  auto local_for = [&](const ObjCorner& c) -> int {
    auto it = local_of_global.find(c.v);
    if (it != local_of_global.end()) return it->second;
    const int li = raw.m_V.Count();
    local_of_global[c.v] = li;
    vt_of_local.push_back(c.vt);
    raw.m_V.Append(positions[static_cast<size_t>(c.v)]);
    return li;
  };
  for (const ObjFace& f : group.faces) {
    std::vector<int> local_indices;
    local_indices.reserve(f.corners.size());
    for (const ObjCorner& c : f.corners) local_indices.push_back(local_for(c));
    if (local_indices.size() <= 4) {
      ON_MeshFace face;
      face.vi[0] = local_indices[0];
      face.vi[1] = local_indices[1];
      face.vi[2] = local_indices[2];
      face.vi[3] = local_indices.size() == 4 ? local_indices[3] : local_indices[2];
      raw.m_F.Append(face);
    } else {
      // n-gon (n > 4): fan-triangulate, same accommodation kernel::Mesh::LoadObj makes.
      for (size_t i = 1; i + 1 < local_indices.size(); ++i) {
        ON_MeshFace face;
        face.vi[0] = local_indices[0];
        face.vi[1] = local_indices[static_cast<int>(i)];
        face.vi[2] = local_indices[static_cast<int>(i) + 1];
        face.vi[3] = face.vi[2];
        raw.m_F.Append(face);
      }
    }
  }
  if (raw.m_F.Count() == 0) return false;
  out_mesh.raw() = raw;
  bool has_uv = !vt_of_local.empty();
  for (int vt : vt_of_local) {
    if (vt < 0) { has_uv = false; break; }
  }
  if (has_uv) {
    std::vector<kernel::Point2d> uvs(vt_of_local.size());
    for (size_t i = 0; i < vt_of_local.size(); ++i) uvs[i] = texcoords[static_cast<size_t>(vt_of_local[i])];
    out_mesh.SetTextureCoordinates(uvs);
  }
  return true;
}

// Parses a Wavefront .mtl file's `newmtl`/`Kd` pairs into name -> diffuse
// color. Every other statement (Ka/Ks/Ns/map_Kd/illum/...) is silently
// skipped, same "round-trips geometry (and now per-object color), not a
// full material model" scope as the rest of this OBJ support.
std::map<std::string, Color> ParseObjMtl(const std::string& mtl_path) {
  std::map<std::string, Color> out;
  std::ifstream in(mtl_path);
  if (!in) return out;
  std::string line, current;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "newmtl") {
      ss >> current;
    } else if (tag == "Kd" && !current.empty()) {
      double r, g, b;
      if (ss >> r >> g >> b) {
        out[current] = Color{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), 1.f};
      }
    }
  }
  return out;
}

std::string ObjSanitizeName(std::string name) {
  for (char& c : name) {
    if (std::isspace(static_cast<unsigned char>(c)) || c == '/' || c == '\\') c = '_';
  }
  return name;
}

std::string ObjTrim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

bool ImportObjMulti(Document& doc, const std::string& path, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "Could not open " + path;
    return false;
  }
  std::vector<ON_3fPoint> positions;
  std::vector<kernel::Point2d> texcoords;
  std::vector<ObjGroup> groups;
  std::string mtllib;
  groups.push_back(ObjGroup{});  // implicit group for any faces before the first "o"/"g"

  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "v") {
      double x, y, z;
      if (!(ss >> x >> y >> z)) { error = "Malformed .obj: bad v line in " + path; return false; }
      positions.push_back(ON_3fPoint(x, y, z));
    } else if (tag == "vt") {
      double u, v;
      if (!(ss >> u >> v)) { error = "Malformed .obj: bad vt line in " + path; return false; }
      texcoords.push_back(kernel::Point2d(u, v));
    } else if (tag == "mtllib") {
      ss >> mtllib;
    } else if (tag == "usemtl") {
      std::string m;
      ss >> m;
      groups.back().material = m;
    } else if (tag == "o" || tag == "g") {
      std::string name;
      std::getline(ss, name);
      groups.push_back(ObjGroup{});
      groups.back().name = ObjTrim(name);
    } else if (tag == "f") {
      ObjFace face;
      std::string token;
      while (ss >> token) {
        int v_index = 0, vt_index = 0;
        bool has_vt = false;
        if (!ParseObjFaceToken(token, v_index, vt_index, has_vt)) { error = "Malformed .obj: bad f line in " + path; return false; }
        if (v_index < 0) v_index = static_cast<int>(positions.size()) + v_index + 1;
        if (v_index < 1 || v_index > static_cast<int>(positions.size())) {
          error = "Malformed .obj: vertex index out of range in " + path;
          return false;
        }
        ObjCorner corner;
        corner.v = v_index - 1;
        if (has_vt) {
          if (vt_index < 0) vt_index = static_cast<int>(texcoords.size()) + vt_index + 1;
          if (vt_index < 1 || vt_index > static_cast<int>(texcoords.size())) {
            error = "Malformed .obj: texture index out of range in " + path;
            return false;
          }
          corner.vt = vt_index - 1;
        }
        face.corners.push_back(corner);
      }
      if (face.corners.size() < 3) { error = "Malformed .obj: face with fewer than 3 vertices in " + path; return false; }
      groups.back().faces.push_back(std::move(face));
    }
    // Every other tag (comments, vn, s, ...) is silently skipped, same as kernel::Mesh::LoadObj.
  }

  std::map<std::string, Color> materials;
  if (!mtllib.empty()) {
    const std::filesystem::path mtl_path = std::filesystem::path(path).parent_path() / mtllib;
    materials = ParseObjMtl(mtl_path.string());
  }

  int nonempty = 0;
  for (const ObjGroup& g : groups) if (!g.faces.empty()) ++nonempty;
  if (nonempty == 0) {
    error = "Could not read a mesh from " + path;
    return false;
  }
  const std::string stem = std::filesystem::path(path).stem().string();
  int index = 0;
  for (const ObjGroup& g : groups) {
    if (g.faces.empty()) continue;
    kernel::Mesh mesh;
    if (!BuildMeshFromObjGroup(positions, texcoords, g, mesh)) continue;
    ++index;
    SceneObject o = SceneObject::MakeMesh(mesh);
    o.name = !g.name.empty() ? g.name : (nonempty == 1 ? stem : "Group " + std::to_string(index));
    if (!g.material.empty()) {
      auto mit = materials.find(g.material);
      if (mit != materials.end()) {
        o.color = mit->second;
        o.color_by_layer = false;
      }
    }
    doc.Add(std::move(o));
  }
  return true;
}

bool ExportObjMulti(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  struct ExportGroup {
    std::string name;
    kernel::Mesh mesh;
    bool has_color = false;
    Color color;
  };
  std::vector<ExportGroup> groups;
  std::map<std::string, int> used_names;
  for (const SceneObject& o : doc.Objects()) {
    if (selected_only && !o.selected) continue;
    if (!doc.IsObjectVisible(o)) continue;
    std::optional<kernel::Mesh> m;
    if (o.kind == ObjectKind::Mesh && o.mesh) {
      m = *o.mesh;
    } else if (o.kind == ObjectKind::Brep && o.brep) {
      BrepMeshOptions opt;
      opt.chord_tolerance = 0.01;
      m = MeshBrepClosed(o.brep->raw(), opt);
    } else if (o.kind == ObjectKind::Surface && o.surface) {
      m = o.surface->TessellateGridAdaptive(0.01);
    } else if (o.kind == ObjectKind::SubD && o.subd) {
      m = o.subd->ToApproximateMesh();
    }
    if (!m || m->FaceCount() == 0) continue;
    ExportGroup g;
    std::string name = ObjSanitizeName(o.name.empty() ? "Object" : o.name);
    const int n = ++used_names[name];
    if (n > 1) name += "_" + std::to_string(n);
    g.name = name;
    g.mesh = std::move(*m);
    if (!o.color_by_layer) {
      g.has_color = true;
      g.color = o.color;
    } else if (!doc.Layers().empty()) {
      const size_t li = static_cast<size_t>(std::clamp(o.layer_index, 0, static_cast<int>(doc.Layers().size()) - 1));
      g.has_color = true;
      g.color = doc.Layers()[li].color;
    }
    groups.push_back(std::move(g));
  }
  if (groups.empty()) {
    error = "Nothing to export: select meshes, surfaces, polysurfaces or SubDs";
    return false;
  }

  std::ofstream out(path);
  if (!out) {
    error = "Could not write " + path;
    return false;
  }
  const std::string mtl_name = std::filesystem::path(path).stem().string() + ".mtl";
  const std::string mtl_path = (std::filesystem::path(path).parent_path() / mtl_name).string();
  std::ofstream mtl(mtl_path);
  if (!mtl) {
    error = "Could not write " + mtl_path;
    return false;
  }
  out << "mtllib " << mtl_name << "\n";

  int vertex_base = 0;
  for (const ExportGroup& g : groups) {
    const ON_Mesh& m = g.mesh.raw();
    out << "o " << g.name << "\n";
    if (g.has_color) {
      out << "usemtl " << g.name << "\n";
      mtl << "newmtl " << g.name << "\n";
      mtl << "Kd " << g.color.r << ' ' << g.color.g << ' ' << g.color.b << "\n";
      mtl << "d " << g.color.a << "\n\n";
    }
    for (int i = 0; i < m.m_V.Count(); ++i) {
      const ON_3fPoint& v = m.m_V[i];
      out << "v " << v.x << ' ' << v.y << ' ' << v.z << "\n";
    }
    const std::vector<kernel::Vector3d> normals = g.mesh.ComputeVertexNormals();
    for (const kernel::Vector3d& n : normals) out << "vn " << n.x << ' ' << n.y << ' ' << n.z << "\n";
    const bool has_uvs = g.mesh.HasTextureCoordinates();
    if (has_uvs) {
      for (int i = 0; i < m.m_V.Count(); ++i) {
        const kernel::Point2d uv = g.mesh.TextureCoordinateAt(i);
        out << "vt " << uv.x << ' ' << uv.y << "\n";
      }
    }
    for (int i = 0; i < m.m_F.Count(); ++i) {
      const ON_MeshFace& f = m.m_F[i];
      auto write_corner = [&](int vi) {
        const int gi = vertex_base + vi + 1;
        if (has_uvs) out << gi << '/' << gi << '/' << gi;
        else out << gi << "//" << gi;
      };
      out << "f ";
      write_corner(f.vi[0]);
      out << ' ';
      write_corner(f.vi[1]);
      out << ' ';
      write_corner(f.vi[2]);
      if (f.IsQuad()) {
        out << ' ';
        write_corner(f.vi[3]);
      }
      out << "\n";
    }
    vertex_base += m.m_V.Count();
  }
  if (!out.good() || !mtl.good()) {
    error = "Could not write " + path;
    return false;
  }
  return true;
}

}  // namespace

bool ImportMeshFile(Document& doc, const std::string& path, std::string& error) {
  const std::string ext = LowerExt(path);
  if (ext == ".obj") return ImportObjMulti(doc, path, error);
  if (ext != ".stl") {
    error = "Unsupported mesh format: " + ext;
    return false;
  }
  kernel::Mesh mesh;
  if (kernel::Mesh::LoadStl(path, mesh) != kernel::Result::Ok || mesh.FaceCount() == 0) {
    error = "Could not read a mesh from " + path;
    return false;
  }
  SceneObject o = SceneObject::MakeMesh(mesh);
  o.name = std::filesystem::path(path).stem().string();
  doc.Add(std::move(o));
  return true;
}

bool ExportMeshFile(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  const std::string ext = LowerExt(path);
  if (ext == ".obj") return ExportObjMulti(doc, path, selected_only, error);
  if (ext != ".stl") {
    error = "Unsupported export format: " + ext;
    return false;
  }
  std::vector<kernel::Mesh> meshes;
  for (const SceneObject& o : doc.Objects()) {
    if (selected_only && !o.selected) continue;
    if (!doc.IsObjectVisible(o)) continue;
    if (o.kind == ObjectKind::Mesh && o.mesh) {
      meshes.push_back(*o.mesh);
    } else if (o.kind == ObjectKind::Brep && o.brep) {
      BrepMeshOptions opt; opt.chord_tolerance = 0.01; meshes.push_back(MeshBrepClosed(o.brep->raw(), opt));
    } else if (o.kind == ObjectKind::Surface && o.surface) {
      meshes.push_back(o.surface->TessellateGridAdaptive(0.01));
    } else if (o.kind == ObjectKind::SubD && o.subd) {
      meshes.push_back(o.subd->ToApproximateMesh());
    }
  }
  if (meshes.empty()) {
    error = "Nothing to export: select meshes, surfaces, polysurfaces or SubDs";
    return false;
  }
  kernel::Mesh merged = meshes.size() == 1 ? meshes[0] : kernel::Mesh::MergeAndWeld(meshes);
  if (merged.SaveStlBinary(path) != kernel::Result::Ok) {
    error = "Could not write " + path;
    return false;
  }
  return true;
}

}  // namespace dino8::app
