#pragma once

#include <string>

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
  int AddLayer(const std::string& name, Color color = Color());

  // Every Add*() below takes an optional object `name` and `layer_index`.
  // Before `name` existed, every object this kernel ever put into a Model
  // got a default, empty ON_3dmObjectAttributes - a real, disclosed gap in
  // .3dm metadata fidelity (PARITY_MAP.md's own "kernel-level data
  // exchange" evidence: "write a default ON_3dmObjectAttributes only"): a
  // caller had no way to attach even the most basic identifying metadata
  // .3dm consumers actually rely on (Rhino's own object name, used for
  // selection-by-name, block/part naming, and round-tripping identity
  // across a save/reload; and, now, which layer the object lives on,
  // needed for the same reasons AddLayer() itself exists - see its own
  // doc comment above). An empty (default) `name` and a `layer_index` of 0
  // (the model's always-present default layer, `AddLayer()`'s own doc
  // comment aside) leave the attributes exactly as before - no behavior
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
  void AddCurve(const NurbsCurve& curve, const std::string& name = std::string(),
                int layer_index = 0);
  void AddBrep(const Brep& brep, const std::string& name = std::string(), int layer_index = 0);

  // Adds a mesh (a box, cylinder, boolean result, ...) as its own model
  // object - the missing counterpart to AddCurve()/AddBrep() that closed
  // a real gap: every closed-solid primitive and every BooleanCombine()
  // result here is a Mesh, but until now there was no way to put one into
  // a .3dm file at all, only to export it separately via
  // Mesh::SaveObj()/SaveStl(). Same pattern as the other two: copies
  // `mesh`'s underlying ON_Mesh into a new model geometry component.
  void AddMesh(const Mesh& mesh, const std::string& name = std::string(), int layer_index = 0);

  // Adds a SubD control cage/subdivision surface as its own model
  // object - the same "no way to put this object type into a .3dm at
  // all" gap AddMesh() closed, just for SubD instead of Mesh. Same
  // pattern: copies the SubD's underlying ON_SubD into a new model
  // geometry component.
  void AddSubD(const SubD& subd, const std::string& name = std::string(), int layer_index = 0);

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
                     int layer_index = 0);

  int ObjectCount() const;

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
