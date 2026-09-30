#include "plugins/MarketplacePanel.h"

#include <cstdio>
#include <ctime>
#include <string>

#include "app/Application.h"
#include "app/Settings.h"
#include "imgui.h"
#include "plugins/Marketplace.h"
#include "plugins/PluginReviews.h"

namespace dino8::plugins {

namespace {

const char* CompatibilityLabel(Compatibility c) {
  switch (c) {
    case Compatibility::Compatible: return "Compatible";
    case Compatibility::ApiTooNew: return "Needs newer Dino 8";
    case Compatibility::AppTooOld: return "Needs newer Dino 8";
    case Compatibility::Unknown: return "Unknown";
  }
  return "Unknown";
}

ImVec4 CompatibilityColor(Compatibility c) {
  switch (c) {
    case Compatibility::Compatible: return ImVec4(0.4f, 0.8f, 0.4f, 1);
    case Compatibility::ApiTooNew: return ImVec4(0.9f, 0.4f, 0.4f, 1);
    case Compatibility::AppTooOld: return ImVec4(0.9f, 0.4f, 0.4f, 1);
    case Compatibility::Unknown: return ImVec4(0.8f, 0.8f, 0.4f, 1);
  }
  return ImVec4(0.8f, 0.8f, 0.4f, 1);
}

// Display names (falling back to the id) for an entry's declared
// dependencies, each tagged with whether that dependency is currently
// satisfied - the same FindInstalled check the "Installed" column already
// uses per row, just applied to the selected entry's dependency list so the
// detail panel actually shows what Install/Update will pull in (or already
// found satisfied) before the user clicks it.
std::string DependencySummary(const Marketplace& market, const MarketplaceEntry& entry) {
  if (entry.dependencies.empty()) return "";
  std::string out;
  for (const std::string& dep_id : entry.dependencies) {
    if (!out.empty()) out += ", ";
    const MarketplaceEntry* dep = nullptr;
    for (const MarketplaceEntry& e : market.Index().plugins)
      if (e.id == dep_id) { dep = &e; break; }
    out += dep ? dep->name : dep_id;
    std::string installed_version;
    UpdateStatus status;
    if (dep && market.FindInstalled(*dep, installed_version, status)) out += " (installed)";
    else if (!dep) out += " (missing from index)";
    else out += " (not installed)";
  }
  return out;
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

std::string CurrentTimestamp() {
  const std::time_t now = std::time(nullptr);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
  return buf;
}

// "4.3 (3)" for a rated entry, "Not yet rated" for one with no reviews -
// used both in the plugin list's "Rating" column and the selected entry's
// detail panel.
std::string RatingSummary(const std::string& plugin_id) {
  const auto& reviews = PluginReviewStore::Get().ReviewsFor(plugin_id);
  if (reviews.empty()) return "Not yet rated";
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f (%d)", AverageRating(reviews), static_cast<int>(reviews.size()));
  return buf;
}

}  // namespace

void DrawPluginMarketplacePanel(app::Application& app, bool& open) {
  if (!open) return;
  PluginReviewStore::Get().EnsureLoaded(app::ConfigDirectory());
  ImGui::SetNextWindowSize(ImVec2(680, 460), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Plug-in Marketplace", &open)) { ImGui::End(); return; }

  Marketplace& market = Marketplace::Get();
  static char source[512] = "";
  static std::string selected_id;
  static std::string status;
  static int review_rating = 5;
  static char review_reviewer[128] = "";
  static char review_comment[256] = "";

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

  if (ImGui::BeginTable("marketplace_plugins", 8,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Version");
    ImGui::TableSetupColumn("Installed");
    ImGui::TableSetupColumn("Author");
    ImGui::TableSetupColumn("Compatibility");
    ImGui::TableSetupColumn("Rating");
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
      const Compatibility compat = CheckCompatibility(e, DINO8_VERSION);
      ImGui::TableNextColumn(); ImGui::TextColored(CompatibilityColor(compat), "%s", CompatibilityLabel(compat));
      ImGui::TableNextColumn();
      const std::string rating_summary = RatingSummary(e.id);
      if (rating_summary == "Not yet rated") ImGui::TextDisabled("%s", rating_summary.c_str());
      else ImGui::TextUnformatted(rating_summary.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(e.bundled_path.empty() ? "remote download" : "bundled with this build");
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(compat == Compatibility::ApiTooNew || compat == Compatibility::AppTooOld);
      const char* button_label = update_status == UpdateStatus::UpdateAvailable ? "Update" : "Install";
      if (ImGui::SmallButton(button_label)) {
        std::string error;
        // Goes through InstallById, not InstallEntry directly, so a click
        // here resolves e's dependencies exactly like the PluginMarketplaceInstall
        // command does (plugin-index/SCHEMA.md's "Dependencies" section) -
        // installing straight from the panel used to skip that resolution
        // entirely and could load e with an unmet dependency still missing.
        if (market.InstallById(app, e.id, error)) {
          status = std::string(button_label) + "d " + e.name + " " + e.version + ".";
          app.Notify("Plug-in Marketplace: installed " + e.name + " " + e.version);
        } else {
          status = "Install failed: " + error;
          app.Notify("Plug-in Marketplace: install failed - " + error);
        }
      }
      ImGui::EndDisabled();
      // Only offer to uninstall a copy the marketplace itself put in
      // <config>/plugins - never the sample plug-ins auto-loaded from next
      // to the executable, which UninstallById leaves alone (see its own
      // comment in Marketplace.h).
      ImGui::SameLine();
      ImGui::BeginDisabled(!installed);
      if (ImGui::SmallButton("Uninstall")) {
        std::vector<std::string> removed;
        std::string error;
        if (market.UninstallById(e.id, removed, error)) {
          status = "Uninstalled " + e.name + (removed.size() > 1 ? " and " + std::to_string(removed.size() - 1) +
                                                                        " now-unneeded dependenc" +
                                                                        (removed.size() == 2 ? "y" : "ies") + "."
                                                                  : ".");
          app.Notify("Plug-in Marketplace: uninstalled " + e.name);
        } else {
          status = "Uninstall failed: " + error;
          app.Notify("Plug-in Marketplace: uninstall failed - " + error);
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
    ImGui::TextDisabled("Plug-in API v%d - %s", e.api_version, CompatibilityLabel(CheckCompatibility(e, DINO8_VERSION)));
    const std::string deps = DependencySummary(market, e);
    if (!deps.empty()) ImGui::TextDisabled("Requires: %s", deps.c_str());

    ImGui::Separator();
    const auto& reviews = PluginReviewStore::Get().ReviewsFor(e.id);
    ImGui::TextWrapped("Ratings & Reviews: %s", RatingSummary(e.id).c_str());
    for (const PluginReview& r : reviews) {
      ImGui::BulletText("%s - %d/5%s", r.reviewer.empty() ? "Anonymous" : r.reviewer.c_str(), r.rating,
                        r.comment.empty() ? "" : (": " + r.comment).c_str());
    }
    ImGui::SliderInt("Your rating", &review_rating, 1, 5);
    ImGui::InputTextWithHint("##reviewer_name", "Your name (optional)", review_reviewer, sizeof review_reviewer);
    ImGui::InputTextWithHint("##review_comment", "Comment (optional)", review_comment, sizeof review_comment);
    if (ImGui::Button("Submit Review")) {
      std::string error;
      if (PluginReviewStore::Get().AddReview(e.id, review_rating, review_comment, review_reviewer, CurrentTimestamp(), error)) {
        status = "Thanks for rating " + e.name + "!";
        review_comment[0] = '\0';
      } else {
        status = "Rating failed: " + error;
      }
    }
    break;
  }

  ImGui::End();
}

}  // namespace dino8::plugins
