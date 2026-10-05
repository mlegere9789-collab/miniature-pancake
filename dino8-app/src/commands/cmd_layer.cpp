// Layer commands.
#include "commands/cmd_common.h"
#include "doc/PlotStyleTables.h"

#include <cstdio>

namespace dino8::app {

namespace {

// "r,g,b" (0-255 each) or "ByLayer"/"Default"/"None" to clear the override
// - the same small local parser shape cmd_render.cpp's own ParseColor has,
// scoped down to just what LayerPlotColor needs (no named-color table: a
// plot-style pen color is normally picked to match, not look pretty).
bool ParsePlotColorArg(const std::string& text, bool& clear, Color& out) {
  const std::string lower = [&] { std::string s = text; for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }();
  if (lower == "bylayer" || lower == "default" || lower == "none" || lower.empty()) { clear = true; return true; }
  int r, g, b;
  if (std::sscanf(text.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
    clear = false;
    out = Color::FromBytes(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
    return true;
  }
  return false;
}

class NewLayerCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override { WantText("New layer name", "Layer " + std::to_string(ctx.Doc().Layers().size() + 1)); }
  void OnText(CommandContext& ctx, const std::string& name) override {
    if (name.empty()) { Finish(); return; }
    ctx.Doc().BeginChange("NewLayer");
    int idx = ctx.Doc().AddLayer(name);
    ctx.Doc().SetCurrentLayer(idx);
    ctx.Print("Layer '" + name + "' created and made current");
    Finish();
  }
};

class SetLayerCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantText("Layer name"); }
  void OnText(CommandContext& ctx, const std::string& name) override {
    int idx = ctx.Doc().FindLayer(name);
    if (idx < 0) { ctx.Warn("No layer named '" + name + "'"); Finish(); return; }
    ctx.Doc().SetCurrentLayer(idx);
    ctx.Print("Current layer: " + ctx.Doc().LayerFullPath(idx));
    Finish();
  }
};

class ChangeLayerCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to change layer"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantText("Layer name"); }
  void OnText(CommandContext& ctx, const std::string& name) override {
    int idx = ctx.Doc().FindLayer(name);
    if (idx < 0) {
      // Adding a layer touches layers_ (document-level state), which the
      // fast path's contract excludes - stay on the general path here.
      ctx.Doc().BeginChange("ChangeLayer");
      idx = ctx.Doc().AddLayer(name);
    } else {
      // Existing layer: only layer_index changes on the fixed ids_
      // selection, nothing else about the document - fast path candidate.
      ctx.Doc().BeginChangeForObjects("ChangeLayer", ids_);
    }
    for (SceneObject* o : ctx.Doc().FindMany(ids_)) if (o) { o->layer_index = idx; o->InvalidateDisplay(); }
    ctx.Print("Moved " + std::to_string(ids_.size()) + " object(s) to " + ctx.Doc().LayerFullPath(idx));
    Finish();
  }
  std::vector<ObjectId> ids_;
};

