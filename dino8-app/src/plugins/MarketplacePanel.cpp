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

// Builds the "Loaded ... (N plug-in(s))" status line for a freshly loaded
// index and, if any already-installed plug-in is older than what the index
// now offers, appends an update count and raises a Notify() toast - the
// same notification channel InstallEntry's own success/failure already use
// below, so an update becoming available is surfaced the same way an
// install completing is.
std::string LoadedStatus(app::Application& app, const Marketplace& market) {
  std::string status = "Loaded \"" + market.Index().index_name + "\" (" +
                        std::to_string(market.Index().plugins.size()) + " plug-in(s)) from " + market.Source();
  const auto updates = market.CheckForUpdates();
  if (!updates.empty()) {
    status += " - " + std::to_string(updates.size()) + " update(s) available";
    std::string names;
    for (const auto& u : updates) names += (names.empty() ? "" : ", ") + u.name + " " + u.installed_version + " -> " + u.available_version;
    app.Notify("Plug-in Marketplace: update available for " + names);
  }
  return status;
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
      status = LoadedStatus(app, market);
      selected_id.clear();
    } else {
      status = "Load failed: " + error;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Browse Local Index...")) {
    app.ShowFileDialog("Load Plug-in Index", {".json"}, false, [&app](const std::string& path) {
      std::snprintf(source, sizeof source, "%s", path.c_str());
      std::string error;
      if (Marketplace::Get().LoadFrom(path, error)) {
        status = LoadedStatus(app, Marketplace::Get());
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

  if (ImGui::BeginTable("marketplace_plugins", 7,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Version");
    ImGui::TableSetupColumn("Installed");
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

      std::string installed_version;
      UpdateStatus update_status = UpdateStatus::Unknown;
      const bool installed = market.FindInstalled(e, installed_version, update_status);
      ImGui::TableNextColumn();
      if (!installed) {
        ImGui::TextDisabled("not installed");
      } else if (update_status == UpdateStatus::UpdateAvailable) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1), "%s (update available)", installed_version.c_str());
      } else {
        ImGui::TextUnformatted(installed_version.c_str());
      }

      ImGui::TableNextColumn(); ImGui::TextUnformatted(e.author.c_str());
      const Compatibility compat = CheckCompatibility(e);
      ImGui::TableNextColumn(); ImGui::TextColored(CompatibilityColor(compat), "%s", CompatibilityLabel(compat));
      ImGui::TableNextColumn(); ImGui::TextUnformatted(e.bundled_path.empty() ? "remote download" : "bundled with this build");
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(compat == Compatibility::ApiTooNew);
      const char* button_label = update_status == UpdateStatus::UpdateAvailable ? "Update" : "Install";
      if (ImGui::SmallButton(button_label)) {
        std::string error;
        if (InstallEntry(app, e, app.ExeDir(), error)) {
          status = std::string(button_label) + "d " + e.name + " " + e.version + ".";
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
