#pragma once

#include <string>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// 3MF (3D Manufacturing Format, ISO/IEC 25422; the public 3MF Core Specification and the OPC / ECMA-376 part 2 package
// conventions): a ZIP package whose model part (normally 3D/3dmodel.model, found through _rels/.rels) is XML holding
// `<object>` meshes (`<vertices>` / `<triangles>`) or assemblies (`<components>` with row-vector 3x4 transforms), and a
// `<build>` list of `<item>`s with their own transform. The earlier Mesh::Save3mf / Load3mf were deliberately narrow (stored
// ZIP entries only, one flat object, no units, no transforms); this is the real reader/writer, written from the public layout
// with zlib (the copy OpenNURBS already links) for deflate.
//
// Scope, stated up front:
//   read : deflated and stored ZIP entries (CRC-checked), the model part located through the package relationships (falling
//          back to 3D/3dmodel.model), core-spec meshes, components (nested, cycle-checked) with transforms, build items with
//          transforms, object names, units. Every build item becomes one ThreeMfPart whose mesh is flattened to world space (a
//          negative-determinant transform flips the winding so the part keeps its outward orientation).
//   write: one object + one build item per part, `unit` from the model, deflate-compressed ZIP (stored when that is smaller).
//   not handled (refused with Result::Failed when it changes geometry, ignored when it is only decoration): external-part
//          components (the production extension's `path`), the materials/colour/texture/slice/beam-lattice extensions (their
//          elements and attributes are ignored, so a coloured file reads as plain geometry), thumbnails, metadata, ZIP64,
//          encrypted entries.

struct ThreeMfPart {
  std::string name;
  Mesh mesh;  // triangles, in the model's unit, in the build's (world) space
};

struct ThreeMfModel {
  // One of the core spec's units: micron, millimeter, centimeter, inch, foot, meter.
  std::string unit = "millimeter";
  std::vector<ThreeMfPart> parts;
};

struct ThreeMfWriteOptions {
  bool compress_entries = true;
};

struct ThreeMfReadOptions {
  // Scale every coordinate to millimetres and report unit "millimeter".
  bool convert_to_millimeters = false;
};

// Millimetres per `unit`, or 0 for a name that is not one of the six core units.
double ThreeMfUnitToMillimeters(const std::string& unit);

// Writes `model`. Result::Failed (nothing promised on disk) for an unknown unit, no parts, a part with no vertices or no
// triangles, a coordinate that is not finite, or an unwritable path. A quad face is written as its two triangles.
Result SaveThreeMf(const std::string& path, const ThreeMfModel& model, const ThreeMfWriteOptions& options = {});

// Reads `path` into `model` (left unchanged on failure). Result::Failed for: a missing file, a broken ZIP (no end record,
// truncated, bad CRC, an entry that does not inflate to its declared size, ZIP64, encryption, a method other than stored or
// deflate), no model part, malformed XML, an unknown unit, a vertex or triangle attribute that is missing / not a number /
// not finite, a triangle index outside its object's vertex list, a build item or component naming an object that does not
// exist, a component cycle (or nesting deeper than 32), an external-part component, a transform that is not 12 finite
// numbers, or a file whose build yields no triangles. A file with no <build> element builds every object that no other
// object uses as a component.
Result LoadThreeMf(const std::string& path, ThreeMfModel& model, const ThreeMfReadOptions& options = {});

}  // namespace dino8::kernel
