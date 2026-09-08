#include "render/GpuRaytracer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dino8::render {

using app::LightType;

namespace {
// DINO8_RT_TIMING also enables a glGetError() check after every stage of
// Render() below, to pinpoint exactly which GL call an error came from
// (temporary debug aid - see tests/gpu_render_notes.md for how it was used).
bool g_debug_gl = std::getenv("DINO8_RT_TIMING") != nullptr;
void Check(const char* where) {
  if (!g_debug_gl) return;
  GLenum e = glGetError();
  if (e) std::fprintf(stderr, "GL error 0x%x after %s\n", e, where);
}
}  // namespace

namespace {

// Fullscreen-triangle vertex shader shared by both passes (no vertex
// buffer needed - same trick as GlRenderer's kBgVS/kTexVS).
const char* kFullscreenVS = R"(#version 330 core
void main() {
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  gl_Position = vec4(p, 0.0, 1.0);
}
)";

// Trace fragment shader: one primary ray per pixel against a BVH held in
// two buffer textures, direct lighting (point/spot/directional, area
// lights approximated as points - see gpu_render_notes.md) with a shadow
// ray, one indirect/reflection bounce, blended into a temporal
// accumulation buffer. Writes (accum.rgb, 1) to attachment 0 and
// (normal.xyz, linear_depth) to attachment 1 for the denoiser.
const char* kTraceFS = R"(#version 330 core
layout(location = 0) out vec4 out_accum;
layout(location = 1) out vec4 out_gbuf;

uniform samplerBuffer u_nodes;
uniform samplerBuffer u_tris;
uniform sampler2D u_prev;

uniform vec3 u_eye, u_fwd, u_right, u_up;
uniform float u_tan_fov, u_aspect, u_ortho_h;
uniform int u_ortho;
uniform vec2 u_resolution;
uniform int u_seed;  // set via glUniform1i - the loader has no glUniform1ui
uniform float u_alpha;  // temporal blend weight: 1 = full reset, ->0 as frames accumulate

const int kMaxMat = 64;
uniform int u_mat_count;
uniform vec4 u_mat_a[kMaxMat];  // diffuse.rgb, gloss
uniform vec4 u_mat_b[kMaxMat];  // specular.rgb, reflectivity
uniform vec4 u_mat_c[kMaxMat];  // emission.rgb, transparency (unused: opaque only)

const int kMaxLights = 8;
uniform int u_light_count;
uniform vec4 u_light_a[kMaxLights];  // position.xyz, type (0 point/area 1 spot 2 directional)
uniform vec4 u_light_b[kMaxLights];  // direction.xyz, cos_outer
uniform vec4 u_light_c[kMaxLights];  // color.rgb, cos_inner

uniform int u_bg_mode;  // 0 solid, 1 gradient, 2 sky
uniform vec3 u_bg_top, u_bg_bottom;
uniform int u_sun_enabled;
uniform vec3 u_sun_dir, u_sun_color;
uniform float u_sun_intensity;

// ---- RNG (xorshift/hash, reseeded per pixel + frame) ----------------------
uint hash1(uint x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return x;
}
uint g_rng;
float rnd() { g_rng = hash1(g_rng); return float(g_rng) / 4294967295.0; }

vec3 cosineSampleHemisphere(vec3 n) {
  float u1 = rnd(), u2 = rnd();
  float r = sqrt(u1), theta = 6.2831853 * u2;
  vec3 t = abs(n.z) < 0.999 ? normalize(cross(vec3(0, 0, 1), n)) : vec3(1, 0, 0);
  vec3 b = cross(n, t);
  return normalize(t * (r * cos(theta)) + b * (r * sin(theta)) + n * sqrt(max(0.0, 1.0 - u1)));
}

// ---- sky (approximates PathTracer::SkyColor for Sky/Gradient/Solid) -------
vec3 skyColor(vec3 d) {
  if (u_bg_mode == 0) return u_bg_top;
  float t = clamp(d.z * 0.5 + 0.5, 0.0, 1.0);
  if (u_bg_mode == 1) return mix(u_bg_bottom, u_bg_top, t);
  vec3 zenith = vec3(0.30, 0.45, 0.75), horizon = vec3(0.75, 0.82, 0.90);
  vec3 sky = mix(horizon, zenith, pow(t, 0.7));
  if (u_sun_enabled == 1) {
    float cosang = max(0.0, dot(d, normalize(u_sun_dir)));
    float disc = cosang > 0.9998 ? 1.0 : 0.0;
    float glow = pow(cosang, 256.0) * 0.6;
    sky += u_sun_color * (disc * 8.0 * u_sun_intensity + glow * u_sun_intensity);
  }
  return sky;
}

