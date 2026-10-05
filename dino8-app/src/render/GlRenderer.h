// OpenGL 3.3 core renderer used by every viewport. Renders into a
// per-viewport framebuffer texture that ImGui then displays as an image, so
// viewports dock, float and resize like any other panel.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <array>
#include <string>
#include <vector>

#include "gl/gl_loader.h"
#include "doc/SceneObject.h"
#include "viewport/Camera.h"

namespace dino8::app {

class RenderTarget {
 public:
  ~RenderTarget();
  // (Re)creates the framebuffer if the size changed. Returns false on GL error.
  bool Resize(int width, int height);
  void Bind() const;
  static void Unbind();
  GLuint Texture() const { return color_tex_; }
  int Width() const { return width_; }
  int Height() const { return height_; }
  // Reads the colour buffer as RGB, top-down rows.
  void ReadPixels(std::vector<unsigned char>& rgb) const;

 private:
  void Destroy();
  GLuint fbo_ = 0, color_tex_ = 0, depth_rb_ = 0;
  int width_ = 0, height_ = 0;
};

// A light as the shader sees it (world space; the renderer moves it into
// view space with the current view matrix).
struct GpuLight {
  enum Kind { Point = 0, Directional = 1, Spot = 2 };
  Kind kind = Point;
  kernel::Point3d position{0, 0, 0};   // Point / Spot
  kernel::Vector3d direction{0, 0, -1};  // Spot axis, or the direction the light travels (Directional)
  float r = 1, g = 1, b = 1;           // colour * intensity
  float cos_outer = 0.f, cos_inner = 0.f;
};
constexpr int kMaxGpuLights = 8;

// Blinn-Phong material parameters for DrawTrianglesRendered.
struct RenderMaterial {
  Color diffuse = Color::FromBytes(200, 200, 200);  // alpha = opacity
  Color specular = Color::FromBytes(255, 255, 255);
  float shininess = 32.f;   // Blinn-Phong exponent
  float reflectivity = 0.f;
  Color emission = Color::FromBytes(0, 0, 0);
  GLuint texture = 0;       // 0 = untextured
};

// A soft elliptical contact shadow the ground plane shows under an object.
struct ShadowBlob {
  float cx = 0, cy = 0, rx = 1, ry = 1, strength = 1;
};
constexpr int kMaxShadowBlobs = 16;

class GlRenderer {
 public:
  bool Init(std::string& error);
  void Shutdown();

  void SetMatrices(const Mat4& view, const Mat4& projection);
  void SetLightDirection(kernel::Vector3d view_space_direction);
  // Lights for the Rendered mode (at most kMaxGpuLights are used) plus the
  // ambient term (sky light).
  void SetLights(const std::vector<GpuLight>& lights, Color ambient);

  // Clipping planes (world-space plane equations a,b,c,d; a fragment is
  // kept where a*x+b*y+c*z+d >= 0). Up to kMaxClipPlanes are honoured by
  // every mesh/line/point draw until cleared; the matching
  // GL_CLIP_DISTANCEi states are enabled/disabled here.
  static constexpr int kMaxClipPlanes = 6;
  void SetClipPlanes(const std::vector<std::array<float, 4>>& planes);
  void ClearClipPlanes() { SetClipPlanes({}); }

  void ClearGradient(Color top, Color bottom);
  // Triangles: interleaved x,y,z,nx,ny,nz. `lit` = shaded, otherwise flat color.
  void DrawTriangles(const std::vector<float>& data, Color color, bool lit = true);
  // Triangles with a per-vertex colour (r,g,b per vertex, same vertex count
  // as `data`), lit with the standard key light. `alpha` applies to all.
  void DrawTriangles(const std::vector<float>& data, const std::vector<float>& colors, float alpha = 1.0f);
  // ShowZBuffer: per-vertex colour drawn as-is, with no lighting/shading
  // applied (unlike the DrawTriangles(..., colors, alpha) above, whose
  // kVertexColor mode still multiplies by a diffuse term) - the colour is
  // meant to *be* a depth value, so it must not vary with surface
  // orientation the way a lit colour would.
  void DrawTrianglesDepth(const std::vector<float>& data, const std::vector<float>& colors);
  // Surface-analysis shading, environment-mapped by the view-space
  // reflection vector. Zebra: black/white stripes (`vertical` picks the
  // stripe direction, `density` the stripe frequency). EMap: a procedural
  // chrome sky/ground environment tinted by `tint`.
  void DrawTrianglesZebra(const std::vector<float>& data, bool vertical, float density, float alpha = 1.0f);
  void DrawTrianglesEMap(const std::vector<float>& data, Color tint);
  // Rendered display mode: Blinn-Phong with the lights from SetLights and
  // an optional texture sampled with `uvs` (u,v per vertex; may be null).
  void DrawTrianglesRendered(const std::vector<float>& data, const std::vector<float>* uvs, const RenderMaterial& m);
  // Background::Image's real equirectangular texture (the same GLuint the
  // full-viewport background quad already draws - see Viewport.cpp's
  // DrawBackgroundImage), sampled by a reflective Rendered-mode surface's
  // reflection vector instead of the procedural studio Environment(). 0
  // (the default) keeps the procedural fallback, closing the "interactive
  // rasterizer viewport ... no reflection contribution" gap.
  void SetEnvironmentMap(GLuint tex) { env_map_tex_ = tex; }

