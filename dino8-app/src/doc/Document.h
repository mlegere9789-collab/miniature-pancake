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
  enum class Background { Solid, Gradient, Sky, Image };
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
  std::string environment_image;  // used as the background when background == Image
  // BackgroundBitmap: a picture shown behind the model in every viewport as
  // a modelling aid (not part of a final Render, which uses `background`
  // Image above instead).
  std::string background_bitmap;
  bool background_bitmap_enabled = false;
};

// A linetype: a name and a dash pattern (dash, gap, dash, gap... lengths in
// model units, multiplied by DocumentSettings::linetype_scale for display).
// An empty pattern draws continuous.
struct Linetype {
  std::string name = "Continuous";
  std::vector<double> pattern;
};

// An annotation style: defaults for the text / dimension commands. Every
// annotation group records the style it was made with (user text "Style").
struct AnnotationStyle {
  std::string name = "Default";
  double text_height = 0;   // 0: twice the grid spacing (the legacy default)
  double arrow_size = 0;    // 0: the text height
  std::string font;         // empty: the first system sans-serif font found
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

// A saved selection set (NamedSelections) and a saved construction plane
// (NamedCPlane). Kept as plain data so the document stays free of viewport code.
struct NamedSelection {
  std::string name;
  std::vector<ObjectId> ids;
};

// A named position (NamedPosition): full copies of the selected objects'
// geometry at Save time, keyed by their own object id, restored in place
// (an object deleted since Save is skipped; an object added since Save is
// untouched) - a lighter-weight, selection-scoped cousin of the full-
// document Snapshots below, independent of Undo/Redo like they are.
struct NamedPosition {
  std::string name;
  std::vector<SceneObject> objects;
};

// A construction plane saved by name (NamedCPlane). Mirrors
// ConstructionPlane in viewport/Viewport.h without pulling it in here.
struct NamedCPlane {
  std::string name;
  kernel::Point3d origin{0, 0, 0};
  kernel::Vector3d x_axis{1, 0, 0};
  kernel::Vector3d y_axis{0, 1, 0};
};

// A clipping plane: everything on the side the normal (x cross y) points
// to is cut away in the viewports it clips (all when `viewports` is empty).
// Drawn as a translucent rectangle `width` x `height` around `origin`.
struct ClippingPlane {
  int id = 0;
  std::string name;
  kernel::Point3d origin{0, 0, 0};
  kernel::Vector3d x_axis{1, 0, 0};
  kernel::Vector3d y_axis{0, 1, 0};
  double width = 10, height = 10;
  std::vector<std::string> viewports;  // viewport names; empty = every viewport
  bool enabled = true;
  bool selected = false;
  kernel::Vector3d Normal() const { return ON_CrossProduct(x_axis, y_axis); }
  bool ClipsViewport(const std::string& viewport_name) const {
    if (viewports.empty()) return true;
    for (const std::string& v : viewports) if (v == viewport_name) return true;
    return false;
  }
};

// A detail on a layout page: a rectangle (page millimetres, origin at the
// lower-left page corner) showing the model through its own camera.
struct LayoutDetail {
  std::string name;
  double x = 10, y = 10, width = 100, height = 80;  // page mm
  CameraState camera;
  std::string standard_view = "Perspective";
  std::string display_mode = "Shaded";
  double scale = 0;  // page mm per model unit; 0 = free zoom
  bool locked = false;
  bool selected = false;
  std::vector<int> hidden_layers;         // layer indices hidden in this detail only
  std::vector<ObjectId> hidden_objects;   // objects hidden in this detail only
};

struct Layout {
  std::string name;
  double width_mm = 297, height_mm = 210;  // A4 landscape
  std::vector<LayoutDetail> details;
};

// A camera animation (turntable, path, fly-through): the frames are
// precomputed camera states the view commands step through.
struct Animation {
  std::string kind;  // "Turntable", "Path", "Flythrough", ""
  std::vector<CameraState> frames;
  std::string viewport;  // viewport the animation was set up in
};

// A reference model attached through Worksession: another .3dm's objects,
// copied in locked (greyed, per the existing IsObjectLocked display tint)
// and tagged "Dino8.Reference" = its source path so Save skips them again
// unless explicitly exported (see File3dm.cpp Save3dm's include_reference
// flag). Kept here as plain session state - not written into the .3dm
// itself, same as Rhino's own worksessions (see cmd_session.cpp / the
// Worksession command's Save=/Load= .rws file for that).
struct ReferenceModel {
  std::string path;
  std::string alias;                 // filename by default; the Worksession panel label
  std::vector<ObjectId> object_ids;  // this model's objects in the current document
  bool has_limit_box = false;        // LimitReferenceModel restricted which objects loaded
  kernel::Point3d limit_min{0, 0, 0}, limit_max{0, 0, 0};
};

// The data behind an editable hole feature (RoundHole/PlaceHole/
// RevolvedHole/ArrayHole*'s result): the solid as it was before this hole
// (`pre_cut_parent`) and the tool that cut it (`cutter`), both in world
// space. CopyHole/MirrorHole/MoveHole/RotateHole (cmd_solidtools.cpp) look
// this up by the hole object's id, transform `cutter` (and, for Move/
// Rotate, replay the boolean against the unchanged `pre_cut_parent`) and
// commit the new mesh - so a hole is a real, repositionable feature
// instead of a one-shot mesh edit. Kept in a side table rather than on
// SceneObject itself so ordinary objects, Save3dm, ObjectCount() and the
// object list are untouched by it; like ReferenceModel, it is session
// state only - not written to the .3dm and not restored by Undo/Redo (an
// Undo past the cut leaves a harmless orphaned entry, cleared whenever the
// object itself is removed).
struct HoleFeature {
  kernel::Mesh pre_cut_parent;
  kernel::Mesh cutter;
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
  // Linetypes (see Linetype): global display scale and on/off switch.
  double linetype_scale = 1.0;
  bool linetype_display = true;
  // Base point for hatch patterns (HatchBase) and the layer new dimensions
  // go on (SetDimensionLayer; empty = the current layer).
  kernel::Point3d hatch_base{0, 0, 0};
  std::string dimension_layer;
  std::string annotation_style = "Default";
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
  // SelPrev: swaps in the selection that was active before the last
  // SelectNone/InvertSelection/SelectWhere/SelectAll (Select() on individual
  // ids is too fine-grained to record - a window/click select still leaves
  // whatever the selection was before that command as the one restored here).
  // Calling it again swaps back. Returns false when there is nothing to
  // restore (nothing selected anything yet).
  bool RestorePreviousSelection();

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
  std::vector<Group>& Groups() { return groups_; }
  Group* FindGroup(int group_id) { for (Group& g : groups_) if (g.id == group_id) return &g; return nullptr; }
  std::vector<ObjectId> GroupMembers(int group_id) const;

