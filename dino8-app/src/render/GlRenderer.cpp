#include "render/GlRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "render/ImageIO.h"
#include "render/MaterialLibrary.h"
#include "render/SsaoKernel.h"

namespace dino8::app {

namespace {

const char* kMeshVS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec3 a_col;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_mvp;
uniform mat4 u_view;
uniform vec4 u_clip[6];
uniform int u_clip_count;
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
  vec4 wp = vec4(a_pos, 1.0);
  gl_ClipDistance[0] = (u_clip_count > 0) ? dot(u_clip[0], wp) : 1.0;
  gl_ClipDistance[1] = (u_clip_count > 1) ? dot(u_clip[1], wp) : 1.0;
  gl_ClipDistance[2] = (u_clip_count > 2) ? dot(u_clip[2], wp) : 1.0;
  gl_ClipDistance[3] = (u_clip_count > 3) ? dot(u_clip[3], wp) : 1.0;
  gl_ClipDistance[4] = (u_clip_count > 4) ? dot(u_clip[4], wp) : 1.0;
  gl_ClipDistance[5] = (u_clip_count > 5) ? dot(u_clip[5], wp) : 1.0;
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
uniform mat4 u_view;  // shared with the vertex shader's own u_view (same GL uniform, one glUniform call sets both)
uniform sampler2D u_env_map;    // Background::Image's real texture, valid only when u_env_map_valid == 1
uniform int u_env_map_valid;
// Rendered mode.
const int MAX_LIGHTS = 8;
uniform sampler2DArray u_shadow_map;  // one depth layer per light index (GlRenderer::BeginShadowPass)
uniform int u_shadow_valid_mask;      // bit i set when light i's own shadow layer was rendered this frame
uniform sampler2D u_ssao_map;  // screen-space AO buffer (GlRenderer::BeginSsaoPass/EndSsaoPass), valid only when u_ssao_valid == 1
uniform int u_ssao_valid;
uniform vec2 u_viewport_size;  // pixels - turns gl_FragCoord into u_ssao_map's own [0,1] UV
uniform mat4 u_light_vp[MAX_LIGHTS];  // light index -> that light's own view-projection, world space in
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

// Real Background::Image reflection: same equirectangular lat-long mapping
// as PathTracer::SkyColor's Background::Image branch and GpuRaytracer's
// skyColor() u_bg_mode==3 branch, in this app's z-up world-space
// convention. `d` must already be a world-space direction.
vec3 SampleEnvMap(vec3 d) {
  float theta = acos(clamp(d.z, -1.0, 1.0));
  float u = atan(d.y, d.x) / (2.0 * 3.14159265358979) + 0.5;
  float v = 1.0 - theta / 3.14159265358979;
  return texture(u_env_map, vec2(fract(u), 1.0 - fract(v))).rgb;
}

// Real-time shadow map lookup for light index `i` (GlRenderer::BeginShadowPass
// - a genuine per-light atlas, every enabled light casts its own shadow, not
// just one documented shadow-caster). 3x3 PCF against the depth layer built
// from that light's own point of view; a fragment outside that light's
// frustum, or behind its far plane, is never shadowed by it.
float ShadowFactor(int i) {
  if (((u_shadow_valid_mask >> i) & 1) == 0) return 0.0;
  vec4 pos_light = u_light_vp[i] * vec4(v_pos_world, 1.0);
  vec3 proj = pos_light.xyz / pos_light.w;
  proj = proj * 0.5 + 0.5;
  if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0) return 0.0;
  const float bias = 0.0015;
  vec2 texel = 1.0 / vec2(textureSize(u_shadow_map, 0).xy);
  float shadow = 0.0;
  for (int dx = -1; dx <= 1; ++dx) {
    for (int dy = -1; dy <= 1; ++dy) {
      float depth = texture(u_shadow_map, vec3(proj.xy + vec2(dx, dy) * texel, float(i))).r;
      shadow += (proj.z - bias > depth) ? 1.0 : 0.0;
    }
  }
  return shadow / 9.0;
}

// Blinn-Phong over the light list; `n` faces the eye, `view_dir` points
// from the eye into the scene.
vec3 Shade(vec3 base, vec3 n, vec3 view_dir) {
  vec3 V = -view_dir;
  // Hemispherical sky light: brighter on up-facing surfaces.
  float up = clamp(normalize(v_nrm_world).z * 0.5 + 0.5, 0.0, 1.0);
  // Real SSAO (GlRenderer::BeginSsaoPass/EndSsaoPass): darkens only the
  // ambient/sky term above, the same scope a direct light's own real
  // shadow map (ShadowFactor) already covers for direct lighting - 1.0
  // (no darkening at all) whenever no AO buffer was computed this frame.
  float ao = (u_ssao_valid == 1) ? texture(u_ssao_map, gl_FragCoord.xy / u_viewport_size).r : 1.0;
  vec3 color = base * u_ambient * mix(0.55, 1.0, up) * ao;
  for (int i = 0; i < u_light_count; ++i) {
    vec3 L = (u_light_pos[i].w < 0.5) ? normalize(u_light_pos[i].xyz) : normalize(u_light_pos[i].xyz - v_pos_view);
    float spot = 1.0;
    if (u_light_spot[i].x > -1.5) spot = smoothstep(u_light_spot[i].x, u_light_spot[i].y, dot(-L, u_light_dir[i]));
    spot *= (1.0 - ShadowFactor(i));
    float nd = max(dot(n, L), 0.0);
    vec3 H = normalize(L + V);
    float sp = (nd > 0.0) ? pow(max(dot(n, H), 0.0), u_specular.w) : 0.0;
    color += (base * nd + u_specular.rgb * sp * (0.04 + 0.96 * min(1.0, u_specular.w / 64.0))) * u_light_color[i] * spot;
  }
  if (u_reflectivity > 0.0) {
    vec3 r_view = reflect(view_dir, n);
    // u_view maps world -> view space and is orthonormal (a camera has no
    // scale/shear), so its transpose is its inverse: view -> world.
    vec3 env = (u_env_map_valid == 1) ? SampleEnvMap(normalize(transpose(mat3(u_view)) * r_view))
                                       : Environment(r_view);
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
  // ShowZBuffer: the per-vertex colour already *is* the final depth-grey
  // value (see Viewport::DrawObjects) - unlike mode 4 below, it must not
  // be modulated by lighting, or two equally-distant surfaces with
  // different normals would show different greys for the same depth.
  if (u_mode == 7) { frag = vec4(v_col, u_color.a); return; }
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
uniform vec4 u_clip[6];
uniform int u_clip_count;
void main() {
  vec4 p = u_mvp * vec4(a_pos, 1.0);
  p.xy += u_offset * p.w;
  gl_Position = p;
  gl_PointSize = u_size;
  vec4 wp = vec4(a_pos, 1.0);
  gl_ClipDistance[0] = (u_clip_count > 0) ? dot(u_clip[0], wp) : 1.0;
  gl_ClipDistance[1] = (u_clip_count > 1) ? dot(u_clip[1], wp) : 1.0;
  gl_ClipDistance[2] = (u_clip_count > 2) ? dot(u_clip[2], wp) : 1.0;
  gl_ClipDistance[3] = (u_clip_count > 3) ? dot(u_clip[3], wp) : 1.0;
  gl_ClipDistance[4] = (u_clip_count > 4) ? dot(u_clip[4], wp) : 1.0;
  gl_ClipDistance[5] = (u_clip_count > 5) ? dot(u_clip[5], wp) : 1.0;
}
)";

const char* kLineFS = R"(#version 330 core
uniform vec4 u_color;
out vec4 frag;
void main() { frag = u_color; }
)";

// Depth-only pass for BeginShadowPass/EndShadowPass: positions only (same
// vao_/vbo_ layout DrawMesh already uses - attribute 0, stride 6 floats),
// no fragment output at all (the shadow FBO has no colour attachment).
const char* kShadowVS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_light_vp;
void main() { gl_Position = u_light_vp * vec4(a_pos, 1.0); }
)";