  // Real-time shadow mapping for Rendered mode, closing "Real-time shadow
  // maps in the rasterized renderer" (only ground-plane ShadowBlob contact
  // shadows existed before - see DrawGroundPlane above, which is unrelated
  // and still applies independently: object-on-ground, not object-on-
  // object). Every enabled light (up to kMaxGpuLights, the same cap
  // SetLights already enforces) casts its own real shadow from its own
  // depth-only pass into its own layer of a shared depth texture array - a
  // genuine per-light shadow atlas, not a single documented shadow-caster.
  //
  // BeginShadowPass(light_index, ...) binds layer `light_index` of the
  // shadow array FBO, computes an orthographic light-space view/projection
  // framing a sphere of `radius` centred on `center` from `light_dir` (the
  // direction the light travels, INTO the scene - the same convention
  // GpuLight::direction/SetLightDirection use), and clears that layer's
  // depth; every DrawTriangles*/DrawMesh call made before the matching
  // EndShadowPass writes only into that one layer (DrawLines/DrawPoints are
  // no-ops during the pass - only mesh geometry casts a shadow). Returns
  // false (nothing drawn, no shadow for this light this frame) if
  // `light_index` is out of range, `light_dir` has no usable direction, or
  // the FBO fails to build. ClearShadowValidity should be called once per
  // frame before the per-light BeginShadowPass/EndShadowPass loop, so a
  // light that no longer exists this frame doesn't keep shading against a
  // stale map from an earlier frame.
  bool BeginShadowPass(int light_index, kernel::Vector3d light_dir, kernel::Point3d center, double radius);
  void EndShadowPass();
  void ClearShadowValidity() { shadow_valid_mask_ = 0; }
  static constexpr int kShadowMapSize = 1024;

  // Real screen-space ambient occlusion for Rendered mode, closing "SSAO in
  // the rasterized renderer" (no "ssao"/"ambient occlusion" code existed
  // anywhere in this renderer before - see PARITY_MAP.md, app_display).
  // BeginSsaoPass() starts a depth-only prepass of the main camera's own
  // view, sized to whatever FBO/viewport is currently bound (queried with
  // GL_VIEWPORT, the same idiom DrawLines' own thick-line branch already
  // uses - so this works unchanged whether the caller is a docked
  // viewport's own RenderTarget or RenderToImage's supersampled one) and
  // reusing the shadow pass's existing depth-only program/vao - every
  // DrawMesh/DrawTriangles* call made before the matching EndSsaoPass
  // writes only into this pass's own depth texture (DrawLines/DrawPoints
  // are no-ops during it, the same reason they are during
  // BeginShadowPass/EndShadowPass). EndSsaoPass() closes the prepass and
  // immediately computes a real AO buffer from it - view-space position
  // reconstructed per fragment from the depth texture via the projection
  // matrix's own CPU-side inverse, a per-fragment normal taken from that
  // position's own screen-space derivatives (no separate G-buffer normal
  // attachment, the same "Alchemy AO" shortcut several shipped engines
  // use), a 16-sample hemisphere kernel (SsaoKernel.h) rotated by a
  // per-pixel procedural hash, then box-blurred - leaving the result bound
  // for every following kRendered/kGround DrawMesh call this frame to
  // sample inside Shade() (no separate "use it" call needed). ClearSSAO()
  // disables it for the rest of this frame (the default state before the
  // first BeginSsaoPass/EndSsaoPass of a frame): Shade() then samples no
  // AO term at all, exactly as before this feature existed.
  void BeginSsaoPass();
  void EndSsaoPass();
  void ClearSSAO() { ssao_tex_ = 0; }
  // View-space sample radius (world units) and self-occlusion depth bias
  // the next EndSsaoPass computes with; defaults cover a typical few-
  // metre-scale scene reasonably, same as the shadow pass's own fixed
  // kShadowMapSize rather than exposing every knob to a document setting.
  void SetSsaoParams(float radius, float bias) { ssao_radius_ = radius; ssao_bias_ = bias; }
  // Ground plane quad at world height z, centred on (cx, cy) with the
  // given half-size, fading out beyond `fade_radius`, with contact shadows.
  void DrawGroundPlane(double cx, double cy, double z, double half_size, double fade_radius, Color color,
                       const std::vector<ShadowBlob>& blobs);
  // Lines: x,y,z pairs. `width_px` > 1 draws the set several times with
  // sub-pixel screen offsets (core profiles reject wide glLineWidth).
  void DrawLines(const std::vector<float>& data, Color color, float width_px = 1.0f);
  // Points: x,y,z each, drawn as squares of `size` pixels.
  void DrawPoints(const std::vector<float>& data, Color color, float size);

