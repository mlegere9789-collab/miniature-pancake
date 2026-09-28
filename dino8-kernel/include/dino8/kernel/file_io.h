#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/point_cloud.h"
#include "dino8/kernel/subd.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// A plain 0-255 RGB triple for Model::AddLayer()'s `color` parameter, kept
// independent of ON_Color so a caller naming a layer color doesn't need to
// know the OpenNURBS type underneath (the same reasoning file_io.h's other
// wrapper types follow throughout this header).
struct Color {
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;
};

// One dash or gap in a Linetype's repeating pattern, kept independent of
// ON_LinetypeSegment for the same reason Color above is kept independent
// of ON_Color. `is_dash = true` draws a line segment `length_mm`
// millimeters long; `is_dash = false` leaves a gap of the same length. A
// pattern's first segment must be a dash - the same requirement
// ON_LinetypeSegment's own header states for a curve to be drawn starting
// at its own start point.
struct LinetypeSegment {
  double length_mm = 0.0;
  bool is_dash = true;
};

// A linetype's dash pattern, in on-disk millimeters, for
// Model::AddLinetype()'s `pattern` parameter below. An empty pattern (the
// default) matches ON_Linetype's own "no pattern" continuous-line behavior.
using LinetypePattern = std::vector<LinetypeSegment>;

// Key/value pairs for the Add*() methods' `user_strings` parameter below -
// Rhino's own "user text" mechanism (ON_3dmObjectAttributes::SetUserString()
// underneath), the free-form attribute data every .3dm object can carry
// alongside its name/layer/color (a part number, a material spec, a link
// back to some external database row - anything a plugin or a user wants
// attached to an object that isn't already a first-class attribute).
using UserStrings = std::vector<std::pair<std::string, std::string>>;

// A layer read back from Model::LayerAt() below - the read-side
// counterpart to AddLayer()'s own `color`/`linetype_index` parameters.
struct LayerInfo {
  std::string name;
  Color color;
  int linetype_index = -1;
};

// A linetype read back from Model::LinetypeAt() below - the read-side
// counterpart to AddLinetype()'s own `pattern` parameter.
struct LinetypeInfo {
  std::string name;
  LinetypePattern pattern;
};

// A render material read back from Model::MaterialAt() below - the
// read-side counterpart to AddMaterial()'s own `diffuse_color` parameter.
// Only `diffuse_color` is populated, matching AddMaterial()'s own
// "only Name()/Diffuse() are set" scope (see its doc comment).
struct MaterialInfo {
  std::string name;
  Color diffuse_color;
};

// One object's attributes, read back from Model::ObjectAttributesAt()
// below - the read-side counterpart to every Add*() method's own name/
// layer_index/render_color/user_strings/linetype_index/group_indices/
// material_index parameters. `render_color`/`linetype_index`/
// `material_index` are std::nullopt when the object inherits that value
// from its layer (ON::color_from_layer / ON::linetype_from_layer /
// ON::material_from_layer) rather than overriding it at the object level -
// the same std::nullopt-means-"inherit from layer" contract those Add*()
// parameters themselves use on the write side.
struct ObjectAttributes {
  std::string name;
  int layer_index = 0;
  std::optional<Color> render_color;
  std::optional<int> linetype_index;
  std::vector<int> group_indices;
  UserStrings user_strings;
  std::optional<int> material_index;
};

// Thin wrapper around ONX_Model so .3dm compatibility comes from
// OpenNURBS directly rather than a reimplementation. This is the
// "can open/save .3dm" exit criterion for chunk 1 — nothing more.
class Model {
 public:
  Model();

