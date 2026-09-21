// The Dino 8 document: objects, layers, groups, named views, document user
// text, settings, and a diff-based undo/redo stack.
//
// Undo model: every command that modifies the document calls
// BeginChange("label") first (or, for an audited call site that knows
// exactly which existing objects it's about to touch,
// BeginChangeForObjects("label", ids) - see its comment). Internally this
// records enough of the pre-edit state to later compute, once the command
// has finished mutating, a StateDelta: the objects that were actually
// added/removed/possibly-modified plus the (cheap) document-level state,
// rather than a deep copy of the whole document. Undo/Redo apply or invert
// that delta directly - no extra full-document capture on every step the
// old snapshot-per-step model needed. There is still no per-operation
// inverse to hand-write and get wrong: a delta always carries real
// before/after values, never a recomputed inverse, so Undo/Redo keeps the
// same robustness the old full-snapshot model had. See the StateDelta/
// HistoryEntry/PendingChange comments below for the exact mechanics.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "doc/SceneObject.h"
#include "spatial/ObjectGrid.h"

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
//
// `states` names this block's Visibility-parameter states (BlockAddState /
// BlockRemoveState in cmd_drafting.cpp), e.g. {"Open", "Closed"}; empty
// means the block is a plain (non-dynamic) block, unaffected by anything in
// doc/BlockInstances.h. Each object in `objects` optionally carries a
// "Dino8.VisStates" user-text tag (a comma list of the states it appears
// in); an untagged object is visible in every state. See BlockInstances.h
// for how a placed instance picks and rebuilds its active state.
struct BlockDefinition {
  std::string name;
  kernel::Point3d base{0, 0, 0};
  std::vector<SceneObject> objects;
  std::string description;
  std::vector<std::string> states;
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

// A Layer State: a named snapshot of every layer's visible/locked flags
// (the Layer State Manager panel's "Save state" / restore), keyed by layer
// name rather than index so it survives layers being reordered or new ones
// added between save and restore. A per-Document member (not a process-wide
// static) so states are scoped to the document that saved them, and
// persisted to the .3dm the same way AnnotationStyles are: one
// "Dino8.LayerState.<name>" document user-string per state, packed as
// "layer,V,L|layer2,V,L|..." (see io/File3dm.cpp).
struct LayerState {
  std::string name;
  std::vector<std::pair<std::string, std::pair<bool, bool>>> layers;  // name -> {visible, locked}
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

// A guide line (AddGuide/RemoveGuide): stored as a two-point segment rather
// than Rhino's true infinite construction line. Kept as document data, like
// NamedCPlane above; not drawn in the viewport and not consulted by object
// snaps yet, so it is a real place to hang a construction reference but not
// a full replacement for Rhino's guide feature.
struct Guide {
  kernel::Point3d a{0, 0, 0};
  kernel::Point3d b{1, 0, 0};
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
  std::string kind;  // "Turntable", "Path", "Flythrough", "OneDaySun", "SeasonalSun", ""
  std::vector<CameraState> frames;
  std::string viewport;  // viewport the animation was set up in
  // Sun-driven animations (SetOneDaySunAnimation / SetSeasonalSunAnimation)
  // additionally vary RenderSettings::sun_azimuth/sun_altitude per frame,
  // with the camera held still; empty when the animation is camera-only.
  std::vector<double> sun_azimuth;
  std::vector<double> sun_altitude;
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

// The data behind ExtractPipedCurve: the rail curve a Pipe surface was
// swept along, kept as real geometry (a value copy, not an ObjectId
// reference) so the tag survives the source curve object being deleted -
// same rationale, and same side-table pattern, as HoleFeature above. Session
// state only: not written to the .3dm and not restored by Undo/Redo (see
// HoleFeature's comment for why that tradeoff is acceptable here too).
struct PipeFeature {
  kernel::NurbsCurve rail;
};

// General parent/child provenance for an object created out of another one:
// which object it was derived from (`parent_id`, kNoObject if none recorded)
// and what kind of derivation it was. Two producers currently populate this:
//  - BlockInstanceMember: InstantiateBlockInDocument (doc/BlockInstances.h /
//    cmd_drafting.cpp) tags every member of a placed instance except the
//    first with parent_id = that first member's ObjectId, so the instance
//    has one real "anchor" object children point at - SelChildren/SelParents
//    (cmd_select2.cpp) walk this instead of the group-symmetric fallback
//    selection used before.
//  - ExtrusionRail: ExtrudeCommand (cmd_solids.cpp) tags the solid/surface
//    it builds with parent_id = the profile curve (or source surface/brep)
//    it was extruded from, so SelExtrusion/SelParents can find the real
//    source instead of guessing from object kind alone.
// Kept in a side table for exactly HoleFeature/PipeFeature's reasons above
// (ordinary objects, Save3dm, ObjectCount() and the object list are
// untouched by it) and with the same scope: session state only, not written
// to the .3dm and not restored by Undo/Redo. `parent_id` is intentionally
// allowed to go stale (the referenced object may later be deleted, e.g. the
// extrusion's source curve) - callers always re-check the id still resolves
// via Find() before using it, so a dangling reference degrades to "no parent
// found" rather than a crash, same as a HoleFeature outliving an Undo past
// its cut.
enum class ProvenanceKind { BlockInstanceMember, ExtrusionRail };
struct ProvenanceInfo {
  ObjectId parent_id = kNoObject;
  ProvenanceKind kind = ProvenanceKind::BlockInstanceMember;
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
  // Layer new Centermark/CenterLine objects go on (SetCenterLayer /
  // CENTERLAYER; empty = the current layer) - same pattern as
  // dimension_layer/SetDimensionLayer above.
  std::string center_layer;
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
  // Removes every group with no member objects (see Ungroup, which does the
  // same cleanup for the groups it just emptied). Returns the count removed.
  int RemoveEmptyGroups();

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
  // Removes a custom linetype by name (refuses "Continuous", the one every
  // new layer/object implicitly falls back to). Returns false if not found
  // or protected.
  bool RemoveLinetype(const std::string& name);
  std::vector<AnnotationStyle>& AnnotationStyles() { return annotation_styles_; }
  const std::vector<AnnotationStyle>& AnnotationStyles() const { return annotation_styles_; }
  AnnotationStyle* FindAnnotationStyle(const std::string& name);
  const AnnotationStyle& CurrentAnnotationStyle() const;
  // ---- Layer States (Layer State Manager panel) -------------------------
  std::vector<LayerState>& LayerStates() { return layer_states_; }
  const std::vector<LayerState>& LayerStates() const { return layer_states_; }
  LayerState* FindLayerState(const std::string& name);
  // Removes an annotation style by name (refuses the current one, see
  // DocumentSettings::annotation_style). Returns false if not found or protected.
  bool RemoveAnnotationStyle(const std::string& name);
  std::vector<NamedSelection>& NamedSelections() { return named_selections_; }
  std::vector<NamedPosition>& NamedPositions() { return named_positions_; }
  std::vector<NamedCPlane>& NamedCPlanes() { return named_cplanes_; }
  const std::vector<NamedCPlane>& NamedCPlanes() const { return named_cplanes_; }
  NamedCPlane* FindNamedCPlane(const std::string& name);
  std::vector<Guide>& Guides() { return guides_; }
  const std::vector<Guide>& Guides() const { return guides_; }

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
  const std::vector<BlockDefinition>& Blocks() const { return blocks_; }
  BlockDefinition* FindBlock(const std::string& name) { for (BlockDefinition& b : blocks_) if (b.name == name) return &b; return nullptr; }
  // Removes a block definition by name. Returns false if not found.
  bool RemoveBlock(const std::string& name);
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
  void SetPipeFeature(ObjectId id, kernel::NurbsCurve rail) { pipe_features_[id] = PipeFeature{std::move(rail)}; }
  const PipeFeature* FindPipeFeature(ObjectId id) const {
    const auto it = pipe_features_.find(id);
    return it == pipe_features_.end() ? nullptr : &it->second;
  }
  void ClearPipeFeature(ObjectId id) { pipe_features_.erase(id); }
  void SetProvenance(ObjectId id, ObjectId parent_id, ProvenanceKind kind) {
    provenance_[id] = ProvenanceInfo{parent_id, kind};
  }
  const ProvenanceInfo* FindProvenance(ObjectId id) const {
    const auto it = provenance_.find(id);
    return it == provenance_.end() ? nullptr : &it->second;
  }
  void ClearProvenance(ObjectId id) { provenance_.erase(id); }
  // Every currently-recorded child of `parent_id` (objects whose provenance
  // parent_id equals it), regardless of `parent_id` itself still existing -
  // callers that need that check (SelParents) do it themselves via Find().
  std::vector<ObjectId> ProvenanceChildren(ObjectId parent_id) const {
    std::vector<ObjectId> out;
    for (const auto& [id, info] : provenance_) if (info.parent_id == parent_id) out.push_back(id);
    return out;
  }
  std::map<std::string, std::string>& UserText() { return user_text_; }
  std::string& Notes() { return notes_; }
  DocumentSettings& Settings() { return settings_; }
  const DocumentSettings& Settings() const { return settings_; }