  void EnableDepthTest(bool on);
  void EnableDepthWrite(bool on);
  void EnablePolygonOffset(bool on);  // push faces back so edges draw on top
  void EnableBlend(bool on);

  // Blits a GL texture over the whole current viewport, unlit and
  // unfiltered by depth - used by the RayTracedViewport display mode to
  // present the path tracer's progressive accumulation texture.
  void DrawFullscreenTexture(GLuint texture);

  // Texture cache: loads an image file into a GL texture (0 when the file
  // cannot be read; the failure is remembered until RefreshTextures). A
  // "proc:<kind>:<seed>" path (MaterialLibrary::IsProceduralTexture) is
  // generated at 256x256 instead of read from disk.
  GLuint TextureFor(const std::string& path);
  // Uploads an RGB(A) image as a texture the caller owns (render window).
  GLuint CreateTexture(int width, int height, const unsigned char* rgb, int channels);
  void DeleteTexture(GLuint tex);
  void RefreshTextures();
  const std::set<std::string>& MissingTextures() const { return missing_textures_; }

 private:
  GLuint CompileProgram(const char* vs, const char* fs, std::string& error);
  // Shared mesh path: `mode` is the shader's u_mode (see kMeshFS).
  enum MeshMode { kLit = 0, kFlat = 1, kZebra = 2, kEMap = 3, kVertexColor = 4, kRendered = 5, kGround = 6, kDepthGray = 7 };
  void DrawMesh(const std::vector<float>& data, const std::vector<float>* colors, const std::vector<float>* uvs,
                MeshMode mode, Color color, float param0, float param1);
  void UploadLights();
  GLuint mesh_program_ = 0, line_program_ = 0, bg_program_ = 0, tex_program_ = 0;
  GLint tex_u_sampler_ = -1;
  GLuint vao_ = 0, vbo_ = 0, color_vbo_ = 0, uv_vbo_ = 0, bg_vao_ = 0;
  Mat4 view_ = Mat4::Identity(), proj_ = Mat4::Identity();
  kernel::Vector3d light_{0.3, 0.5, 1.0};
  std::vector<GpuLight> lights_;
  Color ambient_ = Color::FromBytes(40, 42, 46);
  RenderMaterial material_;
  std::vector<ShadowBlob> blobs_;
  float ground_params_[4] = {0, 0, 1, 1};
  GLint mesh_u_mvp_ = -1, mesh_u_view_ = -1, mesh_u_color_ = -1, mesh_u_light_ = -1, mesh_u_mode_ = -1,
        mesh_u_params_ = -1, mesh_u_ortho_ = -1;
  GLint mesh_u_light_count_ = -1, mesh_u_light_pos_ = -1, mesh_u_light_dir_ = -1, mesh_u_light_color_ = -1,
        mesh_u_light_spot_ = -1, mesh_u_ambient_ = -1, mesh_u_specular_ = -1, mesh_u_emission_ = -1,
        mesh_u_reflectivity_ = -1, mesh_u_use_texture_ = -1, mesh_u_texture_ = -1, mesh_u_blob_count_ = -1,
        mesh_u_blobs_ = -1, mesh_u_blob_strength_ = -1, mesh_u_ground_ = -1, mesh_u_env_map_ = -1,
        mesh_u_env_map_valid_ = -1, mesh_u_light_vp_ = -1, mesh_u_shadow_map_ = -1, mesh_u_shadow_valid_mask_ = -1;
  GLint mesh_u_ssao_map_ = -1, mesh_u_ssao_valid_ = -1, mesh_u_viewport_size_ = -1;
  GLuint env_map_tex_ = 0;

