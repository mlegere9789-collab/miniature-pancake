// Rendering panels: Materials, Lights, Rendering options, Environments,
// Textures and the Render Window that shows the last rendered image.
#include "ui/Panels.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "app/Application.h"
#include "imgui.h"

namespace dino8::app {

namespace {

bool InputText(const char* label, std::string& value, ImGuiInputTextFlags flags = 0) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", value.c_str());
  if (ImGui::InputText(label, buf, sizeof(buf), flags)) {
    value = buf;
    return true;
  }
  return false;
}

bool MappingCombo(const char* label, TextureMapping& mapping, bool allow_default) {
  static const TextureMapping kAll[] = {TextureMapping::Default, TextureMapping::Surface, TextureMapping::Planar,
                                        TextureMapping::Box, TextureMapping::Cylindrical, TextureMapping::Spherical,
                                        TextureMapping::Custom};
  bool changed = false;
  if (ImGui::BeginCombo(label, TextureMappingName(mapping))) {
    for (TextureMapping m : kAll) {
      if (m == TextureMapping::Default && !allow_default) continue;
      if (ImGui::Selectable(TextureMappingName(m), m == mapping)) { mapping = m; changed = true; }
    }
    ImGui::EndCombo();
  }
  return changed;
}

// ContentFilter: a case-insensitive name substring shared by the Materials,
// Textures and Environments panels below (Application::State().content_filter).
bool PassesContentFilter(Application& app, const std::string& name) {
  const std::string& f = app.State().content_filter;
  if (f.empty()) return true;
  auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s; };
  return lower(name).find(lower(f)) != std::string::npos;
}

void DrawContentFilterBox(Application& app) {
  std::string& f = app.State().content_filter;
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%s", f.c_str());
  if (ImGui::InputTextWithHint("##content_filter", "Filter by name (ContentFilter)", buf, sizeof(buf))) f = buf;
  if (!f.empty()) { ImGui::SameLine(); if (ImGui::SmallButton("Clear##content_filter")) f.clear(); }
}

std::string UniqueMaterialName(const Document& doc, const std::string& base) {
  if (!doc.FindMaterial(base)) return base;
  for (int i = 2; i < 10000; ++i) {
    const std::string n = base + " " + std::to_string(i);
    if (!doc.FindMaterial(n)) return n;
  }
  return base;
}

