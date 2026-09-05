// OpenGL 3.3 core renderer used by every viewport. Renders into a
// per-viewport framebuffer texture that ImGui then displays as an image, so
// viewports dock, float and resize like any other panel.
#pragma once

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
  // Surface-analysis shading, environment-mapped by the view-space
  // reflection vector. Zebra: black/white stripes (`vertical` picks the
  // stripe direction, `density` the stripe frequency). EMap: a procedural
  // chrome sky/ground environment tinted by `tint`.
  void DrawTrianglesZebra(const std::vector<float>& data, bool vertical, float density, float alpha = 1.0f);
  void DrawTrianglesEMap(const std::vector<float>& data, Color tint);
  // Rendered display mode: Blinn-Phong with the lights from SetLights and
  // an optional texture sampled with `uvs` (u,v per vertex; may be null).
  void DrawTrianglesRendered(const std::vector<float>& data, const std::vector<float>* uvs, const RenderMaterial& m);
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

  // Texture cache: loads an image file into a GL texture (0 when the file
  // cannot be read; the failure is remembered until RefreshTextures).
  GLuint TextureFor(const std::string& path);
  // Uploads an RGB(A) image as a texture the caller owns (render window).
  GLuint CreateTexture(int width, int height, const unsigned char* rgb, int channels);
  void DeleteTexture(GLuint tex);
  void RefreshTextures();
  const std::set<std::string>& MissingTextures() const { return missing_textures_; }

 private:
  GLuint CompileProgram(const char* vs, const char* fs, std::string& error);
  // Shared mesh path: `mode` is the shader's u_mode (see kMeshFS).
  enum MeshMode { kLit = 0, kFlat = 1, kZebra = 2, kEMap = 3, kVertexColor = 4, kRendered = 5, kGround = 6 };
  void DrawMesh(const std::vector<float>& data, const std::vector<float>* colors, const std::vector<float>* uvs,
                MeshMode mode, Color color, float param0, float param1);
  void UploadLights();
  GLuint mesh_program_ = 0, line_program_ = 0, bg_program_ = 0;
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
        mesh_u_blobs_ = -1, mesh_u_blob_strength_ = -1, mesh_u_ground_ = -1;
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