// ---- BVH traversal (mirrors render::Bvh::Intersect/IntersectAny) ---------
bool hitBox(vec3 bmin, vec3 bmax, vec3 o, vec3 invd, float tmin, float tmax) {
  vec3 t0s = (bmin - o) * invd, t1s = (bmax - o) * invd;
  vec3 tsm = min(t0s, t1s), tsM = max(t0s, t1s);
  float t0 = max(tmin, max(tsm.x, max(tsm.y, tsm.z)));
  float t1 = min(tmax, min(tsM.x, min(tsM.y, tsM.z)));
  return t0 <= t1;
}

bool hitTri(int tri, vec3 o, vec3 d, float tmin, float tmax, out float outT, out int outMat, out vec3 outN) {
  vec4 t0 = texelFetch(u_tris, tri * 6 + 0);
  vec4 t1 = texelFetch(u_tris, tri * 6 + 1);
  vec4 t2 = texelFetch(u_tris, tri * 6 + 2);
  vec3 v0 = t0.xyz, v1 = t1.xyz, v2 = t2.xyz;
  vec3 e1 = v1 - v0, e2 = v2 - v0;
  vec3 p = cross(d, e2);
  float det = dot(e1, p);
  if (abs(det) < 1e-9) return false;
  float invDet = 1.0 / det;
  vec3 tv = o - v0;
  float u = dot(tv, p) * invDet;
  if (u < -1e-5 || u > 1.0 + 1e-5) return false;
  vec3 q = cross(tv, e1);
  float v = dot(d, q) * invDet;
  if (v < -1e-5 || u + v > 1.0 + 1e-5) return false;
  float tt = dot(e2, q) * invDet;
  if (tt < tmin || tt > tmax) return false;
  outT = tt; outMat = int(t0.w);
  vec3 n0 = texelFetch(u_tris, tri * 6 + 3).xyz;
  vec3 n1 = texelFetch(u_tris, tri * 6 + 4).xyz;
  vec3 n2 = texelFetch(u_tris, tri * 6 + 5).xyz;
  outN = normalize(n0 * (1.0 - u - v) + n1 * u + n2 * v);
  return true;
}

// Fixed-size explicit stack (GLSL has no recursion) - matches the CPU
// traversal's own stack[64] depth budget.
bool closestHit(vec3 o, vec3 d, float tmin, float tmax, out float outT, out int outMat, out vec3 outN, out vec3 outP) {
  vec3 invd = 1.0 / d;
  int stack[64]; int sp = 0; stack[sp++] = 0;
  bool found = false; float closest = tmax;
  while (sp > 0) {
    int idx = stack[--sp];
    vec4 n0v = texelFetch(u_nodes, idx * 3 + 0);
    vec4 n1v = texelFetch(u_nodes, idx * 3 + 1);
    if (!hitBox(n0v.xyz, n1v.xyz, o, invd, tmin, closest)) continue;
    int left = int(n0v.w); int count = int(n1v.w);
    if (count > 0) {
      int start = int(texelFetch(u_nodes, idx * 3 + 2).x);
      for (int i = 0; i < count; ++i) {
        float tt; int mi; vec3 nn;
        if (hitTri(start + i, o, d, tmin, closest, tt, mi, nn)) {
          closest = tt; outT = tt; outMat = mi; outN = nn; found = true;
        }
      }
    } else if (sp < 62) {
      stack[sp++] = left; stack[sp++] = left + 1;
    }
  }
  if (found) outP = o + d * outT;
  return found;
}