// LayerState: "LayerState Save name" / "Restore name" / "Delete name" /
// "List" - the scriptable/command-line counterpart of the Layer State
// Manager panel (Panels.cpp's DrawLayerStateManager), operating on the same
// per-Document doc.LayerStates() (see doc/Document.h's LayerState) so a
// state saved from either surface is visible to, and persisted the same
// way (Dino8.LayerState.<name> in io/File3dm.cpp Save3dm/Load3dm) as, the
// other.
class LayerStateCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    options = {{"Save", "", {}, false, false}, {"Restore", "", {}, false, false}, {"Delete", "", {}, false, false}, {"List", "", {}, false, false}};
    if (auto t = ctx.Engine().TakePendingInput()) { OnOption(ctx, *t, ""); return; }
    WantEnter("Layer states (Save/Restore/Delete/List)");
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override {
    const std::string l = ToLower(n);
    if (l == "list") { List(ctx); Finish(); return; }
    if (l != "save" && l != "restore" && l != "delete") { ctx.Warn("Unknown option '" + n + "'"); Finish(); return; }
    action_ = l;
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Name");
  }
  void OnText(CommandContext& ctx, const std::string& name) override {
    if (action_.empty()) { OnOption(ctx, name, ""); return; }
    Document& doc = ctx.Doc();
    LayerState* existing = doc.FindLayerState(name);
    if (action_ == "save") {
      LayerState s;
      s.name = name;
      for (const Layer& L : doc.Layers()) s.layers.push_back({L.name, {L.visible, L.locked}});
      if (existing) *existing = s; else doc.LayerStates().push_back(s);
      doc.Touch();
      ctx.Print("Layer state '" + name + "' saved (" + std::to_string(s.layers.size()) + " layer(s))");
    } else if (action_ == "restore") {
      if (!existing) { ctx.Warn("No layer state '" + name + "'"); }
      else {
        for (const auto& [lname, vis_lock] : existing->layers) {
          int idx = doc.FindLayer(lname);
          if (idx >= 0) { doc.Layers()[static_cast<size_t>(idx)].visible = vis_lock.first; doc.Layers()[static_cast<size_t>(idx)].locked = vis_lock.second; }
        }
        doc.Touch();
        ctx.Print("Layer state '" + name + "' restored");
      }
    } else if (action_ == "delete") {
      if (!existing) { ctx.Warn("No layer state '" + name + "'"); }
      else {
        auto& list = doc.LayerStates();
        list.erase(std::find_if(list.begin(), list.end(), [&](const LayerState& s) { return s.name == name; }));
        doc.Touch();
        ctx.Print("Layer state '" + name + "' deleted");
      }
    }
    Finish();
  }
  void OnEnter(CommandContext& ctx) override { List(ctx); Finish(); }

 private:
  void List(CommandContext& ctx) {
    auto& l = ctx.Doc().LayerStates();
    ctx.Print(std::to_string(l.size()) + " layer state(s)");
    for (auto& s : l) ctx.Print("  " + s.name + ": " + std::to_string(s.layers.size()) + " layer(s)");
  }
  std::string action_;
};

// PlotStyleTableSet: creates (if needed) a named, reusable plot style
// table and sets/replaces one layer's row in it - the real CTB/STB-style
// table PARITY_MAP.md's "Print and plot output" item names as still
// missing even after LayerPrintWidth/LayerPlotColor closed the per-layer
// lineweight/color halves: those two commands can only ever set one flat
// pair of fields per layer, with no way to define a reusable named style
// (e.g. "Monochrome") once and assign it to several layers, or swap a
// document between tables. Four chained text prompts (table name, layer
// name, color, width), each checking the script-input queue first
// (ctx.Engine().TakePendingInput()) the same way LayerStateCommand above
// does, so a script can supply all four tokens up front. Re-running this
// for the same table+layer replaces that row rather than appending a
// duplicate, matching BlockSetLookupTable's own "replaces, not appends"
// row semantics for the same `std::find_if`-by-key reason.
class PlotStyleTableSetCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Plot style table name");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (step_ == 0) {
      table_ = t;
      step_ = 1;
      if (auto n = ctx.Engine().TakePendingInput()) { OnText(ctx, *n); return; }
      WantText("Layer name");
      return;
    }
    if (step_ == 1) {
      layer_ = t;
      step_ = 2;
      if (auto n = ctx.Engine().TakePendingInput()) { OnText(ctx, *n); return; }
      WantText("Plot color r,g,b (or ByLayer/Default/None to leave unset)");
      return;
    }
    if (step_ == 2) {
      if (!ParsePlotColorArg(t, clear_, color_)) { ctx.Warn("'" + t + "' is not r,g,b or ByLayer/Default/None"); Finish(); return; }
      step_ = 3;
      if (auto n = ctx.Engine().TakePendingInput()) { OnText(ctx, *n); return; }
      WantText("Print width mm (0 = document default, negative = does not print)");
      return;
    }
    char* end = nullptr;
    const double width = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) { ctx.Warn("'" + t + "' is not a number"); Finish(); return; }
    ctx.Doc().BeginChange("PlotStyleTableSet");
    std::vector<PlotStyleTable> tables = LoadPlotStyleTables(ctx.Doc());
    auto tit = std::find_if(tables.begin(), tables.end(), [&](const PlotStyleTable& x) { return x.name == table_; });
    if (tit == tables.end()) { tables.push_back({table_, {}}); tit = tables.end() - 1; }
    PlotStyleEntry entry;
    entry.layer = layer_;
    entry.has_color = !clear_;
    if (!clear_) entry.color = color_;
    entry.width_mm = width;
    auto eit = std::find_if(tit->entries.begin(), tit->entries.end(), [&](const PlotStyleEntry& x) { return x.layer == layer_; });
    if (eit != tit->entries.end()) *eit = entry; else tit->entries.push_back(entry);
    SavePlotStyleTables(ctx.Doc(), tables);
    ctx.Print("PlotStyleTableSet: table '" + table_ + "', layer '" + layer_ + "' row set");
    Finish();
  }
  int step_ = 0;
  std::string table_, layer_;
  bool clear_ = true;
  Color color_;
};

