#include "render/PathTracer.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include "render/ImageIO.h"
#include "render/MaterialLibrary.h"

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

namespace {

// ---- small deterministic RNG / low-discrepancy helpers --------------------

unsigned Hash32(unsigned x) {
  x ^= x >> 16; x *= 0x7feb352du;
  x ^= x >> 15; x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// xorshift32: fast, good enough for path tracing, fully deterministic from
// its seed (the "fixed random sequence per pixel" the task asks for: every
// pixel's seed is a hash of its coordinates, not wall-clock or thread id).
float NextFloat(unsigned& s) {
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return static_cast<float>(s >> 8) / static_cast<float>(1u << 24);
}

double Halton(unsigned index, int base) {
  double f = 1.0, r = 0.0;
  while (index > 0) { f /= base; r += f * (index % static_cast<unsigned>(base)); index /= static_cast<unsigned>(base); }
  return r;
}

Vector3d Reflect(const Vector3d& d, const Vector3d& n) { return d - n * (2.0 * ON_DotProduct(d, n)); }

// Snell refraction of incident direction `d` (unit, pointing into the
// surface) through a normal `n` (unit, pointing against `d`) with relative
// index of refraction eta = n1/n2. Returns false on total internal reflection.
bool Refract(const Vector3d& d, const Vector3d& n, double eta, Vector3d& out) {
  const double cosi = -ON_DotProduct(d, n);
  const double sin2t = eta * eta * std::max(0.0, 1.0 - cosi * cosi);
  if (sin2t > 1.0) return false;
  const double cost = std::sqrt(1.0 - sin2t);
  out = d * eta + n * (eta * cosi - cost);
  return true;
}

double SchlickFresnel(double cosi, double ior) {
  double r0 = (1.0 - ior) / (1.0 + ior);
  r0 *= r0;
  const double m = std::clamp(1.0 - cosi, 0.0, 1.0);
  return r0 + (1.0 - r0) * m * m * m * m * m;
}

void MakeBasis(const Vector3d& n, Vector3d& t, Vector3d& b) {
  const Vector3d ref = std::fabs(n.z) < 0.999 ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  t = ON_CrossProduct(ref, n); t.Unitize();
  b = ON_CrossProduct(n, t);
}

// Cosine-weighted hemisphere sample about `n`.
Vector3d SampleCosineHemisphere(const Vector3d& n, double u1, double u2) {
  const double r = std::sqrt(u1), theta = 2.0 * ON_PI * u2;
  const double x = r * std::cos(theta), y = r * std::sin(theta), z = std::sqrt(std::max(0.0, 1.0 - u1));
  Vector3d t, b;
  MakeBasis(n, t, b);
  Vector3d dir = t * x + b * y + n * z;
  dir.Unitize();
  return dir;
}

// Approximate glossy lobe: a cosine-power (Phong-like) sample around a
// central direction, exponent derived from the GGX roughness so Gloss=1
// (roughness 0) is a mirror and Gloss=0 is nearly diffuse-wide. Not a true
// GGX importance sample, but gives the right qualitative falloff for the
// reflective/refractive lobes without a full microfacet sampler.
Vector3d SampleGlossyLobe(const Vector3d& central, double roughness, double u1, double u2) {
  Vector3d c = central;
  c.Unitize();
  const double alpha = std::max(roughness * roughness, 1e-4);
  const double exponent = std::max(2.0 / (alpha * alpha) - 2.0, 0.0);
  const double cos_theta = std::pow(u1, 1.0 / (exponent + 1.0));
  const double sin_theta = std::sqrt(std::max(0.0, 1.0 - cos_theta * cos_theta));
  const double phi = 2.0 * ON_PI * u2;
  Vector3d t, b;
  MakeBasis(c, t, b);
  Vector3d dir = t * (sin_theta * std::cos(phi)) + b * (sin_theta * std::sin(phi)) + c * cos_theta;
  dir.Unitize();
  return dir;
}

kernel::Vector3d ToVec(Color c) { return kernel::Vector3d(c.r, c.g, c.b); }

// Component-wise product. ON_3dVector's own operator* between two vectors
// is the dot product (returns a double), so every RGB "tint" multiply in
// this file goes through here instead.
Vector3d Mul(const Vector3d& a, const Vector3d& b) { return Vector3d(a.x * b.x, a.y * b.y, a.z * b.z); }

// ACES filmic tonemap (Narkowicz fit) + 1/2.2 gamma.
float AcesTonemap(float x) {
  const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
  return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

unsigned char ToByte(float linear) {
  const float mapped = AcesTonemap(std::max(linear, 0.0f));
  const float gamma = std::pow(mapped, 1.0f / 2.2f);
  return static_cast<unsigned char>(std::clamp(gamma, 0.0f, 1.0f) * 255.0f + 0.5f);
}

}  // namespace

// ---------------------------------------------------------------------------
// Scene gathering
// ---------------------------------------------------------------------------

void PathTracer::Prepare(const Document& doc, const CameraState& camera, double aspect, double curve_tol,
                         double surface_tol) {
  doc_ = &doc;
  camera_ = camera;
  aspect_ = aspect;
  render_settings_ = doc.Render();
  materials_.clear();
  tex_cache_.clear();
  std::vector<render::BvhTriangle> tris;

  int object_index = 0;
  for (const SceneObject& o : doc.Objects()) {
    if (!doc.IsObjectVisible(o)) { ++object_index; continue; }
    o.EnsureDisplay(curve_tol, surface_tol);
    const DisplayCache& d = o.Display();
    if (d.triangles.empty()) { ++object_index; continue; }
    const Material m = doc.MaterialFor(o);
    const int mat_index = static_cast<int>(materials_.size());
    materials_.push_back(m);
    const std::vector<float>* uvs = nullptr;
    if (!m.texture_path.empty()) {
      const TextureMapping mapping = o.mapping != TextureMapping::Default ? o.mapping : m.mapping;
      const float scale = o.mapping != TextureMapping::Default ? o.mapping_scale : m.mapping_scale;
      o.EnsureMappedUVs(mapping, scale);
      if (d.mapped_uvs.size() == d.triangles.size() / 3) uvs = &d.mapped_uvs;
    }
    const size_t n = d.triangles.size() / 6;
    for (size_t i = 0; i + 2 < n; i += 3) {
      render::BvhTriangle t;
      const float* a = &d.triangles[i * 6]; const float* b = &d.triangles[(i + 1) * 6]; const float* c = &d.triangles[(i + 2) * 6];
      t.v0 = Point3d(a[0], a[1], a[2]); t.v1 = Point3d(b[0], b[1], b[2]); t.v2 = Point3d(c[0], c[1], c[2]);
      t.n0 = Vector3d(a[3], a[4], a[5]); t.n1 = Vector3d(b[3], b[4], b[5]); t.n2 = Vector3d(c[3], c[4], c[5]);
      if (uvs) {
        t.uv0[0] = (*uvs)[i * 2]; t.uv0[1] = (*uvs)[i * 2 + 1];
        t.uv1[0] = (*uvs)[(i + 1) * 2]; t.uv1[1] = (*uvs)[(i + 1) * 2 + 1];
        t.uv2[0] = (*uvs)[(i + 2) * 2]; t.uv2[1] = (*uvs)[(i + 2) * 2 + 1];
      }
      t.material = mat_index;
      t.object = object_index;
      tris.push_back(t);
    }
    ++object_index;
  }
  bvh_.Build(std::move(tris));

  ground_ = GroundPlane{};
  ground_.enabled = render_settings_.ground_plane;
  if (ground_.enabled) {
    kernel::BoundingBox box;
    const bool has = doc.VisibleBoundingBox(box);
    if (!has) { box.min = Point3d(-10, -10, 0); box.max = Point3d(10, 10, 0); }
    ground_.z = render_settings_.ground_auto_height ? box.min.z : render_settings_.ground_height;
    const double radius = std::max({box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, 1.0}) / 2;
    ground_.half_size = std::max(radius * 40.0, 200.0);
    ground_.color = render_settings_.ground_color;
    ground_.shadows = render_settings_.ground_shadows;
  }

  lights_.clear();
  // Empirical radiometric scale so the app's 0..~2 "Intensity" slider gives
  // a well-exposed image at typical (tens-of-units) scene scale; matches no
  // particular unit system, same spirit as the GL rasteriser's flat scale.
  constexpr float kPointScale = 400.0f, kAreaScale = 12.0f, kDirScale = 1.0f;
  for (const Light& L : doc.Lights()) {
    if (!L.enabled) continue;
    SceneLight s;
    s.type = L.type;
    s.position = L.position;
    Vector3d dir = L.direction; dir.Unitize(); s.direction = dir;
    switch (L.type) {
      case LightType::Point:
        s.r = L.color.r * L.intensity * kPointScale; s.g = L.color.g * L.intensity * kPointScale; s.b = L.color.b * L.intensity * kPointScale;
        break;
      case LightType::Spot: {
        s.r = L.color.r * L.intensity * kPointScale; s.g = L.color.g * L.intensity * kPointScale; s.b = L.color.b * L.intensity * kPointScale;
        const double outer = std::clamp(static_cast<double>(L.spot_angle), 1.0, 89.0) * ON_PI / 180.0;
        const double inner = outer * (0.35 + 0.6 * std::clamp(L.spot_hardness, 0.f, 1.f));
        s.cos_outer = std::cos(outer); s.cos_inner = std::cos(inner);
        break;
      }
      case LightType::Directional:
        s.r = L.color.r * L.intensity * kDirScale; s.g = L.color.g * L.intensity * kDirScale; s.b = L.color.b * L.intensity * kDirScale;
        break;
      case LightType::Rectangular: {
        Vector3d xa = L.x_axis; xa.Unitize();
        Vector3d ya = ON_CrossProduct(s.direction, xa); ya.Unitize();
        s.x_axis = xa; s.y_axis = ya; s.length = L.length; s.width = L.width;
        s.r = L.color.r * L.intensity * kAreaScale; s.g = L.color.g * L.intensity * kAreaScale; s.b = L.color.b * L.intensity * kAreaScale;
        break;
      }
      case LightType::Linear: {
        Vector3d xa = L.x_axis; xa.Unitize();
        Vector3d ya = ON_CrossProduct(s.direction, xa); ya.Unitize();
        s.x_axis = xa; s.y_axis = ya; s.length = L.length; s.width = std::max(L.length * 0.03, 0.02);
        s.r = L.color.r * L.intensity * kAreaScale; s.g = L.color.g * L.intensity * kAreaScale; s.b = L.color.b * L.intensity * kAreaScale;
        break;
      }
    }
    lights_.push_back(s);
  }
  if (render_settings_.sun) {
    SceneLight s;
    s.type = LightType::Directional;
    s.is_sun = true;
    const double az = render_settings_.sun_azimuth * ON_PI / 180.0, alt = render_settings_.sun_altitude * ON_PI / 180.0;
    const Vector3d towards_sun(std::cos(alt) * std::sin(az), std::cos(alt) * std::cos(az), std::sin(alt));
    s.direction = -towards_sun;  // travels from the sun into the scene, matches Light::direction convention
    s.r = render_settings_.sun_color.r * render_settings_.sun_intensity;
    s.g = render_settings_.sun_color.g * render_settings_.sun_intensity;
    s.b = render_settings_.sun_color.b * render_settings_.sun_intensity;
    lights_.push_back(s);
  }
}

// ---------------------------------------------------------------------------
// Sky / textures
// ---------------------------------------------------------------------------

Vector3d PathTracer::SkyColor(const Vector3d& dir) const {
  const RenderSettings& r = render_settings_;
  Vector3d d = dir; d.Unitize();
  if (r.background == RenderSettings::Background::Solid) return ToVec(r.background_color);
  if (r.background == RenderSettings::Background::Gradient) {
    const double t = std::clamp(d.z * 0.5 + 0.5, 0.0, 1.0);
    return ToVec(r.gradient_bottom) * (1.0 - t) + ToVec(r.gradient_top) * t;
  }
  // Sky: physically-plausible gradient (horizon light, zenith blue-ish)
  // with a bright sun disc when the sun is enabled.
  const double t = std::clamp(d.z * 0.5 + 0.5, 0.0, 1.0);
  Vector3d zenith(0.30, 0.45, 0.75), horizon(0.75, 0.82, 0.90);
  Vector3d sky = horizon * (1.0 - std::pow(t, 0.7)) + zenith * std::pow(t, 0.7);
  if (r.sun) {
    const double az = r.sun_azimuth * ON_PI / 180.0, alt = r.sun_altitude * ON_PI / 180.0;
    Vector3d sun_dir(std::cos(alt) * std::sin(az), std::cos(alt) * std::cos(az), std::sin(alt));
    sun_dir.Unitize();
    const double cosang = std::max(0.0, ON_DotProduct(d, sun_dir));
    // Disc (~0.53 deg real sun, exaggerated a little for visibility at
    // render resolutions) plus a soft glow falloff.
    const double disc = cosang > 0.9998 ? 1.0 : 0.0;
    const double glow = std::pow(cosang, 256.0) * 0.6;
    sky += ToVec(r.sun_color) * (disc * 8.0 * r.sun_intensity + glow * r.sun_intensity);
  }
  return sky;
}

const PathTracer::TexCache* PathTracer::TextureFor(const std::string& path) const {
  for (auto& [p, c] : tex_cache_) if (p == path) return &c;
  TexCache tc;
  bool ok = false;
  if (IsProceduralTexture(path)) {
    ok = GenerateProceduralTexture(path, 128, 128, tc.rgba);
    tc.w = tc.h = ok ? 128 : 0;
  } else {
    Image img; std::string err;
    if (LoadImageFile(path, img, err) && img.Valid()) { tc.rgba = img.rgba; tc.w = img.width; tc.h = img.height; ok = true; }
  }
  tex_cache_.emplace_back(path, ok ? tc : TexCache{});
  return &tex_cache_.back().second;
}

Vector3d PathTracer::AlbedoAt(const Material& mat, float u, float v) const {
  Vector3d base(mat.diffuse.r, mat.diffuse.g, mat.diffuse.b);
  if (mat.texture_path.empty()) return base;
  const TexCache* tc = TextureFor(mat.texture_path);
  if (!tc || tc->w <= 0 || tc->h <= 0) return base;
  float fu = u - std::floor(u), fv = 1.0f - (v - std::floor(v));  // v flipped to match GL texture convention
  const float fx = fu * tc->w - 0.5f, fy = fv * tc->h - 0.5f;
  int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
  const float tx = fx - x0, ty = fy - y0;
  auto wrap = [](int v, int n) { v %= n; return v < 0 ? v + n : v; };
  auto sample = [&](int x, int y) {
    x = wrap(x, tc->w); y = wrap(y, tc->h);
    const unsigned char* p = &tc->rgba[(static_cast<size_t>(y) * tc->w + x) * 4];
    return Vector3d(p[0] / 255.0, p[1] / 255.0, p[2] / 255.0);
  };
  const Vector3d c00 = sample(x0, y0), c10 = sample(x0 + 1, y0), c01 = sample(x0, y0 + 1), c11 = sample(x0 + 1, y0 + 1);
  const Vector3d top = c00 * (1 - tx) + c10 * tx, bot = c01 * (1 - tx) + c11 * tx;
  const Vector3d tex = top * (1 - ty) + bot * ty;
  return Vector3d(base.x * tex.x, base.y * tex.y, base.z * tex.z);
}

// ---------------------------------------------------------------------------
// Occlusion / direct lighting
// ---------------------------------------------------------------------------

bool PathTracer::Occluded(const Point3d& from, const Point3d& to) const {
  Vector3d d = to - from;
  const double dist = d.Length();
  if (dist < 1e-9) return false;
  d.Unitize();
  const float tmax = static_cast<float>(dist) - 1e-3f;
  if (tmax <= 0) return false;
  if (bvh_.IntersectAny(from, d, 1e-4f, tmax)) return true;
  if (ground_.enabled && ground_.shadows && std::fabs(d.z) > 1e-9) {
    const double t = (ground_.z - from.z) / d.z;
    if (t > 1e-4 && t < tmax) return true;
  }
  return false;
}

Vector3d PathTracer::SampleDirectLighting(const Point3d& p, const Vector3d& n, const Vector3d& /*wo*/,
                                          const Material& /*mat*/, const Vector3d& albedo, unsigned& rng) const {
  if (lights_.empty()) return Vector3d(0, 0, 0);
  const int idx = std::min(static_cast<int>(NextFloat(rng) * lights_.size()), static_cast<int>(lights_.size()) - 1);
  const SceneLight& L = lights_[static_cast<size_t>(idx)];
  const double pmf = 1.0 / lights_.size();
  const Vector3d brdf = albedo / ON_PI;

  if (L.type == LightType::Point || L.type == LightType::Spot) {
    Vector3d to = L.position - p;
    const double dist2 = to.x * to.x + to.y * to.y + to.z * to.z;
    if (dist2 < 1e-9) return Vector3d(0, 0, 0);
    Vector3d wi = to; wi.Unitize();
    const double cosS = ON_DotProduct(n, wi);
    if (cosS <= 0) return Vector3d(0, 0, 0);
    double cone = 1.0;
    if (L.type == LightType::Spot) {
      const double cosang = ON_DotProduct(-wi, L.direction);
      if (cosang < L.cos_outer) return Vector3d(0, 0, 0);
      cone = std::clamp((cosang - L.cos_outer) / std::max(L.cos_inner - L.cos_outer, 1e-6), 0.0, 1.0);
    }
    if (Occluded(p + n * 1e-4, L.position)) return Vector3d(0, 0, 0);
    const Vector3d Le(L.r, L.g, L.b);
    return Vector3d(brdf.x * Le.x, brdf.y * Le.y, brdf.z * Le.z) * (cosS * cone / dist2) / pmf;
  }
  if (L.type == LightType::Directional) {
    const Vector3d wi = -L.direction;
    const double cosS = ON_DotProduct(n, wi);
    if (cosS <= 0) return Vector3d(0, 0, 0);
    if (Occluded(p + n * 1e-4, p + wi * 1e6)) return Vector3d(0, 0, 0);
    const Vector3d Le(L.r, L.g, L.b);
    return Vector3d(brdf.x * Le.x, brdf.y * Le.y, brdf.z * Le.z) * cosS / pmf;
  }
  // Rectangular / linear area lights: uniform sample on the rectangle, MIS
  // (power heuristic) against the diffuse cosine BSDF pdf.
  const double u1 = NextFloat(rng) - 0.5, u2 = NextFloat(rng) - 0.5;
  const Point3d sample = L.position + L.x_axis * (u1 * L.length) + L.y_axis * (u2 * L.width);
  Vector3d to = sample - p;
  const double dist2 = to.x * to.x + to.y * to.y + to.z * to.z;
  if (dist2 < 1e-9) return Vector3d(0, 0, 0);
  Vector3d wi = to; wi.Unitize();
  const double cosS = ON_DotProduct(n, wi);
  const double cosL = ON_DotProduct(L.direction, -wi);
  if (cosS <= 0 || cosL <= 0) return Vector3d(0, 0, 0);
  if (Occluded(p + n * 1e-4, sample)) return Vector3d(0, 0, 0);
  const double area = std::max(L.length * L.width, 1e-9);
  const double pdf_area = 1.0 / area;
  const double pdf_light_solid = pdf_area * dist2 / cosL;
  const double pdf_bsdf_solid = cosS / ON_PI;
  const double weight = (pdf_light_solid * pdf_light_solid) /
                        std::max(pdf_light_solid * pdf_light_solid + pdf_bsdf_solid * pdf_bsdf_solid, 1e-12);
  const Vector3d Le(L.r, L.g, L.b);
  return Vector3d(brdf.x * Le.x, brdf.y * Le.y, brdf.z * Le.z) * (cosS * cosL / dist2 / pdf_area * weight) / pmf;
}

namespace {
// Returns the emission (with MIS weight applied) picked up by a BSDF-sampled
// ray that heads straight at a rectangular/linear light before it would hit
// any occluder. `max_t` is the distance to the closest scene/ground hit (or
// a very large number when the ray escaped to the sky).
Vector3d MisAreaLightHit(const std::vector<PathTracer::SceneLight>* lights, const Point3d& origin, const Vector3d& dir,
                         double max_t, double pdf_bsdf_solid) {
  Vector3d total(0, 0, 0);
  for (const auto& L : *lights) {
    if (L.type != LightType::Rectangular && L.type != LightType::Linear) continue;
    const double denom = ON_DotProduct(dir, L.direction);
    if (std::fabs(denom) < 1e-9) continue;
    const double t = ON_DotProduct(L.position - origin, L.direction) / denom;
    if (t <= 1e-4 || t >= max_t) continue;
    const Point3d hit = origin + dir * t;
    const Vector3d rel = hit - L.position;
    const double u = ON_DotProduct(rel, L.x_axis), v = ON_DotProduct(rel, L.y_axis);
    if (std::fabs(u) > L.length * 0.5 || std::fabs(v) > L.width * 0.5) continue;
    const double cosL = ON_DotProduct(L.direction, -dir);
    if (cosL <= 0) continue;
    const double dist2 = t * t;
    const double area = std::max(L.length * L.width, 1e-9);
    const double pdf_light_solid = (1.0 / area) * dist2 / cosL;
    const double weight = (pdf_bsdf_solid * pdf_bsdf_solid) /
                          std::max(pdf_bsdf_solid * pdf_bsdf_solid + pdf_light_solid * pdf_light_solid, 1e-12);
    total += Vector3d(L.r, L.g, L.b) * weight;
  }
  return total;
}
}  // namespace

// ---------------------------------------------------------------------------
// Path tracing
// ---------------------------------------------------------------------------

Vector3d PathTracer::TracePath(Point3d origin, Vector3d dir, unsigned& rng, int bounces, bool arctic,
                               Vector3d* first_albedo, Vector3d* first_normal) const {
  Vector3d radiance(0, 0, 0), throughput(1, 1, 1);
  dir.Unitize();
  bool specular_bounce = true;  // primary rays see emission/sky unweighted

  for (int bounce = 0; bounce < bounces; ++bounce) {
    render::BvhHit hit;
    const bool hit_scene = bvh_.Intersect(origin, dir, 1e-4f, 1e30f, hit);
    double ground_t = -1;
    if (ground_.enabled && std::fabs(dir.z) > 1e-9) {
      const double t = (ground_.z - origin.z) / dir.z;
      if (t > 1e-4 && (!hit_scene || t < hit.t)) ground_t = t;
    }

    if (!hit_scene && ground_t < 0) {
      radiance += Mul(throughput, SkyColor(dir));
      break;
    }

    Point3d p; Vector3d n, geo_n; Vector3d albedo; Material mat;
    if (ground_t > 0) {
      p = origin + dir * ground_t;
      n = geo_n = Vector3d(0, 0, 1);
      albedo = ToVec(ground_.color);
      mat = Material{};
      mat.diffuse = ground_.color;
      mat.gloss = 0.05f;
    } else {
      const render::BvhTriangle& t = bvh_.Triangles()[static_cast<size_t>(hit.tri)];
      const float w = 1.0f - hit.u - hit.v;
      p = Point3d(t.v0.x * w + t.v1.x * hit.u + t.v2.x * hit.v, t.v0.y * w + t.v1.y * hit.u + t.v2.y * hit.v,
                 t.v0.z * w + t.v1.z * hit.u + t.v2.z * hit.v);
      n = t.n0 * w + t.n1 * hit.u + t.n2 * hit.v;
      if (!n.Unitize()) n = Vector3d(0, 0, 1);
      geo_n = ON_CrossProduct(t.v1 - t.v0, t.v2 - t.v0);
      if (!geo_n.Unitize()) geo_n = n;
      if (ON_DotProduct(geo_n, n) < 0) geo_n = -geo_n;
      const float u = t.uv0[0] * w + t.uv1[0] * hit.u + t.uv2[0] * hit.v;
      const float v = t.uv0[1] * w + t.uv1[1] * hit.u + t.uv2[1] * hit.v;
      mat = materials_[static_cast<size_t>(t.material)];
      albedo = AlbedoAt(mat, u, v);
    }
    if (arctic) {
      // RenderArctic: every material becomes white matte (no reflections,
      // glass or emission), matching the rasteriser's "arctic" mode.
      albedo = Vector3d(0.96, 0.96, 0.96);
      mat = Material{};
      mat.diffuse = Color::FromBytes(245, 245, 245);
      mat.gloss = 0.05f;
    }

    // Face the shading/geometric normals against the incoming ray.
    const bool entering = ON_DotProduct(dir, geo_n) < 0;
    if (!entering) { n = -n; geo_n = -geo_n; }

    if (bounce == 0) {
      if (first_albedo) *first_albedo = albedo;
      if (first_normal) *first_normal = n;
    }

    // Emission is picked up unweighted on specular/primary rays, and with
    // an MIS weight (against the light-sampling pdf of the *previous*
    // vertex) when reached through a diffuse BSDF sample - approximated
    // here by simply always counting it once (materials rarely double as
    // both a sampled light and a random hit in the same path).
    if (mat.emission.r + mat.emission.g + mat.emission.b > 0 && specular_bounce) {
      radiance += Mul(throughput, ToVec(mat.emission));
    }

    const float roughness = std::clamp(1.0f - mat.gloss, 0.02f, 1.0f);
    const float transparency = std::clamp(mat.transparency, 0.f, 1.f);
    const float reflectivity = std::clamp(mat.reflectivity, 0.f, 1.f);

    Vector3d new_dir;
    bool new_specular = true;

    if (transparency > 0.001f) {
      const double ior = 1.5;
      const double eta = entering ? 1.0 / ior : ior;
      const double cosi = std::clamp(-ON_DotProduct(dir, n), -1.0, 1.0);
      const double fresnel = SchlickFresnel(std::fabs(cosi), entering ? 1.0 / ior : ior);
      Vector3d refracted;
      const bool can_refract = Refract(dir, n, eta, refracted);
      const double reflect_prob = can_refract ? fresnel : 1.0;
      if (NextFloat(rng) < reflect_prob) {
        new_dir = Reflect(dir, n);
        if (roughness > 0.02f) new_dir = SampleGlossyLobe(new_dir, roughness * 0.3, NextFloat(rng), NextFloat(rng));
        origin = p + geo_n * 1e-4;
      } else {
        new_dir = refracted;
        if (roughness > 0.02f) new_dir = SampleGlossyLobe(new_dir, roughness * 0.3, NextFloat(rng), NextFloat(rng));
        throughput = Vector3d(throughput.x * albedo.x, throughput.y * albedo.y, throughput.z * albedo.z) * 0.4 + throughput * 0.6;
        origin = p - geo_n * 1e-4;
      }
      new_dir.Unitize();
      new_specular = true;
    } else if (NextFloat(rng) < reflectivity) {
      Vector3d refl = Reflect(dir, n);
      new_dir = roughness > 0.02f ? SampleGlossyLobe(refl, roughness, NextFloat(rng), NextFloat(rng)) : refl;
      new_dir.Unitize();
      if (ON_DotProduct(new_dir, n) <= 0) new_dir = refl;  // clamp a lobe sample that dipped below the surface
      const Vector3d spec_tint = ToVec(mat.specular);
      throughput = Vector3d(throughput.x * spec_tint.x, throughput.y * spec_tint.y, throughput.z * spec_tint.z);
      origin = p + geo_n * 1e-4;
      new_specular = true;
    } else {
      radiance += Mul(throughput, SampleDirectLighting(p, n, -dir, mat, albedo, rng));
      new_dir = SampleCosineHemisphere(n, NextFloat(rng), NextFloat(rng));
      throughput = Vector3d(throughput.x * albedo.x, throughput.y * albedo.y, throughput.z * albedo.z);
      origin = p + geo_n * 1e-4;
      new_specular = false;
      // MIS: this bsdf-sampled ray might head straight at an area light.
      render::BvhHit next_hit;
      const bool next_hit_scene = bvh_.Intersect(origin, new_dir, 1e-4f, 1e30f, next_hit);
      double next_ground_t = -1;
      if (ground_.enabled && std::fabs(new_dir.z) > 1e-9) {
        const double t = (ground_.z - origin.z) / new_dir.z;
        if (t > 1e-4 && (!next_hit_scene || t < next_hit.t)) next_ground_t = t;
      }
      const double max_t = next_hit_scene ? (next_ground_t > 0 ? std::min<double>(next_hit.t, next_ground_t) : next_hit.t)
                                          : (next_ground_t > 0 ? next_ground_t : 1e29);
      const double pdf_bsdf_solid = std::max(ON_DotProduct(n, new_dir), 0.0) / ON_PI;
      radiance += Mul(throughput, MisAreaLightHit(&lights_, origin, new_dir, max_t, pdf_bsdf_solid));
    }

    dir = new_dir;
    specular_bounce = new_specular;

    // Russian roulette.
    if (bounce > 2) {
      const double p_continue = std::clamp(std::max({throughput.x, throughput.y, throughput.z}), 0.05, 0.97);
      if (NextFloat(rng) >= p_continue) break;
      throughput = throughput / p_continue;
    }
  }
  return radiance;
}

// ---------------------------------------------------------------------------
// One-shot / progressive drivers
// ---------------------------------------------------------------------------

std::vector<unsigned char> PathTracer::Render(const PathTraceSettings& settings,
                                              const std::function<bool(int)>& progress) const {
  const int w = std::max(settings.width, 1), h = std::max(settings.height, 1);
  std::vector<float> hdr(static_cast<size_t>(w) * h * 3, 0.f);
  std::vector<float> albedo_buf(static_cast<size_t>(w) * h * 3, 0.f);
  std::vector<float> normal_buf(static_cast<size_t>(w) * h * 3, 0.f);
  Camera cam; cam.SetState(camera_);

  const int nthreads = std::max(1u, std::thread::hardware_concurrency());
  std::vector<std::thread> workers;
  std::atomic<int> next_row{0};
  const int samples = std::max(settings.samples, 1);
  const int bounces = std::max(settings.bounces, 1);

  auto worker = [&]() {
    for (;;) {
      const int y = next_row.fetch_add(1);
      if (y >= h) break;
      for (int x = 0; x < w; ++x) {
        const unsigned base_seed = Hash32(static_cast<unsigned>(y) * 9781u + static_cast<unsigned>(x) * 6151u + 17u);
        Vector3d sum(0, 0, 0), a_sum(0, 0, 0), n_sum(0, 0, 0);
        for (int s = 0; s < samples; ++s) {
          unsigned rng = Hash32(base_seed + static_cast<unsigned>(s) * 2654435761u);
          const double jx = Halton(static_cast<unsigned>(s) + 1, 2), jy = Halton(static_cast<unsigned>(s) + 1, 3);
          unsigned rot_seed_x = base_seed, rot_seed_y = base_seed ^ 0x9e3779b9u;
          const double rot_x = NextFloat(rot_seed_x), rot_y = NextFloat(rot_seed_y);
          const double px = x + std::fmod(jx + rot_x, 1.0), py = y + std::fmod(jy + rot_y, 1.0);
          const double ndc_x = (2.0 * px / w - 1.0), ndc_y = 1.0 - 2.0 * py / h;
          const Ray ray = cam.ScreenRay(ndc_x, ndc_y, static_cast<double>(w) / h);
          Vector3d first_albedo(0, 0, 0), first_normal(0, 0, 1);
          Vector3d c = TracePath(ray.origin, ray.direction, rng, bounces, settings.arctic, &first_albedo, &first_normal);
          if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) c = Vector3d(0, 0, 0);
          sum += c; a_sum += first_albedo; n_sum += first_normal;
        }
        const size_t idx = (static_cast<size_t>(y) * w + x) * 3;
        hdr[idx] = static_cast<float>(sum.x / samples); hdr[idx + 1] = static_cast<float>(sum.y / samples); hdr[idx + 2] = static_cast<float>(sum.z / samples);
        albedo_buf[idx] = static_cast<float>(a_sum.x / samples); albedo_buf[idx + 1] = static_cast<float>(a_sum.y / samples); albedo_buf[idx + 2] = static_cast<float>(a_sum.z / samples);
        normal_buf[idx] = static_cast<float>(n_sum.x / samples); normal_buf[idx + 1] = static_cast<float>(n_sum.y / samples); normal_buf[idx + 2] = static_cast<float>(n_sum.z / samples);
      }
    }
  };
  for (int i = 0; i < nthreads; ++i) workers.emplace_back(worker);
  for (auto& t : workers) t.join();
  if (progress) progress(samples);

  std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    rgb[i * 3] = ToByte(hdr[i * 3]); rgb[i * 3 + 1] = ToByte(hdr[i * 3 + 1]); rgb[i * 3 + 2] = ToByte(hdr[i * 3 + 2]);
  }
  if (!settings.denoise) return rgb;