bool anyHit(vec3 o, vec3 d, float tmin, float tmax) {
  vec3 invd = 1.0 / d;
  int stack[64]; int sp = 0; stack[sp++] = 0;
  while (sp > 0) {
    int idx = stack[--sp];
    vec4 n0v = texelFetch(u_nodes, idx * 3 + 0);
    vec4 n1v = texelFetch(u_nodes, idx * 3 + 1);
    if (!hitBox(n0v.xyz, n1v.xyz, o, invd, tmin, tmax)) continue;
    int left = int(n0v.w); int count = int(n1v.w);
    if (count > 0) {
      int start = int(texelFetch(u_nodes, idx * 3 + 2).x);
      for (int i = 0; i < count; ++i) {
        float tt; int mi; vec3 nn;
        if (hitTri(start + i, o, d, tmin, tmax, tt, mi, nn)) return true;
      }
    } else if (sp < 62) {
      stack[sp++] = left; stack[sp++] = left + 1;
    }
  }
  return false;
}

// Direct lighting at a shaded point: sums every light (<=8, so a full loop
// each frame is affordable rather than the CPU tracer's single
// light-sampled NEE), each with its own shadow ray.
vec3 directLighting(vec3 p, vec3 n, vec3 viewDir, vec3 albedo, vec3 specColor, float gloss) {
  vec3 total = vec3(0.0);
  float shin = mix(4.0, 200.0, gloss);
  for (int i = 0; i < u_light_count; ++i) {
    vec4 La = u_light_a[i], Lb = u_light_b[i], Lc = u_light_c[i];
    int type = int(La.w);
    vec3 wi; float atten = 1.0; float shadowMax;
    if (type == 2) {
      wi = normalize(-Lb.xyz);
      shadowMax = 1e6;
    } else {
      vec3 to = La.xyz - p;
      float dist2 = dot(to, to);
      if (dist2 < 1e-9) continue;
      float dist = sqrt(dist2);
      wi = to / dist;
      atten = 1.0 / dist2;
      shadowMax = dist - 1e-3;
      if (type == 1) {
        float cosang = dot(-wi, normalize(Lb.xyz));
        float cosOuter = Lb.w, cosInner = Lc.w;
        if (cosang < cosOuter) continue;
        atten *= clamp((cosang - cosOuter) / max(cosInner - cosOuter, 1e-6), 0.0, 1.0);
      }
    }
    float ndotl = dot(n, wi);
    if (ndotl <= 0.0) continue;
    if (shadowMax > 0.0 && anyHit(p + n * 1e-4, wi, 1e-4, shadowMax)) continue;
    vec3 Le = Lc.rgb * atten;
    vec3 diffuse = albedo * (1.0 / 3.14159265) * ndotl;
    vec3 h = normalize(wi + viewDir);
    float spec = pow(max(dot(n, h), 0.0), shin);
    total += (diffuse + specColor * spec) * Le;
  }
  return total;
}

