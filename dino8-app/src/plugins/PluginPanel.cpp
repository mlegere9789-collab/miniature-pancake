#include "plugins/PluginPanel.h"

#include "app/Application.h"
#include "app/Settings.h"
#include "imgui.h"
#include "plugins/PluginManager.h"

namespace dino8::plugins {

void DrawPlugInManagerPanel(app::Application& app, bool& open) {
  if (!open) return;
  ImGui::SetNextWindowSize(ImVec2(560, 360), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Plug-in Manager", &open)) { ImGui::End(); return; }
  ImGui::TextWrapped("Plug-ins loaded from %s/plugins and %s/plugins.", app::ConfigDirectory().c_str(), app.ExeDir().c_str());
  ImGui::Separator();
  static char load_path[512] = "";
  ImGui::InputText("##loadpath", load_path, sizeof load_path);
  ImGui::SameLine();
  if (ImGui::Button("Load File...")) {
    app.ShowFileDialog("Load Plug-in", {".so", ".dll", ".dylib"}, false, [&app](const std::string& path) {
      std::string error;
      if (!Manager::Get().LoadFile(app, path, error)) app.Notify("Plug-in load failed: " + error);
      else app.Notify("Loaded plug-in: " + path);
    });
  }
  ImGui::SameLine();
  if (ImGui::Button("Rescan Folders")) Manager::Get().ScanDefaultFolders(app);
  ImGui::Separator();
  if (ImGui::BeginTable("plugins", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Version");
    ImGui::TableSetupColumn("Commands");
    ImGui::TableSetupColumn("Flow Nodes");
    ImGui::TableSetupColumn("Status");
    ImGui::TableHeadersRow();
    for (const LoadedPlugin& p : Manager::Get().Plugins()) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn(); ImGui::TextUnformatted(p.name.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(p.version.c_str());
      ImGui::TableNextColumn(); ImGui::Text("%d", static_cast<int>(p.commands.size()));
      ImGui::TableNextColumn(); ImGui::Text("%d", static_cast<int>(p.flow_nodes.size()));
      ImGui::TableNextColumn();
      if (p.loaded_ok) { ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1), "Loaded"); }
      else { ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1), "Error"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.error.c_str()); }
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void DrawPackageManagerPanel(app::Application& app, bool& open) {
  if (!open) return;
  ImGui::SetNextWindowSize(ImVec2(560, 340), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Package Manager", &open)) { ImGui::End(); return; }
  ImGui::TextWrapped("Dino 8 has no online package catalogue: install packages from a local folder "
                     "(a folder containing manifest.json plus a plug-in library, .lua scripts, or a .dflow graph).");
  ImGui::Separator();
  static char folder[512] = "";
  ImGui::InputText("Search folder", folder, sizeof folder);
  ImGui::SameLine();
  if (ImGui::Button("Browse...")) {
    app.ShowFileDialog("Choose a folder of packages", {}, false, [](const std::string& path) {
      std::snprintf(folder, sizeof folder, "%s", path.c_str());
    });
  }
  static std::vector<PackageInfo> found;
  if (ImGui::Button("Scan")) found = Manager::Get().ScanPackages(folder);
  ImGui::Separator();
  for (const PackageInfo& pkg : found) {
    ImGui::PushID(pkg.folder.c_str());
    ImGui::TextUnformatted(pkg.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", pkg.version.c_str());
    if (!pkg.description.empty()) ImGui::TextWrapped("%s", pkg.description.c_str());
    if (ImGui::SmallButton("Install")) {
      std::string error;
      if (Manager::Get().InstallPackage(app, pkg, error)) app.Notify("Installed " + pkg.name);
      else app.Notify("Install failed: " + error);
    }
    ImGui::Separator();
    ImGui::PopID();
  }
  ImGui::End();
}

}  // namespace dino8::plugins
