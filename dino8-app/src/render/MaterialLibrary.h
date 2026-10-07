// A built-in library of 40+ physically-plausible material presets (the
// "material library" the doc4 blueprint promised, AUDIT.md row 30) plus
// runtime-generated procedural textures ("proc:wood:1", "proc:marble:2",
// ...) that need no files on disk. Presets set the existing Document
// Material fields (diffuse/gloss/reflectivity/transparency/emission) so
// both the GL rasteriser and the path tracer (PathTracer.h) shade them the
// same way; the procedural spec, when set as a material's texture_path, is
// handled by GlRenderer::TextureFor (GL preview) and PathTracer (ray hits)
// identically since both call GenerateProceduralTexture for a "proc:" path.
#pragma once

#include <string>
#include <vector>

#include "doc/Document.h"

namespace dino8::app {

struct MaterialPreset {
  std::string name;
  Material material;  // diffuse/specular/gloss/reflectivity/transparency/emission
};

// All built-in presets, in a fixed display order (metals, plastics, glass,
// wood, stone, paint, fabric, rubber, emissive).
const std::vector<MaterialPreset>& MaterialPresets();
// Case-insensitive lookup by preset name ("Gold", "gold", "GOLD" all match).
const MaterialPreset* FindMaterialPreset(const std::string& name);

// True for a texture_path this module generates itself at render time
// instead of loading from disk ("proc:<kind>:<seed>").
bool IsProceduralTexture(const std::string& path);

// Renders a procedural texture spec into a `w` x `h` RGBA image (top-down
// rows, 4 bytes/pixel, alpha always 255). Returns false when `spec` is not
// a recognised "proc:" spec, leaving `rgba` untouched. Deterministic in
// `w`/`h`/`spec` so repeated calls (GL cache miss, path-tracer texture
// cache miss) produce the same pixels.
bool GenerateProceduralTexture(const std::string& spec, int w, int h, std::vector<unsigned char>& rgba);

}  // namespace dino8::app