// One primary trace + one bounce. Returns linear radiance.
vec3 trace(vec3 ro, vec3 rd) {
  float t; int mat; vec3 n, p;
  if (!closestHit(ro, rd, 1e-3, 1e6, t, mat, n, p)) return skyColor(rd);
  if (dot(n, rd) > 0.0) n = -n;  // face the viewer
  vec3 albedo = mat >= 0 ? u_mat_a[mat].xyz : vec3(0.7);
  vec3 specColor = mat >= 0 ? u_mat_b[mat].xyz : vec3(1.0);
  float gloss = mat >= 0 ? u_mat_a[mat].w : 0.3;
  float reflectivity = mat >= 0 ? u_mat_b[mat].w : 0.0;
  vec3 emission = mat >= 0 ? u_mat_c[mat].xyz : vec3(0.0);
  vec3 viewDir = -rd;

  vec3 direct = directLighting(p, n, viewDir, albedo, specColor, gloss);

  // One indirect bounce: stochastically either a mirror reflection lobe
  // (weighted by reflectivity) or a cosine-weighted diffuse GI lobe.
  vec3 bounceDir = rnd() < reflectivity ? reflect(rd, n) : cosineSampleHemisphere(n);
  float t2; int hit_mat2; vec3 n2, p2;
  vec3 indirect;
  if (closestHit(p + n * 1e-4, bounceDir, 1e-3, 1e6, t2, hit_mat2, n2, p2)) {
    if (dot(n2, bounceDir) > 0.0) n2 = -n2;
    vec3 albedo2 = hit_mat2 >= 0 ? u_mat_a[hit_mat2].xyz : vec3(0.7);
    vec3 spec2 = hit_mat2 >= 0 ? u_mat_b[hit_mat2].xyz : vec3(1.0);
    float gloss2 = hit_mat2 >= 0 ? u_mat_a[hit_mat2].w : 0.3;
    vec3 emission2 = hit_mat2 >= 0 ? u_mat_c[hit_mat2].xyz : vec3(0.0);
    indirect = emission2 + directLighting(p2, n2, -bounceDir, albedo2, spec2, gloss2);
  } else {
    indirect = skyColor(bounceDir);
  }
  vec3 color = emission + direct + albedo * indirect;
  return min(color, vec3(12.0));  // firefly clamp - 1 spp/frame is noisy before accumulation converges
}

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  g_rng = hash1(uint(px.x) * 1973u + uint(px.y) * 9277u + uint(u_seed) * 26699u) | 1u;

  // NDC with y flipped so the final DrawFullscreenTexture blit (which
  // flips v to match top-down image convention) presents the image the
  // right way up - see the derivation in GpuRaytracer.cpp.
  vec2 ndc = (gl_FragCoord.xy / u_resolution) * 2.0 - 1.0;
  ndc.y = -ndc.y;

  vec3 ro, rd;
  if (u_ortho == 1) {
    float h = u_ortho_h * 0.5;
    ro = u_eye + u_right * (ndc.x * u_aspect * h) + u_up * (ndc.y * h);
    rd = u_fwd;
  } else {
    ro = u_eye;
    rd = normalize(u_fwd + u_right * (ndc.x * u_aspect * u_tan_fov) + u_up * (ndc.y * u_tan_fov));
  }

  vec3 radiance = trace(ro, rd);

  vec3 prev = texelFetch(u_prev, px, 0).rgb;
  vec3 blended = mix(prev, radiance, u_alpha);
  out_accum = vec4(blended, 1.0);

  // G-buffer for the denoiser: re-fetch the primary hit's normal/depth
  // (cheap relative to the trace above; keeps `trace()` a single closed
  // function instead of threading extra out-parameters through it).
  float t; int mat; vec3 n, p;
  if (closestHit(ro, rd, 1e-3, 1e6, t, mat, n, p)) out_gbuf = vec4(normalize(n), t);
  else out_gbuf = vec4(0.0, 0.0, 0.0, 0.0);
}
)";

// Edge-aware (bilateral) 5x5 denoise: blurs the accumulated radiance,
// weighted by normal/depth similarity from the G-buffer, tapering off as
// more temporal samples have accumulated (the image is already converging,
// so less spatial blur is needed / wanted).
const char* kDenoiseFS = R"(#version 330 core
out vec4 frag;
uniform sampler2D u_accum;
uniform sampler2D u_gbuf;
uniform vec2 u_resolution;
uniform int u_accum_frames;

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 centerG = texelFetch(u_gbuf, px, 0);
  vec3 centerC = texelFetch(u_accum, px, 0).rgb;
  vec3 centerN = centerG.xyz; float centerD = centerG.w;

  float taper = clamp(1.0 - float(u_accum_frames) / 24.0, 0.1, 1.0);
  int radius = int(mix(1.0, 2.0, taper));
  float sigmaN = 0.4, sigmaD = mix(0.02, 0.25, taper);

  vec3 sum = vec3(0.0); float wsum = 0.0;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      ivec2 sp = px + ivec2(dx, dy);
      vec4 g = texelFetch(u_gbuf, sp, 0);
      vec3 c = texelFetch(u_accum, sp, 0).rgb;
      float spatial = exp(-float(dx * dx + dy * dy) / (2.0 * 1.5 * 1.5));
      float wn = centerD > 0.0 && g.w > 0.0 ? pow(max(dot(g.xyz, centerN), 0.0), 1.0 / sigmaN) : 1.0;
      float wd = centerD > 0.0 && g.w > 0.0 ? exp(-abs(g.w - centerD) / max(sigmaD, 1e-4)) : (g.w == centerD ? 1.0 : 0.05);
      float w = spatial * wn * wd;
      sum += c * w; wsum += w;
    }
  }
  frag = vec4(wsum > 1e-6 ? sum / wsum : centerC, 1.0);
}
)";