// PlotStyleTableActivate: makes a named plot style table the document's
// active one for Print/Export, or clears it back to "none" so each
// layer's own print_width_mm/plot_color apply directly again. A real,
// stateful Command (not Immediate) for the same reason
// PlotStyleTableSetCommand above is one rather than a single-shot lambda:
// a script supplies the table name on its own separate line (same
// one-token-per-line convention block_array_script.txt/block_lookup_
// script.txt already use for their own multi-step commands), and only a
// command actively waiting via WantText - not an Immediate handler, which
// returns before the engine would ever feed it a later line - is fed that
// next line by the engine's own dispatch loop.
class PlotStyleTableActivateCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Plot style table to activate (or None to deactivate)", ActivePlotStyleTableName(ctx.Doc()));
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    const bool none = t.empty() || ToLower(t) == "none";
    if (!none) {
      const std::vector<PlotStyleTable> tables = LoadPlotStyleTables(ctx.Doc());
      if (!FindPlotStyleTable(tables, t)) { ctx.Warn("No plot style table named '" + t + "' (use PlotStyleTableSet first)"); Finish(); return; }
    }
    ctx.Doc().BeginChange("PlotStyleTableActivate");
    SetActivePlotStyleTableName(ctx.Doc(), none ? "" : t);
    ctx.Print(none ? "PlotStyleTableActivate: no active plot style table (per-layer print width/plot color apply directly)"
                   : "PlotStyleTableActivate: '" + t + "' is now the active plot style table");
    Finish();
  }
};

}  // namespace