  // Adds a layer to the model and returns its index (>= 0) for use as the
  // Add*() methods' `layer_index` parameter below - the object-organization
  // counterpart to their `name` parameter. Before this, this kernel had no
  // concept of a layer at all (PARITY_MAP.md's own "kernel-level data
  // exchange" evidence names it specifically: "grep ON_Layer/ON_Material in
  // dino8-kernel/src: none"), so nothing it saved could carry the
  // organizational metadata Rhino itself is built around - color-by-layer,
  // per-layer visibility, selection-by-layer, none of it reachable from
  // this API even though ONX_Model (and so the .3dm format underneath)
  // has always supported it. Wraps ONX_Model::AddLayer(), OpenNURBS' own
  // "easy way to add a layer" helper. Returns -1 for an empty `name`
  // instead of forwarding to OpenNURBS, whose own contract for that case
  // (an unnamed layer aliasing "Default") is surprising for a caller who
  // asked to add a named layer.
  // AddLayer()'s own `linetype_index` parameter, along with every Add*()
  // method's own `linetype_index` parameter below, closes the last field
  // PARITY_MAP.md's ".3dm attribute/metadata fidelity" evidence names
  // alongside layers/materials/user-strings: "linetypes". Before this, a
  // layer (and so every object left on it) could only ever draw as a solid
  // line - ON_Layer::m_linetype_index's own default of -1 (Continuous) -
  // even though .3dm's linetype table has always supported Rhino's own
  // named dash patterns (Dashed, DashDot, Center, Border, Hidden, Dots,
  // ...). `linetype_index` of -1 (the default) leaves the layer's linetype
  // untouched at that same -1 (Continuous) default - no behavior change for
  // existing callers, same reasoning as `color`'s own UnsetColor no-op
  // above. A non-negative value is a caller error unless it came from this
  // model's own AddLinetype() (or is one of ON_Linetype's built-in negative-
  // aliased indices like Dashed/DashDot/... - see opennurbs_linetype.h),
  // exactly like AddLayer()'s own `layer_index` contract for Add*() below.
  int AddLayer(const std::string& name, Color color = Color(), int linetype_index = -1);

  // Adds a linetype (named dash pattern) to the model and returns its index
  // (>= 0) for use as AddLayer()'s `linetype_index` parameter above and
  // every Add*() method's own `linetype_index` parameter below - see
  // AddLayer()'s own doc comment for why this exists. `pattern` is empty by
  // default, matching ON_Linetype's own "no pattern" continuous-line
  // behavior; a non-empty pattern is written as alternating dash/gap
  // segments via ON_Linetype::AppendSegment(), in the order given. Returns
  // -1 for an empty `name`, same contract as AddLayer()'s own -1 return for
  // an empty name.
  int AddLinetype(const std::string& name, const LinetypePattern& pattern = LinetypePattern());

  // Adds a group to the model and returns its index (>= 0) for use in every
  // Add*() method's own `group_indices` parameter below - the last field
  // PARITY_MAP.md's ".3dm attribute/metadata fidelity" evidence names
  // alongside layers/materials/linetypes/user-strings that this kernel had
  // no way to write at all: Rhino's own Group/Ungroup commands, which let a
  // user select every object in a group with one click even though the
  // objects themselves may span multiple layers - a relationship a .3dm's
  // layer table cannot express at all. Wraps ON_Group (opennurbs_group.h),
  // added to the model the same way AddLayer()/AddLinetype() add their own
  // component types via AddModelComponent(). Returns -1 for an empty
  // `name`, same contract as AddLayer()/AddLinetype().
  int AddGroup(const std::string& name);

  // Adds a render material to the model and returns its index (>= 0) for use
  // as every Add*() method's own `material_index` parameter below - the last
  // field PARITY_MAP.md's ".3dm attribute/metadata fidelity" evidence names
  // that this kernel had no way to write at all ("grep ON_Layer/ON_Material
  // in dino8-kernel/src: none"). Before this, an object's rendered
  // appearance could only ever be Rhino's generic default material
  // (ON::material_from_layer, ON_3dmObjectAttributes' own default, resolving
  // to a layer that itself never carried a material either) - there was no
  // way for this kernel to give an object its own named material with its
  // own diffuse color, the same gap `render_color` closed for the simpler
  // wireframe/shaded-viewport color but not for a real render material
  // (Rhino keeps the two separate: `render_color`/`ColorSource()` drive the
  // object's plain display color, while a material's `Diffuse()` drives
  // rendered/rendered-viewport shading and is a distinct .3dm component
  // table, `ON_ModelComponent::Type::RenderMaterial`). Wraps `ON_Material`
  // (opennurbs_material.h), added to the model the same way
  // AddLayer()/AddLinetype()/AddGroup() add their own component types via
  // AddModelComponent(). Only `Name()` and `Diffuse()` are set - texture
  // maps, specular/emission/shine/transparency/reflectivity remain a
  // disclosed gap, same as PARITY_MAP.md's own evidence already states.
  // Returns -1 for an empty `name`, same contract as
  // AddLayer()/AddLinetype()/AddGroup().
  int AddMaterial(const std::string& name, Color diffuse_color = Color());

