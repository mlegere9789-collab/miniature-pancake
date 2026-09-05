#include "render/GlRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "render/ImageIO.h"

namespace dino8::app {

namespace {

const char* kMeshVS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec3 a_col;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_mvp;
uniform mat4 u_view;
out vec3 v_nrm_view;
out vec3 v_nrm_world;
out vec3 v_pos_view;
out vec3 v_pos_world;
out vec3 v_col;
out vec2 v_uv;
void main() {
  gl_Position = u_mvp * vec4(a_pos, 1.0);
  v_nrm_view = mat3(u_view) * a_nrm;
  v_nrm_world = a_nrm;
  v_pos_view = (u_view * vec4(a_pos, 1.0)).xyz;
  v_pos_world = a_pos;
  v_col = a_col;
  v_uv = a_uv;
}
)";

// u_mode: 0 lit, 1 flat, 2 zebra, 3 environment map, 4 per-vertex colour,
//         5 rendered (Blinn-Phong, lights, texture), 6 ground plane.
// u_params: zebra = (vertical ? 1 : 0, density); others unused.
const char* kMeshFS = R"(#version 330 core
in vec3 v_nrm_view;
in vec3 v_nrm_world;
in vec3 v_pos_view;
in vec3 v_pos_world;
in vec3 v_col;
in vec2 v_uv;
uniform vec4 u_color;
uniform vec3 u_light;
uniform int u_mode;
uniform vec2 u_params;
uniform int u_ortho;
// Rendered mode.
const int MAX_LIGHTS = 8;
uniform int u_light_count;
uniform vec4 u_light_pos[MAX_LIGHTS];    // xyz view space; w = 0: xyz is the direction towards a directional light
uniform vec3 u_light_dir[MAX_LIGHTS];    // spot axis, view space, from the light into the scene
uniform vec3 u_light_color[MAX_LIGHTS];  // colour * intensity
uniform vec2 u_light_spot[MAX_LIGHTS];   // (cos outer, cos inner); x < -1.5 = not a spot
uniform vec3 u_ambient;
uniform vec4 u_specular;                 // rgb + shininess
uniform vec3 u_emission;
uniform float u_reflectivity;
uniform int u_use_texture;
uniform sampler2D u_texture;
// Ground plane.
const int MAX_BLOBS = 16;
uniform int u_blob_count;
uniform vec4 u_blobs[MAX_BLOBS];         // cx, cy, rx, ry (world)
uniform float u_blob_strength[MAX_BLOBS];
uniform vec4 u_ground;                   // cx, cy, fade radius, unused
out vec4 frag;

vec3 Environment(vec3 r) {
  // Procedural studio environment in view space: blue sky above a warm
  // horizon, neutral ground below, a soft sun and two long light banks so
  // curvature shows up as moving highlights like a real chrome ball.
  float up = r.y;
  vec3 sky = mix(vec3(0.80, 0.86, 0.94), vec3(0.20, 0.42, 0.78), clamp(up, 0.0, 1.0));
  vec3 ground = mix(vec3(0.70, 0.66, 0.60), vec3(0.16, 0.15, 0.14), clamp(-up, 0.0, 1.0));
  vec3 env = up >= 0.0 ? sky : ground;
  float horizon = exp(-abs(up) * 12.0);
  env = mix(env, vec3(0.95, 0.88, 0.75), horizon * 0.6);
  vec3 sun = normalize(vec3(0.45, 0.65, 0.6));
  env += vec3(1.0, 0.95, 0.85) * pow(max(dot(r, sun), 0.0), 60.0) * 1.2;
  float bank1 = smoothstep(0.02, 0.0, abs(up - 0.55) - 0.06);
  float bank2 = smoothstep(0.02, 0.0, abs(up + 0.35) - 0.04);
  env += vec3(0.9) * (bank1 * 0.5 + bank2 * 0.25);
  return env;
}

// Blinn-Phong over the light list; `n` faces the eye, `view_dir` points
// from the eye into the scene.
vec3 Shade(vec3 base, vec3 n, vec3 view_dir) {
  vec3 V = -view_dir;
  // Hemispherical sky light: brighter on up-facing surfaces.
  float up = clamp(normalize(v_nrm_world).z * 0.5 + 0.5, 0.0, 1.0);
  vec3 color = base * u_ambient * mix(0.55, 1.0, up);
  for (int i = 0; i < u_light_count; ++i) {
    vec3 L = (u_light_pos[i].w < 0.5) ? normalize(u_light_pos[i].xyz) : normalize(u_light_pos[i].xyz - v_pos_view);
    float spot = 1.0;
    if (u_light_spot[i].x > -1.5) spot = smoothstep(u_light_spot[i].x, u_light_spot[i].y, dot(-L, u_light_dir[i]));
    float nd = max(dot(n, L), 0.0);
    vec3 H = normalize(L + V);
    float sp = (nd > 0.0) ? pow(max(dot(n, H), 0.0), u_specular.w) : 0.0;
    color += (base * nd + u_specular.rgb * sp * (0.04 + 0.96 * min(1.0, u_specular.w / 64.0))) * u_light_color[i] * spot;
  }
  if (u_reflectivity > 0.0) {
    vec3 env = Environment(reflect(view_dir, n));
    float fresnel = 0.6 + 0.4 * pow(1.0 - abs(dot(n, V)), 3.0);
    color = mix(color, env * mix(vec3(1.0), base, 0.5), u_reflectivity * fresnel);
  }
  color += u_emission;
  // Soft shoulder above 0.8 so several lights add up without clipping to
  // flat white (linear below, exponential roll-off above).
  vec3 hi = step(vec3(0.8), color);
  return mix(color, 0.8 + 0.2 * (1.0 - exp(-(color - 0.8) / 0.2)), hi);
}

void main() {
  if (u_mode == 1) { frag = u_color; return; }
  vec3 n = normalize(v_nrm_view);
  vec3 view_dir = (u_ortho == 1) ? vec3(0.0, 0.0, -1.0) : normalize(v_pos_view);
  // Back faces: flip the normal towards the eye so both sides shade alike.
  if (dot(n, view_dir) > 0.0) n = -n;
  if (u_mode == 2) {
    vec3 r = reflect(view_dir, n);
    float coord = (u_params.x > 0.5) ? r.x : r.y;
    float f = abs(fract(coord * u_params.y) - 0.5) * 2.0;  // triangle wave, seamless
    float w = fwidth(f);
    float stripe = smoothstep(0.5 - w, 0.5 + w, f);
    frag = vec4(mix(vec3(0.04), vec3(0.97), stripe), u_color.a);
    return;
  }
  if (u_mode == 3) {
    vec3 r = reflect(view_dir, n);
    vec3 env = Environment(r);
    float fresnel = 0.55 + 0.45 * pow(1.0 - abs(dot(n, view_dir)), 2.0);
    frag = vec4(env * u_color.rgb * fresnel, u_color.a);
    return;
  }
  if (u_mode == 5) {
    vec3 base = u_color.rgb;
    float alpha = u_color.a;
    if (u_use_texture == 1) {
      vec4 t = texture(u_texture, v_uv);
      base *= t.rgb;
      alpha *= t.a;
    }
    frag = vec4(Shade(base, n, view_dir), alpha);
    return;
  }
  if (u_mode == 6) {
    vec3 color = Shade(u_color.rgb, n, view_dir);
    float shadow = 0.0;
    for (int i = 0; i < u_blob_count; ++i) {
      vec2 d = (v_pos_world.xy - u_blobs[i].xy) / max(u_blobs[i].zw, vec2(1e-6));
      float r = length(d);
      shadow = max(shadow, (1.0 - smoothstep(0.45, 1.35, r)) * u_blob_strength[i]);
    }
    color *= 1.0 - 0.62 * shadow;
    float dist = length(v_pos_world.xy - u_ground.xy);
    float alpha = u_color.a * (1.0 - smoothstep(u_ground.z * 0.45, u_ground.z, dist));
    frag = vec4(color, alpha);
    return;
  }
  vec3 l = normalize(u_light);
  float diffuse = abs(dot(n, l));
  float key = 0.35 + 0.65 * diffuse;
  float rim = pow(1.0 - abs(n.z), 3.0) * 0.12;
  if (u_mode == 4) { frag = vec4(v_col * (0.55 + 0.45 * diffuse), u_color.a); return; }
  frag = vec4(u_color.rgb * key + rim, u_color.a);
}
)";