GLuint CompileShader(GLenum type, const char* src, std::string& error) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    GLsizei len = 0;
    glGetShaderInfoLog(shader, sizeof(log), &len, log);
    error = std::string("shader compile failed: ") + std::string(log, static_cast<size_t>(len));
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint CompileProgram(const char* vs_src, const char* fs_src, std::string& error) {
  GLuint vs = CompileShader(GL_VERTEX_SHADER, vs_src, error);
  if (!vs) return 0;
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fs_src, error);
  if (!fs) { glDeleteShader(vs); return 0; }
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    GLsizei len = 0;
    glGetProgramInfoLog(program, sizeof(log), &len, log);
    error = std::string("program link failed: ") + std::string(log, static_cast<size_t>(len));
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

GLuint MakeFloatTexBuffer(GLuint& buf, const std::vector<float>& data) {
  if (!buf) glGenBuffers(1, &buf);
  glBindBuffer(GL_TEXTURE_BUFFER, buf);
  glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(data.empty() ? 16 : data.size() * sizeof(float)),
               data.empty() ? nullptr : data.data(), GL_STATIC_DRAW);
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_BUFFER, tex);
  glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buf);
  glBindTexture(GL_TEXTURE_BUFFER, 0);
  glBindBuffer(GL_TEXTURE_BUFFER, 0);
  return tex;
}

}  // namespace

bool GpuRaytracer::CompilePrograms(std::string& error) {
  trace_program_ = CompileProgram(kFullscreenVS, kTraceFS, error);
  if (!trace_program_) return false;
  denoise_program_ = CompileProgram(kFullscreenVS, kDenoiseFS, error);
  if (!denoise_program_) return false;

  t_prev_ = glGetUniformLocation(trace_program_, "u_prev");
  t_nodes_ = glGetUniformLocation(trace_program_, "u_nodes");
  t_tris_ = glGetUniformLocation(trace_program_, "u_tris");
  t_eye_ = glGetUniformLocation(trace_program_, "u_eye");
  t_fwd_ = glGetUniformLocation(trace_program_, "u_fwd");
  t_right_ = glGetUniformLocation(trace_program_, "u_right");
  t_up_ = glGetUniformLocation(trace_program_, "u_up");
  t_tan_fov_ = glGetUniformLocation(trace_program_, "u_tan_fov");
  t_aspect_ = glGetUniformLocation(trace_program_, "u_aspect");
  t_ortho_ = glGetUniformLocation(trace_program_, "u_ortho");
  t_ortho_h_ = glGetUniformLocation(trace_program_, "u_ortho_h");
  t_resolution_ = glGetUniformLocation(trace_program_, "u_resolution");
  t_seed_ = glGetUniformLocation(trace_program_, "u_seed");
  t_alpha_ = glGetUniformLocation(trace_program_, "u_alpha");
  t_mat_count_ = glGetUniformLocation(trace_program_, "u_mat_count");
  t_mat_a_ = glGetUniformLocation(trace_program_, "u_mat_a");
  t_mat_b_ = glGetUniformLocation(trace_program_, "u_mat_b");
  t_mat_c_ = glGetUniformLocation(trace_program_, "u_mat_c");
  t_light_count_ = glGetUniformLocation(trace_program_, "u_light_count");
  t_light_a_ = glGetUniformLocation(trace_program_, "u_light_a");
  t_light_b_ = glGetUniformLocation(trace_program_, "u_light_b");
  t_light_c_ = glGetUniformLocation(trace_program_, "u_light_c");
  t_bg_mode_ = glGetUniformLocation(trace_program_, "u_bg_mode");
  t_bg_top_ = glGetUniformLocation(trace_program_, "u_bg_top");
  t_bg_bottom_ = glGetUniformLocation(trace_program_, "u_bg_bottom");
  t_sun_enabled_ = glGetUniformLocation(trace_program_, "u_sun_enabled");
  t_sun_dir_ = glGetUniformLocation(trace_program_, "u_sun_dir");
  t_sun_color_ = glGetUniformLocation(trace_program_, "u_sun_color");
  t_sun_intensity_ = glGetUniformLocation(trace_program_, "u_sun_intensity");

  d_accum_ = glGetUniformLocation(denoise_program_, "u_accum");
  d_gbuf_ = glGetUniformLocation(denoise_program_, "u_gbuf");
  d_resolution_ = glGetUniformLocation(denoise_program_, "u_resolution");
  d_accum_frames_ = glGetUniformLocation(denoise_program_, "u_accum_frames");
  return true;
}