  // Shadow-pass state (see BeginShadowPass/EndShadowPass above). One
  // GL_TEXTURE_2D_ARRAY with kMaxGpuLights layers, one layer per light
  // index - a real per-light atlas rather than a single shared map.
  GLuint shadow_fbo_ = 0, shadow_array_tex_ = 0, shadow_program_ = 0;
  GLint shadow_u_light_vp_ = -1;
  bool shadow_pass_ = false;        // true only between BeginShadowPass/EndShadowPass
  uint32_t shadow_valid_mask_ = 0;  // bit i set once light i's shadow layer has been rendered this frame
  Mat4 light_vp_[kMaxGpuLights];    // light index -> that light's own view-projection, read by the fragment shader
  Mat4 current_shadow_vp_ = Mat4::Identity();  // the light_vp_ entry the in-progress shadow pass is writing
  GLuint shadow_prev_fbo_ = 0;
  GLint shadow_prev_viewport_[4] = {0, 0, 0, 0};

  // SSAO state (see BeginSsaoPass/EndSsaoPass above).
  // Depth-only prepass of the main camera's own view (reuses
  // shadow_program_/vao_ - any plain mat4-to-clip-space transform works
  // for a depth-only draw, so no separate shader is needed) into its own
  // 2D depth texture, resized on demand like RenderTarget::Resize.
  GLuint ssao_depth_fbo_ = 0, ssao_depth_tex_ = 0;
  int ssao_depth_w_ = 0, ssao_depth_h_ = 0;
  bool ssao_prepass_ = false;  // true only between BeginSsaoPass/EndSsaoPass
  Mat4 ssao_prepass_mvp_ = Mat4::Identity();
  GLuint ssao_prepass_prev_fbo_ = 0;
  GLint ssao_prepass_prev_viewport_[4] = {0, 0, 0, 0};
  // AO compute (reconstructs view-space position from ssao_depth_tex_ via
  // u_inv_proj, derives a per-fragment normal from that position's own
  // screen-space derivatives, and samples SsaoKernel.h's hemisphere kernel
  // around it) into a single-channel raw buffer, then a 4x4 box blur of it
  // into ssao_tex_ - the one DrawMesh's kRendered/kGround branch binds.
  GLuint ssao_program_ = 0, ssao_fbo_ = 0, ssao_raw_tex_ = 0;
  GLint ssao_u_depth_ = -1, ssao_u_inv_proj_ = -1, ssao_u_proj_ = -1, ssao_u_texel_ = -1, ssao_u_radius_ = -1,
        ssao_u_bias_ = -1, ssao_u_kernel_ = -1;
  GLuint ssao_blur_program_ = 0, ssao_blur_fbo_ = 0, ssao_blur_tex_ = 0;
  GLint ssao_blur_u_tex_ = -1, ssao_blur_u_texel_ = -1;
  // 0 = disabled (ClearSSAO, or before this frame's first EndSsaoPass);
  // otherwise always equal to ssao_blur_tex_ - a separate member (rather
  // than reusing ssao_blur_tex_ itself as the enabled flag) so ClearSSAO
  // can disable sampling for the rest of a frame without losing track of,
  // and leaking, the persistent GL texture object across frames.
  GLuint ssao_tex_ = 0;
  float ssao_radius_ = 1.0f, ssao_bias_ = 0.015f;
  static constexpr int kSsaoKernelSize = 16;
  std::array<float, kSsaoKernelSize * 3> ssao_kernel_{};

  GLint line_u_mvp_ = -1, line_u_color_ = -1, line_u_size_ = -1, line_u_offset_ = -1;
  GLint mesh_u_clip_[kMaxClipPlanes] = {-1, -1, -1, -1, -1, -1}, mesh_u_clip_count_ = -1;
  GLint line_u_clip_[kMaxClipPlanes] = {-1, -1, -1, -1, -1, -1}, line_u_clip_count_ = -1;
  std::vector<std::array<float, 4>> clip_planes_;
  void ApplyClipUniforms(const GLint* locations, GLint count_location);
  GLint bg_u_top_ = -1, bg_u_bottom_ = -1;
  std::map<std::string, GLuint> textures_;
  std::set<std::string> missing_textures_;
};

}  // namespace dino8::app