// Assigns a material to the selected objects (BeginChange included).
void AssignToSelection(Document& doc, const std::string& name) {
  doc.BeginChange("Assign material");
  for (SceneObject& o : doc.Objects()) {
    if (!o.selected) continue;
    o.material_name = name;
    o.InvalidateDisplay();
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

void DrawMaterialsPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin(PanelTitle("panel.materials", "Materials").c_str(), &app.Panels().materials)) { ImGui::End(); return; }
  static std::string selected;
  static char new_name[64] = "";
  ImGui::InputTextWithHint("##nm", "new material name", new_name, sizeof(new_name));
  ImGui::SameLine();
  if (ImGui::Button("New")) {
    Material m;
    m.name = UniqueMaterialName(doc, new_name[0] ? new_name : "Material");
    doc.BeginChange("New material");
    selected = doc.AddMaterial(m);
    new_name[0] = 0;
  }
  ImGui::SameLine();
  if (ImGui::Button("Presets")) ImGui::OpenPopup("material_presets");
  if (ImGui::BeginPopup("material_presets")) {
    struct Preset { const char* name; int r, g, b; float gloss, refl, transp; };
    static const Preset presets[] = {
        {"Plastic Red", 220, 60, 50, 0.6f, 0.05f, 0.f}, {"Plastic Blue", 60, 110, 220, 0.6f, 0.05f, 0.f},
        {"Matte White", 235, 235, 235, 0.1f, 0.f, 0.f}, {"Steel", 150, 155, 165, 0.8f, 0.45f, 0.f},
        {"Brass", 205, 170, 80, 0.75f, 0.4f, 0.f}, {"Chrome", 230, 232, 236, 0.95f, 0.85f, 0.f},
        {"Glass", 200, 225, 240, 0.9f, 0.25f, 0.65f}, {"Wood", 160, 110, 60, 0.3f, 0.f, 0.f},
        {"Rubber Black", 35, 35, 38, 0.15f, 0.f, 0.f}, {"Gold", 245, 195, 90, 0.85f, 0.55f, 0.f}};
    for (const Preset& p : presets) {
      if (ImGui::Selectable(p.name)) {
        Material m;
        m.name = UniqueMaterialName(doc, p.name);
        m.diffuse = Color::FromBytes(p.r, p.g, p.b);
        m.gloss = p.gloss; m.reflectivity = p.refl; m.transparency = p.transp;
        doc.BeginChange("New material");
        selected = doc.AddMaterial(m);
      }
    }
    ImGui::EndPopup();
  }
  ImGui::Separator();
  DrawContentFilterBox(app);
  if (doc.Materials().empty()) ImGui::TextDisabled("No materials. Objects use their display colour.");
  const float list_height = std::min(180.0f, 22.0f * static_cast<float>(std::max<size_t>(doc.Materials().size(), 1)) + 8.0f);
  if (ImGui::BeginListBox("##materials", ImVec2(-1, list_height))) {
    int shown = 0;
    for (const Material& m : doc.Materials()) {
      if (!PassesContentFilter(app, m.name)) continue;
      ++shown;
      ImGui::PushID(m.name.c_str());
      ImGui::ColorButton("##swatch", ImVec4(m.diffuse.r, m.diffuse.g, m.diffuse.b, 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
      ImGui::SameLine();
      if (ImGui::Selectable(m.name.c_str(), selected == m.name)) selected = m.name;
      ImGui::PopID();
    }
    if (shown == 0 && !doc.Materials().empty()) ImGui::TextDisabled("No material matches the filter.");
    ImGui::EndListBox();
  }
  Material* m = doc.FindMaterial(selected);
  if (!m) { ImGui::End(); return; }
  ImGui::Separator();
  if (ImGui::Button("Assign to selection")) AssignToSelection(doc, m->name);
  ImGui::SameLine();
  if (ImGui::Button("Assign to layer")) {
    doc.BeginChange("Assign layer material");
    for (const SceneObject& o : doc.Objects()) {
      if (o.selected && o.layer_index >= 0 && o.layer_index < static_cast<int>(doc.Layers().size())) doc.Layers()[static_cast<size_t>(o.layer_index)].material = m->name;
    }
    doc.Touch();
  }
  ImGui::SameLine();
  if (ImGui::Button("Duplicate")) {
    Material copy = *m;
    copy.name = UniqueMaterialName(doc, m->name);
    doc.BeginChange("Duplicate material");
    selected = doc.AddMaterial(copy);
    ImGui::End();
    return;
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete")) {
    doc.BeginChange("Delete material");
    doc.RemoveMaterial(selected);
    selected.clear();
    ImGui::End();
    return;
  }
  int users = 0;
  for (const SceneObject& o : doc.Objects()) if (o.material_name == m->name) ++users;
  ImGui::TextDisabled("Used by %d object(s)", users);
  ImGui::Separator();
  std::string name = m->name;
  if (InputText("Name", name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty() && name != m->name && !doc.FindMaterial(name)) {
    doc.BeginChange("Rename material");
    for (SceneObject& o : doc.Objects()) if (o.material_name == m->name) o.material_name = name;
    for (Layer& l : doc.Layers()) if (l.material == m->name) l.material = name;
    m->name = name;
    selected = name;
    doc.Touch();
  }
  bool changed = false;
  changed |= ColorEdit("Colour", m->diffuse);
  changed |= ColorEdit("Specular colour", m->specular);
  changed |= ImGui::SliderFloat("Gloss finish", &m->gloss, 0.f, 1.f, "%.2f");
  changed |= ImGui::SliderFloat("Reflectivity", &m->reflectivity, 0.f, 1.f, "%.2f");
  changed |= ImGui::SliderFloat("Transparency", &m->transparency, 0.f, 1.f, "%.2f");
  changed |= ColorEdit("Emission", m->emission);
  ImGui::Separator();
  ImGui::Text("Texture");
  if (InputText("Image file", m->texture_path, ImGuiInputTextFlags_EnterReturnsTrue)) { changed = true; app.Renderer().RefreshTextures(); }
  ImGui::SameLine();
  if (ImGui::SmallButton("...")) {
    const std::string mat_name = m->name;
    app.ShowFileDialog("Texture image", {".bmp", ".ppm", ".pgm", ".png"}, false, [&app, mat_name](const std::string& path) {
      if (Material* mm = app.Doc().FindMaterial(mat_name)) { app.Doc().BeginChange("Texture"); mm->texture_path = path; app.Doc().Touch(); app.Renderer().RefreshTextures(); }
    });
  }
  if (!m->texture_path.empty()) {
    std::error_code ec;
    if (!std::filesystem::exists(m->texture_path, ec)) ImGui::TextColored(ImVec4(1.f, 0.55f, 0.3f, 1.f), "File not found");
    else if (app.Renderer().MissingTextures().count(m->texture_path)) ImGui::TextColored(ImVec4(1.f, 0.55f, 0.3f, 1.f), "Could not decode (BMP, PPM/PGM and PNG are supported)");
    if (ImGui::SmallButton("Remove texture")) { m->texture_path.clear(); changed = true; }
  }
  changed |= MappingCombo("Mapping", m->mapping, false);
  changed |= ImGui::DragFloat("Tiling", &m->mapping_scale, 0.05f, 0.01f, 100.f, "%.2f");
  if (changed) {
    doc.Touch();
    for (SceneObject& o : doc.Objects()) if (o.material_name == m->name) o.InvalidateDisplay();
  }
  ImGui::End();
}

// AT-SPI2-queryable snapshot of Document::Materials() (see
// docs/ACCESSIBILITY.md): built straight from Document state, independent of
// whether DrawMaterialsPanel itself has ever been drawn or is open right
// now - mirrors the same name each row's Selectable shows plus its diffuse
// colour as plain "R, G, B" text, the one fact the row's colour swatch
// otherwise conveys only visually (gloss/reflectivity/transparency/texture
// stay in the expanded editor only, out of this mirror, the same way
// Layouts' page size stays out of its own top-level row).
dino8::platform::AccessibleNode MaterialsAccessibleTree(Application& app) {
  std::vector<dino8::platform::MaterialSummary> summaries;
  summaries.reserve(app.Doc().Materials().size());
  for (const auto& m : app.Doc().Materials()) {
    auto byte = [](float c) { return static_cast<int>(std::lround(std::clamp(c, 0.f, 1.f) * 255.f)); };
    const std::string diffuse = std::to_string(byte(m.diffuse.r)) + ", " + std::to_string(byte(m.diffuse.g)) + ", " +
                                 std::to_string(byte(m.diffuse.b));
    summaries.push_back({m.name, diffuse});
  }
  return dino8::platform::BuildMaterialsPanelNode(summaries);
}

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

void DrawLightsPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin(PanelTitle("panel.lights", "Lights").c_str(), &app.Panels().lights)) { ImGui::End(); return; }
  RenderSettings& r = doc.Render();
  if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
    bool t = false;
    t |= ImGui::Checkbox("Sun on", &r.sun);
    float az = static_cast<float>(r.sun_azimuth), alt = static_cast<float>(r.sun_altitude);
    if (ImGui::SliderFloat("Azimuth", &az, 0.f, 360.f, "%.0f deg")) { r.sun_azimuth = az; t = true; }
    if (ImGui::SliderFloat("Altitude", &alt, 0.f, 90.f, "%.0f deg")) { r.sun_altitude = alt; t = true; }
    t |= ImGui::SliderFloat("Sun intensity", &r.sun_intensity, 0.f, 3.f, "%.2f");
    t |= ColorEdit("Sun colour", r.sun_color);
    t |= ImGui::Checkbox("Skylight (ambient sky)", &r.skylight);
    if (t) doc.Touch();
  }
  ImGui::Separator();
  ImGui::Text("Document lights (%zu)", doc.Lights().size());
  ImGui::SameLine();
  if (ImGui::SmallButton("Add point light")) {
    Light L;
    L.type = LightType::Point;
    L.position = kernel::Point3d(20, -20, 30);
    doc.BeginChange("PointLight");
    doc.AddLight(L);
  }
  if (doc.Lights().empty()) ImGui::TextDisabled("No lights: Rendered mode uses the default two-light rig.\nUse PointLight, Spotlight, DirectionalLight, RectangularLight or LinearLight.");
  int remove_id = -1;
  for (Light& L : doc.Lights()) {
    ImGui::PushID(L.id);
    const bool open = ImGui::TreeNodeEx("##light", (L.selected ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_AllowOverlap, "%s (%s)", L.name.c_str(), LightTypeName(L.type));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    if (ImGui::Checkbox("##on", &L.enabled)) doc.Touch();
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) remove_id = L.id;
    if (open) {
      bool t = false;
      t |= InputText("Name", L.name, ImGuiInputTextFlags_EnterReturnsTrue);
      t |= ImGui::Checkbox("Selected", &L.selected);
      t |= ColorEdit("Colour", L.color);
      t |= ImGui::SliderFloat("Intensity", &L.intensity, 0.f, 5.f, "%.2f");
      float pos[3] = {static_cast<float>(L.position.x), static_cast<float>(L.position.y), static_cast<float>(L.position.z)};
      if (L.type != LightType::Directional && ImGui::DragFloat3("Position", pos, 0.5f)) { L.position = kernel::Point3d(pos[0], pos[1], pos[2]); t = true; }
      float dir[3] = {static_cast<float>(L.direction.x), static_cast<float>(L.direction.y), static_cast<float>(L.direction.z)};
      if (L.type != LightType::Point && L.type != LightType::Linear && ImGui::DragFloat3("Direction", dir, 0.05f)) { L.direction = kernel::Vector3d(dir[0], dir[1], dir[2]); t = true; }
      if (L.type == LightType::Spot) {
        t |= ImGui::SliderFloat("Cone angle", &L.spot_angle, 1.f, 89.f, "%.0f deg");
        t |= ImGui::SliderFloat("Hardness", &L.spot_hardness, 0.f, 1.f, "%.2f");
        float len = static_cast<float>(L.length);
        if (ImGui::DragFloat("Cone length", &len, 0.5f, 0.1f, 1e6f)) { L.length = len; t = true; }
      }
      if (L.type == LightType::Rectangular || L.type == LightType::Linear) {
        float len = static_cast<float>(L.length), wid = static_cast<float>(L.width);
        if (ImGui::DragFloat("Length", &len, 0.5f, 0.01f, 1e6f)) { L.length = len; t = true; }
        if (L.type == LightType::Rectangular && ImGui::DragFloat("Width", &wid, 0.5f, 0.01f, 1e6f)) { L.width = wid; t = true; }
      }
      if (t) doc.Touch();
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (remove_id >= 0) {
    doc.BeginChange("Delete light");
    doc.RemoveLight(remove_id);
  }
  ImGui::End();
}

// AT-SPI2-queryable snapshot of Document::Lights() (see
// docs/ACCESSIBILITY.md): built straight from Document state, independent of
// whether DrawLightsPanel itself has ever been drawn or is open right now -
// mirrors the same "name (type)" label and on/off checkbox each row shows
// without needing to expand it (colour, intensity, position and direction
// stay in the expanded TreeNode only, out of this mirror, the same way
// Materials' gloss/reflectivity/transparency/texture stay out of its own
// row mirror).
dino8::platform::AccessibleNode LightsAccessibleTree(Application& app) {
  std::vector<dino8::platform::LightSummary> summaries;
  summaries.reserve(app.Doc().Lights().size());
  for (const Light& l : app.Doc().Lights()) summaries.push_back({l.name, LightTypeName(l.type), l.enabled});
  return dino8::platform::BuildLightsPanelNode(summaries);
}

// ---------------------------------------------------------------------------
// Rendering options
// ---------------------------------------------------------------------------

void DrawRenderingPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin(PanelTitle("panel.rendering", "Rendering").c_str(), &app.Panels().rendering)) { ImGui::End(); return; }
  RenderSettings& r = doc.Render();
  ImGui::Text("Current renderer: Dino 8 built-in (OpenGL, Blinn-Phong)");
  ImGui::Separator();
  ImGui::Text("Resolution");
  bool t = false;
  t |= ImGui::InputInt("Width", &r.render_width, 16, 256);
  t |= ImGui::InputInt("Height", &r.render_height, 16, 256);
  r.render_width = std::clamp(r.render_width, 16, 8192);
  r.render_height = std::clamp(r.render_height, 16, 8192);
  if (ImGui::SmallButton("Viewport size")) {
    if (Viewport* vp = app.ActiveViewport()) { r.render_width = std::max(vp->Width(), 16); r.render_height = std::max(vp->Height(), 16); t = true; }
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("1280 x 720")) { r.render_width = 1280; r.render_height = 720; t = true; }
  ImGui::SameLine();
  if (ImGui::SmallButton("1920 x 1080")) { r.render_width = 1920; r.render_height = 1080; t = true; }
  ImGui::Separator();
  ImGui::Text("Quality");
  const char* quality[] = {"Draft (no anti-aliasing)", "Good (2x2 supersampling)", "High (3x3 supersampling)", "Best (4x4 supersampling)"};
  int q = std::clamp(r.render_quality, 1, 4) - 1;
  if (ImGui::Combo("Anti-aliasing", &q, quality, 4)) { r.render_quality = q + 1; t = true; }
  ImGui::Separator();
  ImGui::Text("Background");
  int bg = static_cast<int>(r.background);
  if (ImGui::Combo("Type", &bg, "Solid colour\0Gradient\0Sky\0")) { r.background = static_cast<RenderSettings::Background>(bg); t = true; }
  if (r.background == RenderSettings::Background::Solid) t |= ColorEdit("Colour", r.background_color);
  if (r.background == RenderSettings::Background::Gradient) { t |= ColorEdit("Top", r.gradient_top); t |= ColorEdit("Bottom", r.gradient_bottom); }
  t |= ImGui::Checkbox("Ground plane", &r.ground_plane);
  t |= ImGui::Checkbox("Skylight", &r.skylight);
  ImGui::Separator();
  if (ImGui::Button("Render")) app.Engine().Execute("Render");
  ImGui::SameLine();
  if (ImGui::Button("Render preview")) app.Engine().Execute("RenderPreview");
  ImGui::SameLine();
  if (ImGui::Button("Environments...")) app.Panels().environments = true;
  if (t) doc.Touch();
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Environments (background + ground plane)
// ---------------------------------------------------------------------------

void DrawEnvironmentsPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin(PanelTitle("panel.environments", "Environments").c_str(), &app.Panels().environments)) { ImGui::End(); return; }
  RenderSettings& r = doc.Render();
  bool t = false;
  if (ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen)) {
    int bg = static_cast<int>(r.background);
    if (ImGui::RadioButton("Solid colour", bg == 0)) { r.background = RenderSettings::Background::Solid; t = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Gradient", bg == 1)) { r.background = RenderSettings::Background::Gradient; t = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Sky", bg == 2)) { r.background = RenderSettings::Background::Sky; t = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Image", bg == 3)) { r.background = RenderSettings::Background::Image; t = true; }
    if (r.background == RenderSettings::Background::Solid) t |= ColorEdit("Colour", r.background_color);
    if (r.background == RenderSettings::Background::Gradient) { t |= ColorEdit("Top colour", r.gradient_top); t |= ColorEdit("Bottom colour", r.gradient_bottom); }
    t |= ImGui::Checkbox("Gradient background in modelling views", &r.gradient_view);
    if (InputText("Environment image", r.environment_image, ImGuiInputTextFlags_EnterReturnsTrue)) t = true;
    if (r.background == RenderSettings::Background::Image) {
      ImGui::TextDisabled("Drawn as a real equirectangular background by a raytraced Render/RenderPreview/RenderArctic "
                           "(PathTracer::SkyColor) and by the interactive Rendered-mode GPU raytraced preview "
                           "(GpuRaytracer's own env-map sampling); a .hdr source keeps its true, unclamped radiance "
                           "on both paths, not just a tone-mapped [0,1] copy.");
    }
  }
  if (ImGui::CollapsingHeader("Ground plane", ImGuiTreeNodeFlags_DefaultOpen)) {
    t |= ImGui::Checkbox("Show ground plane", &r.ground_plane);
    t |= ImGui::Checkbox("Automatic height (bottom of the model)", &r.ground_auto_height);
    if (!r.ground_auto_height) {
      float h = static_cast<float>(r.ground_height);
      if (ImGui::DragFloat("Height", &h, 0.1f)) { r.ground_height = h; t = true; }
    }
    t |= ColorEdit("Ground colour", r.ground_color);
    t |= ImGui::Checkbox("Contact shadows", &r.ground_shadows);
  }
  if (ImGui::CollapsingHeader("Sun and sky")) {
    t |= ImGui::Checkbox("Sun", &r.sun);
    float az = static_cast<float>(r.sun_azimuth), alt = static_cast<float>(r.sun_altitude);
    if (ImGui::SliderFloat("Azimuth", &az, 0.f, 360.f, "%.0f deg")) { r.sun_azimuth = az; t = true; }
    if (ImGui::SliderFloat("Altitude", &alt, 0.f, 90.f, "%.0f deg")) { r.sun_altitude = alt; t = true; }
    t |= ImGui::Checkbox("Skylight", &r.skylight);
  }
  if (t) doc.Touch();
  ImGui::End();
}

