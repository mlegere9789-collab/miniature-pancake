#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// glTF 2.0 (Khronos; `.gltf` JSON with embedded or external buffers, and the binary `.glb` container): the reader and writer written from the
// public specification. The earlier Mesh::SaveGltf / LoadGltf / SaveGlb / LoadGlb only understood files they had written themselves (one mesh,
// bufferViews 0 and 1, float positions and uint32 indices, one embedded buffer); this resolves accessors for real.
//
// Scope, stated up front:
//   read : `.gltf` (embedded base64 `data:` buffers and external `.bin` files next to the .gltf, never outside its directory) and `.glb`
//          (JSON chunk + BIN chunk); accessors with any component type (byte / short / int / float, `normalized`), `byteOffset`, `byteStride`
//          (interleaved attributes); primitives with mode TRIANGLES, TRIANGLE_STRIP and TRIANGLE_FAN (points and lines are skipped), with or
//          without indices; POSITION and NORMAL; several meshes and primitives; scenes and a node hierarchy with `matrix` or translation /
//          rotation / scale, flattened so every triangle primitive of every node that carries a mesh becomes one GltfPart in world space
//          (positions by the node's world matrix, normals by its inverse transpose; a transform with a negative determinant flips the
//          winding); the primitive's material (name, pbrMetallicRoughness factors, emissiveFactor, alphaMode / alphaCutoff, doubleSided) and
//          the node / mesh name.
//   write: one node + mesh + triangle primitive per part (POSITION with min / max as the specification requires, NORMAL, uint32 indices),
//          one material per distinct GltfMaterial, a single scene; `.gltf` with an embedded base64 buffer, `.gltf` with an external `.bin`
//          next to it, or `.glb`.
//   not handled (ignored on read, never written): texture coordinates, textures (a material keeps its factors only), vertex colours,
//          tangents, cameras, lights, animations, skins, morph targets, extensions (a file REQUIRING an extension, `extensionsRequired`, is
//          refused, so Draco / meshopt compressed geometry is not silently misread), sparse accessors (refused), points and lines.
// glTF is in metres and Y-up; `scale` and the axis option convert from / to the caller's units and Z-up.

// A glTF material's factors, exactly as the file has them (colours are LINEAR RGB, not sRGB). The defaults are the specification's.
struct GltfMaterial {
  enum class AlphaMode { Opaque, Mask, Blend };
  std::string name;
  std::array<float, 4> base_color = {1.0f, 1.0f, 1.0f, 1.0f};  // pbrMetallicRoughness.baseColorFactor: linear RGB, alpha = coverage
  float metallic = 1.0f;                                       // pbrMetallicRoughness.metallicFactor
  float roughness = 1.0f;                                      // pbrMetallicRoughness.roughnessFactor
  std::array<float, 3> emissive = {0.0f, 0.0f, 0.0f};          // emissiveFactor, linear RGB
  AlphaMode alpha_mode = AlphaMode::Opaque;                    // alphaMode
  float alpha_cutoff = 0.5f;                                   // alphaCutoff (only written, and only meaningful, for Mask)
  bool double_sided = false;                                   // doubleSided

  bool operator==(const GltfMaterial& o) const {
    return name == o.name && base_color == o.base_color && metallic == o.metallic && roughness == o.roughness && emissive == o.emissive &&
           alpha_mode == o.alpha_mode && alpha_cutoff == o.alpha_cutoff && double_sided == o.double_sided;
  }
  bool operator!=(const GltfMaterial& o) const { return !(*this == o); }
};

struct GltfPart {
  std::string name;
  // Triangles. Read: world space (node transforms applied), then `scale` and the axis conversion; when the primitive has NORMAL, the unit
  // vertex normals are in mesh.raw().m_N (one per vertex, transformed with the node), otherwise m_N is empty. No vertex is welded or split:
  // the mesh has the primitive's own vertex and triangle counts.
  Mesh mesh;
  std::optional<GltfMaterial> material;  // the primitive's material, when it names one
};

struct GltfModel {
  std::vector<GltfPart> parts;
};

// Which NORMAL attribute the writer gives a part.
enum class GltfNormals {
  None,      // no NORMAL (a viewer then shades every triangle flat, as the specification asks of it)
  Computed,  // Mesh::ComputeVertexNormals(): area-weighted over the faces around each vertex, so the mesh's own weld topology decides what is
             // smooth (shared vertices) and what is a hard edge (split vertices)
  Stored,    // the mesh's stored normals (mesh.raw().m_N) where it has a finite, non-zero one for every vertex, else as Computed
};

struct GltfWriteOptions {
  bool binary = false;          // .glb container (one file) instead of .gltf
  bool external_bin = false;    // .gltf only: the buffer goes to "<path without its extension>.bin" next to the .gltf, which names it by a
                                // relative URI, instead of a base64 data URI inside the JSON
  double scale = 1.0;           // multiplies every coordinate (e.g. 0.001 for a millimetre model, since glTF is metres)
  bool z_up_to_y_up = false;    // (x, y, z) -> (x, z, -y), the usual Z-up CAD to Y-up glTF rotation (positions and normals)
  GltfNormals normals = GltfNormals::Computed;
};

struct GltfReadOptions {
  double scale = 1.0;           // multiplies every world coordinate (e.g. 1000 to read metres into a millimetre model)
  bool y_up_to_z_up = false;    // (x, y, z) -> (x, -z, y), the inverse of the writer's rotation (positions and normals)
};

// Writes `model`. Result::Failed (nothing promised on disk) for no parts, a part with no vertices or no triangles, a non-finite coordinate, a
// non-positive or non-finite scale, an out-of-range triangle index, a non-finite material factor, or an unwritable path. A quad face is
// written as its two triangles; no vertex is welded or split, so a part's POSITION count is its mesh's vertex count. Every NORMAL is unit
// length: a vertex no face gives a normal to (unused, or only degenerate faces around it) gets the up direction of the written file.
// Material factors are clamped to 0..1.
Result SaveGltfModel(const std::string& path, const GltfModel& model, const GltfWriteOptions& options = {});

// Reads `path` (.gltf or .glb, told apart by the file's own first bytes) into `model` (left unchanged on failure). Result::Failed for: a missing
// file, malformed JSON or GLB container, a version other than 2.x, `extensionsRequired`, a buffer that cannot be resolved (bad base64, missing
// or unsafe external file, shorter than its byteLength), a bufferView or accessor outside its buffer, an unsupported accessor (sparse, a
// component type or element type a position / normal / index accessor cannot be), a NORMAL accessor whose count differs from POSITION's, an
// index outside the vertex list, a primitive naming a missing material, a node cycle or nesting deeper than 64, or a file with no triangles.
// A malformed material term (wrong type or length) falls back to the specification's default for that term.
Result LoadGltfModel(const std::string& path, GltfModel& model, const GltfReadOptions& options = {});

}  // namespace dino8::kernel