const char* kShadowFS = R"(#version 330 core
void main() {}
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

// Fullscreen-triangle texture blit (RayTracedViewport's progressive image).
const char* kTexVS = R"(#version 330 core
out vec2 v_uv;
void main() {
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  gl_Position = vec4(p, 0.0, 1.0);
  v_uv = vec2((p.x + 1.0) * 0.5, 1.0 - (p.y + 1.0) * 0.5);
}
)";

const char* kTexFS = R"(#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 frag;
void main() { frag = vec4(texture(u_tex, v_uv).rgb, 1.0); }
)";

// Rendered mode's Background::Image backdrop, lat-long (equirectangular)
// unwarped around the camera's own view direction - GlRenderer::
// DrawEnvironmentBackground, used only by Viewport.cpp's DrawBackgroundImage
// for the real environment-image case (never for BackgroundBitmap's
// deliberately-flat modelling-aid stretch, which still uses kTexVS/kTexFS
// above via DrawFullscreenTexture, unchanged). Same oversized fullscreen-
// triangle trick as kBgVS, but both NDC components are carried to the
// fragment shader instead of just v_t.
const char* kEnvBgVS = R"(#version 330 core
out vec2 v_ndc;
void main() {
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  gl_Position = vec4(p, 0.999, 1.0);
  v_ndc = p;
}
)";