  // ---- named views / user text / notes ---------------------------------
  std::vector<NamedView>& NamedViews() { return named_views_; }

  // ---- linetypes / annotation styles -------------------------------------
  std::vector<Linetype>& Linetypes() { return linetypes_; }
  const std::vector<Linetype>& Linetypes() const { return linetypes_; }
  const Linetype* FindLinetype(const std::string& name) const;
  Linetype* FindLinetype(const std::string& name);
  // Adds or replaces a linetype; returns its index.
  int SetLinetype(const std::string& name, const std::vector<double>& pattern);
  // The linetype name an object draws with ("ByLayer" resolves through the layer).
  std::string EffectiveLinetype(const SceneObject& o) const;
  // Its dash pattern scaled for display; empty when continuous or when
  // linetype display is off.
  std::vector<double> EffectiveDashes(const SceneObject& o) const;
  static std::vector<Linetype> DefaultLinetypes();
  std::vector<AnnotationStyle>& AnnotationStyles() { return annotation_styles_; }
  const std::vector<AnnotationStyle>& AnnotationStyles() const { return annotation_styles_; }
  AnnotationStyle* FindAnnotationStyle(const std::string& name);
  const AnnotationStyle& CurrentAnnotationStyle() const;
  std::vector<NamedSelection>& NamedSelections() { return named_selections_; }
  std::vector<NamedPosition>& NamedPositions() { return named_positions_; }
  std::vector<NamedCPlane>& NamedCPlanes() { return named_cplanes_; }
  const std::vector<NamedCPlane>& NamedCPlanes() const { return named_cplanes_; }
  NamedCPlane* FindNamedCPlane(const std::string& name);

