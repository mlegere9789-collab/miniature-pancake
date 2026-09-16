// Layer commands.
#include "commands/cmd_common.h"

namespace dino8::app {

namespace {

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
    for (ObjectId id : ids_) if (SceneObject* o = ctx.Doc().Find(id)) { o->layer_index = idx; o->InvalidateDisplay(); }
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
        for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->layer_index = ctx.Doc().CurrentLayer(); o->InvalidateDisplay(); }
      }));
  Reg(e, "MatchLayer", OnSelection("Select objects, the last one is the layer to match", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        const SceneObject* ref = ctx.Doc().Find(ids.back());
        if (!ref) return;
        // Fixed, known selection; only layer_index changes - fast path.
        ctx.Doc().BeginChangeForObjects("MatchLayer", ids);
        for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->layer_index = ref->layer_index; o->InvalidateDisplay(); }
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
        if (!sel.empty()) { for (ObjectId id : sel) if (SceneObject* o = ctx.Doc().Find(id)) ctx.Doc().Layers()[static_cast<size_t>(o->layer_index)].visible = false; return; }
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
        if (!sel.empty()) { for (ObjectId id : sel) if (SceneObject* o = ctx.Doc().Find(id)) ctx.Doc().Layers()[static_cast<size_t>(o->layer_index)].locked = true; return; }
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
  Reg(e, "LayerStateManager", Immediate([](CommandContext& ctx) { ctx.App().Panels().layer_state_manager = true; }));
  Reg(e, "LayerState", Make<LayerStateCommand>());
  Reg(e, "Purge", Immediate([](CommandContext& ctx) {
        ctx.Doc().BeginChange("Purge");
        int removed = 0;
        for (int i = static_cast<int>(ctx.Doc().Layers().size()) - 1; i >= 0; --i) { bool used = false; for (const SceneObject& o : ctx.Doc().Objects()) if (o.layer_index == i) { used = true; break; } if (!used && i != ctx.Doc().CurrentLayer() && ctx.Doc().RemoveLayer(i)) ++removed; }
        ctx.Print("Purged " + std::to_string(removed) + " unused layer(s)");
      }));
}

}  // namespace dino8::app