  // Edge-aware bilateral denoise guided by the albedo/normal buffers: a 5x5
  // window weighted by colour, albedo and normal similarity plus a spatial
  // Gaussian, applied to the tonemapped image (simple and fast; a
  // wavelet/SVGF-grade denoiser is out of scope for a CPU offline pass).
  std::vector<unsigned char> out(rgb.size());
  const int radius = 2;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const size_t ci = (static_cast<size_t>(y) * w + x) * 3;
      float acc[3] = {0, 0, 0}, wsum = 0;
      const float ca[3] = {albedo_buf[ci], albedo_buf[ci + 1], albedo_buf[ci + 2]};
      const float cn[3] = {normal_buf[ci], normal_buf[ci + 1], normal_buf[ci + 2]};
      const float cc[3] = {hdr[ci], hdr[ci + 1], hdr[ci + 2]};
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          const int nx = std::clamp(x + dx, 0, w - 1), ny = std::clamp(y + dy, 0, h - 1);
          const size_t ni = (static_cast<size_t>(ny) * w + nx) * 3;
          const float spatial = std::exp(-(dx * dx + dy * dy) / 4.0f);
          float da = 0, dn = 0, dcv = 0;
          for (int k = 0; k < 3; ++k) {
            da += std::fabs(albedo_buf[ni + k] - ca[k]);
            dn += std::fabs(normal_buf[ni + k] - cn[k]);
            dcv += std::fabs(hdr[ni + k] - cc[k]);
          }
          const float weight = spatial * std::exp(-da * 12.0f) * std::exp(-dn * 8.0f) * std::exp(-dcv * 3.0f);
          acc[0] += hdr[ni] * weight; acc[1] += hdr[ni + 1] * weight; acc[2] += hdr[ni + 2] * weight;
          wsum += weight;
        }
      }
      if (wsum < 1e-6f) wsum = 1.0f;
      out[ci] = ToByte(acc[0] / wsum); out[ci + 1] = ToByte(acc[1] / wsum); out[ci + 2] = ToByte(acc[2] / wsum);
    }
  }
  return out;
}