  // ---- undo / redo -----------------------------------------------------
  void BeginChange(const std::string& label);
  // Fast path for a command that knows, before it mutates anything, the
  // *complete* set of existing object ids it is about to modify in place -
  // and is certain it will add no objects, remove no objects, and touch no
  // other document state (layers/groups/materials/lights/clipping planes/
  // layouts). Records only those objects' before-images (O(ids.size()),
  // not O(document size)) instead of BeginChange's whole-document capture.
  // Get the id list wrong (miss one that's actually touched, or touch one
  // not listed) and that object's change silently won't undo/redo - so
  // only call this from a call site that has been audited to satisfy the
  // contract; every other caller should keep using the always-safe
  // BeginChange(label) above, which never requires this guarantee.
  void BeginChangeForObjects(const std::string& label, const std::vector<ObjectId>& ids);
  bool Undo();
  bool Redo();
  bool CanUndo() const { return pending_.active || !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }
  std::vector<std::string> UndoLabels() const;
  std::vector<std::string> RedoLabels() const;
  void ClearUndo();

  // ---- bounds ----------------------------------------------------------
  bool BoundingBoxOf(const std::vector<ObjectId>& ids, kernel::BoundingBox& out) const;
  bool VisibleBoundingBox(kernel::BoundingBox& out) const;