// AT-SPI2-queryable snapshot of Document::Render() (see
// docs/ACCESSIBILITY.md): built straight from Document state, independent of
// whether DrawEnvironmentsPanel itself has ever been drawn or is open right
// now - one Label: value fact per field DrawEnvironmentsPanel's own
// "Background", "Ground plane" and "Sun and sky" sections show (the
// background's colour/gradient/image detail only appears for the type it
// actually applies to, matching that section's own conditional fields; the
// "Gradient background in modelling views" checkbox and render
// width/height/quality stay out of this mirror the same way Materials'
// gloss/reflectivity/transparency/texture stay out of its own row mirror).
dino8::platform::AccessibleNode EnvironmentsAccessibleTree(Application& app) {
  const RenderSettings& r = app.Doc().Render();
  auto byte = [](float c) { return static_cast<int>(std::lround(std::clamp(c, 0.f, 1.f) * 255.f)); };
  auto rgb = [&](const Color& c) {
    return std::to_string(byte(c.r)) + ", " + std::to_string(byte(c.g)) + ", " + std::to_string(byte(c.b));
  };
  std::vector<dino8::platform::PropertyEntry> entries;
  const char* bg_names[] = {"Solid", "Gradient", "Sky", "Image"};
  entries.push_back({"Background", bg_names[static_cast<int>(r.background)]});
  if (r.background == RenderSettings::Background::Solid) {
    entries.push_back({"Background colour", rgb(r.background_color)});
  } else if (r.background == RenderSettings::Background::Gradient) {
    entries.push_back({"Gradient top colour", rgb(r.gradient_top)});
    entries.push_back({"Gradient bottom colour", rgb(r.gradient_bottom)});
  } else if (r.background == RenderSettings::Background::Image) {
    entries.push_back({"Background image", r.environment_image.empty() ? "(none set)" : r.environment_image});
  }
  entries.push_back({"Ground plane", r.ground_plane ? "on" : "off"});
  entries.push_back({"Ground height", r.ground_auto_height ? "Automatic" : FormatNumber(r.ground_height)});
  entries.push_back({"Ground colour", rgb(r.ground_color)});
  entries.push_back({"Ground shadows", r.ground_shadows ? "Yes" : "No"});
  entries.push_back({"Sun", r.sun ? "on" : "off"});
  entries.push_back({"Sun azimuth", FormatNumber(r.sun_azimuth) + " deg"});
  entries.push_back({"Sun altitude", FormatNumber(r.sun_altitude) + " deg"});
  entries.push_back({"Skylight", r.skylight ? "on" : "off"});
  return dino8::platform::BuildEnvironmentsPanelNode(entries);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

void DrawTexturesPanel(Application& app) {
  Document& doc = app.Doc();
  if (!ImGui::Begin(PanelTitle("panel.textures", "Textures").c_str(), &app.Panels().textures)) { ImGui::End(); return; }
  ImGui::TextWrapped("Textures are image files (BMP, PPM/PGM, PNG) referenced by materials. Assign one in the Materials panel or with the Picture command.");
  if (ImGui::SmallButton("Reload all")) app.Renderer().RefreshTextures();
  ImGui::Separator();
  DrawContentFilterBox(app);
  int count = 0;
  for (Material& m : doc.Materials()) {
    if (m.texture_path.empty() || !PassesContentFilter(app, m.name)) continue;
    ++count;
    ImGui::PushID(m.name.c_str());
    std::error_code ec;
    const bool exists = std::filesystem::exists(m.texture_path, ec);
    ImGui::Text("%s", m.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s, %s)", TextureMappingName(m.mapping), exists ? "found" : "missing");
    if (InputText("##path", m.texture_path, ImGuiInputTextFlags_EnterReturnsTrue)) { doc.Touch(); app.Renderer().RefreshTextures(); }
    ImGui::PopID();
  }
  if (count == 0) ImGui::TextDisabled("No material carries a texture.");
  ImGui::Separator();
  ImGui::TextDisabled("Texture editing (Partial): procedural textures and channels are planned.");
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Render Window
// ---------------------------------------------------------------------------

void DrawRenderWindow(Application& app) {
  RenderImage& img = app.LastRender();
  ImGui::SetNextWindowSize(ImVec2(760, 540), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(PanelTitle("panel.render_window", "RenderWindow").c_str(), &app.Panels().render_window)) { ImGui::End(); return; }
  if (!img.Valid()) {
    ImGui::TextDisabled("Nothing rendered yet.");
    if (ImGui::Button("Render")) app.Engine().Execute("Render");
    ImGui::End();
    return;
  }
  if (ImGui::Button("Save as...")) {
    app.ShowFileDialog("Save rendering", {".bmp", ".ppm"}, true, [&app](const std::string& path) {
      std::string p = path, err;
      const std::string ext = std::filesystem::path(p).extension().string();
      if (ext != ".bmp" && ext != ".ppm" && ext != ".BMP" && ext != ".PPM") p += ".bmp";
      if (app.SaveLastRender(p, err)) app.Notify("Saved " + p); else app.Notify(err);
    });
  }
  ImGui::SameLine();
  if (ImGui::Button("Render again")) app.Engine().Execute("Render");
  ImGui::SameLine();
  if (ImGui::Button("Close")) app.CloseRenderWindow();
  ImGui::SameLine();
  ImGui::TextDisabled("%d x %d  |  %s  |  %.2f s", img.width, img.height, img.view_name.c_str(), img.seconds);
  if (!img.last_saved_path.empty()) ImGui::TextDisabled("Saved: %s", img.last_saved_path.c_str());
  ImGui::Separator();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  float scale = std::min(avail.x / static_cast<float>(img.width), avail.y / static_cast<float>(img.height));
  if (scale > 1.0f) scale = 1.0f;
  if (scale <= 0.f) scale = 0.1f;
  if (img.texture) {
    ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(img.texture)),
                 ImVec2(static_cast<float>(img.width) * scale, static_cast<float>(img.height) * scale));
  }
  ImGui::End();
}

}  // namespace dino8::app
