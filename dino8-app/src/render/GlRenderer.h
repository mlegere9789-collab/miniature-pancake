// OpenGL 3.3 core renderer used by every viewport. Renders into a
// per-viewport framebuffer texture that ImGui then displays as an image, so
// viewports dock, float and resize like any other panel.
#pragma once

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

 private:
  void Destroy();
  GLuint fbo_ = 0, color_tex_ = 0, depth_rb_ = 0;
  int width_ = 0, height_ = 0;
};

class GlRenderer {
 public:
  bool Init(std::string& error);
  void Shutdown();

  void SetMatrices(const Mat4& view, const Mat4& projection);
  void SetLightDirection(kernel::Vector3d view_space_direction);

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
  // Lines: x,y,z pairs. `width_px` > 1 draws the set several times with
  // sub-pixel screen offsets (core profiles reject wide glLineWidth).
  void DrawLines(const std::vector<float>& data, Color color, float width_px = 1.0f);
  // Points: x,y,z each, drawn as squares of `size` pixels.
  void DrawPoints(const std::vector<float>& data, Color color, float size);

  void EnableDepthTest(bool on);
  void EnablePolygonOffset(bool on);  // push faces back so edges draw on top
  void EnableBlend(bool on);

 private:
  GLuint CompileProgram(const char* vs, const char* fs, std::string& error);
  // Shared mesh path: `mode` is the shader's u_mode (see kMeshFS).
  enum MeshMode { kLit = 0, kFlat = 1, kZebra = 2, kEMap = 3, kVertexColor = 4 };
  void DrawMesh(const std::vector<float>& data, const std::vector<float>* colors, MeshMode mode, Color color,
                float param0, float param1);
  GLuint mesh_program_ = 0, line_program_ = 0, bg_program_ = 0;
  GLuint vao_ = 0, vbo_ = 0, color_vbo_ = 0, bg_vao_ = 0;
  Mat4 view_ = Mat4::Identity(), proj_ = Mat4::Identity();
  kernel::Vector3d light_{0.3, 0.5, 1.0};
  GLint mesh_u_mvp_ = -1, mesh_u_view_ = -1, mesh_u_color_ = -1, mesh_u_light_ = -1, mesh_u_mode_ = -1,
        mesh_u_params_ = -1, mesh_u_ortho_ = -1;
  GLint line_u_mvp_ = -1, line_u_color_ = -1, line_u_size_ = -1, line_u_offset_ = -1;
  GLint bg_u_top_ = -1, bg_u_bottom_ = -1;
};

}  // namespace dino8::app
