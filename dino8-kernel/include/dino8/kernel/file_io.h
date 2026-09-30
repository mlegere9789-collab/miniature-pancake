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

// Color (a plain 0-255 RGB triple, used by Model::AddLayer()'s `color`
// parameter below) now lives in types.h, so mesh.h can use it too for
// Mesh::SetVertexColors() - see that struct's own comment there.

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

// The length unit a Model's coordinates are measured in, for
// Model::SetUnitSystem()/GetUnitSystem() below, kept independent of
// ON::LengthUnitSystem for the same reason Color/LinetypeSegment above are
// kept independent of their own OpenNURBS counterparts. Covers the unit
// systems dino8-app/src/io/File3dm.cpp's own units switch
// (Inches/Feet/Centimeters/Meters, default Millimeters) already round-trips
// at the app layer, plus Kilometers/Microns/Yards/Miles/None from
// ON::LengthUnitSystem's own wider set, on the theory that a caller reaching
// for a kernel-level unit system wants the same breadth OpenNURBS itself
// offers, not just the five names the app happens to expose today.
enum class UnitSystem {
  Millimeters,
  Centimeters,
  Meters,
  Kilometers,
  Microns,
  Inches,
  Feet,
  Yards,
  Miles,
  None,
};

// A render material read back from Model::MaterialAt() below - the
// read-side counterpart to AddMaterial()'s own `diffuse_color`/
// `specular_color`/`emission_color`/`shine`/`transparency`/`reflectivity`
// parameters. Every field is always populated (never optional), the same
// "always set, even if to ON_Material's own constructor default" contract
// `diffuse_color` alone used to have - AddMaterial()'s own new parameters
// are what determines whether a field holds ON_Material's built-in default
// or a caller-supplied value, not whether this struct carries it at all.
struct MaterialInfo {
  std::string name;
  Color diffuse_color;
  Color specular_color;
  Color emission_color;
  double shine = 0.0;
  double transparency = 0.0;
  double reflectivity = 0.0;
};

// Which of ON::light_style's real-world-usable styles Model::AddLight()
// below supports - the two simplest, most common Rhino light types: a
// point light (an omnidirectional bulb at a location, direction ignored)
// and a directional light (parallel rays from a direction, location
// ignored - the sun). ON::light_style's own spot/linear/rectangular/
// ambient/camera-space variants are a materially larger surface (extra
// per-style parameters like spot angle/exponent or length/width vectors)
// and stay out of scope here, same as this bullet's own "narrowing, not
// erasing" convention for a disclosed remaining gap.
enum class LightStyle {
  Point,
  Directional,
};

// A light read back from Model::LightAt() below - the read-side
// counterpart to Model::AddLight()'s own parameters.
struct LightInfo {
  std::string name;
  LightStyle style = LightStyle::Point;
  Point3d location;
  Vector3d direction;
  Color diffuse_color;
  double intensity = 1.0;
};

// A clipping plane read back from Model::ClippingPlaneAt() below - the
// read-side counterpart to Model::AddClippingPlane()'s own parameters.
// `normal` is the plane's own zaxis, exactly as passed to
// AddClippingPlane() (ON_Plane(origin, normal)'s own documented contract:
// "zaxis = unitized normal") - which side of the plane a viewport actually
// clips away is ON_ClippingPlane's own runtime behavior, not renegotiated
// by this struct.
struct ClippingPlaneInfo {
  std::string name;
  Point3d origin;
  Vector3d normal;
  bool enabled = true;
};

