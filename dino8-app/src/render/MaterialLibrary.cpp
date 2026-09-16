#include "render/MaterialLibrary.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace dino8::app {

namespace {

std::string Lower(const std::string& s) {
  std::string t;
  for (char c : s) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return t;
}

MaterialPreset Metal(const char* name, int r, int g, int b, float gloss, float reflectivity) {
  MaterialPreset p;
  p.name = name;
  p.material.diffuse = Color::FromBytes(r, g, b);
  p.material.specular = Color::FromBytes(r, g, b);
  p.material.gloss = gloss;
  p.material.reflectivity = reflectivity;
  return p;
}

MaterialPreset Plastic(const char* name, int r, int g, int b, bool glossy) {
  MaterialPreset p;
  p.name = name;
  p.material.diffuse = Color::FromBytes(r, g, b);
  p.material.gloss = glossy ? 0.75f : 0.12f;
  p.material.reflectivity = glossy ? 0.08f : 0.02f;
  return p;
}

MaterialPreset Glass(const char* name, int r, int g, int b, float transparency, float gloss) {
  MaterialPreset p;
  p.name = name;
  p.material.diffuse = Color::FromBytes(r, g, b);
  p.material.gloss = gloss;
  p.material.reflectivity = 0.08f;
  p.material.transparency = transparency;
  return p;
}

MaterialPreset Wood(const char* name, int seed) {
  MaterialPreset p;
  p.name = name;
  p.material.diffuse = Color::FromBytes(180, 140, 95);
  p.material.gloss = 0.35f;
  p.material.reflectivity = 0.03f;
  p.material.texture_path = "proc:wood:" + std::to_string(seed);
  p.material.mapping = TextureMapping::Box;
  p.material.mapping_scale = 1.5f;
  return p;
}

MaterialPreset Stone(const char* name, const char* kind, int seed, int r, int g, int b) {
  MaterialPreset p;
  p.name = name;
  p.material.diffuse = Color::FromBytes(r, g, b);
  p.material.gloss = 0.1f;
  p.material.reflectivity = 0.02f;
  p.material.texture_path = std::string("proc:") + kind + ":" + std::to_string(seed);
  p.material.mapping = TextureMapping::Box;
  p.material.mapping_scale = 1.0f;
  return p;
}

MaterialPreset Fabric(const char* name, int r, int g, int b) {
  MaterialPreset p;
  p.name = name;
  p.material.diffuse = Color::FromBytes(r, g, b);
  p.material.gloss = 0.06f;
  p.material.reflectivity = 0.0f;
  return p;
}

MaterialPreset Emissive(const char* name, int r, int g, int b, float strength) {
  MaterialPreset p;
  p.name = name;
  p.material.diffuse = Color::FromBytes(r, g, b);
  p.material.emission = Color{r / 255.f * strength, g / 255.f * strength, b / 255.f * strength, 1.f};
  p.material.gloss = 0.05f;
  return p;
}

std::vector<MaterialPreset> BuildPresets() {
  std::vector<MaterialPreset> v;
  // ---- metals -------------------------------------------------------
  v.push_back(Metal("Aluminium", 205, 208, 212, 0.75f, 0.75f));
  v.push_back(Metal("Brass", 205, 170, 80, 0.8f, 0.6f));
  v.push_back(Metal("Chrome", 232, 234, 238, 0.97f, 0.9f));
  v.push_back(Metal("Copper", 205, 120, 90, 0.75f, 0.65f));
  v.push_back(Metal("Gold", 245, 195, 90, 0.9f, 0.85f));
  v.push_back(Metal("Steel", 160, 164, 170, 0.82f, 0.7f));
  v.push_back(Metal("Bronze", 170, 120, 70, 0.7f, 0.55f));
  v.push_back(Metal("Titanium", 150, 150, 155, 0.6f, 0.55f));
  // ---- plastics: 8 colours glossy/matte -----------------------------
  struct PC { const char* n; int r, g, b; };
  static const PC pcs[] = {{"Red", 210, 45, 40}, {"Orange", 235, 140, 30}, {"Yellow", 235, 210, 40},
                           {"Green", 60, 170, 70}, {"Blue", 50, 100, 210}, {"Purple", 130, 70, 190},
                           {"White", 235, 235, 235}, {"Black", 30, 30, 32}};
  for (const PC& c : pcs) {
    v.push_back(Plastic((std::string("Plastic Glossy ") + c.n).c_str(), c.r, c.g, c.b, true));
  }
  for (const PC& c : pcs) {
    v.push_back(Plastic((std::string("Plastic Matte ") + c.n).c_str(), c.r, c.g, c.b, false));
  }
  // ---- glass ---------------------------------------------------------
  v.push_back(Glass("Glass Clear", 250, 250, 252, 0.92f, 0.98f));
  v.push_back(Glass("Glass Frosted", 245, 246, 248, 0.7f, 0.35f));
  v.push_back(Glass("Glass Tinted Blue", 190, 215, 235, 0.75f, 0.9f));
  v.push_back(Glass("Glass Tinted Green", 195, 230, 205, 0.75f, 0.9f));
  // ---- wood: 4 procedural grain textures ------------------------------
  v.push_back(Wood("Wood Oak", 1));
  v.push_back(Wood("Wood Walnut", 2));
  v.push_back(Wood("Wood Maple", 3));
  v.push_back(Wood("Wood Cherry", 4));
  // ---- stone / concrete / brick procedural noise ----------------------
  v.push_back(Stone("Stone Granite", "stone", 1, 150, 148, 145));
  v.push_back(Stone("Stone Marble", "marble", 2, 235, 233, 228));
  v.push_back(Stone("Concrete", "concrete", 1, 175, 173, 168));
  v.push_back(Stone("Brick", "brick", 1, 165, 90, 65));
  // ---- paint: car paint with a clear-coat approximation ----------------
  {
    MaterialPreset p; p.name = "Car Paint Red";
    p.material.diffuse = Color::FromBytes(180, 25, 30); p.material.gloss = 0.95f; p.material.reflectivity = 0.5f;
    v.push_back(p);
  }
  {
    MaterialPreset p; p.name = "Car Paint Blue";
    p.material.diffuse = Color::FromBytes(20, 60, 150); p.material.gloss = 0.95f; p.material.reflectivity = 0.5f;
    v.push_back(p);
  }
  {
    MaterialPreset p; p.name = "Car Paint Black";
    p.material.diffuse = Color::FromBytes(18, 18, 20); p.material.gloss = 0.96f; p.material.reflectivity = 0.55f;
    v.push_back(p);
  }
  // ---- fabric ----------------------------------------------------------
  v.push_back(Fabric("Fabric Cotton", 220, 210, 190));
  v.push_back(Fabric("Fabric Denim", 60, 80, 120));
  v.push_back(Fabric("Fabric Wool Grey", 130, 128, 125));
  // ---- rubber ------------------------------------------------------------
  {
    MaterialPreset p; p.name = "Rubber Black";
    p.material.diffuse = Color::FromBytes(25, 25, 27); p.material.gloss = 0.08f; p.material.reflectivity = 0.0f;
    v.push_back(p);
  }
  {
    MaterialPreset p; p.name = "Rubber Red";
    p.material.diffuse = Color::FromBytes(140, 30, 30); p.material.gloss = 0.1f;
    v.push_back(p);
  }
  // ---- emissive ------------------------------------------------------------
  v.push_back(Emissive("Emissive White", 255, 255, 255, 3.0f));
  v.push_back(Emissive("Emissive Warm", 255, 200, 120, 3.0f));
  v.push_back(Emissive("Emissive Neon Blue", 90, 160, 255, 4.0f));
  v.push_back(Emissive("Emissive Neon Green", 120, 255, 140, 4.0f));
  return v;
}

}  // namespace