const char* kEnvBgFS = R"(#version 330 core
in vec2 v_ndc;
uniform sampler2D u_tex;
uniform vec3 u_forward, u_right, u_up;
uniform float u_tan_fov, u_aspect;
uniform int u_ortho;
out vec4 frag;
void main() {
  // Perspective: the exact same per-pixel ray formula Camera::ScreenRay
  // uses on the CPU, so the background shows the same world direction at
  // each pixel ScreenRay would compute there for picking.
  // Orthographic: Camera::ScreenRay's own ortho branch returns one
  // constant `direction` (u_forward) for every screen point it is asked
  // about - true parallel projection has no per-pixel ray spread - so the
  // background likewise samples one constant direction across the whole
  // screen. That is a physically-correct consequence of parallel
  // projection, not a bug, even though it looks odd next to a perspective
  // view's varying backdrop.
  vec3 dir = (u_ortho == 1) ? u_forward
      : normalize(u_forward + u_right * (v_ndc.x * u_tan_fov * u_aspect) + u_up * (v_ndc.y * u_tan_fov));
  // Same equirectangular mapping as GlRenderer's kMeshFS::SampleEnvMap
  // (the reflective-surface env map already shipped), for consistency.
  float theta = acos(clamp(dir.z, -1.0, 1.0));
  float u = atan(dir.y, dir.x) / (2.0 * 3.14159265358979) + 0.5;
  float v = 1.0 - theta / 3.14159265358979;
  frag = vec4(texture(u_tex, vec2(fract(u), 1.0 - fract(v))).rgb, 1.0);
}
)";

// SSAO compute: reuses kTexVS's fullscreen triangle (v_uv) - see
// GlRenderer::EndSsaoPass. No separate G-buffer normal attachment: the
// per-fragment normal comes straight from how the view-space position
// reconstructed from u_depth changes across neighbouring pixels (screen-
// space derivatives), the same "Alchemy AO" shortcut several shipped
// engines use to avoid a second render target.
const char* kSsaoFS = R"(#version 330 core
in vec2 v_uv;
uniform sampler2D u_depth;
uniform mat4 u_inv_proj;
uniform mat4 u_proj;
uniform vec2 u_texel;
uniform float u_radius;
uniform float u_bias;
const int KERNEL_SIZE = 16;
uniform vec3 u_kernel[KERNEL_SIZE];
out vec4 frag;

vec3 ViewPosAt(vec2 uv) {
  float d = texture(u_depth, uv).r;
  vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  vec4 vp = u_inv_proj * ndc;
  return vp.xyz / vp.w;
}

float Hash(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }

void main() {
  float center_depth = texture(u_depth, v_uv).r;
  if (center_depth > 0.9999) { frag = vec4(1.0); return; }  // empty background: nothing to occlude
  vec3 pos = ViewPosAt(v_uv);
  vec3 dx = ViewPosAt(v_uv + vec2(u_texel.x, 0.0)) - pos;
  vec3 dy = ViewPosAt(v_uv + vec2(0.0, u_texel.y)) - pos;
  vec3 n = normalize(cross(dx, dy));
  if (dot(n, -pos) < 0.0) n = -n;  // face the camera (the eye sits at the view-space origin)
  float angle = Hash(v_uv) * 6.28318530718;
  float ca = cos(angle), sa = sin(angle);
  vec3 up = (abs(n.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  vec3 tangent = normalize(cross(up, n));
  vec3 bitangent = cross(n, tangent);
  mat3 tbn = mat3(tangent, bitangent, n);
  float occlusion = 0.0;
  for (int i = 0; i < KERNEL_SIZE; ++i) {
    vec3 k = u_kernel[i];
    vec3 kr = vec3(k.x * ca - k.y * sa, k.x * sa + k.y * ca, k.z);
    vec3 sample_pos = pos + (tbn * kr) * u_radius;
    vec4 clip = u_proj * vec4(sample_pos, 1.0);
    if (clip.w <= 0.0) continue;
    vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
    vec3 sampled_pos = ViewPosAt(suv);
    float range_check = smoothstep(0.0, 1.0, u_radius / max(1e-4, abs(pos.z - sampled_pos.z)));
    occlusion += (sampled_pos.z >= sample_pos.z + u_bias ? 1.0 : 0.0) * range_check;
  }
  float ao = 1.0 - occlusion / float(KERNEL_SIZE);
  frag = vec4(ao, ao, ao, 1.0);
}
)";

// 4x4 box blur of the raw AO buffer above (reduces the kernel's inherent
// per-pixel noise before Shade() samples it).
const char* kSsaoBlurFS = R"(#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
uniform vec2 u_texel;
out vec4 frag;
void main() {
  float sum = 0.0;
  for (int dx = -1; dx <= 2; ++dx)
    for (int dy = -1; dy <= 2; ++dy)
      sum += texture(u_tex, v_uv + vec2(float(dx), float(dy)) * u_texel).r;
  float ao = sum / 16.0;
  frag = vec4(ao, ao, ao, 1.0);
}
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
  tex_program_ = CompileProgram(kTexVS, kTexFS, error);
  if (!tex_program_) return false;
  env_bg_program_ = CompileProgram(kEnvBgVS, kEnvBgFS, error);
  if (!env_bg_program_) return false;
  shadow_program_ = CompileProgram(kShadowVS, kShadowFS, error);
  if (!shadow_program_) return false;
  ssao_program_ = CompileProgram(kTexVS, kSsaoFS, error);
  if (!ssao_program_) return false;
  ssao_blur_program_ = CompileProgram(kTexVS, kSsaoBlurFS, error);
  if (!ssao_blur_program_) return false;
  ssao_u_depth_ = glGetUniformLocation(ssao_program_, "u_depth");
  ssao_u_inv_proj_ = glGetUniformLocation(ssao_program_, "u_inv_proj");
  ssao_u_proj_ = glGetUniformLocation(ssao_program_, "u_proj");
  ssao_u_texel_ = glGetUniformLocation(ssao_program_, "u_texel");
  ssao_u_radius_ = glGetUniformLocation(ssao_program_, "u_radius");
  ssao_u_bias_ = glGetUniformLocation(ssao_program_, "u_bias");
  ssao_u_kernel_ = ArrayLocation(ssao_program_, "u_kernel");
  ssao_blur_u_tex_ = glGetUniformLocation(ssao_blur_program_, "u_tex");
  ssao_blur_u_texel_ = glGetUniformLocation(ssao_blur_program_, "u_texel");
  {
    const std::vector<std::array<float, 3>> kernel = BuildSsaoKernel(kSsaoKernelSize, 0x9e3779b9u);
    for (int i = 0; i < kSsaoKernelSize && i < static_cast<int>(kernel.size()); ++i) {
      ssao_kernel_[static_cast<size_t>(i) * 3 + 0] = kernel[static_cast<size_t>(i)][0];
      ssao_kernel_[static_cast<size_t>(i) * 3 + 1] = kernel[static_cast<size_t>(i)][1];
      ssao_kernel_[static_cast<size_t>(i) * 3 + 2] = kernel[static_cast<size_t>(i)][2];
    }
  }
  shadow_u_light_vp_ = glGetUniformLocation(shadow_program_, "u_light_vp");
  tex_u_sampler_ = glGetUniformLocation(tex_program_, "u_tex");
  env_bg_u_tex_ = glGetUniformLocation(env_bg_program_, "u_tex");
  env_bg_u_forward_ = glGetUniformLocation(env_bg_program_, "u_forward");
  env_bg_u_right_ = glGetUniformLocation(env_bg_program_, "u_right");
  env_bg_u_up_ = glGetUniformLocation(env_bg_program_, "u_up");
  env_bg_u_tan_fov_ = glGetUniformLocation(env_bg_program_, "u_tan_fov");
  env_bg_u_aspect_ = glGetUniformLocation(env_bg_program_, "u_aspect");
  env_bg_u_ortho_ = glGetUniformLocation(env_bg_program_, "u_ortho");
  mesh_u_mvp_ = glGetUniformLocation(mesh_program_, "u_mvp");
  mesh_u_view_ = glGetUniformLocation(mesh_program_, "u_view");
  mesh_u_light_vp_ = ArrayLocation(mesh_program_, "u_light_vp");
  mesh_u_shadow_map_ = glGetUniformLocation(mesh_program_, "u_shadow_map");
  mesh_u_shadow_valid_mask_ = glGetUniformLocation(mesh_program_, "u_shadow_valid_mask");
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
  mesh_u_env_map_ = glGetUniformLocation(mesh_program_, "u_env_map");
  mesh_u_env_map_valid_ = glGetUniformLocation(mesh_program_, "u_env_map_valid");
  mesh_u_ssao_map_ = glGetUniformLocation(mesh_program_, "u_ssao_map");
  mesh_u_ssao_valid_ = glGetUniformLocation(mesh_program_, "u_ssao_valid");
  mesh_u_viewport_size_ = glGetUniformLocation(mesh_program_, "u_viewport_size");
  line_u_mvp_ = glGetUniformLocation(line_program_, "u_mvp");
  line_u_color_ = glGetUniformLocation(line_program_, "u_color");
  line_u_size_ = glGetUniformLocation(line_program_, "u_size");
  line_u_offset_ = glGetUniformLocation(line_program_, "u_offset");
  for (int i = 0; i < kMaxClipPlanes; ++i) {
    const std::string name = "u_clip[" + std::to_string(i) + "]";
    mesh_u_clip_[i] = glGetUniformLocation(mesh_program_, name.c_str());
    line_u_clip_[i] = glGetUniformLocation(line_program_, name.c_str());
  }
  mesh_u_clip_count_ = glGetUniformLocation(mesh_program_, "u_clip_count");
  line_u_clip_count_ = glGetUniformLocation(line_program_, "u_clip_count");
  bg_u_top_ = glGetUniformLocation(bg_program_, "u_top");
  bg_u_bottom_ = glGetUniformLocation(bg_program_, "u_bottom");
  // Bind each sampler uniform to its own fixed texture unit right away,
  // once, rather than only inside the per-draw kRendered/kGround branch:
  // every sampler uniform in a program defaults to unit 0 until its value
  // is set, and u_shadow_map (now sampler2DArray, for the per-light shadow
  // atlas) defaulting to the same unit 0 as u_texture/u_env_map (sampler2D)
  // is two *different* sampler types bound to the same unit - undefined
  // behaviour that real drivers (this one included) raise
  // GL_INVALID_OPERATION for on any draw with this program, even a plain
  // Wireframe/Shaded one that never reaches the kRendered branch at all.
  glUseProgram(mesh_program_);
  glUniform1i(mesh_u_texture_, 0);
  glUniform1i(mesh_u_env_map_, 1);
  glUniform1i(mesh_u_shadow_map_, 2);
  glUniform1i(mesh_u_ssao_map_, 3);
  glUseProgram(0);
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
  if (tex_program_) glDeleteProgram(tex_program_);
  if (env_bg_program_) glDeleteProgram(env_bg_program_);
  if (shadow_program_) glDeleteProgram(shadow_program_);
  if (shadow_fbo_) glDeleteFramebuffers(1, &shadow_fbo_);
  if (shadow_array_tex_) glDeleteTextures(1, &shadow_array_tex_);
  shadow_program_ = shadow_fbo_ = shadow_array_tex_ = 0;
  shadow_pass_ = false;
  shadow_valid_mask_ = 0;
  if (ssao_program_) glDeleteProgram(ssao_program_);
  if (ssao_blur_program_) glDeleteProgram(ssao_blur_program_);
  if (ssao_depth_fbo_) glDeleteFramebuffers(1, &ssao_depth_fbo_);
  if (ssao_depth_tex_) glDeleteTextures(1, &ssao_depth_tex_);
  if (ssao_fbo_) glDeleteFramebuffers(1, &ssao_fbo_);
  if (ssao_raw_tex_) glDeleteTextures(1, &ssao_raw_tex_);
  if (ssao_blur_fbo_) glDeleteFramebuffers(1, &ssao_blur_fbo_);
  if (ssao_blur_tex_) glDeleteTextures(1, &ssao_blur_tex_);
  ssao_program_ = ssao_blur_program_ = ssao_depth_fbo_ = ssao_depth_tex_ = 0;
  ssao_fbo_ = ssao_raw_tex_ = ssao_blur_fbo_ = ssao_blur_tex_ = ssao_tex_ = 0;
  ssao_depth_w_ = ssao_depth_h_ = 0;
  ssao_prepass_ = false;
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (color_vbo_) glDeleteBuffers(1, &color_vbo_);
  if (uv_vbo_) glDeleteBuffers(1, &uv_vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (bg_vao_) glDeleteVertexArrays(1, &bg_vao_);
  for (auto& [path, tex] : textures_) if (tex) glDeleteTextures(1, &tex);
  textures_.clear();
  missing_textures_.clear();
  mesh_program_ = line_program_ = bg_program_ = tex_program_ = env_bg_program_ = vao_ = vbo_ = color_vbo_ = uv_vbo_ =
      bg_vao_ = 0;
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

#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0 0x3000
#endif

void GlRenderer::SetClipPlanes(const std::vector<std::array<float, 4>>& planes) {
  clip_planes_.assign(planes.begin(), planes.begin() + std::min<size_t>(planes.size(), kMaxClipPlanes));
  for (int i = 0; i < kMaxClipPlanes; ++i) {
    if (i < static_cast<int>(clip_planes_.size())) glEnable(GL_CLIP_DISTANCE0 + i);
    else glDisable(GL_CLIP_DISTANCE0 + i);
  }
}

void GlRenderer::ApplyClipUniforms(const GLint* locations, GLint count_location) {
  glUniform1i(count_location, static_cast<int>(clip_planes_.size()));
  for (size_t i = 0; i < clip_planes_.size(); ++i) {
    const std::array<float, 4>& p = clip_planes_[i];
    glUniform4f(locations[i], p[0], p[1], p[2], p[3]);
  }
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
  if (shadow_pass_ || ssao_prepass_) {
    // Depth-only: only positions (attribute 0 of the shared vao_/vbo_
    // layout) matter, so every other MeshMode/material/texture argument is
    // ignored here - the same triangle data still casts a correct shadow
    // (shadow_pass_) or writes correct depth for the SSAO prepass
    // (ssao_prepass_) regardless of how the main pass would have shaded
    // it. shadow_program_'s "u_light_vp" uniform is really just "whatever
    // mat4 maps a_pos to clip space" - the main camera's own mvp works
    // through the exact same shader during the SSAO prepass.
    glUseProgram(shadow_program_);
    glUniformMatrix4fv(shadow_u_light_vp_, 1, GL_FALSE, (shadow_pass_ ? current_shadow_vp_ : ssao_prepass_mvp_).Data());
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDrawArrays(GL_TRIANGLES, 0, vertex_count);
    glBindVertexArray(0);
    return;
  }
  const bool wants_colors = mode == kVertexColor || mode == kDepthGray;
  const bool use_colors = wants_colors && colors && colors->size() >= static_cast<size_t>(vertex_count) * 3;
  if (wants_colors && !use_colors) mode = kLit;
  const bool use_uvs = mode == kRendered && uvs && uvs->size() >= static_cast<size_t>(vertex_count) * 2 && material_.texture != 0;
  const Mat4 mvp = proj_ * view_;
  // An orthographic projection has w == 1 for every vertex (m[15] == 1);
  // perspective leaves m[15] == 0. The shader uses this to pick the view
  // direction for reflections.
  const bool ortho = proj_.m[15] == 1.0f;
  glUseProgram(mesh_program_);
  glUniformMatrix4fv(mesh_u_mvp_, 1, GL_FALSE, mvp.Data());
  glUniformMatrix4fv(mesh_u_view_, 1, GL_FALSE, view_.Data());
  glUniformMatrix4fv(mesh_u_light_vp_, kMaxGpuLights, GL_FALSE, light_vp_[0].Data());
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
    glUniform1i(mesh_u_env_map_valid_, env_map_tex_ != 0 ? 1 : 0);
    glUniform1i(mesh_u_env_map_, 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, env_map_tex_);
    glUniform1i(mesh_u_shadow_valid_mask_, static_cast<int>(shadow_valid_mask_));
    glUniform1i(mesh_u_shadow_map_, 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_array_tex_);
    glUniform1i(mesh_u_ssao_valid_, ssao_tex_ != 0 ? 1 : 0);
    glUniform1i(mesh_u_ssao_map_, 3);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, ssao_tex_);
    {
      // SSAO is a screen-space buffer, sampled by this fragment's own
      // gl_FragCoord rather than any mesh UV - it needs to know the
      // current viewport's pixel size to turn that into a [0,1] texture
      // coordinate (see kMeshFS's Shade()).
      GLint vp[4] = {0, 0, 1, 1};
      glGetIntegerv(GL_VIEWPORT, vp);
      glUniform2f(mesh_u_viewport_size_, static_cast<float>(std::max(vp[2], 1)), static_cast<float>(std::max(vp[3], 1)));
    }
    glActiveTexture(GL_TEXTURE0);
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
  ApplyClipUniforms(mesh_u_clip_, mesh_u_clip_count_);
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
  if (mode == kRendered || mode == kGround) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
  }
}