  // Every Add*() below takes an optional object `name` and `layer_index`.
  // Before `name` existed, every object this kernel ever put into a Model
  // got a default, empty ON_3dmObjectAttributes - a real, disclosed gap in
  // .3dm metadata fidelity (PARITY_MAP.md's own "kernel-level data
  // exchange" evidence: "write a default ON_3dmObjectAttributes only"): a
  // caller had no way to attach even the most basic identifying metadata
  // .3dm consumers actually rely on (Rhino's own object name, used for
  // selection-by-name, block/part naming, and round-tripping identity
  // across a save/reload; and which layer the object lives on, needed for
  // the same reasons AddLayer() itself exists - see its own doc comment
  // above). An empty (default) `name` and a `layer_index` of 0 (the
  // model's always-present default layer, `AddLayer()`'s own doc comment
  // aside) leave the attributes exactly as before - no behavior
  // change for existing callers. A non-empty `name` is set via
  // ON_3dmObjectAttributes::SetName(..., /*bFixInvalidName=*/true), the
  // same call dino8-app/src/io/File3dm.cpp already uses for every other
  // named entity it writes (layers, views, materials, ...) - `true` fixes
  // up characters ON_ModelComponent::IsValidComponentName() would
  // otherwise reject (e.g. a name that's pure whitespace) rather than
  // silently dropping the name or failing outright. `layer_index` is
  // written straight to ON_3dmObjectAttributes::m_layer_index; passing an
  // index AddLayer() didn't return is a caller error (as it is for
  // ONX_Model itself), not something this wrapper detects.
  //
  // Every Add*() below also takes an optional `render_color`. Before this,
  // an object's displayed color could only ever come from its layer
  // (ON::color_from_layer, ON_3dmObjectAttributes' own default) - the same
  // "kernel-level data exchange" gap AddLayer()'s doc comment quotes names
  // by grep ("write a default ON_3dmObjectAttributes only"), just for the
  // `m_color`/`ColorSource()` fields instead of `m_layer_index`. A caller
  // could color a whole layer via AddLayer(), but never override a single
  // object's own color the way Rhino's own per-object color picker does -
  // e.g. two Breps sharing a layer that still need to render as different
  // colors on reload. `std::nullopt` (the default) leaves ColorSource() at
  // its default ON::color_from_layer - no behavior change for existing
  // callers, exactly like `name`/`layer_index` before it. A present value
  // is written to `m_color` with ColorSource() switched to
  // ON::color_from_object, the same "object, not layer" override Rhino's
  // own per-object color assignment uses, so it isn't silently shadowed by
  // whatever color the object's layer happens to carry.
  //
  // Every Add*() below also takes optional `user_strings`: Rhino's own
  // "user text" key/value attribute data (see UserStrings' own doc comment
  // above), the last of the fields PARITY_MAP.md's ".3dm attribute/metadata
  // fidelity" evidence lists that this kernel had no way to write at all.
  // An empty (default) list is a no-op - no behavior change for existing
  // callers, same as every other optional parameter here. Each pair is
  // written via ON_3dmObjectAttributes::SetUserString(key, value); a
  // repeated key keeps only the last value for that key, matching
  // SetUserString()'s own "replace" contract for a key it's already seen.
  //
  // Every Add*() below also takes an optional `linetype_index`, the same
  // per-object override AddLayer()'s own `linetype_index` parameter
  // provides at the layer level (see its doc comment for why this exists
  // at all). A caller could give a whole layer a dash pattern via
  // AddLayer(), but never override a single object's own linetype the way
  // Rhino's own per-object linetype picker does - e.g. two Breps sharing a
  // layer that still need to draw with different dash patterns on reload.
  // `std::nullopt` (the default) leaves LinetypeSource() at its default
  // ON::linetype_from_layer - no behavior change for existing callers,
  // exactly like `render_color` before it. A present value is written to
  // `m_linetype_index` with LinetypeSource() switched to
  // ON::linetype_from_object, the same "object, not layer" override
  // pattern `render_color` uses for `m_color`/ColorSource().
  //
  // Every Add*() below also takes optional `group_indices`: zero or more
  // indices returned by AddGroup() above, unlike `layer_index` an object can
  // belong to any number of groups at once (Rhino's own nested-group model),
  // so this is a list rather than a single value. An empty (default) list is
  // a no-op - no behavior change for existing callers, same as every other
  // optional parameter here. Each index is written via
  // ON_3dmObjectAttributes::AddToGroup(); passing an index AddGroup() didn't
  // return is a caller error, same contract `layer_index`/`linetype_index`
  // already have for AddLayer()/AddLinetype().
  //
  // Every Add*() below also takes an optional `material_index`, the
  // per-object render material assignment AddMaterial() above makes
  // possible - see its own doc comment for why this exists. `std::nullopt`
  // (the default) leaves MaterialSource() at its default
  // ON::material_from_layer - no behavior change for existing callers,
  // exactly like `render_color`/`linetype_index` before it. A present value
  // is written to `m_material_index` with MaterialSource() switched to
  // ON::material_from_object, the same "object, not layer" override pattern
  // `render_color`/`linetype_index` use for their own fields.
  void AddCurve(const NurbsCurve& curve, const std::string& name = std::string(),
                int layer_index = 0, std::optional<Color> render_color = std::nullopt,
                const UserStrings& user_strings = UserStrings(),
                std::optional<int> linetype_index = std::nullopt,
                const std::vector<int>& group_indices = std::vector<int>(),
                std::optional<int> material_index = std::nullopt);
  void AddBrep(const Brep& brep, const std::string& name = std::string(), int layer_index = 0,
               std::optional<Color> render_color = std::nullopt,
               const UserStrings& user_strings = UserStrings(),
               std::optional<int> linetype_index = std::nullopt,
               const std::vector<int>& group_indices = std::vector<int>(),
               std::optional<int> material_index = std::nullopt);

