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
  for (const std::string& dep_spec : entry.dependencies) {
    if (!out.empty()) out += ", ";
    std::string dep_id, min_version;
    SplitDependencySpec(dep_spec, dep_id, min_version);
    const MarketplaceEntry* dep = nullptr;
    for (const MarketplaceEntry& e : market.Index().plugins)
      if (e.id == dep_id) { dep = &e; break; }
    out += dep ? dep->name : dep_id;
    if (!min_version.empty()) out += " >=" + min_version;
    std::string installed_version;
    UpdateStatus status;
    if (dep && market.FindInstalled(*dep, installed_version, status)) {
      out += (min_version.empty() || CompareVersions(installed_version, min_version) >= 0)
                 ? " (installed)"
                 : " (installed " + installed_version + ", too old)";
    } else if (!dep) {
      out += " (missing from index)";
    } else {
      out += " (not installed)";
    }
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
  static char filter[128] = "";

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
  ImGui::SameLine();
  // Discovery without typing anything: the reference index this build ships
  // with (plugin-index/index.json, see Application::DefaultMarketplaceIndexPath),
  // the same default PluginMarketplaceIndex falls back to with no argument.
  const std::string default_index_path = app.DefaultMarketplaceIndexPath();
  ImGui::BeginDisabled(default_index_path.empty());
  if (ImGui::Button("Load Bundled Index")) {
    std::snprintf(source, sizeof source, "%s", default_index_path.c_str());
    std::string error;
    if (market.LoadFrom(default_index_path, error)) {
      status = LoadedStatus(app, market);
      selected_id.clear();
    } else {
      status = "Load failed: " + error;
    }
  }
  ImGui::EndDisabled();
  if (default_index_path.empty() && ImGui::IsItemHovered())
    ImGui::SetTooltip("No bundled reference index was found next to this build.");
  if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
  ImGui::Separator();

  if (market.Index().plugins.empty()) {
    ImGui::TextDisabled("No index loaded yet. A ready-to-use reference index ships at plugin-index/index.json.");
    ImGui::End();
    return;
  }

  ImGui::TextDisabled("%s%s", market.Index().index_name.c_str(),
                      market.Index().updated.empty() ? "" : (" - updated " + market.Index().updated).c_str());

  size_t not_installed_count = 0;
  for (const MarketplaceEntry& e : market.Index().plugins) {
    std::string installed_version;
    UpdateStatus status;
    if (!market.FindInstalled(e, installed_version, status)) ++not_installed_count;
  }
  ImGui::BeginDisabled(not_installed_count == 0);
  if (ImGui::Button(not_installed_count == 0 ? "Install All" : ("Install All (" + std::to_string(not_installed_count) + ")").c_str())) {
    // Goes through Marketplace::InstallAll, which installs each not-yet-installed
    // entry via InstallById - the same per-row Install button's own path -
    // one at a time, so bringing in a whole index resolves dependencies
    // exactly like a single click would and one failure doesn't stop the rest.
    std::vector<std::string> installed, failed;
    market.InstallAll(app, installed, failed);
    status = installed.empty() ? "" : ("Installed " + std::to_string(installed.size()) + " plug-in(s).");
    if (!failed.empty()) {
      std::string names;
      for (const std::string& f : failed) names += (names.empty() ? "" : "; ") + f;
      status += (status.empty() ? "" : " ") + std::string("Failed: ") + names;
    }
    if (!installed.empty()) app.Notify("Plug-in Marketplace: installed " + std::to_string(installed.size()) + " plug-in(s)");
    if (!failed.empty()) app.Notify("Plug-in Marketplace: " + std::to_string(failed.size()) + " install(s) failed");
  }
  ImGui::EndDisabled();
  if (not_installed_count == 0 && ImGui::IsItemHovered()) ImGui::SetTooltip("Everything in the loaded index is already installed.");

  ImGui::SameLine();
  const auto pending_updates = market.CheckForUpdates();
  ImGui::BeginDisabled(pending_updates.empty());
  if (ImGui::Button(pending_updates.empty() ? "Update All" : ("Update All (" + std::to_string(pending_updates.size()) + ")").c_str())) {
    // Goes through Marketplace::UpdateAll, which installs each pending
    // update via InstallById - the same per-row Update button's own path -
    // one at a time, so a batch upgrade resolves dependencies exactly like a
    // single click would and one failure doesn't stop the rest.
    std::vector<std::string> updated, failed;
    market.UpdateAll(app, updated, failed);
    status = updated.empty() ? "" : ("Updated " + std::to_string(updated.size()) + " plug-in(s).");
    if (!failed.empty()) {
      std::string names;
      for (const std::string& f : failed) names += (names.empty() ? "" : "; ") + f;
      status += (status.empty() ? "" : " ") + std::string("Failed: ") + names;
    }
    if (!updated.empty()) app.Notify("Plug-in Marketplace: updated " + std::to_string(updated.size()) + " plug-in(s)");
    if (!failed.empty()) app.Notify("Plug-in Marketplace: " + std::to_string(failed.size()) + " update(s) failed");
  }
  ImGui::EndDisabled();

  // AnyInstalled() is a cheap (no file hashing) check, unlike calling
  // VerifyAll() itself just to see if it would have anything to do - which
  // matters here since this runs every frame the panel is open.
  const bool any_installed = market.AnyInstalled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!any_installed);
  if (ImGui::Button("Verify All")) {
    // Marketplace::VerifyAll - the batch counterpart to the per-entry
    // Verify button below, re-hashing every currently-installed entry's
    // file against the index's sha256 in one click.
    const auto results = market.VerifyAll();
    size_t mismatches = 0;
    for (const auto& r : results)
      if (r.status == Marketplace::VerifyStatus::Mismatch) ++mismatches;
    status = "Verified " + std::to_string(results.size()) + " installed plug-in(s)" +
             (mismatches ? (" - " + std::to_string(mismatches) + " mismatch(es).") : std::string("."));
    if (mismatches) app.Notify("Plug-in Marketplace: " + std::to_string(mismatches) + " installed plug-in(s) failed verification");
  }
  ImGui::EndDisabled();
  if (!any_installed && ImGui::IsItemHovered()) ImGui::SetTooltip("Install a plug-in first to verify it.");

  ImGui::SameLine();
  ImGui::BeginDisabled(!any_installed);
  if (ImGui::Button("Uninstall All")) {
    // Marketplace::UninstallAll - the batch counterpart to a single row's
    // Uninstall button, cascading each target's own now-unneeded
    // dependencies exactly like uninstalling it by hand would.
    std::vector<std::string> removed, failed;
    market.UninstallAll(removed, failed);
    status = removed.empty() ? "" : ("Uninstalled " + std::to_string(removed.size()) + " plug-in(s).");
    if (!failed.empty()) {
      std::string names;
      for (const std::string& f : failed) names += (names.empty() ? "" : "; ") + f;
      status += (status.empty() ? "" : " ") + std::string("Failed: ") + names;
    }
    if (!removed.empty()) app.Notify("Plug-in Marketplace: uninstalled " + std::to_string(removed.size()) + " plug-in(s)");
  }
  ImGui::EndDisabled();
  if (!any_installed && ImGui::IsItemHovered()) ImGui::SetTooltip("Nothing is currently installed via the marketplace.");

  ImGui::InputTextWithHint("##filter", "Filter by name, tag, author, or id...", filter, sizeof filter);
  size_t shown = 0;
  for (const MarketplaceEntry& e : market.Index().plugins)
    if (MatchesFilter(e, filter)) ++shown;
  if (filter[0] != '\0')
    ImGui::TextDisabled("%zu of %zu plug-in(s) match", shown, market.Index().plugins.size());

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
      if (!MatchesFilter(e, filter)) continue;
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
      ImGui::TableNextColumn();
      ImGui::TextColored(CompatibilityColor(compat), "%s", CompatibilityLabel(compat));
      if ((compat == Compatibility::ApiTooNew || compat == Compatibility::AppTooOld) && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", CompatibilityReason(e, DINO8_VERSION).c_str());
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
      if ((compat == Compatibility::ApiTooNew || compat == Compatibility::AppTooOld) && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", CompatibilityReason(e, DINO8_VERSION).c_str());
      // Only offer to uninstall a copy the marketplace itself put in
      // <config>/plugins - never the sample plug-ins auto-loaded from next
      // to the executable, which UninstallById leaves alone (see its own
      // comment in Marketplace.h).
      ImGui::SameLine();
      ImGui::BeginDisabled(!installed);
      if (ImGui::SmallButton("Uninstall")) {
        std::vector<std::string> removed;
        std::vector<std::string> still_needed_by;
        std::string error;
        if (market.UninstallById(e.id, removed, still_needed_by, error)) {
          status = "Uninstalled " + e.name + (removed.size() > 1 ? " and " + std::to_string(removed.size() - 1) +
                                                                        " now-unneeded dependenc" +
                                                                        (removed.size() == 2 ? "y" : "ies") + "."
                                                                  : ".");
          if (!still_needed_by.empty()) {
            std::string names;
            for (const std::string& n : still_needed_by) names += (names.empty() ? "" : ", ") + n;
            status += " Warning: " + names + " still list" + (still_needed_by.size() == 1 ? "s" : "") +
                      " this as a dependency and may now be broken.";
          }
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
    const Compatibility detail_compat = CheckCompatibility(e, DINO8_VERSION);
    ImGui::TextDisabled("Plug-in API v%d - %s", e.api_version, CompatibilityLabel(detail_compat));
    if (detail_compat == Compatibility::ApiTooNew || detail_compat == Compatibility::AppTooOld)
      ImGui::TextColored(CompatibilityColor(detail_compat), "%s", CompatibilityReason(e, DINO8_VERSION).c_str());
    const std::string deps = DependencySummary(market, e);
    if (!deps.empty()) ImGui::TextDisabled("Requires: %s", deps.c_str());

    {
      std::string installed_version;
      UpdateStatus update_status;
      const bool installed = market.FindInstalled(e, installed_version, update_status);
      ImGui::BeginDisabled(!installed);
      if (ImGui::SmallButton("Verify")) {
        // Re-hashes the file actually on disk right now against the index's
        // sha256 - catches a copy that was corrupted or edited after install
        // without requiring a reinstall to find out (Marketplace::VerifyInstalled).
        std::string detail;
        const auto vstatus = market.VerifyInstalled(e.id, detail);
        status = detail;
        if (vstatus == Marketplace::VerifyStatus::Mismatch) app.Notify("Plug-in Marketplace: " + detail);
      }
      ImGui::EndDisabled();
      if (!installed && ImGui::IsItemHovered()) ImGui::SetTooltip("Install this plug-in first to verify it.");
    }

    ImGui::Separator();
    const auto& reviews = PluginReviewStore::Get().ReviewsFor(e.id);
    ImGui::TextWrapped("Ratings & Reviews: %s", RatingSummary(e.id).c_str());
    // Deleting shifts every later index down, and reviews is a live
    // reference into the store PluginReviewStore::Get().DeleteReview is
    // about to mutate - so the delete itself has to happen after this loop
    // finishes walking `reviews`, never from inside it.
    int delete_index = -1;
    for (size_t i = 0; i < reviews.size(); ++i) {
      const PluginReview& r = reviews[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::BulletText("%s - %d/5%s", r.reviewer.empty() ? "Anonymous" : r.reviewer.c_str(), r.rating,
                        r.comment.empty() ? "" : (": " + r.comment).c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Delete")) delete_index = static_cast<int>(i);
      ImGui::PopID();
    }
    if (delete_index >= 0) {
      std::string error;
      if (PluginReviewStore::Get().DeleteReview(e.id, static_cast<size_t>(delete_index), error)) {
        status = "Removed a review for " + e.name + ".";
      } else {
        status = "Delete review failed: " + error;
      }
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