void GlRenderer::DrawTriangles(const std::vector<float>& data, Color color, bool lit) {
  DrawMesh(data, nullptr, nullptr, lit ? kLit : kFlat, color, 0.f, 0.f);
}

void GlRenderer::DrawTriangles(const std::vector<float>& data, const std::vector<float>& colors, float alpha) {
  DrawMesh(data, &colors, nullptr, kVertexColor, Color{1.f, 1.f, 1.f, alpha}, 0.f, 0.f);
}

void GlRenderer::DrawTrianglesDepth(const std::vector<float>& data, const std::vector<float>& colors) {
  DrawMesh(data, &colors, nullptr, kDepthGray, Color{1.f, 1.f, 1.f, 1.f}, 0.f, 0.f);
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

bool GlRenderer::BeginShadowPass(int light_index, kernel::Vector3d light_dir, kernel::Point3d center, double radius) {
  if (light_index < 0 || light_index >= kMaxGpuLights) return false;
  if (!light_dir.Unitize()) return false;
  if (!(radius > 1e-6)) radius = 1.0;
  if (!shadow_fbo_) {
    glGenTextures(1, &shadow_array_tex_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_array_tex_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, kShadowMapSize, kShadowMapSize, kMaxGpuLights, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.f, 1.f, 1.f, 1.f};  // max depth outside the light's frustum: never shadowed
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glGenFramebuffers(1, &shadow_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadow_array_tex_, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
      glDeleteFramebuffers(1, &shadow_fbo_);
      glDeleteTextures(1, &shadow_array_tex_);
      shadow_fbo_ = shadow_array_tex_ = 0;
      return false;
    }
  }
  GLint prev_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  shadow_prev_fbo_ = static_cast<GLuint>(prev_fbo);
  glGetIntegerv(GL_VIEWPORT, shadow_prev_viewport_);
  // The light sits well outside the scene's bounding sphere, looking back
  // at its centre; an orthographic frustum exactly big enough to cover
  // that sphere keeps the whole visible scene in this light's own shadow
  // layer at the available resolution.
  const kernel::Point3d eye = center - light_dir * (radius * 3.0);
  kernel::Vector3d up(0, 0, 1);
  if (std::fabs(light_dir.z) > 0.95) up = kernel::Vector3d(0, 1, 0);
  const Mat4 light_view = Mat4::LookAt(eye, center, up);
  const Mat4 light_proj = Mat4::Ortho(-radius, radius, -radius, radius, 0.01, radius * 6.0);
  current_shadow_vp_ = light_proj * light_view;
  light_vp_[static_cast<size_t>(light_index)] = current_shadow_vp_;
  glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadow_array_tex_, 0, light_index);
  glViewport(0, 0, kShadowMapSize, kShadowMapSize);
  glClear(GL_DEPTH_BUFFER_BIT);
  shadow_pass_ = true;
  shadow_valid_mask_ |= (1u << static_cast<unsigned>(light_index));
  return true;
}

