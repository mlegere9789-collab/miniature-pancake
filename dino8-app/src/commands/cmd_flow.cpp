// Dino Flow (Grasshopper-class node editor) and plug-in system commands.
// Registered last so it can freely reference every other subsystem.
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <sstream>

#include "app/Settings.h"
#include "commands/cmd_common.h"
#include "flow/FlowEditor.h"
#include "flow/FlowPreview.h"
#include "plugins/Marketplace.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginPanel.h"
#include "plugins/PluginReviews.h"

namespace dino8::app {

namespace fs = std::filesystem;

namespace {

std::string LowerStr(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool OptionValue(const std::vector<std::string>& toks, const std::string& key, std::string& out) {
  const std::string prefix = LowerStr(key) + "=";
  for (const std::string& t : toks) {
    if (t.size() > prefix.size() && LowerStr(t.substr(0, prefix.size())) == prefix) { out = t.substr(prefix.size()); return true; }
  }
  return false;
}

bool IsOption(const std::string& tok, const std::string& key) {
  const std::string prefix = LowerStr(key) + "=";
  return tok.size() > prefix.size() && LowerStr(tok.substr(0, prefix.size())) == prefix;
}

std::string CurrentTimestamp() {
  const std::time_t now = std::time(nullptr);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
  return buf;
}

}  // namespace

void RegisterFlowCommands(CommandEngine& e) {
  Reg(e, "Grasshopper", Immediate([](CommandContext& ctx) {
        flow::Editor::Get().open = true;
        ctx.App().Panels().dino_flow = true;
        ctx.Print("Dino Flow: opened the node editor.");
      }));

  Reg(e, "GrasshopperPlayer", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        if (toks.empty()) { ctx.Warn("GrasshopperPlayer: usage GrasshopperPlayer file.dflow [Bake=Yes]"); return; }
        std::string path = toks[0];
        std::string bake_opt;
        bool bake = OptionValue(toks, "Bake", bake_opt) ? LowerStr(bake_opt) != "no" : false;
        std::string error;
        if (!flow::Editor::Get().RunHeadless(ctx.App(), path, bake, error)) { ctx.Warn("GrasshopperPlayer: " + error); return; }
        flow::Graph& g = flow::Editor::Get().graph;
        ctx.Print("GrasshopperPlayer: solved " + std::to_string(g.Nodes().size()) + " node(s) in " + FormatNumber(g.last_stats.total_ms) + " ms" +
                  (bake ? ", baked " + std::to_string(ctx.Doc().ObjectCount()) + " document object(s) total" : ""));
        for (auto& n : g.Nodes()) {
          if (!n->def || n->def->special != flow::NodeDef::Special::Solver) continue;
          ctx.Print("GrasshopperPlayer: " + n->type + " (#" + std::to_string(n->id) + ") best fitness " +
                    FormatNumber(n->solver_best_fitness) + " after " + std::to_string(n->solver_generations_run) + " generation(s)" +
                    (n->error.empty() ? "" : " - error: " + n->error));
        }
      }));