const char* kLineVS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_mvp;
uniform float u_size;
uniform vec2 u_offset;  // screen-space offset in NDC units (thick lines)
void main() {
  vec4 p = u_mvp * vec4(a_pos, 1.0);
  p.xy += u_offset * p.w;
  gl_Position = p;
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

// Uniform array locations: "name[0]" is the portable spelling.
GLint ArrayLocation(GLuint program, const char* name) {
  const std::string first = std::string(name) + "[0]";
  GLint loc = glGetUniformLocation(program, first.c_str());
  if (loc < 0) loc = glGetUniformLocation(program, name);
  return loc;
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

void RenderTarget::ReadPixels(std::vector<unsigned char>& rgb) const {
  rgb.assign(static_cast<size_t>(width_) * height_ * 3, 0);
  if (!fbo_) return;
  std::vector<unsigned char> bottom_up(rgb.size());
  Bind();
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, bottom_up.data());
  Unbind();
  const size_t row = static_cast<size_t>(width_) * 3;
  for (int y = 0; y < height_; ++y) {
    std::memcpy(&rgb[static_cast<size_t>(y) * row], &bottom_up[static_cast<size_t>(height_ - 1 - y) * row], row);
  }
}

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
  mesh_u_mode_ = glGetUniformLocation(mesh_program_, "u_mode");
  mesh_u_params_ = glGetUniformLocation(mesh_program_, "u_params");
  mesh_u_ortho_ = glGetUniformLocation(mesh_program_, "u_ortho");
  mesh_u_light_count_ = glGetUniformLocation(mesh_program_, "u_light_count");
  mesh_u_light_pos_ = ArrayLocation(mesh_program_, "u_light_pos");
  mesh_u_light_dir_ = ArrayLocation(mesh_program_, "u_light_dir");
  mesh_u_light_color_ = ArrayLocation(mesh_program_, "u_light_color");
  mesh_u_light_spot_ = ArrayLocation(mesh_program_, "u_light_spot");
  mesh_u_ambient_ = glGetUniformLocation(mesh_program_, "u_ambient");
  mesh_u_specular_ = glGetUniformLocation(mesh_program_, "u_specular");
  mesh_u_emission_ = glGetUniformLocation(mesh_program_, "u_emission");
  mesh_u_reflectivity_ = glGetUniformLocation(mesh_program_, "u_reflectivity");
  mesh_u_use_texture_ = glGetUniformLocation(mesh_program_, "u_use_texture");
  mesh_u_texture_ = glGetUniformLocation(mesh_program_, "u_texture");
  mesh_u_blob_count_ = glGetUniformLocation(mesh_program_, "u_blob_count");
  mesh_u_blobs_ = ArrayLocation(mesh_program_, "u_blobs");
  mesh_u_blob_strength_ = ArrayLocation(mesh_program_, "u_blob_strength");
  mesh_u_ground_ = glGetUniformLocation(mesh_program_, "u_ground");
  line_u_mvp_ = glGetUniformLocation(line_program_, "u_mvp");
  line_u_color_ = glGetUniformLocation(line_program_, "u_color");
  line_u_size_ = glGetUniformLocation(line_program_, "u_size");
  line_u_offset_ = glGetUniformLocation(line_program_, "u_offset");
  bg_u_top_ = glGetUniformLocation(bg_program_, "u_top");
  bg_u_bottom_ = glGetUniformLocation(bg_program_, "u_bottom");
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &color_vbo_);
  glGenBuffers(1, &uv_vbo_);
  glGenVertexArrays(1, &bg_vao_);
  return true;
}

