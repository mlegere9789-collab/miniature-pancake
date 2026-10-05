// Layer commands.
#include "commands/cmd_common.h"

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
  // PlotStyleTable: creates or edits a named, reusable PlotStyle row - the
  // CTB/STB-table half of "plot styles" (PARITY_MAP.md's "Print and plot
  // output" item), alongside LayerPrintWidth/LayerPlotColor's own flat
  // per-layer-only fields above: a named row (color + lineweight) a layer
  // can point at by name (LayerPlotStyle below) instead of carrying its own
  // copy, so several layers sharing one style name all change together when
  // the row is edited here. Three-token scriptable form "PlotStyleTable
  // <name> <r,g,b|ByLayer> <width>" (same TakePendingInput pattern as
  // LayerPrintWidth/LayerPlotColor); with no name queued, lists every row.
  Reg(e, "PlotStyleTable", Immediate([](CommandContext& ctx) {
        auto name = ctx.Engine().TakePendingInput();
        if (!name) {
          if (ctx.Doc().PlotStyles().empty()) { ctx.Print("No plot styles defined. Use PlotStyleTable <name> <r,g,b|ByLayer> <width> to create one."); return; }
          for (const PlotStyle& s : ctx.Doc().PlotStyles()) {
            const std::string color = s.has_color ? std::to_string(static_cast<int>(s.color.r * 255 + 0.5f)) + "," + std::to_string(static_cast<int>(s.color.g * 255 + 0.5f)) + "," + std::to_string(static_cast<int>(s.color.b * 255 + 0.5f)) : "ByLayer";
            ctx.Print("PlotStyle '" + s.name + "': color " + color + ", width " + (s.width_mm > 0 ? FormatNumber(s.width_mm) + " mm" : (s.width_mm < 0 ? "does not print" : "document default")));
          }
          return;
        }
        auto color_text = ctx.Engine().TakePendingInput();
        auto width_text = ctx.Engine().TakePendingInput();
        if (!color_text || !width_text) { ctx.Warn("Usage: PlotStyleTable <name> <r,g,b|ByLayer> <width>"); return; }
        bool clear = true;
        Color c;
        if (!ParsePlotColorArg(*color_text, clear, c)) { ctx.Warn("'" + *color_text + "' is not r,g,b or ByLayer/Default/None"); return; }
        char* end = nullptr;
        const double w = std::strtod(width_text->c_str(), &end);
        if (end == width_text->c_str()) { ctx.Warn("'" + *width_text + "' is not a number"); return; }
        ctx.Doc().BeginChange("PlotStyleTable");
        PlotStyle* st = ctx.Doc().FindPlotStyle(*name);
        if (!st) { PlotStyle fresh; fresh.name = *name; ctx.Doc().PlotStyles().push_back(fresh); st = &ctx.Doc().PlotStyles().back(); }
        st->has_color = !clear;
        if (!clear) st->color = c;
        st->width_mm = w;
        ctx.Print("PlotStyleTable: '" + *name + "' saved (color " + (st->has_color ? *color_text : "ByLayer") + ", width " + FormatNumber(w) + " mm)");
      }));
  // LayerPlotStyle: assigns a layer's named PlotStyle row (PlotStyleTable
  // above) - Layer::plot_style, read by ResolvePlotStyle/LayerPrints/
  // EffectivePrintWidthMm/EffectivePlotColor's 3-argument overloads
  // (doc/Document.h) ahead of the layer's own flat print_width_mm/
  // has_plot_color/plot_color fields above, so a layer pointed at a named
  // style prints with that style's width/color instead of its own. Same
  // scriptable "LayerPlotStyle [layer name] style name" two-token form as
  // LayerPrintWidth/LayerPlotColor; "None" (or no name at all) clears it
  // back to the layer's own flat fields.
  Reg(e, "LayerPlotStyle", Immediate([](CommandContext& ctx) {
        int idx = ctx.Doc().CurrentLayer();
        auto first = ctx.Engine().TakePendingInput();
        std::optional<std::string> style_text = ctx.Engine().TakePendingInput();
        if (first && style_text) {
          idx = ctx.Doc().FindLayer(*first);
          if (idx < 0) { ctx.Warn("No layer named '" + *first + "'"); return; }
        } else if (first) {
          style_text = first;  // one token: style name for the current layer
        }
        if (!style_text) { ctx.Warn("Usage: LayerPlotStyle [layer name] style name (or None to clear)"); return; }
        const std::string name = ToLower(*style_text) == "none" ? "" : *style_text;
        if (!name.empty() && !ctx.Doc().FindPlotStyle(name)) { ctx.Warn("No plot style named '" + name + "' (use PlotStyleTable to create one)"); return; }
        ctx.Doc().BeginChange("LayerPlotStyle");
        ctx.Doc().Layers()[static_cast<size_t>(idx)].plot_style = name;
        ctx.Print("Layer '" + ctx.Doc().LayerFullPath(idx) + "' plot style: " + (name.empty() ? "none (uses its own print width/color)" : name));
      }));
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
