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
    if (idx < 0) { ctx.Doc().BeginChange("ChangeLayer"); idx = ctx.Doc().AddLayer(name); } else ctx.Doc().BeginChange("ChangeLayer");
    for (ObjectId id : ids_) if (SceneObject* o = ctx.Doc().Find(id)) { o->layer_index = idx; o->InvalidateDisplay(); }
    ctx.Print("Moved " + std::to_string(ids_.size()) + " object(s) to " + ctx.Doc().LayerFullPath(idx));
    Finish();
  }
  std::vector<ObjectId> ids_;
};

}  // namespace

void RegisterLayerCommands(CommandEngine& e) {
  Reg(e, "Layer", Immediate([](CommandContext& ctx) { ctx.App().Panels().layers = true; }));
  Reg(e, "Layers", Immediate([](CommandContext& ctx) { ctx.App().Panels().layers = true; }));
  Reg(e, "NewLayer", Make<NewLayerCommand>());
  Reg(e, "SetLayer", Make<SetLayerCommand>());
  Reg(e, "ChangeLayer", Make<ChangeLayerCommand>());
  Reg(e, "ChangeToCurrentLayer", OnSelection("Select objects to move to the current layer", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("ChangeToCurrentLayer");
        for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->layer_index = ctx.Doc().CurrentLayer(); o->InvalidateDisplay(); }
      }));
  Reg(e, "MatchLayer", OnSelection("Select objects, the last one is the layer to match", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        const SceneObject* ref = ctx.Doc().Find(ids.back());
        if (!ref) return;
        ctx.Doc().BeginChange("MatchLayer");
        for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->layer_index = ref->layer_index; o->InvalidateDisplay(); }
      }, 2));
  Reg(e, "SetLayerToObject", OnSelection("Select an object", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        if (const SceneObject* o = ctx.Doc().Find(ids.front())) { ctx.Doc().SetCurrentLayer(o->layer_index); ctx.Print("Current layer: " + ctx.Doc().LayerFullPath(o->layer_index)); }
      }));
  Reg(e, "OneLayerOn", Immediate([](CommandContext& ctx) { for (Layer& L : ctx.Doc().Layers()) L.visible = false; ctx.Doc().Layers()[static_cast<size_t>(ctx.Doc().CurrentLayer())].visible = true; }));
  Reg(e, "OneLayerOff", Immediate([](CommandContext& ctx) { ctx.Doc().Layers()[static_cast<size_t>(ctx.Doc().CurrentLayer())].visible = false; }));
  Reg(e, "AllLayersOn", Immediate([](CommandContext& ctx) { for (Layer& L : ctx.Doc().Layers()) L.visible = true; }));
  Reg(e, "LayerOn", Immediate([](CommandContext& ctx) { ctx.Doc().Layers()[static_cast<size_t>(ctx.Doc().CurrentLayer())].visible = true; }), CommandStatus::Partial, "Turns on the current layer; toggle others in the Layers panel.");
  Reg(e, "LayerOff", Immediate([](CommandContext& ctx) { for (const SceneObject& o : ctx.Doc().Objects()) if (o.selected) ctx.Doc().Layers()[static_cast<size_t>(o.layer_index)].visible = false; }), CommandStatus::Partial, "Turns off the layers of selected objects.");
  Reg(e, "LayerLock", Immediate([](CommandContext& ctx) { for (const SceneObject& o : ctx.Doc().Objects()) if (o.selected) ctx.Doc().Layers()[static_cast<size_t>(o.layer_index)].locked = true; }), CommandStatus::Partial);
  Reg(e, "LayerUnlock", Immediate([](CommandContext& ctx) { for (Layer& L : ctx.Doc().Layers()) L.locked = false; }), CommandStatus::Partial, "Unlocks all layers.");
  Reg(e, "LayerStateManager", Immediate([](CommandContext& ctx) { ctx.App().Panels().layer_state_manager = true; }));
  Reg(e, "Purge", Immediate([](CommandContext& ctx) {
        ctx.Doc().BeginChange("Purge");
        int removed = 0;
        for (int i = static_cast<int>(ctx.Doc().Layers().size()) - 1; i >= 0; --i) { bool used = false; for (const SceneObject& o : ctx.Doc().Objects()) if (o.layer_index == i) { used = true; break; } if (!used && i != ctx.Doc().CurrentLayer() && ctx.Doc().RemoveLayer(i)) ++removed; }
        ctx.Print("Purged " + std::to_string(removed) + " unused layer(s)");
      }));
}

}  // namespace dino8::app