bool GpuRaytracer::Init(std::string& error) {
  if (inited_) return true;
  if (!CompilePrograms(error)) return false;
  glGenVertexArrays(1, &vao_);
  inited_ = true;
  return true;
}

void GpuRaytracer::Shutdown() {
  if (!inited_) return;
  if (trace_program_) glDeleteProgram(trace_program_);
  if (denoise_program_) glDeleteProgram(denoise_program_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (node_tex_) glDeleteTextures(1, &node_tex_);
  if (tri_tex_) glDeleteTextures(1, &tri_tex_);
  if (node_buf_) glDeleteBuffers(1, &node_buf_);
  if (tri_buf_) glDeleteBuffers(1, &tri_buf_);
  for (GLuint t : accum_tex_) if (t) glDeleteTextures(1, &t);
  if (gbuf_tex_) glDeleteTextures(1, &gbuf_tex_);
  for (GLuint f : trace_fbo_) if (f) glDeleteFramebuffers(1, &f);
  if (present_tex_) glDeleteTextures(1, &present_tex_);
  if (present_fbo_) glDeleteFramebuffers(1, &present_fbo_);
  *this = GpuRaytracer();
}

void GpuRaytracer::UploadBuffer(GLuint& buf, GLuint& tex, const std::vector<float>& floats) {
  if (tex) { glDeleteTextures(1, &tex); tex = 0; }
  tex = MakeFloatTexBuffer(buf, floats);
}

void GpuRaytracer::UploadScene(const app::PathTracer& tracer, const app::RenderSettings& settings) {
  const Bvh& bvh = tracer.SceneBvh();
  std::vector<float> nodes, tris;
  bvh.ExportGpuNodes(nodes);
  bvh.ExportGpuTriangles(tris);
  node_count_ = static_cast<int>(nodes.size() / 12);
  tri_count_ = static_cast<int>(tris.size() / 24);
  UploadBuffer(node_buf_, node_tex_, nodes);
  Check("UploadScene nodes");
  UploadBuffer(tri_buf_, tri_tex_, tris);
  Check("UploadScene tris");

  const auto& mats = tracer.SceneMaterials();
  mat_count_ = std::min<int>(static_cast<int>(mats.size()), 64);
  mat_a_.assign(static_cast<size_t>(64) * 4, 0.f);
  mat_b_.assign(static_cast<size_t>(64) * 4, 0.f);
  mat_c_.assign(static_cast<size_t>(64) * 4, 0.f);
  for (int i = 0; i < mat_count_; ++i) {
    const app::Material& m = mats[static_cast<size_t>(i)];
    mat_a_[static_cast<size_t>(i) * 4 + 0] = m.diffuse.r; mat_a_[static_cast<size_t>(i) * 4 + 1] = m.diffuse.g;
    mat_a_[static_cast<size_t>(i) * 4 + 2] = m.diffuse.b; mat_a_[static_cast<size_t>(i) * 4 + 3] = m.gloss;
    mat_b_[static_cast<size_t>(i) * 4 + 0] = m.specular.r; mat_b_[static_cast<size_t>(i) * 4 + 1] = m.specular.g;
    mat_b_[static_cast<size_t>(i) * 4 + 2] = m.specular.b; mat_b_[static_cast<size_t>(i) * 4 + 3] = m.reflectivity;
    mat_c_[static_cast<size_t>(i) * 4 + 0] = m.emission.r; mat_c_[static_cast<size_t>(i) * 4 + 1] = m.emission.g;
    mat_c_[static_cast<size_t>(i) * 4 + 2] = m.emission.b; mat_c_[static_cast<size_t>(i) * 4 + 3] = m.transparency;
  }

  const auto& lights = tracer.SceneLights();
  light_count_ = std::min<int>(static_cast<int>(lights.size()), 8);
  light_a_.assign(static_cast<size_t>(8) * 4, 0.f);
  light_b_.assign(static_cast<size_t>(8) * 4, 0.f);
  light_c_.assign(static_cast<size_t>(8) * 4, 0.f);
  for (int i = 0; i < light_count_; ++i) {
    const app::PathTracer::SceneLight& L = lights[static_cast<size_t>(i)];
    // Area lights (Rectangular/Linear) are approximated as point lights at
    // their centre for the GPU pass - see gpu_render_notes.md.
    const int type = L.type == LightType::Directional ? 2 : L.type == LightType::Spot ? 1 : 0;
    light_a_[static_cast<size_t>(i) * 4 + 0] = static_cast<float>(L.position.x);
    light_a_[static_cast<size_t>(i) * 4 + 1] = static_cast<float>(L.position.y);
    light_a_[static_cast<size_t>(i) * 4 + 2] = static_cast<float>(L.position.z);
    light_a_[static_cast<size_t>(i) * 4 + 3] = static_cast<float>(type);
    light_b_[static_cast<size_t>(i) * 4 + 0] = static_cast<float>(L.direction.x);
    light_b_[static_cast<size_t>(i) * 4 + 1] = static_cast<float>(L.direction.y);
    light_b_[static_cast<size_t>(i) * 4 + 2] = static_cast<float>(L.direction.z);
    light_b_[static_cast<size_t>(i) * 4 + 3] = static_cast<float>(L.cos_outer);
    light_c_[static_cast<size_t>(i) * 4 + 0] = L.r; light_c_[static_cast<size_t>(i) * 4 + 1] = L.g;
    light_c_[static_cast<size_t>(i) * 4 + 2] = L.b; light_c_[static_cast<size_t>(i) * 4 + 3] = static_cast<float>(L.cos_inner);
  }

  bg_mode_ = static_cast<int>(settings.background);
  if (settings.background == app::RenderSettings::Background::Image) bg_mode_ = 0;  // no env-map sampling on GPU
  bg_top_[0] = settings.background == app::RenderSettings::Background::Gradient ? settings.gradient_top.r : settings.background_color.r;
  bg_top_[1] = settings.background == app::RenderSettings::Background::Gradient ? settings.gradient_top.g : settings.background_color.g;
  bg_top_[2] = settings.background == app::RenderSettings::Background::Gradient ? settings.gradient_top.b : settings.background_color.b;
  bg_bottom_[0] = settings.gradient_bottom.r; bg_bottom_[1] = settings.gradient_bottom.g; bg_bottom_[2] = settings.gradient_bottom.b;
  sun_enabled_ = settings.sun;
  const double az = settings.sun_azimuth * 3.14159265358979 / 180.0, alt = settings.sun_altitude * 3.14159265358979 / 180.0;
  sun_dir_[0] = static_cast<float>(std::cos(alt) * std::sin(az));
  sun_dir_[1] = static_cast<float>(std::cos(alt) * std::cos(az));
  sun_dir_[2] = static_cast<float>(std::sin(alt));
  sun_color_[0] = settings.sun_color.r; sun_color_[1] = settings.sun_color.g; sun_color_[2] = settings.sun_color.b;
  sun_intensity_ = settings.sun_intensity;

  accum_frames_ = 0;  // scene changed: whatever was in the accumulation buffers is stale
}

bool GpuRaytracer::EnsureTargets(int width, int height) {
  if (width == tex_w_ && height == tex_h_ && trace_fbo_[0] && trace_fbo_[1] && present_fbo_) return true;
  tex_w_ = width; tex_h_ = height;

  auto makeTex = [&](GLuint& tex, GLenum internal_fmt) {
    if (tex) glDeleteTextures(1, &tex);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal_fmt), width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
  };
  makeTex(accum_tex_[0], GL_RGBA16F);
  makeTex(accum_tex_[1], GL_RGBA16F);
  makeTex(gbuf_tex_, GL_RGBA16F);
  makeTex(present_tex_, GL_RGBA16F);

  for (int i = 0; i < 2; ++i) {
    if (trace_fbo_[i]) glDeleteFramebuffers(1, &trace_fbo_[i]);
    glGenFramebuffers(1, &trace_fbo_[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, trace_fbo_[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accum_tex_[i], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gbuf_tex_, 0);
    const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { glBindFramebuffer(GL_FRAMEBUFFER, 0); return false; }
  }
  if (present_fbo_) glDeleteFramebuffers(1, &present_fbo_);
  glGenFramebuffers(1, &present_fbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, present_fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, present_tex_, 0);
  const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  ping_ = 0;
  accum_frames_ = 0;
  Check("EnsureTargets rebuild");
  return ok;
}

GLuint GpuRaytracer::Render(const app::Camera& camera, double aspect, int width, int height, bool reset_accum) {
  if (!inited_ || tri_count_ == 0) return 0;
  width = std::max(width, 1); height = std::max(height, 1);
  if (!EnsureTargets(width, height)) return 0;
  if (reset_accum) { accum_frames_ = 0; ping_ = 0; }

  const int dst = ping_, src = 1 - ping_;
  const float alpha = accum_frames_ == 0 ? 1.0f : 1.0f / static_cast<float>(std::min(accum_frames_ + 1, 256));

  glViewport(0, 0, width, height);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, trace_fbo_[dst]);
  glUseProgram(trace_program_);

  const auto& cs = camera.State();
  const kernel::Vector3d fwd = camera.Forward(), right = camera.Right(), up = camera.Up();
  glUniform3f(t_eye_, static_cast<float>(cs.eye.x), static_cast<float>(cs.eye.y), static_cast<float>(cs.eye.z));
  glUniform3f(t_fwd_, static_cast<float>(fwd.x), static_cast<float>(fwd.y), static_cast<float>(fwd.z));
  glUniform3f(t_right_, static_cast<float>(right.x), static_cast<float>(right.y), static_cast<float>(right.z));
  glUniform3f(t_up_, static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z));
  const double fov = 2.0 * std::atan(18.0 / cs.lens_mm);
  glUniform1f(t_tan_fov_, static_cast<float>(std::tan(fov * 0.5)));
  glUniform1f(t_aspect_, static_cast<float>(aspect));
  glUniform1i(t_ortho_, cs.perspective ? 0 : 1);
  glUniform1f(t_ortho_h_, static_cast<float>(cs.ortho_height));
  glUniform2f(t_resolution_, static_cast<float>(width), static_cast<float>(height));
  // u_seed is declared `int` in the shader (not `uint`) specifically so this
  // can use glUniform1i - the hand-rolled loader (gl_loader.h) has no
  // glUniform1ui, and mixing signedness here is a real GL_INVALID_OPERATION
  // on a strict driver, not just a lint nit (caught via DINO8_RT_TIMING's
  // per-stage glGetError checks while bringing this up - see
  // gpu_render_notes.md).
  glUniform1i(t_seed_, static_cast<GLint>(frame_seed_++));
  glUniform1f(t_alpha_, alpha);

  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_BUFFER, node_tex_); glUniform1i(t_nodes_, 0);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_BUFFER, tri_tex_); glUniform1i(t_tris_, 1);
  glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, accum_tex_[src]); glUniform1i(t_prev_, 2);

  glUniform1i(t_mat_count_, mat_count_);
  glUniform4fv(t_mat_a_, 64, mat_a_.data());
  glUniform4fv(t_mat_b_, 64, mat_b_.data());
  glUniform4fv(t_mat_c_, 64, mat_c_.data());
  glUniform1i(t_light_count_, light_count_);
  glUniform4fv(t_light_a_, 8, light_a_.data());
  glUniform4fv(t_light_b_, 8, light_b_.data());
  glUniform4fv(t_light_c_, 8, light_c_.data());
  glUniform1i(t_bg_mode_, bg_mode_);
  glUniform3f(t_bg_top_, bg_top_[0], bg_top_[1], bg_top_[2]);
  glUniform3f(t_bg_bottom_, bg_bottom_[0], bg_bottom_[1], bg_bottom_[2]);
  glUniform1i(t_sun_enabled_, sun_enabled_ ? 1 : 0);
  glUniform3f(t_sun_dir_, sun_dir_[0], sun_dir_[1], sun_dir_[2]);
  glUniform3f(t_sun_color_, sun_color_[0], sun_color_[1], sun_color_[2]);
  glUniform1f(t_sun_intensity_, sun_intensity_);
  Check("trace uniforms/textures");

  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  Check("trace draw");

  glBindFramebuffer(GL_FRAMEBUFFER, present_fbo_);
  glUseProgram(denoise_program_);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, accum_tex_[dst]); glUniform1i(d_accum_, 0);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gbuf_tex_); glUniform1i(d_gbuf_, 1);
  glUniform2f(d_resolution_, static_cast<float>(width), static_cast<float>(height));
  glUniform1i(d_accum_frames_, accum_frames_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  Check("denoise draw");

  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glEnable(GL_DEPTH_TEST);

  ping_ = dst;
  ++accum_frames_;
  return present_tex_;
}

}  // namespace dino8::render