  // Adds a mesh (a box, cylinder, boolean result, ...) as its own model
  // object - the missing counterpart to AddCurve()/AddBrep() that closed
  // a real gap: every closed-solid primitive and every BooleanCombine()
  // result here is a Mesh, but until now there was no way to put one into
  // a .3dm file at all, only to export it separately via
  // Mesh::SaveObj()/SaveStl(). Same pattern as the other two: copies
  // `mesh`'s underlying ON_Mesh into a new model geometry component.
  void AddMesh(const Mesh& mesh, const std::string& name = std::string(), int layer_index = 0,
               std::optional<Color> render_color = std::nullopt,
               const UserStrings& user_strings = UserStrings(),
               std::optional<int> linetype_index = std::nullopt,
               const std::vector<int>& group_indices = std::vector<int>(),
               std::optional<int> material_index = std::nullopt);

  // Adds a SubD control cage/subdivision surface as its own model
  // object - the same "no way to put this object type into a .3dm at
  // all" gap AddMesh() closed, just for SubD instead of Mesh. Same
  // pattern: copies the SubD's underlying ON_SubD into a new model
  // geometry component.
  void AddSubD(const SubD& subd, const std::string& name = std::string(), int layer_index = 0,
               std::optional<Color> render_color = std::nullopt,
               const UserStrings& user_strings = UserStrings(),
               std::optional<int> linetype_index = std::nullopt,
               const std::vector<int>& group_indices = std::vector<int>(),
               std::optional<int> material_index = std::nullopt);

  // Adds a point cloud as its own model object. PointCloud's own doc
  // comment claims ON_PointCloud is "the same one [OpenNURBS'] .3dm
  // reader/writer already round-trips" - true of the underlying
  // OpenNURBS class, but until this method existed there was no way to
  // actually get a dino8::kernel::PointCloud INTO a Model at all, so
  // that round-trip claim was unreachable from this kernel's own API
  // (the same "no way to put this object type into a .3dm" gap
  // AddMesh()/AddSubD() closed for their own types). Same pattern: copies
  // `cloud`'s underlying ON_PointCloud (positions, and per-point colors/
  // normals when present) into a new model geometry component.
  void AddPointCloud(const PointCloud& cloud, const std::string& name = std::string(),
                     int layer_index = 0, std::optional<Color> render_color = std::nullopt,
                     const UserStrings& user_strings = UserStrings(),
                     std::optional<int> linetype_index = std::nullopt,
                     const std::vector<int>& group_indices = std::vector<int>(),
                     std::optional<int> material_index = std::nullopt);

  int ObjectCount() const;