void GlRenderer::EndShadowPass() {
  if (!shadow_pass_) return;
  shadow_pass_ = false;
  glBindFramebuffer(GL_FRAMEBUFFER, shadow_prev_fbo_);
  glViewport(shadow_prev_viewport_[0], shadow_prev_viewport_[1], shadow_prev_viewport_[2], shadow_prev_viewport_[3]);
}

namespace {
// A single-colour-attachment, no-depth FBO/texture pair, (re)created at
// `width`x`height` - the shape every SSAO render target below shares
// (the raw AO buffer, and the blurred one Shade() actually samples).
bool EnsureColorTarget(GLuint& fbo, GLuint& tex, int width, int height) {
  if (!fbo) glGenFramebuffers(1, &fbo);
  if (!tex) glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  return ok;
}
}  // namespace

void GlRenderer::BeginSsaoPass() {
  GLint vp[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_VIEWPORT, vp);
  const int width = vp[2], height = vp[3];
  if (width <= 0 || height <= 0) { ssao_prepass_ = false; return; }
  if (width != ssao_depth_w_ || height != ssao_depth_h_ || !ssao_depth_fbo_) {
    if (!ssao_depth_fbo_) glGenFramebuffers(1, &ssao_depth_fbo_);
    if (!ssao_depth_tex_) glGenTextures(1, &ssao_depth_tex_);
    glBindTexture(GL_TEXTURE_2D, ssao_depth_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, ssao_depth_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ssao_depth_tex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool depth_ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    const bool raw_ok = EnsureColorTarget(ssao_fbo_, ssao_raw_tex_, width, height);
    const bool blur_ok = EnsureColorTarget(ssao_blur_fbo_, ssao_blur_tex_, width, height);
    if (!depth_ok || !raw_ok || !blur_ok) { ssao_depth_w_ = ssao_depth_h_ = 0; ssao_prepass_ = false; return; }
    ssao_depth_w_ = width;
    ssao_depth_h_ = height;
  }
  GLint prev_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  ssao_prepass_prev_fbo_ = static_cast<GLuint>(prev_fbo);
  glGetIntegerv(GL_VIEWPORT, ssao_prepass_prev_viewport_);
  ssao_prepass_mvp_ = proj_ * view_;
  glBindFramebuffer(GL_FRAMEBUFFER, ssao_depth_fbo_);
  glViewport(0, 0, width, height);
  glClear(GL_DEPTH_BUFFER_BIT);
  ssao_prepass_ = true;
}

void GlRenderer::EndSsaoPass() {
  if (!ssao_prepass_) return;
  ssao_prepass_ = false;
  const int w = ssao_depth_w_, h = ssao_depth_h_;
  const GLboolean depth_test_was_on = glIsEnabled(GL_DEPTH_TEST);
  glDisable(GL_DEPTH_TEST);

  // Compute the raw AO buffer from ssao_depth_tex_.
  glBindFramebuffer(GL_FRAMEBUFFER, ssao_fbo_);
  glViewport(0, 0, w, h);
  glUseProgram(ssao_program_);
  const Mat4 inv_proj = proj_.Inverse();
  glUniformMatrix4fv(ssao_u_inv_proj_, 1, GL_FALSE, inv_proj.Data());
  glUniformMatrix4fv(ssao_u_proj_, 1, GL_FALSE, proj_.Data());
  glUniform2f(ssao_u_texel_, 1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
  glUniform1f(ssao_u_radius_, ssao_radius_);
  glUniform1f(ssao_u_bias_, ssao_bias_);
  glUniform3fv(ssao_u_kernel_, kSsaoKernelSize, ssao_kernel_.data());
  glUniform1i(ssao_u_depth_, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, ssao_depth_tex_);
  glBindVertexArray(bg_vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // Blur it into ssao_blur_tex_ - the texture Shade() actually samples.
  glBindFramebuffer(GL_FRAMEBUFFER, ssao_blur_fbo_);
  glUseProgram(ssao_blur_program_);
  glUniform1i(ssao_blur_u_tex_, 0);
  glUniform2f(ssao_blur_u_texel_, 1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
  glBindTexture(GL_TEXTURE_2D, ssao_raw_tex_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, ssao_prepass_prev_fbo_);
  glViewport(ssao_prepass_prev_viewport_[0], ssao_prepass_prev_viewport_[1], ssao_prepass_prev_viewport_[2],
             ssao_prepass_prev_viewport_[3]);
  if (depth_test_was_on) glEnable(GL_DEPTH_TEST);
  ssao_tex_ = ssao_blur_tex_;
}

void GlRenderer::DrawLines(const std::vector<float>& data, Color color, float width_px) {
  if (data.empty() || shadow_pass_ || ssao_prepass_) return;  // only mesh triangles cast a shadow/write SSAO depth - see BeginShadowPass/BeginSsaoPass
  const Mat4 mvp = proj_ * view_;
  glUseProgram(line_program_);
  glUniformMatrix4fv(line_u_mvp_, 1, GL_FALSE, mvp.Data());
  glUniform4f(line_u_color_, color.r, color.g, color.b, color.a);
  glUniform1f(line_u_size_, 1.0f);
  glUniform2f(line_u_offset_, 0.f, 0.f);
  ApplyClipUniforms(line_u_clip_, line_u_clip_count_);
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
  if (data.empty() || shadow_pass_ || ssao_prepass_) return;  // only mesh triangles cast a shadow/write SSAO depth - see BeginShadowPass/BeginSsaoPass
  const Mat4 mvp = proj_ * view_;
  glEnable(GL_PROGRAM_POINT_SIZE);
  glUseProgram(line_program_);
  glUniformMatrix4fv(line_u_mvp_, 1, GL_FALSE, mvp.Data());
  glUniform4f(line_u_color_, color.r, color.g, color.b, color.a);
  glUniform1f(line_u_size_, size);
  glUniform2f(line_u_offset_, 0.f, 0.f);
  ApplyClipUniforms(line_u_clip_, line_u_clip_count_);
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
  if (IsProceduralTexture(path)) {
    std::vector<unsigned char> rgba;
    if (!GenerateProceduralTexture(path, 256, 256, rgba)) { missing_textures_.insert(path); return 0; }
    const GLuint tex = CreateTexture(256, 256, rgba.data(), 4);
    textures_[path] = tex;
    return tex;
  }
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

void GlRenderer::DrawFullscreenTexture(GLuint texture) {
  if (!texture || !tex_program_) return;
  glDisable(GL_DEPTH_TEST);
  glUseProgram(tex_program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform1i(tex_u_sampler_, 0);
  glBindVertexArray(bg_vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glEnable(GL_DEPTH_TEST);
}

void GlRenderer::DrawEnvironmentBackground(GLuint texture, kernel::Vector3d forward, kernel::Vector3d right,
                                            kernel::Vector3d up, double tan_half_fov_y, double aspect, bool ortho) {
  if (!texture || !env_bg_program_) return;
  // Mirrors DrawFullscreenTexture's own GL state handling exactly (the one
  // caller, Viewport.cpp's DrawBackgroundImage, already wraps this in its
  // own EnableDepthTest(false)/(true) too - the same harmless redundancy
  // DrawFullscreenTexture's call site already has).
  glDisable(GL_DEPTH_TEST);
  glUseProgram(env_bg_program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform1i(env_bg_u_tex_, 0);
  glUniform3f(env_bg_u_forward_, static_cast<float>(forward.x), static_cast<float>(forward.y), static_cast<float>(forward.z));
  glUniform3f(env_bg_u_right_, static_cast<float>(right.x), static_cast<float>(right.y), static_cast<float>(right.z));
  glUniform3f(env_bg_u_up_, static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z));
  glUniform1f(env_bg_u_tan_fov_, static_cast<float>(tan_half_fov_y));
  glUniform1f(env_bg_u_aspect_, static_cast<float>(aspect));
  glUniform1i(env_bg_u_ortho_, ortho ? 1 : 0);
  glBindVertexArray(bg_vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glEnable(GL_DEPTH_TEST);
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