  // ---- picking acceleration ---------------------------------------------
  // A uniform grid over every object's world AABB, rebuilt lazily whenever
  // Revision() has changed since the last query (see ObjectGrid::EnsureFresh).
  // Viewport picking (PickObject/PickPoint/PickControlPoint/PickSubObject/
  // ObjectsInWindow) queries this instead of scanning Objects() directly so
  // a click or hover in a large assembly only visits nearby objects. `mutable`
  // like the display caches on SceneObject - it is a derived cache of the
  // object list, not document state, so const callers (drawing, picking) can
  // still trigger a rebuild.
  ObjectGrid& PickGrid() const {
    pick_grid_.EnsureFresh(*this);
    return pick_grid_;
  }

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

  // Named Snapshots persistence (extends the feature above across Save/
  // Open): a snapshot's payload is a full Document-shaped state (objects,
  // layers, groups, materials, lights, clipping planes, layouts, id
  // counters) - exactly what Save3dm/Load3dm already know how to write and
  // read for a live Document. Rather than inventing a second, parallel
  // geometry serialization just for snapshots, these two helpers convert a
  // snapshot to/from an ordinary Document so the application layer (which
  // already links io/File3dm.h; Document itself deliberately does not, to
  // avoid a doc->io dependency) can Save3dm() each snapshot to a small
  // sidecar .3dm file next to the main document and Load3dm() it back in.
  // See Application::SaveDocument/OpenDocument for the sidecar-file and
  // manifest handling this enables.
  bool CaptureSnapshotAsDocument(const std::string& name, Document& out) const;  // false if no snapshot has that name
  void AdoptNamedSnapshotFromDocument(const std::string& name, const Document& src);  // add/overwrite

  // ---- Activity Log (local analogue of AutoCAD's Activity Insights) -----
  //
  // Every StateDelta already carries a human-readable label plus the exact
  // set of objects it added/removed/modified (see the StateDelta comment
  // below) - this struct is just that information, boiled down to one
  // durable, human-readable line per completed edit, kept independently of
  // the undo_/redo_ stacks so trimming old undo history (max_undo_) never
  // erases the activity record. Geometry itself is *not* retained here
  // (only labels/ids/counts) - see ActivityLogEntry's comment for why.
  struct ActivityLogEntry {
    std::string timestamp_utc;  // "YYYY-MM-DD HH:MM:SS" UTC, wall-clock time FinalizePending ran
    std::string label;          // the BeginChange/BeginChangeForObjects label, e.g. "Move", "Delete"
    std::string summary;        // e.g. "+2 -0 ~3 object(s) [ids 12,13,14,...]"
  };
  const std::vector<ActivityLogEntry>& ActivityLog() const { return activity_log_; }

