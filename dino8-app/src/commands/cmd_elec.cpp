// Commands for the parametric electrical schematic symbols in elec/
// ElecComponents.h (Resistor/Capacitor/Switch/Ground/Lamp/WireRun - Rhino
// has none of these; see ElecComponents.h's own scope/disclosure comment).
// Each symbol-creation command follows cmd_arch.cpp's ArchLineCommand shape
// almost exactly: a placement point, then a direction point (magnitude
// ignored, the same p0/p1 convention ArchComponent's Bolt/Nut/Washer use),
// then a numeric follow-up (length/height/gap/size...). WireRun is the one
// exception - both of its points are real endpoints, and ElecRebuild
// re-resolves them the same way UpdateBillOfMaterials/UpdateDimensions
// re-derive other associative annotations elsewhere in this app.
#include <sstream>

#include "commands/annotate_common.h"
#include "commands/cmd_common.h"
#include "elec/ElecComponents.h"

namespace dino8::app {

using elec::ElecComponent;
using elec::ElecType;

namespace {

// Two points (placement, then a direction-only second point) then a numeric
// follow-up - the same shape as cmd_arch.cpp's ArchLineCommand, duplicated
// here rather than shared across files (that file's own class is likewise
// private to it; StructShapeCommand/MepRunCommand there don't share a base
// with it either).
class ElecLineCommand : public Command {
 public:
  struct NumberStep {
    std::string prompt;
    double default_value;
  };
  ElecLineCommand(std::string first_prompt, std::string second_prompt, std::vector<NumberStep> numbers,
                  std::function<void(CommandContext&, Point3d, Point3d, const std::vector<double>&)> on_done)
      : first_(std::move(first_prompt)), second_(std::move(second_prompt)), numbers_(std::move(numbers)), on_done_(std::move(on_done)) {}