  Reg(e, "GrasshopperUpdateBakes", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        flow::Graph& g = flow::Editor::Get().graph;
        std::string path = toks.empty() ? g.path : toks[0];
        if (path.empty()) { ctx.Warn("GrasshopperUpdateBakes: no graph file given and none loaded (GrasshopperPlayer file.dflow Bake=Yes first, or pass one here)"); return; }
        std::string error;
        if (!toks.empty()) {
          // A file was named explicitly: always reload it from disk, even
          // if it's the same path already open - the whole point of naming
          // it is to pick up edits made to the .dflow file since the last
          // bake (a changed slider value, a rewired node, ...). Re-solving
          // the in-memory graph without reloading (the no-argument path
          // below) only catches changes made live in the open node editor.
          if (!g.LoadFile(path, error)) { ctx.Warn("GrasshopperUpdateBakes: " + error); return; }
        }
        // Re-solve in case inputs (sliders, referenced objects) changed
        // since the last bake.
        g.MarkAllDirty();
        g.Solve(&ctx.Doc());
        ctx.Doc().BeginChange("GrasshopperUpdateBakes");
        // Count stale objects first, purely for the report.
        int stale = 0;
        for (const SceneObject& o : ctx.Doc().Objects()) {
          auto it = o.user_text.find("FlowGraph");
          if (it != o.user_text.end() && it->second == path) ++stale;
        }
        const int baked = flow::RebakeGraph(g, ctx.Doc(), path);
        ctx.Print("GrasshopperUpdateBakes: replaced " + std::to_string(stale) + " object(s) from a previous bake of " + path + " with " + std::to_string(baked) + " freshly-solved object(s)");
      }));

  Reg(e, "GrasshopperFolders", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        auto& mgr = plugins::Manager::Get();
        if (!toks.empty() && LowerStr(toks[0]) == "add" && toks.size() > 1) {
          mgr.extra_folders.push_back(toks[1]);
          mgr.LoadFolder(ctx.App(), toks[1]);
          ctx.Print("GrasshopperFolders: added " + toks[1]);
          return;
        }
        ctx.Print("GrasshopperFolders: " + std::to_string(mgr.extra_folders.size()) + " extra folder(s) (plus <config>/plugins and the app's plugins/ folder)");
        for (const std::string& f : mgr.extra_folders) ctx.Print("  " + f);
      }));

  Reg(e, "GrasshopperPluginList", Immediate([](CommandContext& ctx) {
        const auto& list = plugins::Manager::Get().Plugins();
        ctx.Print("GrasshopperPluginList: " + std::to_string(list.size()) + " plug-in(s) found");
        for (const plugins::LoadedPlugin& p : list)
          ctx.Print("  " + p.name + " " + p.version + " - " + (p.loaded_ok ? std::to_string(p.commands.size()) + " command(s), " + std::to_string(p.flow_nodes.size()) + " node(s)" : "failed: " + p.error));
        ctx.App().Panels().plugin_manager = true;
      }));

  Reg(e, "GrasshopperLoadOneByOne", Immediate([](CommandContext& ctx) {
        auto& mgr = plugins::Manager::Get();
        mgr.load_one_by_one = !mgr.load_one_by_one;
        ctx.Print(std::string("GrasshopperLoadOneByOne: ") + (mgr.load_one_by_one ? "on" : "off"));
      }));

  Reg(e, "GrasshopperIgnorePlugin", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        auto& mgr = plugins::Manager::Get();
        if (toks.empty()) { ctx.Print("GrasshopperIgnorePlugin: " + std::to_string(mgr.ignored_plugins.size()) + " plug-in(s) ignored"); return; }
        mgr.ignored_plugins.push_back(toks[0]);
        ctx.Print("GrasshopperIgnorePlugin: will not load " + toks[0]);
      }));

  Reg(e, "GrasshopperDeveloperSettings", Immediate([](CommandContext& ctx) {
        ctx.App().ShowHelpFor("GrasshopperDeveloperSettings");
        ctx.App().Panels().help = true;
        ctx.Print("GrasshopperDeveloperSettings: node SDK docs are in the Help panel; the C API header is include/dino8_plugin.h, and docs/PLUGIN_SDK.md walks through it with example code.");
      }));

  Reg(e, "GrasshopperGetSDKDocumentation", Immediate([](CommandContext& ctx) {
        ctx.App().ShowHelpFor("GrasshopperGetSDKDocumentation");
        ctx.App().Panels().help = true;
        ctx.Print("GrasshopperGetSDKDocumentation: opened the Help panel; see include/dino8_plugin.h for the plug-in C ABI and docs/PLUGIN_SDK.md for a guided walkthrough with example plug-ins (plugins/sample, plugins/mesh_tools, plugins/curve_tools, plugins/analysis_tools).");
      }));

  Reg(e, "AttachGHSData", OnSelection("Select objects to attach Dino Flow data to", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        flow::Graph& g = flow::Editor::Get().graph;
        int attached = 0;
        ctx.Doc().BeginChange("AttachGHSData");
        for (ObjectId id : ids) {
          SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          std::string summary;
          for (auto& n : g.Nodes()) {
            for (flow::Port& out : n->outputs) {
              if (!out.data.Empty()) summary += (summary.empty() ? "" : "; ") + n->type + "=" + out.data.Summary();
            }
          }
          if (summary.empty()) summary = "(no active Dino Flow graph)";
          o->user_text["DinoFlowData"] = summary;
          ++attached;
        }
        ctx.Print("AttachGHSData: attached graph data to " + std::to_string(attached) + " object(s)");
      }));

  Reg(e, "PlugInManager", Immediate([](CommandContext& ctx) { ctx.App().Panels().plugin_manager = true; ctx.Print("PlugInManager: opened the plug-in manager."); }));
  Reg(e, "PluginManager", Immediate([](CommandContext& ctx) { ctx.App().Panels().plugin_manager = true; ctx.Print("PluginManager: opened the plug-in manager."); }));
  Reg(e, "PackageManager", Immediate([](CommandContext& ctx) { ctx.App().Panels().package_manager = true; ctx.Print("PackageManager: opened the package manager."); }));

  Reg(e, "PluginMarketplace", Immediate([](CommandContext& ctx) {
        ctx.App().Panels().plugin_marketplace = true;
        ctx.Print("PluginMarketplace: opened the plug-in marketplace.");
      }));

  Reg(e, "PluginMarketplaceIndex", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        // No argument: fall back to the bundled reference index (see
        // Application::DefaultMarketplaceIndexPath) rather than just
        // demanding a path/URL - the same "Load Bundled Index" default the
        // panel's own button offers, so the command line has a one-word way
        // to browse the real reference index too.
        std::string source = toks.empty() ? ctx.App().DefaultMarketplaceIndexPath() : toks[0];
        if (source.empty()) {
          ctx.Warn("PluginMarketplaceIndex: give a local path or an http(s) URL to a plug-in index (see plugin-index/SCHEMA.md) - "
                    "no bundled default index was found next to this build");
          return;
        }
        std::string error;
        if (plugins::Marketplace::Get().LoadFrom(source, error)) {
          const auto& idx = plugins::Marketplace::Get().Index();
          ctx.Print("PluginMarketplaceIndex: loaded \"" + idx.index_name + "\" - " + std::to_string(idx.plugins.size()) +
                    " plug-in(s) from " + source);
        } else {
          ctx.Warn("PluginMarketplaceIndex: " + error);
        }
      }));

  Reg(e, "PluginMarketplaceList", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        std::string filter;
        for (const std::string& t : toks) filter += (filter.empty() ? "" : " ") + t;

        const auto& idx = plugins::Marketplace::Get().Index();
        std::vector<const plugins::MarketplaceEntry*> matches;
        for (const plugins::MarketplaceEntry& p : idx.plugins) {
          if (filter.empty() || plugins::MatchesFilter(p, filter)) matches.push_back(&p);
        }

        if (filter.empty()) {
          ctx.Print("PluginMarketplaceList: " + std::to_string(idx.plugins.size()) + " plug-in(s) in the loaded index");
        } else {
          ctx.Print("PluginMarketplaceList: " + std::to_string(matches.size()) + " of " +
                    std::to_string(idx.plugins.size()) + " plug-in(s) match \"" + filter + "\"");
        }
        for (const plugins::MarketplaceEntry* pp : matches) {
          const plugins::MarketplaceEntry& p = *pp;
          const plugins::Compatibility compat = plugins::CheckCompatibility(p, DINO8_VERSION);
          std::string compat_label;
          if (compat == plugins::Compatibility::Compatible) {
            compat_label = "compatible";
          } else if (compat == plugins::Compatibility::Unknown) {
            compat_label = "compatibility unknown";
          } else {
            // ApiTooNew/AppTooOld: the specific requirement (mirrors
            // InstallEntry's own refusal text) instead of a generic "needs
            // newer Dino 8" that never said what version is actually
            // needed - visible here without having to attempt, and fail,
            // an install just to find out.
            compat_label = plugins::CompatibilityReason(p, DINO8_VERSION);
          }
          std::string line = "  " + p.id + ": " + p.name + " " + p.version + " by " + p.author + " (api v" +
                              std::to_string(p.api_version) + ", " + compat_label + ")";
          if (!p.dependencies.empty()) {
            std::string deps;
            for (const std::string& d : p.dependencies) deps += (deps.empty() ? "" : ", ") + d;
            line += " - requires " + deps;
          }
          ctx.Print(line);
        }
      }));

  Reg(e, "PluginMarketplaceInstall", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        if (toks.empty()) {
          ctx.Warn("PluginMarketplaceInstall: give the id of a plug-in from the loaded index (PluginMarketplaceList shows ids)");
          return;
        }
        std::string error;
        if (plugins::Marketplace::Get().InstallById(ctx.App(), toks[0], error)) {
          // LoadFile appends the newly loaded plug-in last, and InstallById
          // only returned true because that load just succeeded - so the
          // vector's last entry is exactly the one just installed.
          const auto& list = plugins::Manager::Get().Plugins();
          const plugins::LoadedPlugin* loaded = list.empty() ? nullptr : &list.back();
          ctx.Print("PluginMarketplaceInstall: installed " + toks[0] +
                    (loaded ? " - " + std::to_string(loaded->commands.size()) + " command(s), " +
                                  std::to_string(loaded->flow_nodes.size()) + " flow node(s) registered"
                            : ""));
        } else {
          ctx.Warn("PluginMarketplaceInstall: " + error);
        }
      }));

  Reg(e, "PluginMarketplaceUninstall", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        if (toks.empty()) {
          ctx.Warn("PluginMarketplaceUninstall: give the id of an installed plug-in to remove (PluginMarketplaceList shows ids)");
          return;
        }
        std::vector<std::string> removed;
        std::vector<std::string> still_needed_by;
        std::string error;
        if (plugins::Marketplace::Get().UninstallById(toks[0], removed, still_needed_by, error)) {
          std::string msg = "PluginMarketplaceUninstall: uninstalled " + toks[0];
          if (removed.size() > 1) {
            std::string extra;
            for (size_t i = 1; i < removed.size(); ++i) extra += (i > 1 ? ", " : "") + removed[i];
            msg += " - also removed " + std::to_string(removed.size() - 1) + " now-orphaned dependenc" +
                   (removed.size() == 2 ? "y" : "ies") + " (" + extra + ")";
          }
          ctx.Print(msg);
          if (!still_needed_by.empty()) {
            std::string names;
            for (const std::string& n : still_needed_by) names += (names.empty() ? "" : ", ") + n;
            ctx.Warn("PluginMarketplaceUninstall: " + names + " still list" +
                     (still_needed_by.size() == 1 ? "s" : "") + " " + toks[0] +
                     " as a dependency and may now be broken");
          }
        } else {
          ctx.Warn("PluginMarketplaceUninstall: " + error);
        }
      }));

  Reg(e, "PluginMarketplaceCheckUpdates", Immediate([](CommandContext& ctx) {
        const auto updates = plugins::Marketplace::Get().CheckForUpdates();
        if (updates.empty()) {
          ctx.Print("PluginMarketplaceCheckUpdates: all installed plug-ins are up to date with the loaded index");
          return;
        }
        ctx.Print("PluginMarketplaceCheckUpdates: " + std::to_string(updates.size()) + " update(s) available");
        for (const auto& u : updates) {
          ctx.Print("  " + u.id + ": " + u.name + " " + u.installed_version + " -> " + u.available_version);
        }
      }));

  Reg(e, "PluginMarketplaceUpdateAll", Immediate([](CommandContext& ctx) {
        std::vector<std::string> updated, failed;
        plugins::Marketplace::Get().UpdateAll(ctx.App(), updated, failed);
        if (updated.empty() && failed.empty()) {
          ctx.Print("PluginMarketplaceUpdateAll: all installed plug-ins are up to date with the loaded index");
          return;
        }
        if (!updated.empty()) {
          std::string ids;
          for (const std::string& id : updated) ids += (ids.empty() ? "" : ", ") + id;
          ctx.Print("PluginMarketplaceUpdateAll: updated " + std::to_string(updated.size()) + " plug-in(s) (" + ids + ")");
        }
        for (const std::string& f : failed) ctx.Warn("PluginMarketplaceUpdateAll: " + f);
      }));

  Reg(e, "PluginMarketplaceInstallAll", Immediate([](CommandContext& ctx) {
        // Installs every entry FindInstalled doesn't already match to a
        // loaded plug-in, the batch counterpart to a single row's Install
        // button - so a freshly loaded (or freshly switched-to) index can
        // be brought in wholesale instead of clicking Install once per row.
        std::vector<std::string> installed, failed;
        plugins::Marketplace::Get().InstallAll(ctx.App(), installed, failed);
        if (installed.empty() && failed.empty()) {
          ctx.Print("PluginMarketplaceInstallAll: everything in the loaded index is already installed");
          return;
        }
        if (!installed.empty()) {
          std::string ids;
          for (const std::string& id : installed) ids += (ids.empty() ? "" : ", ") + id;
          ctx.Print("PluginMarketplaceInstallAll: installed " + std::to_string(installed.size()) + " plug-in(s) (" + ids + ")");
        }
        for (const std::string& f : failed) ctx.Warn("PluginMarketplaceInstallAll: " + f);
      }));

  Reg(e, "PluginMarketplaceVerify", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        if (toks.empty()) {
          ctx.Warn("PluginMarketplaceVerify: give the id of an installed plug-in to verify (PluginMarketplaceList shows ids)");
          return;
        }
        std::string detail;
        const auto status = plugins::Marketplace::Get().VerifyInstalled(toks[0], detail);
        // NoHashToCheck isn't a problem - it just means the index gave
        // nothing to compare against (a bundled_path entry, or one that
        // simply omits sha256) - so it's printed like a normal result, not
        // warned about the way an actual mismatch or install-state issue is.
        if (status == plugins::Marketplace::VerifyStatus::Verified ||
            status == plugins::Marketplace::VerifyStatus::NoHashToCheck) {
          ctx.Print("PluginMarketplaceVerify: " + detail);
        } else {
          ctx.Warn("PluginMarketplaceVerify: " + detail);
        }
      }));

  Reg(e, "PluginMarketplaceVerifyAll", Immediate([](CommandContext& ctx) {
        // VerifyInstalled, run once per entry currently installed via the
        // marketplace - the batch counterpart to PluginMarketplaceVerify,
        // the same way PluginMarketplaceUpdateAll batches PluginMarketplaceCheckUpdates'
        // per-id Update.
        const auto results = plugins::Marketplace::Get().VerifyAll();
        if (results.empty()) {
          ctx.Print("PluginMarketplaceVerifyAll: nothing installed via the marketplace to verify");
          return;
        }
        size_t mismatches = 0;
        for (const auto& r : results) {
          if (r.status == plugins::Marketplace::VerifyStatus::Mismatch ||
              r.status == plugins::Marketplace::VerifyStatus::Error) {
            ctx.Warn("PluginMarketplaceVerifyAll: " + r.detail);
            if (r.status == plugins::Marketplace::VerifyStatus::Mismatch) ++mismatches;
          } else {
            ctx.Print("PluginMarketplaceVerifyAll: " + r.detail);
          }
        }
        ctx.Print("PluginMarketplaceVerifyAll: checked " + std::to_string(results.size()) + " installed plug-in(s)" +
                  (mismatches ? " - " + std::to_string(mismatches) + " mismatch(es)" : ""));
      }));

  Reg(e, "PluginMarketplaceUninstallAll", Immediate([](CommandContext& ctx) {
        // The batch counterpart to PluginMarketplaceUninstall, the same way
        // PluginMarketplaceUpdateAll batches Install: uninstalls every entry
        // in the loaded index currently installed via the marketplace,
        // cascading each one's now-unneeded dependencies exactly like a
        // manual per-row Uninstall would.
        std::vector<std::string> removed, failed;
        plugins::Marketplace::Get().UninstallAll(removed, failed);
        if (removed.empty() && failed.empty()) {
          ctx.Print("PluginMarketplaceUninstallAll: nothing installed via the marketplace to uninstall");
          return;
        }
        if (!removed.empty()) {
          std::string ids;
          for (const std::string& id : removed) ids += (ids.empty() ? "" : ", ") + id;
          ctx.Print("PluginMarketplaceUninstallAll: uninstalled " + std::to_string(removed.size()) + " plug-in(s) (" + ids + ")");
        }
        for (const std::string& f : failed) ctx.Warn("PluginMarketplaceUninstallAll: " + f);
      }));

  Reg(e, "PluginMarketplaceRate", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        if (toks.size() < 2) {
          ctx.Warn("PluginMarketplaceRate: usage PluginMarketplaceRate id rating [comment words...] [Reviewer=Name]");
          return;
        }
        const std::string id = toks[0];
        char* end = nullptr;
        const long rating = std::strtol(toks[1].c_str(), &end, 10);
        if (end == toks[1].c_str() || *end != '\0') {
          ctx.Warn("PluginMarketplaceRate: rating must be a whole number 1..5, got \"" + toks[1] + "\"");
          return;
        }
        std::string reviewer;
        OptionValue(toks, "Reviewer", reviewer);
        std::string comment;
        for (size_t i = 2; i < toks.size(); ++i) {
          if (IsOption(toks[i], "Reviewer")) continue;
          comment += (comment.empty() ? "" : " ") + toks[i];
        }
        plugins::PluginReviewStore::Get().EnsureLoaded(ConfigDirectory());
        std::string error;
        if (plugins::PluginReviewStore::Get().AddReview(id, static_cast<int>(rating), comment, reviewer, CurrentTimestamp(), error)) {
          ctx.Print("PluginMarketplaceRate: recorded a " + std::to_string(rating) + "/5 rating for " + id);
        } else {
          ctx.Warn("PluginMarketplaceRate: " + error);
        }
      }));

  Reg(e, "PluginMarketplaceReviews", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        if (toks.empty()) {
          ctx.Warn("PluginMarketplaceReviews: give the id of a plug-in to list reviews for");
          return;
        }
        plugins::PluginReviewStore::Get().EnsureLoaded(ConfigDirectory());
        const auto& reviews = plugins::PluginReviewStore::Get().ReviewsFor(toks[0]);
        if (reviews.empty()) {
          ctx.Print("PluginMarketplaceReviews: " + toks[0] + " has no reviews yet");
          return;
        }
        ctx.Print("PluginMarketplaceReviews: " + toks[0] + " - " + std::to_string(reviews.size()) + " review(s), average " +
                  FormatNumber(plugins::AverageRating(reviews)) + "/5");
        for (size_t i = 0; i < reviews.size(); ++i) {
          const auto& r = reviews[i];
          ctx.Print("  [" + std::to_string(i) + "] " + std::string(r.reviewer.empty() ? "Anonymous" : r.reviewer) + ": " +
                    std::to_string(r.rating) + "/5" + (r.comment.empty() ? "" : " - " + r.comment));
        }
      }));

  Reg(e, "PluginMarketplaceDeleteReview", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        if (toks.size() < 2) {
          ctx.Warn("PluginMarketplaceDeleteReview: usage PluginMarketplaceDeleteReview id index "
                    "(PluginMarketplaceReviews shows each review's [index])");
          return;
        }
        char* end = nullptr;
        const long index = std::strtol(toks[1].c_str(), &end, 10);
        if (end == toks[1].c_str() || *end != '\0' || index < 0) {
          ctx.Warn("PluginMarketplaceDeleteReview: index must be a non-negative whole number, got \"" + toks[1] + "\"");
          return;
        }
        plugins::PluginReviewStore::Get().EnsureLoaded(ConfigDirectory());
        std::string error;
        if (plugins::PluginReviewStore::Get().DeleteReview(toks[0], static_cast<size_t>(index), error)) {
          ctx.Print("PluginMarketplaceDeleteReview: removed review [" + toks[1] + "] from " + toks[0]);
        } else {
          ctx.Warn("PluginMarketplaceDeleteReview: " + error);
        }
      }));

  Reg(e, "MigratePlugins", Immediate([](CommandContext& ctx) {
        std::vector<std::string> toks;
        while (auto tok = ctx.Engine().TakePendingInput()) toks.push_back(*tok);
        std::string old_dir;
        if (!toks.empty()) old_dir = toks[0];
        else {
          // Best guess at a prior major version's config folder next to this one.
          const fs::path cfg = ConfigDirectory();
          old_dir = (cfg.parent_path() / "Dino7").string();
        }
        std::string error;
        const int n = plugins::Manager::Get().MigrateFrom(ctx.App(), old_dir, error);
        if (n > 0) ctx.Print("MigratePlugins: migrated " + std::to_string(n) + " file(s) from " + old_dir);
        else ctx.Warn("MigratePlugins: " + (error.empty() ? "nothing to migrate from " + old_dir : error));
      }));
}

}  // namespace dino8::app