void RegisterLayerCommands(CommandEngine& e) {
  Reg(e, "Layer", Immediate([](CommandContext& ctx) { ctx.App().Panels().layers = true; }));
  Reg(e, "Layers", Immediate([](CommandContext& ctx) { ctx.App().Panels().layers = true; }));
  Reg(e, "NewLayer", Make<NewLayerCommand>());
  Reg(e, "SetLayer", Make<SetLayerCommand>());
  Reg(e, "ChangeLayer", Make<ChangeLayerCommand>());
  Reg(e, "ChangeToCurrentLayer", OnSelection("Select objects to move to the current layer", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        // Fixed, known selection; only layer_index changes - fast path.
        ctx.Doc().BeginChangeForObjects("ChangeToCurrentLayer", ids);
        const int current = ctx.Doc().CurrentLayer();
        for (SceneObject* o : ctx.Doc().FindMany(ids)) if (o) { o->layer_index = current; o->InvalidateDisplay(); }
      }));
  Reg(e, "MatchLayer", OnSelection("Select objects, the last one is the layer to match", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        const SceneObject* ref = ctx.Doc().Find(ids.back());
        if (!ref) return;
        const int ref_layer = ref->layer_index;
        // Fixed, known selection; only layer_index changes - fast path.
        ctx.Doc().BeginChangeForObjects("MatchLayer", ids);
        for (SceneObject* o : ctx.Doc().FindMany(ids)) if (o) { o->layer_index = ref_layer; o->InvalidateDisplay(); }
      }, 2));
  Reg(e, "SetLayerToObject", OnSelection("Select an object", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        if (const SceneObject* o = ctx.Doc().Find(ids.front())) { ctx.Doc().SetCurrentLayer(o->layer_index); ctx.Print("Current layer: " + ctx.Doc().LayerFullPath(o->layer_index)); }
      }));
  Reg(e, "OneLayerOn", Immediate([](CommandContext& ctx) { for (Layer& L : ctx.Doc().Layers()) L.visible = false; ctx.Doc().Layers()[static_cast<size_t>(ctx.Doc().CurrentLayer())].visible = true; }));
  Reg(e, "OneLayerOff", Immediate([](CommandContext& ctx) { ctx.Doc().Layers()[static_cast<size_t>(ctx.Doc().CurrentLayer())].visible = false; }));
  Reg(e, "AllLayersOn", Immediate([](CommandContext& ctx) { for (Layer& L : ctx.Doc().Layers()) L.visible = true; }));
  // LayerOn/LayerOff/LayerLock/LayerUnlock take an optional layer-name
  // argument (e.g. "LayerOn Walls") read from any token already queued on
  // the command line (TakePendingInput), so they can target any layer by
  // name from a script or the command line, not only through the Layers
  // panel; called bare they fall back to their previous fixed behavior, so
  // no interactive prompt is introduced (which would otherwise swallow
  // whatever script line follows as if it were the layer name).
  Reg(e, "LayerOn", Immediate([](CommandContext& ctx) {
        int idx = ctx.Doc().CurrentLayer();
        if (auto name = ctx.Engine().TakePendingInput()) {
          idx = ctx.Doc().FindLayer(*name);
          if (idx < 0) { ctx.Warn("No layer named '" + *name + "'"); return; }
        }
        ctx.Doc().Layers()[static_cast<size_t>(idx)].visible = true;
        ctx.Print("Layer '" + ctx.Doc().LayerFullPath(idx) + "' turned on");
      }));
  Reg(e, "LayerOff", Immediate([](CommandContext& ctx) {
        if (auto name = ctx.Engine().TakePendingInput()) {
          int idx = ctx.Doc().FindLayer(*name);
          if (idx < 0) { ctx.Warn("No layer named '" + *name + "'"); return; }
          ctx.Doc().Layers()[static_cast<size_t>(idx)].visible = false;
          ctx.Print("Layer '" + ctx.Doc().LayerFullPath(idx) + "' turned off");
          return;
        }
        std::vector<ObjectId> sel = ctx.Doc().SelectedIds();
        if (!sel.empty()) { for (SceneObject* o : ctx.Doc().FindMany(sel)) if (o) ctx.Doc().Layers()[static_cast<size_t>(o->layer_index)].visible = false; return; }
        ctx.Doc().Layers()[static_cast<size_t>(ctx.Doc().CurrentLayer())].visible = false;
      }));
  Reg(e, "LayerLock", Immediate([](CommandContext& ctx) {
        if (auto name = ctx.Engine().TakePendingInput()) {
          int idx = ctx.Doc().FindLayer(*name);
          if (idx < 0) { ctx.Warn("No layer named '" + *name + "'"); return; }
          ctx.Doc().Layers()[static_cast<size_t>(idx)].locked = true;
          ctx.Print("Layer '" + ctx.Doc().LayerFullPath(idx) + "' locked");
          return;
        }
        std::vector<ObjectId> sel = ctx.Doc().SelectedIds();
        if (!sel.empty()) { for (SceneObject* o : ctx.Doc().FindMany(sel)) if (o) ctx.Doc().Layers()[static_cast<size_t>(o->layer_index)].locked = true; return; }
        ctx.Doc().Layers()[static_cast<size_t>(ctx.Doc().CurrentLayer())].locked = true;
      }));
  Reg(e, "LayerUnlock", Immediate([](CommandContext& ctx) {
        if (auto name = ctx.Engine().TakePendingInput()) {
          int idx = ctx.Doc().FindLayer(*name);
          if (idx < 0) { ctx.Warn("No layer named '" + *name + "'"); return; }
          ctx.Doc().Layers()[static_cast<size_t>(idx)].locked = false;
          ctx.Print("Layer '" + ctx.Doc().LayerFullPath(idx) + "' unlocked");
          return;
        }
        for (Layer& L : ctx.Doc().Layers()) L.locked = false;
        ctx.Print("All layers unlocked");
      }));
  // LayerPrintWidth: sets a layer's Print and plot output lineweight
  // (Layer::print_width_mm, doc/Document.h) - the same real Rhino
  // ON_Layer::PlotWeight convention io/File3dm.cpp round-trips through:
  // 0 = document default, > 0 = an explicit width in mm, < 0 = the layer
  // is skipped entirely by Print/Export (ExportSvg/ExportPdf's CollectPaths,
  // io/FileExchange.cpp), same as before this command existed for the "layer
  // does not print" case. Scriptable two-token form "LayerPrintWidth <name>
  // <width>" (same TakePendingInput pattern as LayerOn/Off/Lock/Unlock
  // above); with one queued token it is the width for the current layer,
  // and with none it prompts for the width interactively.
  Reg(e, "LayerPrintWidth", Immediate([](CommandContext& ctx) {
        int idx = ctx.Doc().CurrentLayer();
        auto first = ctx.Engine().TakePendingInput();
        std::optional<std::string> width_text = ctx.Engine().TakePendingInput();
        if (first && width_text) {
          idx = ctx.Doc().FindLayer(*first);
          if (idx < 0) { ctx.Warn("No layer named '" + *first + "'"); return; }
        } else if (first) {
          width_text = first;  // one token: width for the current layer
        }
        if (!width_text) { ctx.Warn("Usage: LayerPrintWidth [layer name] width"); return; }
        char* end = nullptr;
        const double w = std::strtod(width_text->c_str(), &end);
        if (end == width_text->c_str()) { ctx.Warn("'" + *width_text + "' is not a number"); return; }
        ctx.Doc().BeginChange("LayerPrintWidth");
        ctx.Doc().Layers()[static_cast<size_t>(idx)].print_width_mm = w;
        const std::string desc = w > 0 ? FormatNumber(w) + " mm" : (w < 0 ? "does not print" : "document default");
        ctx.Print("Layer '" + ctx.Doc().LayerFullPath(idx) + "' print width: " + desc);
      }));
  // LayerPlotColor: sets a layer's Print and plot output pen color
  // (Layer::has_plot_color/plot_color, doc/Document.h) - the color half of
  // "plot styles (CTB/STB)" (PARITY_MAP.md's "Print and plot output" item),
  // mirroring real Rhino's own ON_Layer::PlotColor/SetPlotColor the same
  // way LayerPrintWidth above already mirrors ON_Layer::PlotWeight. Same
  // scriptable "LayerPlotColor [layer name] r,g,b" two-token form as
  // LayerPrintWidth; "ByLayer"/"Default"/"None" (or no color at all)
  // clears the override back to the object's own on-screen display color.
  Reg(e, "LayerPlotColor", Immediate([](CommandContext& ctx) {
        int idx = ctx.Doc().CurrentLayer();
        auto first = ctx.Engine().TakePendingInput();
        std::optional<std::string> color_text = ctx.Engine().TakePendingInput();
        if (first && color_text) {
          idx = ctx.Doc().FindLayer(*first);
          if (idx < 0) { ctx.Warn("No layer named '" + *first + "'"); return; }
        } else if (first) {
          color_text = first;  // one token: color for the current layer
        }
        if (!color_text) { ctx.Warn("Usage: LayerPlotColor [layer name] r,g,b (or ByLayer/Default/None to clear)"); return; }
        bool clear = true;
        Color c;
        if (!ParsePlotColorArg(*color_text, clear, c)) { ctx.Warn("'" + *color_text + "' is not r,g,b or ByLayer/Default/None"); return; }
        ctx.Doc().BeginChange("LayerPlotColor");
        Layer& L = ctx.Doc().Layers()[static_cast<size_t>(idx)];
        L.has_plot_color = !clear;
        if (!clear) L.plot_color = c;
        const std::string desc = clear ? "ByLayer (display color)" : std::to_string(static_cast<int>(c.r * 255 + 0.5f)) + "," +
                                             std::to_string(static_cast<int>(c.g * 255 + 0.5f)) + "," + std::to_string(static_cast<int>(c.b * 255 + 0.5f));
        ctx.Print("Layer '" + ctx.Doc().LayerFullPath(idx) + "' plot color: " + desc);
      }));
  // PlotStyleTableSet/PlotStyleTableActivate: the real, named/reusable
  // plot style table (doc/PlotStyleTables.h) PARITY_MAP.md's "Print and
  // plot output" item still named missing even after LayerPrintWidth/
  // LayerPlotColor closed the per-layer lineweight/color halves above -
  // "Monochrome" can now be defined once and assigned to several layers,
  // or a document swapped between tables, rather than only ever one flat
  // color/width pair per layer. io/FileExchange.cpp's CollectPaths/
  // ExportSvg/ExportPdf consult whichever table PlotStyleTableActivate
  // last named (if any) before falling back to each layer's own
  // print_width_mm/plot_color, exactly as before this pair of commands
  // existed.
  Reg(e, "PlotStyleTableSet", Make<PlotStyleTableSetCommand>(), CommandStatus::Implemented,
      "Creates (if needed) a named plot style table and sets/replaces one layer's color+lineweight row in it, the "
      "same two columns a real CTB/STB table assigns per layer - does not itself change what prints until "
      "PlotStyleTableActivate makes this table the document's active one.");
  Reg(e, "PlotStyleTableActivate", Make<PlotStyleTableActivateCommand>(), CommandStatus::Implemented,
      "Makes a named plot style table (PlotStyleTableSet) the document's active one for Print/Export, or clears "
      "the active table back to 'none' (None/empty) so each layer's own print_width_mm/plot_color apply directly - "
      "the active table's own entries, when it has one for a layer, take over from that layer's direct fields.");
  Reg(e, "LayerStateManager", Immediate([](CommandContext& ctx) { ctx.App().Panels().layer_state_manager = true; }));
  Reg(e, "LayerState", Make<LayerStateCommand>());
  Reg(e, "Purge", Immediate([](CommandContext& ctx) {
        // Sweeps every named-item table the document keeps, the same way
        // AutoCAD's PURGE does in one pass: an item is unused when nothing
        // in the live document references it any more (same referenced-count
        // == 0 rule as the pre-existing layer-only purge below), and a
        // handful of built-in defaults (the current layer, "Continuous", the
        // active annotation style) are protected exactly like the current
        // layer already was, since removing them would break the fallback
        // every new object/layer implicitly relies on.
        //
        // Hatch patterns are deliberately out of scope: HatchLibrary (see
        // drafting/HatchLibrary.h) is a static, document-independent .pat
        // library (like a font list), not a per-document named table with
        // per-object usage - there is nothing in a Document to purge there.
        ctx.Doc().BeginChange("Purge");
        Document& doc = ctx.Doc();

        int layers_removed = 0;
        for (int i = static_cast<int>(doc.Layers().size()) - 1; i >= 0; --i) {
          bool used = false;
          for (const SceneObject& o : doc.Objects()) if (o.layer_index == i) { used = true; break; }
          if (!used && i != doc.CurrentLayer() && doc.RemoveLayer(i)) ++layers_removed;
        }

        int blocks_removed = 0;
        for (int i = static_cast<int>(doc.Blocks().size()) - 1; i >= 0; --i) {
          const std::string& name = doc.Blocks()[static_cast<size_t>(i)].name;
          bool used = false;
          for (const SceneObject& o : doc.Objects()) { auto it = o.user_text.find("Block"); if (it != o.user_text.end() && it->second == name) { used = true; break; } }
          if (!used && doc.RemoveBlock(name)) ++blocks_removed;
        }

        int materials_removed = 0;
        for (int i = static_cast<int>(doc.Materials().size()) - 1; i >= 0; --i) {
          const std::string& name = doc.Materials()[static_cast<size_t>(i)].name;
          bool used = false;
          for (const SceneObject& o : doc.Objects()) if (o.material_name == name) { used = true; break; }
          if (!used) for (const Layer& l : doc.Layers()) if (l.material == name) { used = true; break; }
          if (!used && doc.RemoveMaterial(name)) ++materials_removed;
        }

        int linetypes_removed = 0;
        for (int i = static_cast<int>(doc.Linetypes().size()) - 1; i >= 0; --i) {
          const std::string& name = doc.Linetypes()[static_cast<size_t>(i)].name;
          if (name == "Continuous") continue;
          bool used = false;
          for (const SceneObject& o : doc.Objects()) if (o.linetype == name) { used = true; break; }
          if (!used) for (const Layer& l : doc.Layers()) if (l.linetype == name) { used = true; break; }
          if (!used && doc.RemoveLinetype(name)) ++linetypes_removed;
        }

        int styles_removed = 0;
        for (int i = static_cast<int>(doc.AnnotationStyles().size()) - 1; i >= 0; --i) {
          const std::string& name = doc.AnnotationStyles()[static_cast<size_t>(i)].name;
          if (name == doc.Settings().annotation_style) continue;
          bool used = false;
          for (const SceneObject& o : doc.Objects()) { auto it = o.user_text.find("Style"); if (it != o.user_text.end() && it->second == name) { used = true; break; } }
          if (!used && doc.RemoveAnnotationStyle(name)) ++styles_removed;
        }

        int groups_removed = doc.RemoveEmptyGroups();

        // AutoCAD-style summary report, one line per non-zero category so a
        // clean sweep still just says "nothing to purge" instead of a wall
        // of zeros.
        std::vector<std::string> parts;
        auto add = [&](int n, const char* singular, const char* plural) { if (n > 0) parts.push_back(std::to_string(n) + " " + (n == 1 ? singular : plural)); };
        add(layers_removed, "layer", "layers");
        add(blocks_removed, "block", "blocks");
        add(materials_removed, "material", "materials");
        add(linetypes_removed, "linetype", "linetypes");
        add(styles_removed, "annotation style", "annotation styles");
        add(groups_removed, "empty group", "empty groups");
        if (parts.empty()) { ctx.Print("Purge: nothing to remove"); return; }
        std::string msg = "Purge: removed";
        for (size_t i = 0; i < parts.size(); ++i) msg += (i == 0 ? " " : (i + 1 == parts.size() ? " and " : ", ")) + parts[i];
        ctx.Print(msg);
      }));
}

}  // namespace dino8::app