  void Begin(CommandContext&) override { WantPoint(first_); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) { WantPoint(second_); return; }
    NextNumber(ctx);
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.size() != 1) return;
    ctx.ClearPreview();
    ctx.AddPreviewLine(pts_[0], h);
  }
  void NextNumber(CommandContext& ctx) {
    if (values_.size() >= numbers_.size()) { Done(ctx); return; }
    WantNumber(numbers_[values_.size()].prompt, numbers_[values_.size()].default_value);
  }
  void OnNumber(CommandContext& ctx, double v) override { values_.push_back(v); NextNumber(ctx); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override {
    if (want == Want::Number && default_number) OnNumber(ctx, *default_number);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  void Done(CommandContext& ctx) {
    ctx.ClearPreview();
    on_done_(ctx, pts_[0], pts_[1], values_);
    Finish();
  }

 private:
  std::string first_, second_;
  std::vector<NumberStep> numbers_;
  std::function<void(CommandContext&, Point3d, Point3d, const std::vector<double>&)> on_done_;
  std::vector<Point3d> pts_;
  std::vector<double> values_;
};

void ReportNew(CommandContext& ctx, const char* type, int id) {
  ctx.Print(std::string(type) + " #" + std::to_string(id) + " added.");
}

CommandFactory ResistorFactory() {
  return Make<ElecLineCommand>(
      "Resistor position", "Resistor direction (a second point; only its direction from the first matters)",
      std::vector<ElecLineCommand::NumberStep>{{"Resistor length", 0.6}, {"Resistor height", 0.3}},
      [](CommandContext& ctx, Point3d p0, Point3d p1, const std::vector<double>& v) {
        ElecComponent c;
        c.type = ElecType::Resistor;
        c.p0 = p0; c.p1 = p1;
        c.length = v[0]; c.height = v[1];
        ctx.Doc().BeginChange("Resistor");
        int id = elec::AddElecComponent(ctx.Doc(), c);
        ReportNew(ctx, "Resistor", id);
      });
}

CommandFactory CapacitorFactory() {
  return Make<ElecLineCommand>(
      "Capacitor position", "Capacitor direction (a second point; only its direction from the first matters)",
      std::vector<ElecLineCommand::NumberStep>{{"Plate gap", 0.1}, {"Plate length", 0.4}},
      [](CommandContext& ctx, Point3d p0, Point3d p1, const std::vector<double>& v) {
        ElecComponent c;
        c.type = ElecType::Capacitor;
        c.p0 = p0; c.p1 = p1;
        c.gap = v[0]; c.plate_length = v[1];
        ctx.Doc().BeginChange("Capacitor");
        int id = elec::AddElecComponent(ctx.Doc(), c);
        ReportNew(ctx, "Capacitor", id);
      });
}

CommandFactory SwitchFactory() {
  return Make<ElecLineCommand>(
      "Switch position (fixed terminal)", "Switch direction (a second point; only its direction from the first matters)",
      std::vector<ElecLineCommand::NumberStep>{{"Terminal separation", 0.6}, {"Blade open-height", 0.25}},
      [](CommandContext& ctx, Point3d p0, Point3d p1, const std::vector<double>& v) {
        ElecComponent c;
        c.type = ElecType::Switch;
        c.p0 = p0; c.p1 = p1;
        c.length = v[0]; c.height = v[1];
        ctx.Doc().BeginChange("Switch");
        int id = elec::AddElecComponent(ctx.Doc(), c);
        ReportNew(ctx, "Switch", id);
      });
}

CommandFactory GroundFactory() {
  return Make<ElecLineCommand>(
      "Ground/Earth attachment point", "Lead direction (a second point; only its direction from the first matters)",
      std::vector<ElecLineCommand::NumberStep>{{"Symbol size", 0.3}},
      [](CommandContext& ctx, Point3d p0, Point3d p1, const std::vector<double>& v) {
        ElecComponent c;
        c.type = ElecType::Ground;
        c.p0 = p0; c.p1 = p1;
        c.size = v[0];
        ctx.Doc().BeginChange("Ground");
        int id = elec::AddElecComponent(ctx.Doc(), c);
        ReportNew(ctx, "Ground", id);
      });
}

CommandFactory LampFactory() {
  return Make<ElecLineCommand>(
      "Lamp center", "Lamp orientation (a second point; only its direction from the first matters)",
      std::vector<ElecLineCommand::NumberStep>{{"Lamp diameter", 0.3}},
      [](CommandContext& ctx, Point3d p0, Point3d p1, const std::vector<double>& v) {
        ElecComponent c;
        c.type = ElecType::Lamp;
        c.p0 = p0; c.p1 = p1;
        c.size = v[0];
        ctx.Doc().BeginChange("Lamp");
        int id = elec::AddElecComponent(ctx.Doc(), c);
        ReportNew(ctx, "Lamp", id);
      });
}

// WireRun: both points are real endpoints (not a placement + direction
// pair). If either coincides with a real Point object or a curve's
// start/end when the wire is drawn, FindPointAnchor (annotate_common.h -
// the same helper DimLinear/CenterLine use) records it, so ElecRebuild can
// later re-resolve that object's *current* position - the "associative
// rebuild rule" mirroring ArchComponent's host-Wall re-cut pattern.
class WireRunCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("WireRun start point"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!have_p0_) { p0_ = p; have_p0_ = true; ctx.SetLastPoint(p); WantPoint("WireRun end point"); return; }
    p1_ = p;
    ctx.SetLastPoint(p);
    Build(ctx);
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!have_p0_) return;
    ctx.ClearPreview();
    ctx.AddPreviewLine(p0_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  void Build(CommandContext& ctx) {
    ElecComponent c;
    c.type = ElecType::WireRun;
    c.p0 = p0_; c.p1 = p1_;
    ObjectId ref; std::string which;
    if (FindPointAnchor(ctx.Doc(), p0_, ref, which)) { c.has_ref0 = true; c.ref0 = ref; c.end0 = which; }
    if (FindPointAnchor(ctx.Doc(), p1_, ref, which)) { c.has_ref1 = true; c.ref1 = ref; c.end1 = which; }
    ctx.Doc().BeginChange("WireRun");
    int id = elec::AddElecComponent(ctx.Doc(), c);
    ReportNew(ctx, "WireRun", id);
    Finish();
  }

 private:
  bool have_p0_ = false;
  Point3d p0_{0, 0, 0}, p1_{0, 0, 0};
};

// ElecTag: bakes a reference-designator string ("R1", "C3"...) as real
// outline-curve text, exactly the same TextToCurves/AddGlyphCurves path
// cmd_annotate.cpp's Text command uses (see that command's own AddTextCurves
// helper) - so the tag prints, exports and Booleans like any other curve
// geometry, not a display-only label.
class ElecTagCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    height_ = AnnotationTextHeight(ctx);
    options = {{"Height", FormatNumber(height_), {}, true, false}};
    WantText("Reference designator text (e.g. R1, C3)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n != "Height") return;
    double h = std::atof(v.c_str());
    if (h > 0) height_ = h;
    options[0].value = FormatNumber(height_);
  }
  void OnText(CommandContext&, const std::string& t) override {
    if (!text_.empty()) return;
    text_ = t;
    WantPoint("Tag location (near the symbol)");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(p);
    GlyphSpec g;
    g.text = text_; g.height = height_; g.plane = pl; g.center = false;
    ctx.Doc().BeginChange("ElecTag");
    const std::string style = ctx.Settings().annotation_style;
    std::vector<ObjectId> ids = AddGlyphCurves(ctx, g, ctx.Doc().CurrentLayer(), -1, {{"Annotation", "ElecTag"}, {"Style", style}});
    if (ids.empty()) { ctx.Warn("ElecTag: no TrueType font found for text outlines"); Finish(); return; }
    int group = ctx.Doc().CreateGroup(ids, "ElecTag");
    ctx.Print("ElecTag: \"" + text_ + "\" baked as " + std::to_string(ids.size()) + " curve(s) (group " + std::to_string(group) + ").");
    Finish();
  }

