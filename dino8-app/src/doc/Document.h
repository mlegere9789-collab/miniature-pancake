// The Dino 8 document: objects, layers, groups, named views, document user
// text, settings, and a snapshot-based undo/redo stack.
//
// Undo model: every command that modifies the document calls
// BeginChange("label") first. That pushes a full snapshot (objects, layers,
// groups) onto the undo stack. Full snapshots are deliberately simple and
// robust - there is no per-operation inverse to get wrong, so Undo never
// "randomly stops working" the way users report Rhino 8's does.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "doc/SceneObject.h"

namespace dino8::app {

struct Layer {
  std::string name = "Default";
  Color color = Color::FromBytes(0, 0, 0);
  bool visible = true;
  bool locked = false;
  int parent = -1;  // index of parent layer, -1 for a top-level layer
  std::string linetype = "Continuous";
  std::string material;
  std::string description;  // the "layer notes" Rhino 8 users asked for
  bool expanded = true;
};

// A block definition: a named set of objects with a base point. Instances
// are grouped copies tagged with the block name (see cmd_drafting.cpp).
struct BlockDefinition {
  std::string name;
  kernel::Point3d base{0, 0, 0};
  std::vector<SceneObject> objects;
  std::string description;
};

struct Group {
  int id = -1;
  std::string name;
};

// A render material (Rhino's basic material): Blinn-Phong colour and
// specular terms plus one optional diffuse texture and its projection.
struct Material {
  std::string name = "Default";
  Color diffuse = Color::FromBytes(200, 200, 200);
  Color specular = Color::FromBytes(255, 255, 255);
  float gloss = 0.35f;         // 0 = matte, 1 = mirror-tight highlight
  float reflectivity = 0.0f;   // 0..1, blends the procedural environment in
  float transparency = 0.0f;   // 0 = opaque, 1 = invisible
  Color emission = Color::FromBytes(0, 0, 0);
  std::string texture_path;    // BMP / PPM / PNG image, empty = none
  TextureMapping mapping = TextureMapping::Surface;
  float mapping_scale = 1.0f;  // texture repeats across the object

  bool SameAppearance(const Material& o) const {
    auto eq = [](const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
    return eq(diffuse, o.diffuse) && eq(specular, o.specular) && gloss == o.gloss && reflectivity == o.reflectivity &&
           transparency == o.transparency && eq(emission, o.emission) && texture_path == o.texture_path &&
           mapping == o.mapping && mapping_scale == o.mapping_scale;
  }
};

enum class LightType { Point, Spot, Directional, Rectangular, Linear };
const char* LightTypeName(LightType t);

// A document light. `direction` points from the light into the scene
// (spot axis, directional light travel direction, rectangular light normal).
// `length` is the spot cone length / linear light length; `width` the
// rectangular light's second edge length.
struct Light {
  int id = -1;
  std::string name;
  LightType type = LightType::Point;
  kernel::Point3d position{0, 0, 0};
  kernel::Vector3d direction{0, 0, -1};
  Color color = Color::FromBytes(255, 255, 255);
  float intensity = 1.0f;
  float spot_angle = 30.0f;    // half angle in degrees (spot)
  float spot_hardness = 0.5f;  // 0 = soft edge, 1 = hard edge
  double length = 10.0;
  double width = 10.0;
  kernel::Vector3d x_axis{1, 0, 0};  // rectangular/linear light edge direction
  bool enabled = true;
  bool selected = false;
};

// Render environment: background, ground plane and sun.
struct RenderSettings {
  enum class Background { Solid, Gradient, Sky };
  Background background = Background::Sky;
  Color background_color = Color::FromBytes(235, 238, 242);
  Color gradient_top = Color::FromBytes(120, 140, 175);
  Color gradient_bottom = Color::FromBytes(236, 238, 242);
  bool gradient_view = true;      // gradient background in the modelling display modes
  bool ground_plane = false;
  bool ground_auto_height = true;
  double ground_height = 0.0;
  Color ground_color = Color::FromBytes(168, 171, 176);
  bool ground_shadows = true;
  bool sun = false;
  double sun_azimuth = 135.0;     // degrees clockwise from north (+Y)
  double sun_altitude = 45.0;     // degrees above the horizon
  float sun_intensity = 1.0f;
  Color sun_color = Color::FromBytes(255, 248, 232);
  bool skylight = true;           // ambient sky term in Rendered mode
  int render_width = 1280;
  int render_height = 720;
  int render_quality = 2;         // supersampling factor 1..4
  std::string environment_image;  // Partial: shown in the panel only
};

// A camera description that lives in the document (named views) without
// dragging viewport/GL code into the document layer.
struct CameraState {
  kernel::Point3d target{0, 0, 0};
  kernel::Point3d eye{50, -50, 40};
  kernel::Vector3d up{0, 0, 1};
  bool perspective = true;
  double ortho_height = 100.0;  // world-space height of a parallel view
  double lens_mm = 50.0;
};

struct NamedView {
  std::string name;
  CameraState camera;
};

struct DocumentSettings {
  std::string unit_system = "Millimeters";
  std::string title, author, comments;  // file metadata (saved in the .3dm)
  double absolute_tolerance = 0.001;
  double angle_tolerance_degrees = 1.0;
  double grid_spacing = 1.0;
  int grid_major_every = 5;
  int grid_extents = 50;
  bool grid_snap = false;
  bool ortho = false;
  bool planar = false;
  bool show_grid = true;
  bool show_axes = true;
};

class Document {
 public:
  Document();