void GlRenderer::Shutdown() {
  if (mesh_program_) glDeleteProgram(mesh_program_);
  if (line_program_) glDeleteProgram(line_program_);
  if (bg_program_) glDeleteProgram(bg_program_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (color_vbo_) glDeleteBuffers(1, &color_vbo_);
  if (uv_vbo_) glDeleteBuffers(1, &uv_vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (bg_vao_) glDeleteVertexArrays(1, &bg_vao_);
  for (auto& [path, tex] : textures_) if (tex) glDeleteTextures(1, &tex);
  textures_.clear();
  missing_textures_.clear();
  mesh_program_ = line_program_ = bg_program_ = vao_ = vbo_ = color_vbo_ = uv_vbo_ = bg_vao_ = 0;
}

void GlRenderer::SetMatrices(const Mat4& view, const Mat4& projection) {
  view_ = view;
  proj_ = projection;
}

void GlRenderer::SetLightDirection(kernel::Vector3d d) { light_ = d; }

void GlRenderer::SetLights(const std::vector<GpuLight>& lights, Color ambient) {
  lights_ = lights;
  if (lights_.size() > static_cast<size_t>(kMaxGpuLights)) lights_.resize(static_cast<size_t>(kMaxGpuLights));
  ambient_ = ambient;
}

void GlRenderer::UploadLights() {
  // Transform every light into view space with the current view matrix
  // (column-major: m[col*4+row]).
  const float* v = view_.Data();
  auto xform_point = [&](kernel::Point3d p, float* out) {
    out[0] = static_cast<float>(v[0] * p.x + v[4] * p.y + v[8] * p.z + v[12]);
    out[1] = static_cast<float>(v[1] * p.x + v[5] * p.y + v[9] * p.z + v[13]);
    out[2] = static_cast<float>(v[2] * p.x + v[6] * p.y + v[10] * p.z + v[14]);
  };
  auto xform_dir = [&](kernel::Vector3d d, float* out) {
    out[0] = static_cast<float>(v[0] * d.x + v[4] * d.y + v[8] * d.z);
    out[1] = static_cast<float>(v[1] * d.x + v[5] * d.y + v[9] * d.z);
    out[2] = static_cast<float>(v[2] * d.x + v[6] * d.y + v[10] * d.z);
  };
  float pos[kMaxGpuLights * 4] = {}, dir[kMaxGpuLights * 3] = {}, col[kMaxGpuLights * 3] = {}, spot[kMaxGpuLights * 2] = {};
  const int n = static_cast<int>(lights_.size());
  for (int i = 0; i < n; ++i) {
    const GpuLight& L = lights_[static_cast<size_t>(i)];
    if (L.kind == GpuLight::Directional) {
      kernel::Vector3d towards = -L.direction;
      towards.Unitize();
      xform_dir(towards, &pos[i * 4]);
      pos[i * 4 + 3] = 0.f;
    } else {
      xform_point(L.position, &pos[i * 4]);
      pos[i * 4 + 3] = 1.f;
    }
    kernel::Vector3d axis = L.direction;
    if (!axis.Unitize()) axis = kernel::Vector3d(0, 0, -1);
    xform_dir(axis, &dir[i * 3]);
    col[i * 3] = L.r; col[i * 3 + 1] = L.g; col[i * 3 + 2] = L.b;
    spot[i * 2] = L.kind == GpuLight::Spot ? L.cos_outer : -2.f;
    spot[i * 2 + 1] = L.kind == GpuLight::Spot ? L.cos_inner : -2.f;
  }
  glUniform1i(mesh_u_light_count_, n);
  if (n > 0) {
    glUniform4fv(mesh_u_light_pos_, n, pos);
    glUniform3fv(mesh_u_light_dir_, n, dir);
    glUniform3fv(mesh_u_light_color_, n, col);
    glUniform2fv(mesh_u_light_spot_, n, spot);
  }
  glUniform3f(mesh_u_ambient_, ambient_.r, ambient_.g, ambient_.b);
}

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

void GlRenderer::DrawMesh(const std::vector<float>& data, const std::vector<float>* colors,
                          const std::vector<float>* uvs, MeshMode mode, Color color, float param0, float param1) {
  if (data.empty()) return;
  const GLsizei vertex_count = static_cast<GLsizei>(data.size() / 6);
  const bool use_colors = mode == kVertexColor && colors && colors->size() >= static_cast<size_t>(vertex_count) * 3;
  if (mode == kVertexColor && !use_colors) mode = kLit;
  const bool use_uvs = mode == kRendered && uvs && uvs->size() >= static_cast<size_t>(vertex_count) * 2 && material_.texture != 0;
  const Mat4 mvp = proj_ * view_;
  // An orthographic projection has w == 1 for every vertex (m[15] == 1);
  // perspective leaves m[15] == 0. The shader uses this to pick the view
  // direction for reflections.
  const bool ortho = proj_.m[15] == 1.0f;
  glUseProgram(mesh_program_);
  glUniformMatrix4fv(mesh_u_mvp_, 1, GL_FALSE, mvp.Data());
  glUniformMatrix4fv(mesh_u_view_, 1, GL_FALSE, view_.Data());
  glUniform4f(mesh_u_color_, color.r, color.g, color.b, color.a);
  glUniform3f(mesh_u_light_, static_cast<float>(light_.x), static_cast<float>(light_.y), static_cast<float>(light_.z));
  glUniform1i(mesh_u_mode_, static_cast<int>(mode));
  glUniform2f(mesh_u_params_, param0, param1);
  glUniform1i(mesh_u_ortho_, ortho ? 1 : 0);
  if (mode == kRendered || mode == kGround) {
    UploadLights();
    glUniform4f(mesh_u_specular_, material_.specular.r, material_.specular.g, material_.specular.b, material_.shininess);
    glUniform3f(mesh_u_emission_, material_.emission.r, material_.emission.g, material_.emission.b);
    glUniform1f(mesh_u_reflectivity_, material_.reflectivity);
    glUniform1i(mesh_u_use_texture_, use_uvs ? 1 : 0);
    glUniform1i(mesh_u_texture_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, use_uvs ? material_.texture : 0);
    if (mode == kGround) {
      float blobs[kMaxShadowBlobs * 4] = {}, strength[kMaxShadowBlobs] = {};
      const int nb = static_cast<int>(std::min(blobs_.size(), static_cast<size_t>(kMaxShadowBlobs)));
      for (int i = 0; i < nb; ++i) {
        blobs[i * 4] = blobs_[static_cast<size_t>(i)].cx; blobs[i * 4 + 1] = blobs_[static_cast<size_t>(i)].cy;
        blobs[i * 4 + 2] = blobs_[static_cast<size_t>(i)].rx; blobs[i * 4 + 3] = blobs_[static_cast<size_t>(i)].ry;
        strength[i] = blobs_[static_cast<size_t>(i)].strength;
      }
      glUniform1i(mesh_u_blob_count_, nb);
      if (nb > 0) {
        glUniform4fv(mesh_u_blobs_, nb, blobs);
        glUniform1fv(mesh_u_blob_strength_, nb, strength);
      }
      glUniform4f(mesh_u_ground_, ground_params_[0], ground_params_[1], ground_params_[2], ground_params_[3]);
    }
  }
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
  if (use_colors) {
    glBindBuffer(GL_ARRAY_BUFFER, color_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(static_cast<size_t>(vertex_count) * 3 * sizeof(float)),
                 colors->data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
  } else {
    glDisableVertexAttribArray(2);
  }
  if (use_uvs) {
    glBindBuffer(GL_ARRAY_BUFFER, uv_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(static_cast<size_t>(vertex_count) * 2 * sizeof(float)),
                 uvs->data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void*>(0));
  } else {
    glDisableVertexAttribArray(3);
  }
  glDrawArrays(GL_TRIANGLES, 0, vertex_count);
  if (use_colors) glDisableVertexAttribArray(2);
  if (use_uvs) glDisableVertexAttribArray(3);
  glBindVertexArray(0);
  if (mode == kRendered || mode == kGround) glBindTexture(GL_TEXTURE_2D, 0);
}

void GlRenderer::DrawTriangles(const std::vector<float>& data, Color color, bool lit) {
  DrawMesh(data, nullptr, nullptr, lit ? kLit : kFlat, color, 0.f, 0.f);
}

void GlRenderer::DrawTriangles(const std::vector<float>& data, const std::vector<float>& colors, float alpha) {
  DrawMesh(data, &colors, nullptr, kVertexColor, Color{1.f, 1.f, 1.f, alpha}, 0.f, 0.f);
}

void GlRenderer::DrawTrianglesZebra(const std::vector<float>& data, bool vertical, float density, float alpha) {
  DrawMesh(data, nullptr, nullptr, kZebra, Color{1.f, 1.f, 1.f, alpha}, vertical ? 1.f : 0.f, density);
}

void GlRenderer::DrawTrianglesEMap(const std::vector<float>& data, Color tint) {
  DrawMesh(data, nullptr, nullptr, kEMap, tint, 0.f, 0.f);
}

void GlRenderer::DrawTrianglesRendered(const std::vector<float>& data, const std::vector<float>* uvs,
                                       const RenderMaterial& m) {
  material_ = m;
  DrawMesh(data, nullptr, uvs, kRendered, m.diffuse, 0.f, 0.f);
}

void GlRenderer::DrawGroundPlane(double cx, double cy, double z, double half_size, double fade_radius, Color color,
                                 const std::vector<ShadowBlob>& blobs) {
  blobs_ = blobs;
  ground_params_[0] = static_cast<float>(cx);
  ground_params_[1] = static_cast<float>(cy);
  ground_params_[2] = static_cast<float>(fade_radius);
  ground_params_[3] = 0.f;
  material_ = RenderMaterial{};
  material_.diffuse = color;
  material_.specular = Color::FromBytes(60, 60, 60);
  material_.shininess = 8.f;
  material_.texture = 0;
  // Subdivide the quad so per-vertex interpolation cannot flatten the
  // fade or the shadows on huge triangles (the shader works per fragment,
  // but a few cells keep the depth precision reasonable near the model).
  const int cells = 8;
  std::vector<float> tri;
  tri.reserve(static_cast<size_t>(cells) * cells * 36);
  auto push = [&](double x, double y) {
    tri.push_back(static_cast<float>(x)); tri.push_back(static_cast<float>(y)); tri.push_back(static_cast<float>(z));
    tri.push_back(0.f); tri.push_back(0.f); tri.push_back(1.f);
  };
  for (int i = 0; i < cells; ++i) {
    for (int j = 0; j < cells; ++j) {
      const double x0 = cx - half_size + 2 * half_size * i / cells, x1 = cx - half_size + 2 * half_size * (i + 1) / cells;
      const double y0 = cy - half_size + 2 * half_size * j / cells, y1 = cy - half_size + 2 * half_size * (j + 1) / cells;
      push(x0, y0); push(x1, y0); push(x1, y1);
      push(x0, y0); push(x1, y1); push(x0, y1);
    }
  }
  DrawMesh(tri, nullptr, nullptr, kGround, color, 0.f, 0.f);
}

void GlRenderer::DrawLines(const std::vector<float>& data, Color color, float width_px) {
  if (data.empty()) return;
  const Mat4 mvp = proj_ * view_;
  glUseProgram(line_program_);
  glUniformMatrix4fv(line_u_mvp_, 1, GL_FALSE, mvp.Data());
  glUniform4f(line_u_color_, color.r, color.g, color.b, color.a);
  glUniform1f(line_u_size_, 1.0f);
  glUniform2f(line_u_offset_, 0.f, 0.f);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glDisableVertexAttribArray(3);
  const GLsizei count = static_cast<GLsizei>(data.size() / 3);
  if (width_px <= 1.5f) {
    glDrawArrays(GL_LINES, 0, count);
  } else {
    // Thick lines: stamp the set on a small pixel grid around the origin.
    GLint vp[4] = {0, 0, 1, 1};
    glGetIntegerv(GL_VIEWPORT, vp);
    const float px = 2.0f / static_cast<float>(std::max(vp[2], 1));
    const float py = 2.0f / static_cast<float>(std::max(vp[3], 1));
    const int radius = static_cast<int>(width_px / 2.0f);
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (dx * dx + dy * dy > radius * radius + radius) continue;
        glUniform2f(line_u_offset_, dx * px, dy * py);
        glDrawArrays(GL_LINES, 0, count);
      }
    }
    glUniform2f(line_u_offset_, 0.f, 0.f);
  }
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
  glUniform2f(line_u_offset_, 0.f, 0.f);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glDisableVertexAttribArray(3);
  glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(data.size() / 3));
  glBindVertexArray(0);
}