// A named view read back from Model::NamedViewAt() below - the read-side
// counterpart to Model::AddNamedView()'s own camera_location/target_point/
// camera_up parameters.
struct NamedViewInfo {
  std::string name;
  Point3d camera_location;
  Point3d target_point;
  Vector3d camera_up;
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
  int layer_index = -1;
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
  // AddModelComponent(). Returns -1 for an empty `name`, same contract as
  // AddLayer()/AddLinetype()/AddGroup().
  //
  // `specular_color`/`emission_color`/`shine`/`transparency`/
  // `reflectivity` close the rest of the gap this method's own doc comment
  // used to disclose as open ("texture maps, specular/emission/shine/
  // transparency/reflectivity remain a disclosed gap") - texture maps
  // alone stay out of scope (a materially larger problem: an actual bitmap
  // file reference/embedding, not just a scalar or color field). Each
  // parameter is `std::nullopt` by default and left at `ON_Material`'s own
  // constructor default when omitted - no behavior change for an existing
  // caller who only ever passed `diffuse_color`, the same "absent means
  // untouched" contract `render_color`/`linetype_index`/`material_index`
  // already use elsewhere in this API. A present `shine`/`transparency`/
  // `reflectivity` is written via `SetShine()`/`SetTransparency()`/
  // `SetReflectivity()` unclamped - each setter's own documented range
  // (`[0, ON_Material::MaxShine]` for shine, `[0, 1]` for the other two) is
  // ON_Material's own contract to enforce, not re-validated here, matching
  // how `AddLayer()`'s own `color`/`AddNamedView()`'s own `camera_up` are
  // handed to OpenNURBS unclamped elsewhere in this file.
  int AddMaterial(const std::string& name, Color diffuse_color = Color(),
                   std::optional<Color> specular_color = std::nullopt,
                   std::optional<Color> emission_color = std::nullopt,
                   std::optional<double> shine = std::nullopt,
                   std::optional<double> transparency = std::nullopt,
                   std::optional<double> reflectivity = std::nullopt);

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
  // above). An empty (default) `name` leaves the name exactly as before -
  // no behavior change for existing callers. A non-empty `name` is set via
  // ON_3dmObjectAttributes::SetName(..., /*bFixInvalidName=*/true), the
  // same call dino8-app/src/io/File3dm.cpp already uses for every other
  // named entity it writes (layers, views, materials, ...) - `true` fixes
  // up characters ON_ModelComponent::IsValidComponentName() would
  // otherwise reject (e.g. a name that's pure whitespace) rather than
  // silently dropping the name or failing outright.
  //
  // `layer_index` defaults to -1, not 0: -1 is OpenNURBS' own sentinel for
  // "no explicit layer" (`ON_Layer::Default`, opennurbs_layer.h, is
  // documented "index = -1, id set, unique and persistent", and
  // `ONX_Model::LayerFromIndex()` falls back to that same built-in Default
  // layer for any index its own layer table doesn't recognize - see
  // `LayerAt()`'s own doc comment below). A real, previously-confirmed
  // defect lived here: `AddLayer()`'s very first call adds to what starts
  // as an *empty* layer table, so it - not any built-in "always-present"
  // layer - claims index 0; an object left at the old default of plain 0
  // therefore silently ended up aliased onto whatever named layer a caller
  // happened to add first, rather than staying on a genuine default layer
  // distinct from every named one (PARITY_MAP.md's own "kernel-level data
  // exchange" evidence named this precisely: "the true OpenNURBS default
  // layer index is -1 ... first AddLayer() call takes index 0"). -1 can
  // never collide with a real index `AddLayer()` returns (always >= 0), so
  // "no layer given" now stays permanently distinguishable from "layer 0"
  // no matter how many named layers get added afterward, in any order.
  // `layer_index` is written straight to ON_3dmObjectAttributes::m_layer_index;
  // passing a non-negative index AddLayer() didn't return is a caller error
  // (as it is for ONX_Model itself), not something this wrapper detects.
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
                int layer_index = -1, std::optional<Color> render_color = std::nullopt,
                const UserStrings& user_strings = UserStrings(),
                std::optional<int> linetype_index = std::nullopt,
                const std::vector<int>& group_indices = std::vector<int>(),
                std::optional<int> material_index = std::nullopt);
  void AddBrep(const Brep& brep, const std::string& name = std::string(), int layer_index = -1,
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
  void AddMesh(const Mesh& mesh, const std::string& name = std::string(), int layer_index = -1,
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
  void AddSubD(const SubD& subd, const std::string& name = std::string(), int layer_index = -1,
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
                     int layer_index = -1, std::optional<Color> render_color = std::nullopt,
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
  // if none have been). Every Add*() method's own `layer_index` parameter
  // defaults to -1, OpenNURBS' own built-in "Default" layer sentinel
  // (see that parameter's own doc comment above) rather than a real entry
  // in this table, so an object left on the default layer is never counted
  // here even when this returns 0.
  int LayerCount() const;

  // Returns the layer at `layer_index` (as returned by AddLayer() above) -
  // the read-side counterpart to AddLayer()'s own `color`/`linetype_index`
  // parameters, same gap ObjectAttributesAt() above closes for objects.
  // `layer_index` not naming a layer this model actually has - including
  // -1, the built-in Default layer's own sentinel index, which never
  // occupies a real slot in this table - returns a default-constructed
  // LayerInfo.
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

  // Adds a named view (Rhino's own "Named Views" panel entry) to the
  // model and returns its index (>= 0) - the last field PARITY_MAP.md's
  // own ".3dm attribute/metadata fidelity" evidence still names as open
  // ("named views, lights, clipping planes, layouts/details" not yet
  // round-tripped) even after materials/groups/user-strings/linetypes/
  // units all closed: a genuinely distinct feature from the "current
  // viewport" state ONX_Model already carries incidentally as part of its
  // settings chunk - the one a Rhino user explicitly creates (View > Named
  // Views > New) and later picks back from a named list, not just
  // whatever the viewport happened to be aimed at on save. Wraps
  // ON_3dmSettings::m_named_views (an ON_ClassArray<ON_3dmView>), the same
  // table Rhino itself reads from/writes to for that panel; `Save()`/
  // `Load()` below already carry this table through a .3dm unmodified as
  // part of the settings chunk ONX_Model::Write/Read already handles -
  // the same "no change to Save()/Load() themselves needed" situation
  // SetUnitSystem() above found for its own settings field - so this is
  // purely new surface area on top of what already round-trips.
  // `camera_up` defaults to (0, 0, 1) (world Z), the "not looking at the
  // ground sideways" convention a caller not supplying one almost
  // certainly wants. The camera's look direction is derived from
  // `target_point - camera_location` rather than taken as its own
  // parameter, since a named view's whole point is "look at this target
  // from here"; if the two points coincide (a degenerate view with no
  // direction to derive), the direction falls back to straight down
  // (0, 0, -1) - an arbitrary but valid, plausible-looking choice, rather
  // than silently leaving the viewport's own unrelated default direction
  // in place. Returns -1 for an empty `name`, same contract as
  // AddLayer()/AddLinetype()/AddGroup()/AddMaterial() above.
  int AddNamedView(const std::string& name, Point3d camera_location, Point3d target_point,
                    Vector3d camera_up = Vector3d(0, 0, 1));

  // Returns the number of named views added via AddNamedView() above.
  int NamedViewCount() const;

  // Returns the named view at `view_index` (as returned by AddNamedView()
  // above) - the read-side counterpart to AddNamedView()'s own
  // parameters, the same read-side gap ObjectAttributesAt()/LayerAt()/
  // LinetypeAt()/GroupNameAt()/MaterialAt() above each closed for their
  // own component tables. `view_index` not naming a named view this model
  // actually has returns a default-constructed NamedViewInfo.
  NamedViewInfo NamedViewAt(int view_index) const;

  // Adds a light (Rhino's own Point/Directional light object) to the model
  // and returns its index (>= 0) among lights specifically - "lights" is
  // the next field PARITY_MAP.md's own ".3dm attribute/metadata fidelity"
  // evidence still names as open after named views closed. A light is a
  // model GEOMETRY object (ON::light_object, ON_Light : public
  // ON_Geometry), added via AddModelGeometryComponent() the same way
  // AddMesh()/AddBrep()/AddCurve() above add their own geometry, and so
  // takes every Add*() parameter those take (name/layer_index/
  // render_color/user_strings/linetype_index/group_indices/
  // material_index) - but OpenNURBS itself files a light under its OWN
  // component type, ON_ModelComponent::Type::RenderLight, not
  // ModelGeometry (ON_ModelGeometryComponent::Geometry()'s own doc
  // comment: "If the geometry is a light, then ComponentType() will
  // return ON_ModelComponent::Type::RenderLight"), so a light does NOT
  // show up in ObjectCount()/ObjectAttributesAt() the way a mesh does -
  // it lives in this own LightCount()/LightAt() table only, the same
  // situation AddLayer()/AddMaterial() above have for their own tables.
  // `style` selects between the two simplest, most common ON::light_style
  // variants (see LightStyle's own doc comment for why spot/linear/
  // rectangular/ambient are out of scope); `location` is used for Point,
  // `direction` for Directional (ON::light_style's own "ignored for [the
  // other]" contract - both are still stored and read back regardless of
  // `style`, so a caller switching styles later doesn't lose the unused
  // one). `diffuse_color` defaults to white and `intensity` to 1.0
  // (full), ON_Light's own constructor defaults. Returns -1 for an empty
  // `name`, same contract as AddLayer()/AddNamedView()/etc. above - needed
  // here specifically because a caller needs this index back to read the
  // light back via LightAt() below.
  int AddLight(const std::string& name, LightStyle style, Point3d location, Vector3d direction,
               Color diffuse_color = Color{255, 255, 255}, double intensity = 1.0,
               int layer_index = -1, std::optional<Color> render_color = std::nullopt,
               const UserStrings& user_strings = UserStrings(),
               std::optional<int> linetype_index = std::nullopt,
               const std::vector<int>& group_indices = std::vector<int>(),
               std::optional<int> material_index = std::nullopt);

  // Returns the number of lights added via AddLight() above.
  int LightCount() const;

  // Returns the light at `light_index` (as returned by AddLight() above) -
  // the read-side counterpart to AddLight()'s own parameters, the same
  // read-side gap ObjectAttributesAt()/LayerAt()/.../NamedViewAt() above
  // each closed for their own tables. `light_index` not naming a light
  // this model actually has returns a default-constructed LightInfo.
  LightInfo LightAt(int light_index) const;

  // Adds a clipping plane (Rhino's own Section/Clipping Plane object,
  // View > Set Clipping Plane) to the model and returns its index (>= 0)
  // among clipping planes specifically - the last field PARITY_MAP.md's
  // own ".3dm attribute/metadata fidelity" evidence still names as open
  // after lights closed above ("lights, clipping planes, layouts/details"
  // - layouts/details alone remains, a materially larger, page-layout-
  // specific problem out of scope here). A model GEOMETRY object like
  // AddLight() above - ON::clipplane_object,
  // ON_ClippingPlaneSurface : public ON_PlaneSurface - sharing every
  // Add*() parameter AddMesh()/AddLight() take, but unlike a light (its
  // own dedicated RenderLight component type - see AddLight()'s own doc
  // comment), a clipping plane is NOT special-cased by OpenNURBS: it
  // lands in the ordinary ON_ModelComponent::Type::ModelGeometry table
  // AddMesh()/AddBrep()/etc. already use, so it DOES also show up in
  // ObjectCount()/ObjectAttributesAt() alongside them, exactly like a
  // mesh does. `origin`/`normal` build the underlying plane via
  // ON_Plane(origin, normal) (that constructor's own documented contract:
  // "zaxis = unitized normal"); returns -1 without adding anything if the
  // resulting plane is not IsValid() (e.g. a zero `normal`), the same
  // "refuse rather than silently write something broken" stance
  // AddNamedView() takes for other degenerate input elsewhere in this
  // file. `enabled` defaults to true, ON_ClippingPlane's own "active
  // clipping plane" default a user creating one in Rhino gets immediately.
  // Returns -1 for an empty `name` for the same reason AddLight() does:
  // this index is needed back for ClippingPlaneAt() below.
  int AddClippingPlane(const std::string& name, Point3d origin, Vector3d normal,
                        bool enabled = true, int layer_index = -1,
                        std::optional<Color> render_color = std::nullopt,
                        const UserStrings& user_strings = UserStrings(),
                        std::optional<int> linetype_index = std::nullopt,
                        const std::vector<int>& group_indices = std::vector<int>(),
                        std::optional<int> material_index = std::nullopt);

  // Returns the number of clipping planes added via AddClippingPlane()
  // above.
  int ClippingPlaneCount() const;

  // Returns the clipping plane at `clipping_plane_index` (as returned by
  // AddClippingPlane() above) - the read-side counterpart to
  // AddClippingPlane()'s own parameters, the same read-side gap every
  // other `*At()` accessor above closes for its own table.
  // `clipping_plane_index` not naming a clipping plane this model actually
  // has returns a default-constructed ClippingPlaneInfo.
  ClippingPlaneInfo ClippingPlaneAt(int clipping_plane_index) const;

  // Sets the model's length unit system - closing PARITY_MAP.md's own
  // "kernel-level data exchange" evidence for "Unit-system conversion":
  // "Kernel Model never sets units." Before this, a Model's coordinates
  // carried no unit system at all from this API's point of view (every
  // .3dm this kernel wrote landed on ONX_Model's own uninspected default,
  // Millimeters, the same "no behavior change for existing callers"
  // baseline GetUnitSystem() below reports for a Model that never called
  // this), the same "app does real unit handling, kernel Model doesn't"
  // gap the app's own Save3dm()/Load3dm() (dino8-app/src/io/File3dm.cpp,
  // lines ~873-997) papers over entirely outside this kernel by setting
  // model.m_settings.m_ModelUnitsAndTolerances.m_unit_system directly on
  // its own local ONX_Model, unreachable from a caller using this Model
  // wrapper instead. Writes to
  // m_settings.m_ModelUnitsAndTolerances.m_unit_system, the same field the
  // app's own code above sets and Save()/Load() below already carry
  // through .3dm unmodified as part of the settings chunk ONX_Model::Write/
  // Read already handles - so no change to Save()/Load() themselves was
  // needed to make this round-trip.
  void SetUnitSystem(UnitSystem units);

  // Returns the model's length unit system - the read-side counterpart to
  // SetUnitSystem() above. A Model that never called SetUnitSystem()
  // reports UnitSystem::Millimeters, matching
  // ON_3dmUnitsAndTolerances's own documented default ("The default
  // constructor set units to millimeters") rather than some other
  // placeholder.
  UnitSystem GetUnitSystem() const;

  // Returns the scale factor that converts a length measured in `from`
  // units into the equivalent length in `to` units - e.g.
  // UnitConversionFactor(UnitSystem::Meters, UnitSystem::Centimeters) is
  // 100.0. Wraps ON::UnitScale(ON::LengthUnitSystem, ON::LengthUnitSystem),
  // the same static utility OpenNURBS itself already exposes for this -
  // closing PARITY_MAP.md's own remaining "Unit-system conversion" gap:
  // SetUnitSystem()/GetUnitSystem() above only let a caller declare what a
  // model's units already ARE, with no way to compute or apply an actual
  // conversion between two different systems. `UnitSystem::None` on either
  // side returns 1.0 (no conversion is meaningful when one side has no
  // unit at all), matching ON::UnitScale()'s own documented contract for
  // ON::LengthUnitSystem::None.
  static double UnitConversionFactor(UnitSystem from, UnitSystem to);

  // Scales every object currently in the model in place - the geometric
  // half of a unit conversion UnitConversionFactor() above only computes
  // the number for - by UnitConversionFactor(GetUnitSystem(), to), then
  // calls SetUnitSystem(to) so the declared unit system and the actual
  // geometry stay consistent (a caller who scaled coordinates but left the
  // declared unit system at its old value would silently produce a
  // mismatched file - the model would claim, say, Centimeters while every
  // coordinate is still sized for Millimeters). Applies a uniform
  // ON_Xform scale about the world origin to each ModelGeometry
  // component's own geometry via its ExclusiveGeometry() pointer (the
  // same "get a pointer that can be used to modify the geometry in place"
  // primitive OpenNURBS itself provides for exactly this) - not about
  // each object's own bounding-box center or any other per-object
  // reference point, since every object shares one common coordinate
  // origin before and after a length-unit change; unlike a design-intent
  // scale operation, a unit conversion has no other natural center.
  // Returns Result::Failed, leaving the model entirely unmodified (checked
  // up front, before touching anything), if `to` or the model's current
  // GetUnitSystem() is UnitSystem::None (no meaningful factor exists) or
  // if either resolves to a non-finite factor; also returns
  // Result::Failed - potentially after already scaling some earlier
  // objects, since this is not transactional - if any individual object's
  // own geometry is shared (ExclusiveGeometry() returning nullptr per its
  // own documented contract) or its own Transform() call fails outright.
  Result ConvertUnits(UnitSystem to);

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