  // Finalizes whatever edit is mid-flight into a real HistoryEntry/
  // ActivityLogEntry (same finalization BeginChange/Undo/Redo trigger
  // internally) without starting a new one - callers that need the
  // Activity Log or undo history to be fully up to date *right now*
  // without themselves beginning a change (e.g. Application::SaveDocument,
  // so the very last edit before a save is never silently dropped from the
  // exported log) call this explicitly.
  void FlushPendingHistory() { FinalizePending(); }

  // Loads this document's previously-persisted Activity Log (see
  // ActivityLogEntry) from the sidecar file next to Path(), if one exists,
  // replacing the in-memory log. Call after SetPath() on Open so a
  // reopened document's activity history is available again, not just new
  // edits made in the current session. A no-op if Path() is empty.
  void LoadActivityLog();

  // Rewrites the sidecar Activity Log file for the *current* Path() from
  // scratch, from every entry currently in memory (activity_log_) -
  // including entries recorded before this document had a path at all
  // (AppendActivityLogLineToDisk is a no-op with no path, so an edit made
  // to a brand-new/untitled document is otherwise never written to disk
  // until this runs). Called by Application::SaveDocument right after
  // SetPath() on every save, so a document's very first save (or a Save As
  // to a new path) carries its full in-memory history with it instead of
  // silently losing the pre-save entries. A no-op if Path() is empty.
  void FlushActivityLogToDisk() const;

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

  // ---- diff-based undo/redo history -------------------------------------
  //
  // The old model (see Capture/Restore above, still used for named
  // snapshots) pushed a full document Snapshot on every BeginChange, and
  // Undo()/Redo() each took an *additional* full Snapshot before restoring
  // - O(document size) work and memory per step regardless of how small
  // the edit was, which doesn't scale to large documents.
  //
  // StateDelta instead records, for one BeginChange...next-boundary span:
  //  - the small, cheap document-level state (layers/groups/materials/
  //    lights/clipping planes/layouts/id counters) in full, both sides,
  //    unconditionally - these are never the bottleneck (a handful of
  //    entries, no geometry), so there is no need to diff them and thus no
  //    risk of an equality check silently missing a change.
  //  - the objects that actually differ, keyed by ObjectId:
  //     - `added`: existed after but not before (post-image only; Undo
  //       removes them by id, Redo re-appends them).
  //     - `removed`: existed before but not after (pre-image + the index
  //       it lived at in the before-ordering; Undo reinserts them there,
  //       Redo removes them by id).
  //     - `modified_before`/`modified_after`: ids present on both sides
  //       that may have changed value. SceneObject has no operator== (its
  //       geometry members are opaque OpenNURBS wrappers with no cheap,
  //       safe value-equality available), so rather than risk a hand-
  //       rolled comparison silently declaring two different objects
  //       "equal" (which would corrupt Undo), every id present on both
  //       sides is conservatively treated as a candidate. For a command
  //       that only adds/removes objects (the majority of creation/
  //       deletion commands) this candidate set is empty - a real,
  //       unconditional win. For a command that modifies existing objects
  //       in place without adding/removing (Move, color/property edits,
  //       ...) with no id-set change, EVERY object is a candidate under
  //       the general BeginChange(label) path - i.e. no smaller than
  //       today's full snapshot for that case. BeginChangeForObjects lets
  //       an audited call site narrow the candidate set to just the ids it
  //       is actually touching, which is where the real scaling win for
  //       in-place edits comes from (see cmd_transform.cpp's ApplyXform).
  //  - `modified_after` is left empty and filled in lazily, once, the
  //    first time the entry is actually undone (Document::Undo) rather
  //    than at record time - so an edit that is never undone only ever
  //    pays for one copy per candidate object, not two; see Undo()'s
  //    comment for why this is exactly the right moment and why it is
  //    never O(document) more than once per entry.
  struct StateDelta {
    std::string label;

    std::vector<Layer> layers_before, layers_after;
    int current_layer_before = 0, current_layer_after = 0;
    std::vector<Group> groups_before, groups_after;
    std::vector<Material> materials_before, materials_after;
    std::vector<Light> lights_before, lights_after;
    std::vector<ClippingPlane> clipping_planes_before, clipping_planes_after;
    std::vector<Layout> layouts_before, layouts_after;
    ObjectId next_id_before = 1, next_id_after = 1;
    int next_group_id_before = 1, next_group_id_after = 1;
    int next_light_id_before = 1, next_light_id_after = 1;

