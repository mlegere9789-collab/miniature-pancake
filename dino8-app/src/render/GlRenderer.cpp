#include "render/GlRenderer.h"

#include <cstring>

namespace dino8::app {

namespace {

const char* kMeshVS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
uniform mat4 u_mvp;
uniform mat4 u_view;
out vec3 v_nrm_view;
void main() {
  gl_Position = u_mvp * vec4(a_pos, 1.0);
  v_nrm_view = mat3(u_view) * a_nrm;
}
)";

const char* kMeshFS = R"(#version 330 core
in vec3 v_nrm_view;
uniform vec4 u_color;
uniform vec3 u_light;
uniform int u_lit;
out vec4 frag;
void main() {
  if (u_lit == 0) { frag = u_color; return; }
  vec3 n = normalize(v_nrm_view);
  vec3 l = normalize(u_light);
  float diffuse = abs(dot(n, l));
  float key = 0.35 + 0.65 * diffuse;
  float rim = pow(1.0 - abs(n.z), 3.0) * 0.12;
  frag = vec4(u_color.rgb * key + rim, u_color.a);
}
)";

const char* kLineVS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_mvp;
uniform float u_size;
void main() {
  gl_Position = u_mvp * vec4(a_pos, 1.0);
  gl_PointSize = u_size;
}
)";

const char* kLineFS = R"(#version 330 core
uniform vec4 u_color;
out vec4 frag;
void main() { frag = u_color; }
)";

const char* kBgVS = R"(#version 330 core
out float v_t;
void main() {
  // Fullscreen triangle from gl_VertexID, no buffer needed.
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  gl_Position = vec4(p, 0.999, 1.0);
  v_t = (p.y + 1.0) * 0.5;
}
)";

const char* kBgFS = R"(#version 330 core
in float v_t;
uniform vec4 u_top;
uniform vec4 u_bottom;
out vec4 frag;
void main() { frag = mix(u_bottom, u_top, clamp(v_t, 0.0, 1.0)); }
)";

GLuint CompileShader(GLenum type, const char* src, std::string& error) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    GLsizei len = 0;
    glGetShaderInfoLog(shader, sizeof(log), &len, log);
    error = std::string("shader compile failed: ") + std::string(log, static_cast<size_t>(len));
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

RenderTarget::~RenderTarget() { Destroy(); }

void RenderTarget::Destroy() {
  if (fbo_) glDeleteFramebuffers(1, &fbo_);
  if (color_tex_) glDeleteTextures(1, &color_tex_);
  if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
  fbo_ = color_tex_ = depth_rb_ = 0;
  width_ = height_ = 0;
}

bool RenderTarget::Resize(int width, int height) {
  if (width <= 0 || height <= 0) return false;
  if (width == width_ && height == height_ && fbo_) return true;
  Destroy();
  width_ = width;
  height_ = height;
  glGenTextures(1, &color_tex_);
  glBindTexture(GL_TEXTURE_2D, color_tex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenRenderbuffers(1, &depth_rb_);
  glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
  glGenFramebuffers(1, &fbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);
  const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  return ok;
}

void RenderTarget::Bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, width_, height_);
}

void RenderTarget::Unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

GLuint GlRenderer::CompileProgram(const char* vs_src, const char* fs_src, std::string& error) {
  GLuint vs = CompileShader(GL_VERTEX_SHADER, vs_src, error);
  if (!vs) return 0;
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fs_src, error);
  if (!fs) {
    glDeleteShader(vs);
    return 0;
  }
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    GLsizei len = 0;
    glGetProgramInfoLog(program, sizeof(log), &len, log);
    error = std::string("program link failed: ") + std::string(log, static_cast<size_t>(len));
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

