// Dino Flow (Grasshopper-class node editor) and plug-in system commands.
// Registered last so it can freely reference every other subsystem.
#include <cctype>
#include <filesystem>
#include <sstream>

#include "app/Settings.h"
#include "commands/cmd_common.h"
#include "flow/FlowEditor.h"
#include "flow/FlowPreview.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginPanel.h"

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
        ctx.Print("GrasshopperDeveloperSettings: node SDK docs are in the Help panel; the C API header is include/dino8_plugin.h.");
      }));

  Reg(e, "GrasshopperGetSDKDocumentation", Immediate([](CommandContext& ctx) {
        ctx.App().ShowHelpFor("GrasshopperGetSDKDocumentation");
        ctx.App().Panels().help = true;
        ctx.Print("GrasshopperGetSDKDocumentation: opened the Help panel; see include/dino8_plugin.h for the plug-in C ABI.");
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

  Reg(e, "PlugInManager", Immediate([](CommandContext& ctx) { ctx.App().Panels().plugin_manager = true; }));
  Reg(e, "PluginManager", Immediate([](CommandContext& ctx) { ctx.App().Panels().plugin_manager = true; }));
  Reg(e, "PackageManager", Immediate([](CommandContext& ctx) { ctx.App().Panels().package_manager = true; }));

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