    std::vector<SceneObject> modified_before;
    std::vector<SceneObject> modified_after;  // lazily populated - see Document::Undo
    bool modified_after_ready = false;

    std::vector<SceneObject> added;
    struct RemovedObject {
      SceneObject object;
      size_t index_before = 0;  // position in the pre-edit objects_ ordering
    };
    std::vector<RemovedObject> removed;
  };

  // One undo/redo stack entry: a delta plus, every kCheckpointInterval
  // entries, a full document Snapshot taken at the same moment as the
  // delta's "after" state. Because every StateDelta already carries both
  // directions once materialized, ordinary Undo()/Redo() never needs to
  // replay a chain of deltas back to a checkpoint the way a "diff from
  // last checkpoint only" scheme would - applying one entry is always
  // O(that entry's delta), independent of how far back the last checkpoint
  // was. The checkpoint here instead serves as a periodic, bounded-cost,
  // ground-truth anchor: a cheap structural self-check (see Undo()) that
  // catches an object-count/id-counter mismatch between the fast delta
  // path and a full Capture(), rather than something Undo/Redo application
  // itself depends on. It is shared via shared_ptr so holding one costs
  // nothing beyond the single real Capture() taken to make it.
  struct HistoryEntry {
    StateDelta delta;
    std::shared_ptr<const Snapshot> checkpoint;
  };
  static constexpr int kCheckpointInterval = 50;

  // A BeginChange/BeginChangeForObjects call records the pre-edit state
  // here immediately (this part is unavoidably synchronous - it must run
  // before the caller's mutation) but does *not* yet know the post-edit
  // state, since the caller hasn't mutated the document yet. Finalizing
  // into a real StateDelta (see FinalizePending) - which needs both sides
  // to compute the added/removed/modified split - happens lazily, right
  // before the next BeginChange/BeginChangeForObjects/Undo/Redo/ClearUndo,
  // by which point the previous command has finished mutating. This
  // mirrors the old code's implicit contract (a BeginChange's snapshot was
  // always really "the state as of the *next* BeginChange, minus this
  // command's edit") without changing when callers may call BeginChange.
  struct PendingChange {
    bool active = false;
    bool fast_path = false;
    std::string label;
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
    std::vector<SceneObject> objects;            // general path: full pre-edit copy
    std::vector<SceneObject> fast_path_before;    // fast path: just the declared ids
  };
  void FinalizePending();
  void ApplyDelta(const StateDelta& delta, bool undo);
  void ApplyObjectDelta(const StateDelta& delta, bool undo);

  // Activity Log recording: builds one ActivityLogEntry from a just-
  // finalized StateDelta, appends it to activity_log_, and (if Path() is
  // set) appends the same entry as one line to the sidecar log file so it
  // survives past this session even if the document is never explicitly
  // saved again. See FinalizePending's call site.
  void RecordActivityLogEntry(const StateDelta& d);
  void AppendActivityLogLineToDisk(const ActivityLogEntry& e) const;
  std::string ActivityLogPath() const;
  std::vector<ActivityLogEntry> activity_log_;

  PendingChange pending_;
  int ops_since_checkpoint_ = 0;

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
  std::vector<Guide> guides_;
  std::vector<ClippingPlane> clipping_planes_;
  int next_clipping_plane_id_ = 1;
  std::vector<Layout> layouts_;
  Animation animation_;
  std::vector<BlockDefinition> blocks_;
  std::vector<Linetype> linetypes_;
  std::vector<AnnotationStyle> annotation_styles_;
  std::vector<LayerState> layer_states_;
  std::vector<ReferenceModel> reference_models_;
  std::map<ObjectId, HoleFeature> hole_features_;
  std::map<ObjectId, PipeFeature> pipe_features_;
  std::map<ObjectId, ProvenanceInfo> provenance_;
  std::map<std::string, std::string> user_text_;
  std::string notes_;
  DocumentSettings settings_;
  std::string path_;
  bool modified_ = false;
  std::uint64_t revision_ = 0;
  ObjectId next_id_ = 1;
  int next_group_id_ = 1;
  std::vector<HistoryEntry> undo_;
  std::vector<HistoryEntry> redo_;
  size_t max_undo_ = 100;
  std::vector<std::pair<std::string, Snapshot>> named_snapshots_;
  mutable ObjectGrid pick_grid_;
};

}  // namespace dino8::app