  // Returns the attributes of the `index`-th object (0 <= index <
  // ObjectCount()), in the same order ONX_ModelComponentIterator visits
  // ModelGeometry components - the read-side counterpart to every Add*()
  // method's own name/layer_index/render_color/user_strings/
  // linetype_index/group_indices/material_index parameters (`material_index`
  // reads back the same way `linetype_index` does: std::nullopt when the
  // object inherits its material from its layer rather than overriding it,
  // ON::material_from_layer being ON_3dmObjectAttributes' own default).
  // Before this, reading back
  // anything an Add*() call had written meant a caller had to hand-roll
  // an ONX_ModelComponentIterator and cast every ON_ModelGeometryComponent
  // itself, via raw() - exactly what this kernel's own round-trip tests
  // for AddLayer()/AddLinetype()/AddGroup() each did, one hand-rolled copy
  // per test, and the only option this API gave any other caller (the gap
  // PARITY_MAP.md's own ".3dm attribute/metadata fidelity" evidence names:
  // "no read-side accessor apart from raw()"). `index` out of range
  // returns a default-constructed ObjectAttributes rather than reading
  // past the component list.
  ObjectAttributes ObjectAttributesAt(int index) const;

  // Returns the number of layers explicitly added via AddLayer() above (0
  // if none have been - see AddLayer()'s own doc comment on the -1 vs. 0
  // default-layer-index wrinkle this deliberately does not paper over).
  int LayerCount() const;

  // Returns the layer at `layer_index` (as returned by AddLayer() above) -
  // the read-side counterpart to AddLayer()'s own `color`/`linetype_index`
  // parameters, same gap ObjectAttributesAt() above closes for objects.
  // `layer_index` not naming a layer this model actually has returns a
  // default-constructed LayerInfo.
  LayerInfo LayerAt(int layer_index) const;

  // Returns the number of linetypes explicitly added via AddLinetype()
  // above.
  int LinetypeCount() const;

  // Returns the linetype at `linetype_index` (as returned by
  // AddLinetype() above) - the read-side counterpart to AddLinetype()'s
  // own `pattern` parameter. `linetype_index` not naming a linetype this
  // model actually has returns a default-constructed LinetypeInfo.
  LinetypeInfo LinetypeAt(int linetype_index) const;

  // Returns the number of groups explicitly added via AddGroup() above.
  int GroupCount() const;

  // Returns the name of the group at `group_index` (as returned by
  // AddGroup() above) - the read-side counterpart to AddGroup()'s own
  // `name` parameter. `group_index` not naming a group this model
  // actually has returns an empty string.
  std::string GroupNameAt(int group_index) const;

  // Returns the number of materials explicitly added via AddMaterial()
  // above.
  int MaterialCount() const;

  // Returns the material at `material_index` (as returned by
  // AddMaterial() above) - the read-side counterpart to AddMaterial()'s
  // own `diffuse_color` parameter, closing the last read-side gap
  // ObjectAttributesAt()/LayerAt()/LinetypeAt()/GroupNameAt() left open
  // (PARITY_MAP.md's own ".3dm attribute/metadata fidelity" evidence
  // named it: "no MaterialAt()/MaterialCount()"). `material_index` not
  // naming a material this model actually has returns a
  // default-constructed MaterialInfo, same contract as LayerAt()/
  // LinetypeAt() above.
  MaterialInfo MaterialAt(int material_index) const;

  // Writes as a .3dm file. `version` is the OpenNURBS archive version
  // (e.g. 80 for the Rhino-8-generation format); defaults to the newest
  // version this OpenNURBS build knows how to write.
  Result Save(const std::string& path, int version = 0) const;

  // Reads a .3dm file into `out_model`. Returns Result::Failed if
  // OpenNURBS can't read the file, or if any mesh object in it carries a
  // face whose vertex index is outside `[0, VertexCount())` - OpenNURBS'
  // own reader copies face indices off the disk unchecked and never
  // validates them, and every kernel Mesh query indexes the vertex array
  // by those face indices just as unchecked, so before this check a
  // corrupt .3dm loaded with Result::Ok and then read out of bounds
  // silently (confirmed by a debug run, see file_io.cpp). `out_model` is
  // reset to empty in that case rather than left half-trusted. Same
  // contract Mesh::LoadObj() already has for an out-of-range face index.
  static Result Load(const std::string& path, Model& out_model);

  const ONX_Model& raw() const { return model_; }

 private:
  ONX_Model model_;
};

}  // namespace dino8::kernel
