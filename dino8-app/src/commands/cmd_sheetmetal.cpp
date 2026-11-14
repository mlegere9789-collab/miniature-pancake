// Sheet-metal feature commands (parity-map "kernel: Feature operations" -
// "Sheet-metal features" - previously no dino8-app command wired any of
// dino8::kernel::Bend()/MultiBend()/BendAllowance()/UnfoldBend()/
// UnfoldMultiBend() (sheet_metal.h/.cpp) in at all, despite all five
// already being real, tested kernel primitives). SheetMetalBend wires the
// single-bend case: MultiBend/Unfold stay kernel-only for a future pass,
// the same incremental-wiring pattern this app already uses elsewhere
// (e.g. ArchComponents.cpp's Bolt/Nut picking up ScrewThread()).
//
// Named SheetMetalBend, not the shorter "Bend" the kernel function itself
// uses - cmd_meshtools.cpp already registers a Rhino-parity "Bend" command
// (a spine-based FFD mesh deformation, Deform()/BendMap), a completely
// different feature that an unqualified "Bend" here would have silently
// shadowed (CommandEngine::Register has no duplicate-name guard - the
// last Reg() call for a given name simply wins, confirmed directly by
// first trying the shorter name and watching smoke.sh's own pre-existing
// "Bend converted the box to a mesh and bent it" fixture fail instead).
#include <cstdlib>
#include <utility>
#include <vector>

#include "commands/cmd_common.h"
#include "dino8/kernel/sheet_metal.h"

namespace dino8::app {

namespace {

// SheetMetalBend: pick a pivot point for the bend's own arc axis, then
// the 6 parameters kernel::Bend() itself takes, in the same order its own
// signature does. Built in Bend()'s own local frame (profile in the XY
// plane, arc centred at the local origin, extruded along +Z by `width`)
// and then rotated/translated onto the active construction plane at the
// picked point - the same "build in a canonical local frame, then
// ON_Xform::Rotation(from, to) it into place" idiom cmd_solidtools.cpp's
// ProfileCutter() already uses for a profile curve, generalized here to
// a whole solid.
class SheetMetalBendCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Bend pivot point (the bend's own arc axis)"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pivot_ = p;
    ctx.SetLastPoint(p);
    NextNumber(ctx);
  }
  void NextNumber(CommandContext& ctx) {
    static const std::vector<std::pair<const char*, double>> kSteps = {
        {"Leg 1 length", 0.05}, {"Leg 2 length", 0.05}, {"Width", 0.1},
        {"Thickness", 0.001}, {"Inside bend radius", 0.001}, {"Bend angle (degrees)", 90.0},
    };
    if (values_.size() >= kSteps.size()) { Build(ctx); return; }
    const auto& step = kSteps[values_.size()];
    WantNumber(step.first, step.second);
  }
  void OnNumber(CommandContext& ctx, double v) override { values_.push_back(v); NextNumber(ctx); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Number && default_number) OnNumber(ctx, *default_number); }
  void Build(CommandContext& ctx) {
    kernel::Brep b;
    try {
      b = kernel::Bend(values_[0], values_[1], values_[2], values_[3], values_[4], values_[5]);
    } catch (const std::exception& e) {
      ctx.Warn(std::string("SheetMetalBend: ") + e.what());
      Finish();
      return;
    }
    const ON_Plane from(Point3d(0, 0, 0), Vector3d(1, 0, 0), Vector3d(0, 1, 0));
    ON_Plane to = ActivePlane(ctx);
    to.SetOrigin(pivot_);
    ON_Xform xf;
    xf.Rotation(from, to);
    b.raw().Transform(xf);
    ctx.Doc().BeginChange("SheetMetalBend");
    ObjectId id = ctx.Doc().Add(SceneObject::MakeBrep(b));
    ctx.Print("SheetMetalBend #" + std::to_string(id) + " added.");
    Finish();
  }

 private:
  Point3d pivot_{0, 0, 0};
  std::vector<double> values_;
};

}  // namespace

void RegisterSheetMetalCommands(CommandEngine& e) {
  Reg(e, "SheetMetalBend", Make<SheetMetalBendCommand>(), CommandStatus::Implemented,
      "A single sheet-metal bend between two flat legs (kernel::Bend(), sheet_metal.h) - a real swept solid through "
      "a circular arc, not an approximation. Not the same command as Bend (cmd_meshtools.cpp), Rhino's own "
      "spine-based FFD mesh-bend deformer. No multi-bend chain, unfold/flat-pattern, or flange feature wired in "
      "yet (kernel::MultiBend()/UnfoldBend()/UnfoldMultiBend() all exist and are tested, just not called by any "
      "command yet).");
}

}  // namespace dino8::app
