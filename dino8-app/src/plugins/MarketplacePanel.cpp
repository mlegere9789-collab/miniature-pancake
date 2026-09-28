#include "plugins/MarketplacePanel.h"

#include <cstdio>
#include <string>

#include "app/Application.h"
#include "app/Settings.h"
#include "imgui.h"
#include "plugins/Marketplace.h"

namespace dino8::plugins {

namespace {

const char* CompatibilityLabel(Compatibility c) {
  switch (c) {
    case Compatibility::Compatible: return "Compatible";
    case Compatibility::ApiTooNew: return "Needs newer Dino 8";
    case Compatibility::Unknown: return "Unknown";
  }
  return "Unknown";
}

ImVec4 CompatibilityColor(Compatibility c) {
  switch (c) {
    case Compatibility::Compatible: return ImVec4(0.4f, 0.8f, 0.4f, 1);
    case Compatibility::ApiTooNew: return ImVec4(0.9f, 0.4f, 0.4f, 1);
    case Compatibility::Unknown: return ImVec4(0.8f, 0.8f, 0.4f, 1);
  }
  return ImVec4(0.8f, 0.8f, 0.4f, 1);
}

}  // namespace

void DrawPluginMarketplacePanel(app::Application& app, bool& open) {
  if (!open) return;
  ImGui::SetNextWindowSize(ImVec2(680, 460), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Plug-in Marketplace", &open)) { ImGui::End(); return; }

  Marketplace& market = Marketplace::Get();
  static char source[512] = "";
  static std::string selected_id;
  static std::string status;

  ImGui::TextWrapped(
      "Browse a plug-in index - a JSON file listing installable plug-ins (schema: plugin-index/SCHEMA.md). "
      "Load one from disk or from an http(s) URL, then Install a plug-in straight into %s/plugins.",
      app::ConfigDirectory().c_str());
  ImGui::Separator();

  ImGui::InputTextWithHint("##indexsource", "path/to/index.json or https://.../index.json", source, sizeof source);
  ImGui::SameLine();
  if (ImGui::Button("Load")) {
    std::string error;
    if (market.LoadFrom(source, error)) {
      status = "Loaded \"" + market.Index().index_name + "\" (" + std::to_string(market.Index().plugins.size()) + " plug-in(s)) from " + market.Source();
      selected_id.clear();
    } else {
      status = "Load failed: " + error;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Browse Local Index...")) {
    app.ShowFileDialog("Load Plug-in Index", {".json"}, false, [](const std::string& path) {
      std::snprintf(source, sizeof source, "%s", path.c_str());
      std::string error;
      if (Marketplace::Get().LoadFrom(path, error)) {
        status = "Loaded \"" + Marketplace::Get().Index().index_name + "\" (" +
                  std::to_string(Marketplace::Get().Index().plugins.size()) + " plug-in(s)) from " + path;
      } else {
        status = "Load failed: " + error;
      }
    });
  }
  if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
  ImGui::Separator();

  if (market.Index().plugins.empty()) {
    ImGui::TextDisabled("No index loaded yet. A ready-to-use reference index ships at plugin-index/index.json.");
    ImGui::End();
    return;
  }

  ImGui::TextDisabled("%s%s", market.Index().index_name.c_str(),
                      market.Index().updated.empty() ? "" : (" - updated " + market.Index().updated).c_str());

  if (ImGui::BeginTable("marketplace_plugins", 6,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Version");
    ImGui::TableSetupColumn("Author");
    ImGui::TableSetupColumn("Compatibility");
    ImGui::TableSetupColumn("Source");
    ImGui::TableSetupColumn("");
    ImGui::TableHeadersRow();
    for (const MarketplaceEntry& e : market.Index().plugins) {
      ImGui::PushID(e.id.c_str());
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (ImGui::Selectable(e.name.c_str(), selected_id == e.id, ImGuiSelectableFlags_SpanAllColumns)) selected_id = e.id;
      ImGui::TableNextColumn(); ImGui::TextUnformatted(e.version.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(e.author.c_str());
      const Compatibility compat = CheckCompatibility(e);
      ImGui::TableNextColumn(); ImGui::TextColored(CompatibilityColor(compat), "%s", CompatibilityLabel(compat));
      ImGui::TableNextColumn(); ImGui::TextUnformatted(e.bundled_path.empty() ? "remote download" : "bundled with this build");
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(compat == Compatibility::ApiTooNew);
      if (ImGui::SmallButton("Install")) {
        std::string error;
        if (InstallEntry(app, e, app.ExeDir(), error)) {
          status = "Installed " + e.name + " " + e.version + ".";
          app.Notify("Plug-in Marketplace: installed " + e.name + " " + e.version);
        } else {
          status = "Install failed: " + error;
          app.Notify("Plug-in Marketplace: install failed - " + error);
        }
      }
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  for (const MarketplaceEntry& e : market.Index().plugins) {
    if (e.id != selected_id) continue;
    ImGui::Separator();
    ImGui::TextWrapped("%s %s by %s", e.name.c_str(), e.version.c_str(), e.author.c_str());
    if (!e.description.empty()) ImGui::TextWrapped("%s", e.description.c_str());
    if (!e.homepage.empty()) ImGui::TextWrapped("Homepage: %s", e.homepage.c_str());
    if (!e.tags.empty()) {
      std::string tags;
      for (const std::string& t : e.tags) tags += (tags.empty() ? "" : ", ") + t;
      ImGui::TextDisabled("Tags: %s", tags.c_str());
    }
    ImGui::TextDisabled("Plug-in API v%d - %s", e.api_version, CompatibilityLabel(CheckCompatibility(e)));
    break;
  }

  ImGui::End();
}

}  // namespace dino8::plugins