const std::vector<MaterialPreset>& MaterialPresets() {
  static const std::vector<MaterialPreset> presets = BuildPresets();
  return presets;
}

const MaterialPreset* FindMaterialPreset(const std::string& name) {
  const std::string want = Lower(name);
  for (const MaterialPreset& p : MaterialPresets())
    if (Lower(p.name) == want) return &p;
  return nullptr;
}

bool IsProceduralTexture(const std::string& path) { return path.rfind("proc:", 0) == 0; }

namespace {

// A tiny deterministic hash -> [0,1) value noise, no external dependency.
float Hash(int x, int y, unsigned seed) {
  unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

float ValueNoise(float x, float y, unsigned seed) {
  const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
  const float fx = x - x0, fy = y - y0;
  const float v00 = Hash(x0, y0, seed), v10 = Hash(x0 + 1, y0, seed);
  const float v01 = Hash(x0, y0 + 1, seed), v11 = Hash(x0 + 1, y0 + 1, seed);
  const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  return (v00 + (v10 - v00) * sx) + ((v01 + (v11 - v01) * sx) - (v00 + (v10 - v00) * sx)) * sy;
}

float Fbm(float x, float y, unsigned seed, int octaves) {
  float sum = 0, amp = 0.5f, freq = 1.0f, total = 0;
  for (int i = 0; i < octaves; ++i) {
    sum += ValueNoise(x * freq, y * freq, seed + static_cast<unsigned>(i) * 101u) * amp;
    total += amp;
    amp *= 0.5f;
    freq *= 2.0f;
  }
  return total > 0 ? sum / total : 0;
}

void PutPixel(std::vector<unsigned char>& rgba, int w, int x, int y, float r, float g, float b) {
  unsigned char* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
  auto clamp8 = [](float v) { return static_cast<unsigned char>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
  p[0] = clamp8(r); p[1] = clamp8(g); p[2] = clamp8(b); p[3] = 255;
}

}  // namespace

bool GenerateProceduralTexture(const std::string& spec, int w, int h, std::vector<unsigned char>& rgba) {
  if (!IsProceduralTexture(spec) || w <= 0 || h <= 0) return false;
  std::vector<std::string> parts;
  std::stringstream ss(spec);
  std::string tok;
  while (std::getline(ss, tok, ':')) parts.push_back(tok);
  const std::string kind = parts.size() > 1 ? Lower(parts[1]) : "wood";
  unsigned seed = 1;
  if (parts.size() > 2) seed = static_cast<unsigned>(std::max(0, std::atoi(parts[2].c_str())) + 1);

  rgba.assign(static_cast<size_t>(w) * h * 4, 255);
  const float fw = static_cast<float>(w), fh = static_cast<float>(h);

  if (kind == "wood") {
    const float base_r[4] = {0.62f, 0.42f, 0.72f, 0.55f}, base_g[4] = {0.44f, 0.27f, 0.55f, 0.28f}, base_b[4] = {0.27f, 0.16f, 0.34f, 0.20f};
    const int t = static_cast<int>(seed - 1) % 4;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const float u = x / fw * 8.f, v = y / fh * 8.f;
        const float dist = std::sqrt(u * u * 0.15f + v * v) + Fbm(u * 0.5f, v * 0.5f, seed, 3) * 1.5f;
        const float rings = 0.5f + 0.5f * std::sin(dist * 6.2831f);
        const float grain = 0.85f + 0.15f * Fbm(u * 6.f, v * 0.6f, seed + 7, 2);
        const float shade = (0.55f + 0.45f * rings) * grain;
        PutPixel(rgba, w, x, y, base_r[t] * shade, base_g[t] * shade, base_b[t] * shade);
      }
    }
    return true;
  }
  if (kind == "marble") {
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const float u = x / fw * 6.f, v = y / fh * 6.f;
        const float vein = std::sin(u * 3.0f + Fbm(u, v, seed, 4) * 12.0f);
        const float shade = 0.75f + 0.25f * vein;
        PutPixel(rgba, w, x, y, shade * 0.92f, shade * 0.91f, shade * 0.89f);
      }
    }
    return true;
  }
  if (kind == "stone" || kind == "concrete") {
    const float base = kind == "concrete" ? 0.68f : 0.58f;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const float u = x / fw * 10.f, v = y / fh * 10.f;
        const float n = Fbm(u, v, seed, 5);
        const float shade = base + (n - 0.5f) * 0.35f;
        PutPixel(rgba, w, x, y, shade, shade * 0.99f, shade * 0.97f);
      }
    }
    return true;
  }
  if (kind == "brick") {
    const int rows = 6, cols = 3;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const int row = static_cast<int>(y / fh * rows);
        const float offset = (row % 2) * 0.5f;
        const float u = x / fw * cols + offset;
        const int col = static_cast<int>(std::floor(u));
        const float fu = u - col, fv = (y / fh * rows) - row;
        const bool mortar = fu < 0.04f || fu > 0.96f || fv < 0.06f || fv > 0.94f;
        const float noise = 0.85f + 0.3f * Fbm(x / fw * 20.f, y / fh * 20.f, seed + static_cast<unsigned>(row * 13 + col), 2);
        if (mortar) PutPixel(rgba, w, x, y, 0.72f, 0.70f, 0.66f);
        else PutPixel(rgba, w, x, y, 0.62f * noise, 0.34f * noise, 0.24f * noise);
      }
    }
    return true;
  }
  // Unknown kind: flat mid-grey so a bad spec is at least visible, not a crash.
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) PutPixel(rgba, w, x, y, 0.6f, 0.6f, 0.6f);
  return true;
}

}  // namespace dino8::app