void PathTracer::ResetAccumulation(int width, int height) {
  accum_w_ = std::max(width, 1);
  accum_h_ = std::max(height, 1);
  accum_samples_ = 0;
  accum_.assign(static_cast<size_t>(accum_w_) * accum_h_ * 3, 0.f);
}

void PathTracer::Accumulate(int spp, int bounces, std::vector<unsigned char>& out_rgb) {
  if (accum_w_ <= 0 || accum_h_ <= 0) return;
  const int w = accum_w_, h = accum_h_;
  Camera cam; cam.SetState(camera_);
  spp = std::max(spp, 1);
  bounces = std::max(bounces, 1);
  const int start_sample = accum_samples_;

  const int nthreads = std::max(1u, std::thread::hardware_concurrency());
  std::vector<std::thread> workers;
  std::atomic<int> next_row{0};
  auto worker = [&]() {
    for (;;) {
      const int y = next_row.fetch_add(1);
      if (y >= h) break;
      for (int x = 0; x < w; ++x) {
        const unsigned base_seed = Hash32(static_cast<unsigned>(y) * 9781u + static_cast<unsigned>(x) * 6151u + 17u);
        Vector3d sum(0, 0, 0);
        for (int s = 0; s < spp; ++s) {
          const int sample_index = start_sample + s;
          unsigned rng = Hash32(base_seed + static_cast<unsigned>(sample_index) * 2654435761u);
          const double jx = Halton(static_cast<unsigned>(sample_index) + 1, 2), jy = Halton(static_cast<unsigned>(sample_index) + 1, 3);
          unsigned rot_seed_x = base_seed, rot_seed_y = base_seed ^ 0x9e3779b9u;
          const double rot_x = NextFloat(rot_seed_x), rot_y = NextFloat(rot_seed_y);
          const double px = x + std::fmod(jx + rot_x, 1.0), py = y + std::fmod(jy + rot_y, 1.0);
          const double ndc_x = (2.0 * px / w - 1.0), ndc_y = 1.0 - 2.0 * py / h;
          const Ray ray = cam.ScreenRay(ndc_x, ndc_y, static_cast<double>(w) / h);
          Vector3d c = TracePath(ray.origin, ray.direction, rng, bounces, false, nullptr, nullptr);
          if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) c = Vector3d(0, 0, 0);
          sum += c;
        }
        const size_t idx = (static_cast<size_t>(y) * w + x) * 3;
        accum_[idx] += static_cast<float>(sum.x); accum_[idx + 1] += static_cast<float>(sum.y); accum_[idx + 2] += static_cast<float>(sum.z);
      }
    }
  };
  for (int i = 0; i < nthreads; ++i) workers.emplace_back(worker);
  for (auto& t : workers) t.join();
  accum_samples_ += spp;

  out_rgb.assign(static_cast<size_t>(w) * h * 3, 0);
  const float inv = 1.0f / static_cast<float>(std::max(accum_samples_, 1));
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    out_rgb[i * 3] = ToByte(accum_[i * 3] * inv);
    out_rgb[i * 3 + 1] = ToByte(accum_[i * 3 + 1] * inv);
    out_rgb[i * 3 + 2] = ToByte(accum_[i * 3 + 2] * inv);
  }
}

}  // namespace dino8::app