  // ---- clipping planes / layouts / animation ---------------------------
  std::vector<ClippingPlane>& ClippingPlanes() { return clipping_planes_; }
  const std::vector<ClippingPlane>& ClippingPlanes() const { return clipping_planes_; }
  int AddClippingPlane(ClippingPlane plane);  // assigns id, returns it
  ClippingPlane* FindClippingPlane(int id);
  std::vector<Layout>& Layouts() { return layouts_; }
  const std::vector<Layout>& Layouts() const { return layouts_; }
  Layout* FindLayout(const std::string& name);
  Animation& GetAnimation() { return animation_; }
  const Animation& GetAnimation() const { return animation_; }
  std::vector<BlockDefinition>& Blocks() { return blocks_; }
  BlockDefinition* FindBlock(const std::string& name) { for (BlockDefinition& b : blocks_) if (b.name == name) return &b; return nullptr; }
  std::vector<ReferenceModel>& ReferenceModels() { return reference_models_; }
  const std::vector<ReferenceModel>& ReferenceModels() const { return reference_models_; }
  void SetHoleFeature(ObjectId id, kernel::Mesh pre_cut_parent, kernel::Mesh cutter) {
    hole_features_[id] = HoleFeature{std::move(pre_cut_parent), std::move(cutter)};
  }
  const HoleFeature* FindHoleFeature(ObjectId id) const {
    const auto it = hole_features_.find(id);
    return it == hole_features_.end() ? nullptr : &it->second;
  }
  void ClearHoleFeature(ObjectId id) { hole_features_.erase(id); }
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

  // Named Snapshots (Rhino's Snapshots panel): a full document capture
  // under a user-given name, independent of the Undo/Redo stack - it
  // survives further edits and Undo/Redo, so a named snapshot is a
  // stable "restore point" rather than a step in the edit history. Not
  // written to the .3dm (session state only, same as ReferenceModel and
  // HoleFeature above).
  bool SaveNamedSnapshot(const std::string& name);              // captures now; overwrites an existing snapshot of the same name
  bool RestoreNamedSnapshot(const std::string& name);           // false if no snapshot has that name
  bool DeleteNamedSnapshot(const std::string& name);            // false if no snapshot has that name
  std::vector<std::string> NamedSnapshotNames() const;

 private:
  struct Snapshot {
    std::string label;
    std::vector<SceneObject> objects;
    std::vector<Layer> layers;
    int current_layer = 0;
    std::vector<Group> groups;
    std::vector<Material> materials;
    std::vector<Light> lights;
    std::vector<ClippingPlane> clipping_planes;
    std::vector<Layout> layouts;
    ObjectId next_id = 1;
    int next_group_id = 1;
    int next_light_id = 1;
  };
  Snapshot Capture(const std::string& label) const;
  void Restore(const Snapshot& snapshot);

  std::vector<SceneObject> objects_;
  std::vector<ObjectId> prev_selection_;  // SelPrev / RestorePreviousSelection
  std::vector<Layer> layers_;
  int current_layer_ = 0;
  std::vector<Group> groups_;
  std::vector<Material> materials_;
  std::vector<Light> lights_;
  RenderSettings render_;
  int next_light_id_ = 1;
  std::vector<NamedView> named_views_;
  std::vector<NamedSelection> named_selections_;
  std::vector<NamedPosition> named_positions_;
  std::vector<NamedCPlane> named_cplanes_;
  std::vector<ClippingPlane> clipping_planes_;
  int next_clipping_plane_id_ = 1;
  std::vector<Layout> layouts_;
  Animation animation_;
  std::vector<BlockDefinition> blocks_;
  std::vector<Linetype> linetypes_;
  std::vector<AnnotationStyle> annotation_styles_;
  std::vector<ReferenceModel> reference_models_;
  std::map<ObjectId, HoleFeature> hole_features_;
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
  std::vector<std::pair<std::string, Snapshot>> named_snapshots_;
};

}  // namespace dino8::app
