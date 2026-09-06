// CPU path tracer: the "Raytraced" Quality= path for Render / RenderPreview
// / RenderBlowup / BatchRenderNamedViews (src/commands/cmd_raytrace.cpp) and
// the progressive RayTracedViewport display mode (Viewport::Render). Traces
// the same DisplayCache triangles and Document materials/lights the GL
// rasteriser (GlRenderer) shades, over a BVH (Bvh.h), with next-event
// estimation + multiple importance sampling against the document's lights,
// a dielectric glass model (transparency -> IOR 1.5), Russian roulette,
// ACES tonemapping and an edge-aware bilateral denoise pass.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "doc/Document.h"
#include "render/Bvh.h"
#include "viewport/Camera.h"

namespace dino8::app {

struct PathTraceSettings {
  int width = 320, height = 240;
  int samples = 64;
  int bounces = 8;
  bool denoise = true;
  bool arctic = false;  // every material becomes white matte, no lights but the sun/sky
};

class PathTracer {
 public:
  // Gathers the visible objects' display triangles, materials, lights and
  // render settings into the tracer's own scene copy and builds the BVH.
  // `curve_tol`/`surface_tol` control the tessellation fed to EnsureDisplay
  // (finer than the viewport's own, matching Application::RenderView).
  void Prepare(const Document& doc, const CameraState& camera, double aspect, double curve_tol, double surface_tol);

  // Renders `settings.samples` spp in one call and returns a tonemapped,
  // gamma-corrected RGB8 image (width*height*3, top-down rows). Runs
  // hardware_concurrency() worker threads over screen tiles. `progress`,
  // when set, is called after each completed sample pass with the sample
  // index (1-based); returning false cancels the render early (the
  // partial accumulation is still tonemapped and returned).
  std::vector<unsigned char> Render(const PathTraceSettings& settings,
                                    const std::function<bool(int)>& progress = nullptr) const;

  // ---- progressive viewport mode (RayTracedViewport) --------------------
  // Clears the accumulation buffer; call after Prepare() or when the
  // camera/document changed since the last Accumulate().
  void ResetAccumulation(int width, int height);
  int AccumulatedSamples() const { return accum_samples_; }
  // Traces `spp` more samples per pixel into the running accumulation and
  // tonemaps the current average into `out_rgb` (width*height*3, top-down).
  void Accumulate(int spp, int bounces, std::vector<unsigned char>& out_rgb);

  bool Empty() const { return bvh_.Empty() && !ground_.enabled; }

  // A light as the tracer sees it (public so the free helper in
  // PathTracer.cpp that tests a BSDF-sampled ray against area lights for
  // MIS can name the type; not part of the class's real public interface).
  struct SceneLight {
    LightType type;
    kernel::Point3d position;
    kernel::Vector3d direction;   // unit; for area lights the light-plane normal
    kernel::Vector3d x_axis, y_axis;  // rectangular/linear light edges (world units, not unit length)
    float r = 1, g = 1, b = 1;    // colour * intensity, already scaled for the light's kind
    double length = 0, width = 0;
    double cos_outer = -2, cos_inner = -2;  // spot cone (Spot only); < -1 = not a spot
    bool is_sun = false;
  };

 private:
  struct GroundPlane {
    bool enabled = false;
    double z = 0;
    double half_size = 100;
    Color color = Color::FromBytes(180, 180, 180);
    bool shadows = true;
  };

  // Traces one primary ray through the scene and returns its radiance.
  // `rng_state` is advanced in place (xorshift32); `first_albedo`/
  // `first_normal`, when non-null, receive the first-hit surface albedo
  // and shading normal for the denoiser's guide buffers.
  kernel::Vector3d TracePath(kernel::Point3d origin, kernel::Vector3d dir, unsigned& rng_state, int bounces,
                             bool arctic, kernel::Vector3d* first_albedo, kernel::Vector3d* first_normal) const;
  kernel::Vector3d SkyColor(const kernel::Vector3d& dir) const;
  // Direct lighting via NEE with MIS against one uniformly-picked light.
  // `n` is the shading normal (facing the viewer), `wo` the outgoing
  // (toward-eye) direction, both unit.
  kernel::Vector3d SampleDirectLighting(const kernel::Point3d& p, const kernel::Vector3d& n,
                                        const kernel::Vector3d& wo, const Material& mat,
                                        const kernel::Vector3d& albedo, unsigned& rng_state) const;
  bool Occluded(const kernel::Point3d& from, const kernel::Point3d& to) const;
  // Fetches a material's diffuse albedo at a UV, sampling its texture
  // (procedural or file-based, bilinear) when it has one.
  kernel::Vector3d AlbedoAt(const Material& mat, float u, float v) const;

  const Document* doc_ = nullptr;
  render::Bvh bvh_;
  std::vector<Material> materials_;      // deduplicated by name, index matches BvhTriangle::material
  GroundPlane ground_;
  std::vector<SceneLight> lights_;
  RenderSettings render_settings_;
  CameraState camera_;
  double aspect_ = 1.0;

  // Small local texture cache: proc:// specs and file paths -> decoded RGBA + size.
  struct TexCache { int w = 0, h = 0; std::vector<unsigned char> rgba; };
  mutable std::vector<std::pair<std::string, TexCache>> tex_cache_;
  const TexCache* TextureFor(const std::string& path) const;

  // Progressive accumulation state (RayTracedViewport).
  int accum_w_ = 0, accum_h_ = 0, accum_samples_ = 0;
  std::vector<float> accum_;  // running sum of linear radiance, w*h*3
};

}  // namespace dino8::app