  // ---- objects ---------------------------------------------------------
  ObjectId Add(SceneObject object);  // assigns id, current layer if unset
  bool Remove(ObjectId id);
  SceneObject* Find(ObjectId id);
  const SceneObject* Find(ObjectId id) const;
  std::vector<SceneObject>& Objects() { return objects_; }
  const std::vector<SceneObject>& Objects() const { return objects_; }
  size_t ObjectCount() const { return objects_.size(); }

  // ---- selection -------------------------------------------------------
  std::vector<ObjectId> SelectedIds() const;
  size_t SelectedCount() const;
  void Select(ObjectId id, bool selected = true);
  void SelectAll();
  void SelectNone();
  void InvertSelection();
  void SelectWhere(const std::function<bool(const SceneObject&)>& predicate, bool add = false);

  // ---- visibility / lock -----------------------------------------------
  bool IsObjectVisible(const SceneObject& o) const;   // object + layer
  bool IsObjectLocked(const SceneObject& o) const;    // object + layer
  Color EffectiveColor(const SceneObject& o) const;

  // ---- layers ----------------------------------------------------------
  std::vector<Layer>& Layers() { return layers_; }
  const std::vector<Layer>& Layers() const { return layers_; }
  int AddLayer(const std::string& name, Color color = Color::FromBytes(0, 0, 0), int parent = -1);
  bool RemoveLayer(int index);  // refuses if objects use it or it's current
  int FindLayer(const std::string& name) const;
  int CurrentLayer() const { return current_layer_; }
  void SetCurrentLayer(int index);
  std::string LayerFullPath(int index) const;

  // ---- materials / lights ----------------------------------------------
  std::vector<Material>& Materials() { return materials_; }
  const std::vector<Material>& Materials() const { return materials_; }
  Material* FindMaterial(const std::string& name);
  const Material* FindMaterial(const std::string& name) const;
  // Adds (or replaces, when a material of the same name exists) and
  // returns the stored material's name.
  std::string AddMaterial(Material m);
  bool RemoveMaterial(const std::string& name);
  // The material that shades an object: its own, else its layer's, else
  // a default built from the object's display colour.
  Material MaterialFor(const SceneObject& o) const;
  std::vector<Light>& Lights() { return lights_; }
  const std::vector<Light>& Lights() const { return lights_; }
  int AddLight(Light light);
  bool RemoveLight(int id);
  Light* FindLight(int id);
  RenderSettings& Render() { return render_; }
  const RenderSettings& Render() const { return render_; }

  // ---- groups ----------------------------------------------------------
  int CreateGroup(const std::vector<ObjectId>& ids, const std::string& name = "");
  void Ungroup(const std::vector<ObjectId>& ids);
  const std::vector<Group>& Groups() const { return groups_; }
  std::vector<ObjectId> GroupMembers(int group_id) const;

  // ---- named views / user text / notes ---------------------------------
  std::vector<NamedView>& NamedViews() { return named_views_; }
  std::vector<BlockDefinition>& Blocks() { return blocks_; }
  BlockDefinition* FindBlock(const std::string& name) { for (BlockDefinition& b : blocks_) if (b.name == name) return &b; return nullptr; }
  std::map<std::string, std::string>& UserText() { return user_text_; }
  std::string& Notes() { return notes_; }
  DocumentSettings& Settings() { return settings_; }
  const DocumentSettings& Settings() const { return settings_; }

  // ---- undo / redo -----------------------------------------------------
  void BeginChange(const std::string& label);
  bool Undo();
  bool Redo();
  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }
  std::vector<std::string> UndoLabels() const;
  std::vector<std::string> RedoLabels() const;
  void ClearUndo();

  // ---- bounds ----------------------------------------------------------
  bool BoundingBoxOf(const std::vector<ObjectId>& ids, kernel::BoundingBox& out) const;
  bool VisibleBoundingBox(kernel::BoundingBox& out) const;

  // ---- lifecycle -------------------------------------------------------
  void Clear();
  const std::string& Path() const { return path_; }
  void SetPath(const std::string& p) { path_ = p; }
  bool Modified() const { return modified_; }
  void SetModified(bool m) { modified_ = m; }
  void Touch() { modified_ = true; ++revision_; }
  std::uint64_t Revision() const { return revision_; }

 private:
  struct Snapshot {
    std::string label;
    std::vector<SceneObject> objects;
    std::vector<Layer> layers;
    int current_layer = 0;
    std::vector<Group> groups;
    std::vector<Material> materials;
    std::vector<Light> lights;
    ObjectId next_id = 1;
    int next_group_id = 1;
    int next_light_id = 1;
  };
  Snapshot Capture(const std::string& label) const;
  void Restore(const Snapshot& snapshot);

  std::vector<SceneObject> objects_;
  std::vector<Layer> layers_;
  int current_layer_ = 0;
  std::vector<Group> groups_;
  std::vector<Material> materials_;
  std::vector<Light> lights_;
  RenderSettings render_;
  int next_light_id_ = 1;
  std::vector<NamedView> named_views_;
  std::vector<BlockDefinition> blocks_;
  std::map<std::string, std::string> user_text_;
  std::string notes_;
  DocumentSettings settings_;
  std::string path_;
  bool modified_ = false;
  std::uint64_t revision_ = 0;
  ObjectId next_id_ = 1;
  int next_group_id_ = 1;
  std::vector<Snapshot> undo_;
  std::vector<Snapshot> redo_;
  size_t max_undo_ = 100;
};

}  // namespace dino8::app