void GlRenderer::EnableDepthTest(bool on) {
  if (on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

void GlRenderer::EnableDepthWrite(bool on) { glDepthMask(on ? GL_TRUE : GL_FALSE); }

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

GLuint GlRenderer::TextureFor(const std::string& path) {
  if (path.empty()) return 0;
  const auto it = textures_.find(path);
  if (it != textures_.end()) return it->second;
  if (missing_textures_.count(path)) return 0;
  Image img;
  std::string error;
  if (!LoadImageFile(path, img, error) || !img.Valid()) {
    missing_textures_.insert(path);
    return 0;
  }
  const GLuint tex = CreateTexture(img.width, img.height, img.rgba.data(), 4);
  textures_[path] = tex;
  return tex;
}

GLuint GlRenderer::CreateTexture(int width, int height, const unsigned char* pixels, int channels) {
  if (width <= 0 || height <= 0 || !pixels) return 0;
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, channels == 4 ? GL_RGBA8 : GL_RGB8, width, height, 0, channels == 4 ? GL_RGBA : GL_RGB,
               GL_UNSIGNED_BYTE, pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

void GlRenderer::DeleteTexture(GLuint tex) {
  if (tex) glDeleteTextures(1, &tex);
}

void GlRenderer::RefreshTextures() {
  for (auto& [path, tex] : textures_) if (tex) glDeleteTextures(1, &tex);
  textures_.clear();
  missing_textures_.clear();
}

}  // namespace dino8::app
