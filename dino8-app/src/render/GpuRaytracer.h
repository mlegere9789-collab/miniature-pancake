// Genuine per-frame GPU raytracer for the RayTracedViewport display mode
// (src/viewport/Viewport.cpp). Unlike the CPU PathTracer used for the
// offline Render/RenderPreview/RenderBlowup/BatchRenderNamedViews commands,
// this traces the scene on the GPU every frame: a fragment shader walks a
// BVH uploaded as GL_TEXTURE_BUFFER buffer textures, does one-sample direct
// lighting + one indirect bounce, and blends the result into a temporal
// accumulation texture that a bilateral-denoise pass then presents.
//
// GL 3.3 core has no compute shaders (and macOS never exposes them on its
// GL path), so this is deliberately a fragment-shader raymarcher rather
// than a compute-shader tracer - see gpu_render_notes.md for the honest
// performance/quality gap against a true GPU-hardware-accelerated (RTX/
// Cycles-class) path tracer.
#pragma once

#include <string>
#include <vector>

#include "gl/gl_loader.h"
#include "render/PathTracer.h"
#include "viewport/Camera.h"

namespace dino8::render {

class GpuRaytracer {
 public:
  ~GpuRaytracer() { Shutdown(); }

  bool Init(std::string& error);
  void Shutdown();

  // Rebuilds the GPU-side BVH/material/light buffers from `tracer`'s
  // already-Prepare()d scene (PathTracer::SceneBvh/SceneMaterials/
  // SceneLights) plus the document's background/sun settings for the sky
  // shading term. Call only when the document revision or scene actually
  // changed - it re-uploads the whole BVH.
  void UploadScene(const app::PathTracer& tracer, const app::RenderSettings& settings);

  // Traces one more 1-spp frame at `width` x `height` from `camera`,
  // blends it into the running temporal accumulation (or restarts it when
  // `reset_accum` is true), denoises, and returns a GL_TEXTURE_2D holding
  // the presentable result (0 if the scene is empty or GL objects failed
  // to build). Caller draws it with GlRenderer::DrawFullscreenTexture.
  GLuint Render(const app::Camera& camera, double aspect, int width, int height, bool reset_accum);

  int AccumulatedFrames() const { return accum_frames_; }
  int TriangleCount() const { return tri_count_; }
  bool Empty() const { return tri_count_ == 0; }

  // Max indirect-bounce depth (1 = the original single-bounce behaviour).
  // Clamped to [1, kMaxBounceDepth] in Render(). See gpu_render_notes.md
  // for the measured cost of raising this.
  void SetMaxBounces(int n) { max_bounces_ = n; }
  int MaxBounces() const { return max_bounces_; }

 private:
  bool EnsureTargets(int width, int height);
  bool CompilePrograms(std::string& error);
  void UploadBuffer(GLuint& buf, GLuint& tex, const std::vector<float>& floats);
  void UploadTextureAtlas(const std::vector<app::Material>& mats);

  bool inited_ = false;
  GLuint trace_program_ = 0, denoise_program_ = 0;
  GLuint vao_ = 0;  // empty VAO for the attribute-less fullscreen triangle

  GLuint node_buf_ = 0, node_tex_ = 0;
  GLuint tri_buf_ = 0, tri_tex_ = 0;
  int node_count_ = 0, tri_count_ = 0;

  // Materials/lights, mirrored into flat arrays ready for glUniform*fv.
  std::vector<float> mat_a_, mat_b_, mat_c_;  // (diffuse,gloss) (specular,reflectivity) (emission,transparency)
  std::vector<float> mat_d_;  // (has_texture, atlas_layer, 0, 0)
  int mat_count_ = 0;

  // Material texture atlas: one kTexTileSize x kTexTileSize RGBA8 layer per
  // distinct material texture_path (procedural or file-based), up to
  // kMaxTexLayers - see UploadTextureAtlas() / gpu_render_notes.md for the
  // "why a fixed small atlas, not full-resolution per-material textures"
  // tradeoff.
  static constexpr int kTexTileSize = 64;
  static constexpr int kMaxTexLayers = 16;
  GLuint tex_atlas_ = 0;
  int tex_layers_ = 0;

  // Max indirect-bounce depth (see SetMaxBounces above). kMaxBounceDepth is
  // the shader's fixed unrolled-loop bound.
  static constexpr int kMaxBounceDepth = 3;
  int max_bounces_ = 2;
  std::vector<float> light_a_, light_b_, light_c_;  // (pos,type) (dir,cos_outer) (color,cos_inner)
  int light_count_ = 0;
  int bg_mode_ = 0;
  float bg_top_[3] = {0.5f, 0.6f, 0.8f}, bg_bottom_[3] = {0.85f, 0.85f, 0.85f};
  bool sun_enabled_ = false;
  float sun_dir_[3] = {0, 0, -1}, sun_color_[3] = {1, 1, 1};
  float sun_intensity_ = 1.f;

  // Ping-pong accumulation targets + a shared (non-ping-ponged, rewritten
  // every frame) normal/depth G-buffer for the denoiser's bilateral weights.
  GLuint accum_tex_[2] = {0, 0};
  GLuint gbuf_tex_ = 0;
  GLuint trace_fbo_[2] = {0, 0};
  GLuint present_tex_ = 0, present_fbo_ = 0;
  int tex_w_ = 0, tex_h_ = 0;
  int ping_ = 0;
  int accum_frames_ = 0;
  unsigned frame_seed_ = 1;

  // Cached uniform locations (trace program).
  GLint t_prev_ = -1, t_nodes_ = -1, t_tris_ = -1, t_eye_ = -1, t_fwd_ = -1, t_right_ = -1, t_up_ = -1,
        t_tan_fov_ = -1, t_aspect_ = -1, t_ortho_ = -1, t_ortho_h_ = -1, t_resolution_ = -1, t_seed_ = -1,
        t_alpha_ = -1, t_mat_count_ = -1, t_mat_a_ = -1, t_mat_b_ = -1, t_mat_c_ = -1, t_mat_d_ = -1,
        t_tex_atlas_ = -1, t_max_bounces_ = -1, t_light_count_ = -1,
        t_light_a_ = -1, t_light_b_ = -1, t_light_c_ = -1, t_bg_mode_ = -1, t_bg_top_ = -1, t_bg_bottom_ = -1,
        t_sun_enabled_ = -1, t_sun_dir_ = -1, t_sun_color_ = -1, t_sun_intensity_ = -1;
  // Cached uniform locations (denoise program).
  GLint d_accum_ = -1, d_gbuf_ = -1, d_resolution_ = -1, d_accum_frames_ = -1;
};

}  // namespace dino8::render