bool GlRenderer::Init(std::string& error) {
  mesh_program_ = CompileProgram(kMeshVS, kMeshFS, error);
  if (!mesh_program_) return false;
  line_program_ = CompileProgram(kLineVS, kLineFS, error);
  if (!line_program_) return false;
  bg_program_ = CompileProgram(kBgVS, kBgFS, error);
  if (!bg_program_) return false;
  mesh_u_mvp_ = glGetUniformLocation(mesh_program_, "u_mvp");
  mesh_u_view_ = glGetUniformLocation(mesh_program_, "u_view");
  mesh_u_color_ = glGetUniformLocation(mesh_program_, "u_color");
  mesh_u_light_ = glGetUniformLocation(mesh_program_, "u_light");
  mesh_u_lit_ = glGetUniformLocation(mesh_program_, "u_lit");
  line_u_mvp_ = glGetUniformLocation(line_program_, "u_mvp");
  line_u_color_ = glGetUniformLocation(line_program_, "u_color");
  line_u_size_ = glGetUniformLocation(line_program_, "u_size");
  bg_u_top_ = glGetUniformLocation(bg_program_, "u_top");
  bg_u_bottom_ = glGetUniformLocation(bg_program_, "u_bottom");
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenVertexArrays(1, &bg_vao_);
  return true;
}

void GlRenderer::Shutdown() {
  if (mesh_program_) glDeleteProgram(mesh_program_);
  if (line_program_) glDeleteProgram(line_program_);
  if (bg_program_) glDeleteProgram(bg_program_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (bg_vao_) glDeleteVertexArrays(1, &bg_vao_);
  mesh_program_ = line_program_ = bg_program_ = vao_ = vbo_ = bg_vao_ = 0;
}

void GlRenderer::SetMatrices(const Mat4& view, const Mat4& projection) {
  view_ = view;
  proj_ = projection;
}

void GlRenderer::SetLightDirection(kernel::Vector3d d) { light_ = d; }

void GlRenderer::ClearGradient(Color top, Color bottom) {
  glDisable(GL_DEPTH_TEST);
  glClearColor(bottom.r, bottom.g, bottom.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(bg_program_);
  glUniform4f(bg_u_top_, top.r, top.g, top.b, 1.0f);
  glUniform4f(bg_u_bottom_, bottom.r, bottom.g, bottom.b, 1.0f);
  glBindVertexArray(bg_vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glEnable(GL_DEPTH_TEST);
}

void GlRenderer::DrawTriangles(const std::vector<float>& data, Color color, bool lit) {
  if (data.empty()) return;
  const Mat4 mvp = proj_ * view_;
  glUseProgram(mesh_program_);
  glUniformMatrix4fv(mesh_u_mvp_, 1, GL_FALSE, mvp.Data());
  glUniformMatrix4fv(mesh_u_view_, 1, GL_FALSE, view_.Data());
  glUniform4f(mesh_u_color_, color.r, color.g, color.b, color.a);
  glUniform3f(mesh_u_light_, static_cast<float>(light_.x), static_cast<float>(light_.y), static_cast<float>(light_.z));
  glUniform1i(mesh_u_lit_, lit ? 1 : 0);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(data.size() / 6));
  glBindVertexArray(0);
}

void GlRenderer::DrawLines(const std::vector<float>& data, Color color) {
  if (data.empty()) return;
  const Mat4 mvp = proj_ * view_;
  glUseProgram(line_program_);
  glUniformMatrix4fv(line_u_mvp_, 1, GL_FALSE, mvp.Data());
  glUniform4f(line_u_color_, color.r, color.g, color.b, color.a);
  glUniform1f(line_u_size_, 1.0f);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
  glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(data.size() / 3));
  glBindVertexArray(0);
}

void GlRenderer::DrawPoints(const std::vector<float>& data, Color color, float size) {
  if (data.empty()) return;
  const Mat4 mvp = proj_ * view_;
  glEnable(GL_PROGRAM_POINT_SIZE);
  glUseProgram(line_program_);
  glUniformMatrix4fv(line_u_mvp_, 1, GL_FALSE, mvp.Data());
  glUniform4f(line_u_color_, color.r, color.g, color.b, color.a);
  glUniform1f(line_u_size_, size);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
  glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(data.size() / 3));
  glBindVertexArray(0);
}

void GlRenderer::EnableDepthTest(bool on) {
  if (on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

void GlRenderer::EnablePolygonOffset(bool on) {
  if (on) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
  } else {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
}

void GlRenderer::EnableBlend(bool on) {
  if (on) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  } else {
    glDisable(GL_BLEND);
  }
}

}  // namespace dino8::app