 private:
  double height_ = 1;
  std::string text_;
};

}  // namespace

void RegisterElecCommands(CommandEngine& e) {
  Reg(e, "Resistor", ResistorFactory(), CommandStatus::Implemented,
      "A simplified, commonly-recognized zigzag symbol (a lead-in, three zigzag peaks, a lead-out) - not a claim of IEEE 315/ANSI Y32.2 certified symbol geometry.");
  Reg(e, "Capacitor", CapacitorFactory(), CommandStatus::Implemented,
      "Two parallel plate segments at a fixed gap - a simplified, commonly-recognized symbol, same non-certified-standard caveat as Resistor.");
  Reg(e, "Switch", SwitchFactory(), CommandStatus::Implemented,
      "A line plus an open-contact blade at a fixed distance ratio from the terminal separation - a simplified, commonly-recognized single-pole symbol, no claim of a certified standard shape.");
  Reg(e, "Ground", GroundFactory(), CommandStatus::Implemented,
      "A stepped-line earth-ground symbol (a lead plus three decreasing-width rungs) - a simplified, commonly-recognized shape, same non-certified-standard caveat as Resistor.");
  Reg(e, "Lamp", LampFactory(), CommandStatus::Implemented,
      "A circle with an inscribed X touching it at +-45 degrees - a simplified, commonly-recognized indicator-lamp symbol, same non-certified-standard caveat as Resistor.");
  Reg(e, "WireRun", Make<WireRunCommand>(), CommandStatus::Implemented,
      "A straight polyline between two placed points. If either point coincides with a real Point object or a curve's start/end, that endpoint is recorded as an anchor (FindPointAnchor, the same mechanism DimLinear/CenterLine use) - ElecRebuild re-resolves anchored endpoints to the anchor's *current* position. NOT a netlist/connectivity graph: two WireRuns meeting at a point are not automatically electrically joined, and nothing here checks continuity or shorts.");
  Reg(e, "ElecRebuild", Immediate([](CommandContext& ctx) {
        std::vector<ElecComponent> list = elec::LoadElec(ctx.Doc());
        int rebuilt = 0;
        ctx.Doc().BeginChange("ElecRebuild");
        for (ElecComponent c : list) {
          if (c.type != ElecType::WireRun || (!c.has_ref0 && !c.has_ref1)) continue;
          if (c.has_ref0) { Point3d np; if (ResolveAnchor(ctx.Doc(), c.ref0, c.end0, np)) c.p0 = np; }
          if (c.has_ref1) { Point3d np; if (ResolveAnchor(ctx.Doc(), c.ref1, c.end1, np)) c.p1 = np; }
          elec::RebuildElecComponent(ctx.Doc(), c);
          ++rebuilt;
        }
        ctx.Print("ElecRebuild: " + std::to_string(rebuilt) + " anchored WireRun(s) re-evaluated from their current endpoint position(s).");
      }), CommandStatus::Implemented,
      "Re-resolves every WireRun whose endpoint(s) are anchored to a real object (see WireRun's own note) to that object's *current* position and rebuilds the wire - the same explicit-recompute shape as UpdateBillOfMaterials/UpdateDimensions elsewhere in this app, not an automatic hook on every document edit.");
  Reg(e, "ElecTag", Make<ElecTagCommand>(), CommandStatus::Implemented,
      "Bakes a reference-designator string (e.g. R1, C3) as real font-outline curve geometry via the same TextToCurves/AddGlyphCurves path cmd_annotate.cpp's Text command uses - a static bake, not a live-linked callout tied to a particular symbol's id.");
}

}  // namespace dino8::app
