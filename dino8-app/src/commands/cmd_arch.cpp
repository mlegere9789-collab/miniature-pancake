// Commands for the parametric architectural components in arch/
// ArchComponents.h (Wall/Door/Window/Slab/Roof/Stair/Column/Beam - Rhino
// has none of these; see AUDIT.md's declined-feature rows). Each creation
// command is a short point/number picking sequence in the style of
// cmd_solids.cpp's BoxCommand; ArchEdit re-opens an existing component's
// parameters by picking one of its built objects in the viewport.
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include "arch/ArchComponents.h"
#include "dino8/kernel/arch_calc.h"
#include "commands/annotate_common.h"
#include "commands/cmd_common.h"

namespace dino8::app {

using arch::ArchComponent;
using arch::ArchType;

namespace {

// Two base points (a line: Wall centreline, Beam/Stair run, footprint
// diagonal) then a numeric follow-up (height, thickness...) - shared shape
// across most of these commands, so one small state machine covers them
// all; each concrete command supplies what happens once the points and
// numbers are in hand via `on_done`.
class ArchLineCommand : public Command {
 public:
  struct NumberStep {
    std::string prompt;
    double default_value;
  };
  ArchLineCommand(std::string first_prompt, std::string second_prompt, std::vector<NumberStep> numbers,
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

// Wall: centreline p0-p1, then height, then thickness.
CommandFactory WallFactory() {
  return Make<ArchLineCommand>(
      "Wall start point", "Wall end point",
      std::vector<ArchLineCommand::NumberStep>{{"Wall height", 2.4}, {"Wall thickness", 0.2}},
      [](CommandContext& ctx, Point3d p0, Point3d p1, const std::vector<double>& v) {
        ArchComponent c;
        c.type = ArchType::Wall;
        c.p0 = p0; c.p1 = p1;
        c.height = v[0]; c.thickness = v[1];
        ctx.Doc().BeginChange("Wall");
        int id = arch::AddArchComponent(ctx.Doc(), c);
        ReportNew(ctx, "Wall", id);
      });
}

// Door/Window: pick the host wall, then an insertion point on it, then
// width (and, for Window, sill height + height).
class OpeningCommand : public Command {
 public:
  explicit OpeningCommand(bool is_window) : is_window_(is_window) {}
  void Begin(CommandContext&) override { WantObjects(std::string("Select the host wall for the ") + (is_window_ ? "Window" : "Door"), 1); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& objs) override {
    for (ObjectId id : objs) {
      ArchComponent w;
      if (arch::FindArchComponentByObject(ctx.Doc(), id, w) && w.type == ArchType::Wall) { host_ = w; break; }
    }
    if (host_.id < 0) { ctx.Warn("That object is not a Wall."); Finish(); return; }
    WantPoint("Insertion point on the wall");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    insert_ = p;
    ctx.SetLastPoint(p);
    WantNumber(is_window_ ? "Window width" : "Door width", is_window_ ? 1.2 : 0.9);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!have_width_) { width_ = v; have_width_ = true; WantNumber(is_window_ ? "Window height" : "Door height", is_window_ ? 1.2 : 2.1); return; }
    if (!have_height_) {
      height_ = v; have_height_ = true;
      if (is_window_) { WantNumber("Sill height (above the floor)", 0.9); return; }
      Build(ctx);
      return;
    }
    sill_ = v;
    Build(ctx);
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Number && default_number) OnNumber(ctx, *default_number); }
  void Build(CommandContext& ctx) {
    ArchComponent c;
    c.type = is_window_ ? ArchType::Window : ArchType::Door;
    c.host = static_cast<ObjectId>(host_.id);
    c.p0 = insert_;
    c.width = width_;
    c.height = height_;
    c.sill_height = is_window_ ? sill_ : 0.0;
    ctx.Doc().BeginChange(is_window_ ? "Window" : "Door");
    int id = arch::AddArchComponent(ctx.Doc(), c);
    ReportNew(ctx, is_window_ ? "Window" : "Door", id);
    Finish();
  }

 private:
  bool is_window_;
  ArchComponent host_;
  Point3d insert_{0, 0, 0};
  double width_ = 0, height_ = 0, sill_ = 0;
  bool have_width_ = false, have_height_ = false;
};

// Slab: footprint diagonal p0-p1, then thickness.
CommandFactory SlabFactory() {
  return Make<ArchLineCommand>(
      "ArchSlab: first corner", "ArchSlab: opposite corner",
      std::vector<ArchLineCommand::NumberStep>{{"ArchSlab thickness", 0.2}},
      [](CommandContext& ctx, Point3d p0, Point3d p1, const std::vector<double>& v) {
        ArchComponent c;
        c.type = ArchType::Slab;
        c.p0 = p0; c.p1 = p1;
        c.thickness = v[0];
        ctx.Doc().BeginChange("ArchSlab");
        int id = arch::AddArchComponent(ctx.Doc(), c);
        ReportNew(ctx, "ArchSlab", id);
      });
}

// Roof: footprint diagonal, then ridge height (a Type=Gable/Shed option
// picks roof_style before the points are collected).
class RoofCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Type", style_ == 0 ? "Gable" : "Shed", {"Gable", "Shed"}, false, false}};
    WantPoint("Roof footprint: first corner");
  }
  void OnOption(CommandContext&, const std::string& name, const std::string& value) override {
    if (name != "Type") return;
    style_ = value == "Shed" ? 1 : 0;
    options[0].value = value;
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) WantPoint("Roof footprint: opposite corner");
    else WantNumber("Ridge height above the eave", 1.0);
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.size() != 1) return;
    ctx.ClearPreview();
    std::vector<Point3d> box = {pts_[0], Point3d(h.x, pts_[0].y, pts_[0].z), h, Point3d(pts_[0].x, h.y, pts_[0].z)};
    ctx.AddPreviewPolyline(box, true);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    ctx.ClearPreview();
    ArchComponent c;
    c.type = ArchType::Roof;
    c.p0 = pts_[0]; c.p1 = pts_[1];
    c.ridge_height = v;
    c.roof_style = style_;
    ctx.Doc().BeginChange("Roof");
    int id = arch::AddArchComponent(ctx.Doc(), c);
    ReportNew(ctx, "Roof", id);
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Number && default_number) OnNumber(ctx, *default_number); }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  int style_ = 0;
  std::vector<Point3d> pts_;
};

// Stair: base point, run direction point, then step count / rise / run / width.
CommandFactory StairFactory() {
  return Make<ArchLineCommand>(
      "Stair: base of the run", "Stair: direction and length of the run",
      std::vector<ArchLineCommand::NumberStep>{{"Number of risers", 12}, {"Riser height", 0.18}, {"Tread depth", 0.28}, {"Stair width", 1.0}},
      [](CommandContext& ctx, Point3d p0, Point3d p1, const std::vector<double>& v) {
        ArchComponent c;
        c.type = ArchType::Stair;
        c.p0 = p0; c.p1 = p1;
        c.step_count = std::max(1, static_cast<int>(v[0]));
        c.rise = v[1]; c.run = v[2]; c.stair_width = v[3];
        ctx.Doc().BeginChange("Stair");
        int id = arch::AddArchComponent(ctx.Doc(), c);
        ReportNew(ctx, "Stair", id);
      });
}

// Column: base point, then height, width, depth.
class ColumnCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Column base point"); }
  void OnPoint(CommandContext& ctx, Point3d p) override { base_ = p; ctx.SetLastPoint(p); WantNumber("Column height", 3.0); }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!have_h_) { h_ = v; have_h_ = true; WantNumber("Column width", 0.3); return; }
    if (!have_w_) { w_ = v; have_w_ = true; WantNumber("Column depth", 0.3); return; }
    d_ = v;
    ArchComponent c;
    c.type = ArchType::Column;
    c.p0 = base_;
    c.height = h_; c.width = w_; c.thickness = d_;
    ctx.Doc().BeginChange("Column");
    int id = arch::AddArchComponent(ctx.Doc(), c);
    ReportNew(ctx, "Column", id);
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Number && default_number) OnNumber(ctx, *default_number); }

 private:
  Point3d base_{0, 0, 0};
  double h_ = 0, w_ = 0, d_ = 0;
  bool have_h_ = false, have_w_ = false;
};

// Beam: two endpoints, then width, depth.
CommandFactory BeamFactory() {
  return Make<ArchLineCommand>(
      "Beam start point", "Beam end point",
      std::vector<ArchLineCommand::NumberStep>{{"Beam width", 0.2}, {"Beam depth", 0.3}},
      [](CommandContext& ctx, Point3d p0, Point3d p1, const std::vector<double>& v) {
        ArchComponent c;
        c.type = ArchType::Beam;
        c.p0 = p0; c.p1 = p1;
        c.width = v[0]; c.thickness = v[1];
        ctx.Doc().BeginChange("Beam");
        int id = arch::AddArchComponent(ctx.Doc(), c);
        ReportNew(ctx, "Beam", id);
      });
}

// Bolt/Nut/Washer: pick a position point, an axis-direction point, then a
// size (option dropdown over MechSizeNames), then (Bolt only) a shank
// length.
class FastenerCommand : public Command {
 public:
  explicit FastenerCommand(ArchType type) : type_(type) {}
  void Begin(CommandContext&) override {
    size_names_ = arch::MechSizeNames(type_);
    size_index_ = 0;
    options = {{"Size", size_names_.empty() ? "" : size_names_[0], size_names_, false, false}};
    WantPoint(std::string(arch::ArchTypeName(type_)) + " position");
  }
  void OnOption(CommandContext&, const std::string& name, const std::string& value) override {
    if (name != "Size") return;
    auto it = std::find(size_names_.begin(), size_names_.end(), value);
    if (it != size_names_.end()) size_index_ = static_cast<int>(it - size_names_.begin());
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!have_p0_) { p0_ = p; have_p0_ = true; ctx.SetLastPoint(p); WantPoint("Axis direction (a second point above/along the fastener's axis)"); return; }
    p1_ = p;
    ctx.SetLastPoint(p);
    if (type_ == ArchType::Bolt) { WantNumber("Bolt length (shank)", 0.04); return; }
    Build(ctx);
  }
  void OnNumber(CommandContext& ctx, double v) override { length_ = v; Build(ctx); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Number && default_number) OnNumber(ctx, *default_number); }
  void Build(CommandContext& ctx) {
    ArchComponent c;
    c.type = type_;
    c.p0 = p0_; c.p1 = p1_;
    c.size_index = size_index_;
    c.height = length_;
    ctx.Doc().BeginChange(arch::ArchTypeName(type_));
    int id = arch::AddArchComponent(ctx.Doc(), c);
    ReportNew(ctx, arch::ArchTypeName(type_), id);
    Finish();
  }

 private:
  ArchType type_;
  std::vector<std::string> size_names_;
  int size_index_ = 0;
  Point3d p0_{0, 0, 0}, p1_{0, 0, 1};
  bool have_p0_ = false;
  double length_ = 0.04;
};

// IBeam/Channel/Angle: two endpoints (the run line, like Beam), then a size
// (option dropdown over MechSizeNames).
class StructShapeCommand : public Command {
 public:
  explicit StructShapeCommand(ArchType type) : type_(type) {}
  void Begin(CommandContext&) override {
    size_names_ = arch::MechSizeNames(type_);
    size_index_ = 0;
    options = {{"Size", size_names_.empty() ? "" : size_names_[0], size_names_, false, false}};
    WantPoint(std::string(arch::ArchTypeName(type_)) + " start point");
  }
  void OnOption(CommandContext&, const std::string& name, const std::string& value) override {
    if (name != "Size") return;
    auto it = std::find(size_names_.begin(), size_names_.end(), value);
    if (it != size_names_.end()) size_index_ = static_cast<int>(it - size_names_.begin());
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!have_p0_) { p0_ = p; have_p0_ = true; ctx.SetLastPoint(p); WantPoint(std::string(arch::ArchTypeName(type_)) + " end point"); return; }
    p1_ = p;
    ctx.SetLastPoint(p);
    ArchComponent c;
    c.type = type_;
    c.p0 = p0_; c.p1 = p1_;
    c.size_index = size_index_;
    ctx.Doc().BeginChange(arch::ArchTypeName(type_));
    int id = arch::AddArchComponent(ctx.Doc(), c);
    ReportNew(ctx, arch::ArchTypeName(type_), id);
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!have_p0_) return;
    ctx.ClearPreview();
    ctx.AddPreviewLine(p0_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  ArchType type_;
  std::vector<std::string> size_names_;
  int size_index_ = 0;
  Point3d p0_{0, 0, 0}, p1_{1, 0, 0};
  bool have_p0_ = false;
};

// Duct/Pipe/Conduit: two endpoints (the run centreline, like Beam), then
// cross-section (round: diameter; rectangular Duct only: width, height)
// and a flow-rate value SizeDuct/SizePipe can later re-derive a size from
// (stored but not itself used by Build() - see MepDiameterFromFlow()'s own
// doc comment on why sizing is a separate, explicit step).
class MepRunCommand : public Command {
 public:
  explicit MepRunCommand(ArchType type) : type_(type) {}
  void Begin(CommandContext&) override {
    if (type_ == ArchType::Duct) options = {{"Shape", "Round", {"Round", "Rectangular"}, false, false}};
    WantPoint(std::string(arch::ArchTypeName(type_)) + " start point");
  }
  void OnOption(CommandContext&, const std::string& name, const std::string& value) override {
    if (name == "Shape") round_ = value == "Round";
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!have_p0_) { p0_ = p; have_p0_ = true; ctx.SetLastPoint(p); WantPoint(std::string(arch::ArchTypeName(type_)) + " end point"); return; }
    p1_ = p;
    ctx.SetLastPoint(p);
    if (type_ != ArchType::Duct || round_) { WantNumber("Diameter", type_ == ArchType::Duct ? 0.25 : (type_ == ArchType::Pipe ? 0.05 : 0.02)); return; }
    WantNumber("Duct width", 0.4);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (type_ == ArchType::Duct && !round_) {
      if (!have_width_) { width_ = v; have_width_ = true; WantNumber("Duct height", 0.25); return; }
      height_ = v;
    } else {
      diameter_ = v;
    }
    Build(ctx);
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Number && default_number) OnNumber(ctx, *default_number); }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!have_p0_) return;
    ctx.ClearPreview();
    ctx.AddPreviewLine(p0_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  void Build(CommandContext& ctx) {
    ArchComponent c;
    c.type = type_;
    c.p0 = p0_; c.p1 = p1_;
    c.duct_round = round_ ? 1 : 0;
    c.diameter = diameter_;
    c.width = width_; c.thickness = height_;
    ctx.Doc().BeginChange(arch::ArchTypeName(type_));
    int id = arch::AddArchComponent(ctx.Doc(), c);
    ReportNew(ctx, arch::ArchTypeName(type_), id);
    Finish();
  }

 private:
  ArchType type_;
  Point3d p0_{0, 0, 0}, p1_{1, 0, 0};
  bool have_p0_ = false;
  bool round_ = true;
  double diameter_ = 0.05, width_ = 0.4, height_ = 0.25;
  bool have_width_ = false;
};

// SizeDuct/SizePipe: re-derives a run's cross-section from a flow input via
// MepDiameterFromFlow()'s own documented rule-of-thumb heuristic (NOT a
// code-compliance calculation - see that function's doc comment) and
// rebuilds it.
class MepSizeCommand : public Command {
 public:
  explicit MepSizeCommand(ArchType type) : type_(type) {}
  void Begin(CommandContext&) override {
    WantObjects(std::string("Select the ") + arch::ArchTypeName(type_) + " to size", 1);
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& objs) override {
    for (ObjectId id : objs) {
      arch::ArchComponent c;
      if (arch::FindArchComponentByObject(ctx.Doc(), id, c) && c.type == type_) { comp_ = c; found_ = true; break; }
    }
    if (!found_) { ctx.Warn(std::string("That object is not a ") + arch::ArchTypeName(type_) + "."); Finish(); return; }
    WantNumber(type_ == ArchType::Duct ? "Airflow (CFM)" : "Flow rate (GPM)", comp_.flow_rate);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    comp_.flow_rate = v;
    // 1 CFM = 0.00047194745 m^3/s; 1 GPM = 0.0000630902 m^3/s. Assumed
    // velocities are round, commonly-cited rule-of-thumb figures for
    // low-pressure ductwork (~6 m/s, ~1200 ft/min) and domestic-scale
    // piping (~1.5 m/s) - NOT looked up per-application/code, see this
    // command's own doc comment and MepDiameterFromFlow()'s.
    double flow_m3_s = type_ == ArchType::Duct ? v * 0.00047194745 : v * 0.0000630902;
    double velocity = type_ == ArchType::Duct ? 6.0 : 1.5;
    double d = arch::MepDiameterFromFlow(flow_m3_s, velocity);
    comp_.diameter = d;
    if (type_ == ArchType::Duct) comp_.duct_round = 1;
    ctx.Doc().BeginChange(std::string("Size") + arch::ArchTypeName(type_));
    arch::RebuildArchComponent(ctx.Doc(), comp_);
    std::ostringstream msg;
    msg << arch::ArchTypeName(type_) << " #" << comp_.id << " sized to diameter " << d
        << " m from a heuristic velocity assumption (rule-of-thumb only, not ASHRAE/NEC code-compliant).";
    ctx.Print(msg.str());
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Number && default_number) OnNumber(ctx, *default_number); }

 private:
  ArchType type_;
  ArchComponent comp_;
  bool found_ = false;
};

// ArchEdit: pick one of a component's built objects, then edit a chosen
// numeric field (height/thickness/width/...) and rebuild.
class ArchEditCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select an architectural component to edit", 1); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& objs) override {
    for (ObjectId id : objs) {
      if (arch::FindArchComponentByObject(ctx.Doc(), id, comp_)) { found_ = true; break; }
    }
    if (!found_) { ctx.Warn("That object is not a parametric architectural component."); Finish(); return; }
    ctx.Print(std::string(arch::ArchTypeName(comp_.type)) + " #" + std::to_string(comp_.id) + " selected.");
    UpdateOptions();
    WantNumber(FieldPrompt(), CurrentValue());
  }
  void OnOption(CommandContext& ctx, const std::string& name, const std::string& value) override {
    if (name != "Field") return;
    field_ = value;
    (void)ctx;
    WantNumber(FieldPrompt(), CurrentValue());
  }
  void OnNumber(CommandContext& ctx, double v) override {
    SetField(v);
    ctx.Doc().BeginChange("ArchEdit");
    arch::RebuildArchComponent(ctx.Doc(), comp_);
    ctx.Print(std::string(arch::ArchTypeName(comp_.type)) + " #" + std::to_string(comp_.id) + " rebuilt (" + field_ + " = " + std::to_string(v) + ").");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Number && default_number) OnNumber(ctx, *default_number); }

 private:
  void UpdateOptions() {
    std::vector<std::string> fields = {"Height", "Thickness", "Width"};
    if (comp_.type == ArchType::Roof) fields = {"RidgeHeight"};
    if (comp_.type == ArchType::Stair) fields = {"Rise", "Run", "StairWidth", "StepCount"};
    if (field_.empty() || std::find(fields.begin(), fields.end(), field_) == fields.end()) field_ = fields[0];
    options = {{"Field", field_, fields, false, false}};
  }
  std::string FieldPrompt() { return "New value for " + field_; }
  double CurrentValue() {
    if (field_ == "Height") return comp_.height;
    if (field_ == "Thickness") return comp_.thickness;
    if (field_ == "Width") return comp_.width;
    if (field_ == "RidgeHeight") return comp_.ridge_height;
    if (field_ == "Rise") return comp_.rise;
    if (field_ == "Run") return comp_.run;
    if (field_ == "StairWidth") return comp_.stair_width;
    if (field_ == "StepCount") return comp_.step_count;
    return 0;
  }
  void SetField(double v) {
    if (field_ == "Height") comp_.height = v;
    else if (field_ == "Thickness") comp_.thickness = v;
    else if (field_ == "Width") comp_.width = v;
    else if (field_ == "RidgeHeight") comp_.ridge_height = v;
    else if (field_ == "Rise") comp_.rise = v;
    else if (field_ == "Run") comp_.run = v;
    else if (field_ == "StairWidth") comp_.stair_width = v;
    else if (field_ == "StepCount") comp_.step_count = std::max(1, static_cast<int>(v));
  }

  ArchComponent comp_;
  bool found_ = false;
  std::string field_;
};

// Whether `t` is one of the new size-table/line-run mechanical or MEP
// types ArchSchedule reports as a real BOM row (label + qty) rather than
// just a per-type count, the same way the original eight ArchTypes are.
bool IsMechOrMep(ArchType t) {
  switch (t) {
    case ArchType::Bolt: case ArchType::Nut: case ArchType::Washer:
    case ArchType::IBeam: case ArchType::Channel: case ArchType::Angle:
    case ArchType::Duct: case ArchType::Pipe: case ArchType::Conduit:
      return true;
    default: return false;
  }
}

// One human-readable, roundable-quantity label per mechanical/MEP
// component, e.g. "M8 x 40mm Bolt", "IB200 x 6m IBeam", "round d=250mm x
// 4m Duct" - two components with the identical label are the identical
// line item for BOM-grouping purposes (same size/dimensions), matching the
// spec's own "3x M8x40 bolt" / "2x 6m I-beam W150" examples.
std::string MechLabel(const arch::ArchComponent& c) {
  double len = (c.p1 - c.p0).Length();
  auto mm = [](double meters) { return std::round(meters * 1000.0); };
  auto m2 = [](double meters) { return std::round(meters * 100.0) / 100.0; };
  std::ostringstream s;
  switch (c.type) {
    case ArchType::Bolt: s << arch::MechSizeAt(ArchType::Bolt, c.size_index).name << " x " << mm(c.height) << "mm Bolt"; break;
    case ArchType::Nut: s << arch::MechSizeAt(ArchType::Nut, c.size_index).name << " Nut"; break;
    case ArchType::Washer: s << arch::MechSizeAt(ArchType::Washer, c.size_index).name << " Washer"; break;
    case ArchType::IBeam: s << arch::MechSizeAt(ArchType::IBeam, c.size_index).name << " x " << m2(len) << "m IBeam"; break;
    case ArchType::Channel: s << arch::MechSizeAt(ArchType::Channel, c.size_index).name << " x " << m2(len) << "m Channel"; break;
    case ArchType::Angle: s << arch::MechSizeAt(ArchType::Angle, c.size_index).name << " x " << m2(len) << "m Angle"; break;
    case ArchType::Duct:
      if (c.duct_round) s << "round d=" << mm(c.diameter) << "mm x " << m2(len) << "m Duct";
      else s << mm(c.width) << "x" << mm(c.thickness) << "mm x " << m2(len) << "m Duct (rectangular)";
      break;
    case ArchType::Pipe: s << "d=" << mm(c.diameter) << "mm x " << m2(len) << "m Pipe"; break;
    case ArchType::Conduit: s << "d=" << mm(c.diameter) << "mm x " << m2(len) << "m Conduit"; break;
    default: s << arch::ArchTypeName(c.type); break;
  }
  return s.str();
}

}  // namespace

// ---------------------------------------------------------------------------
// Design-rule and quantity commands over dino8/kernel/arch_calc.h. Each is a one-line, script-friendly "Name=Value" command that
// prints its result; lists use '/' between items and ':' between the fields of an item (a polygon is "x,y/x,y/..."), so a whole
// call fits one command line.
namespace calc {

using dino8::kernel::ArchPt2;

double Num(const std::map<std::string, std::string>& o, const char* key, double def) {
  const std::string v = OptionOr(o, key);
  if (v.empty()) return def;
  char* end = nullptr;
  const double d = std::strtod(v.c_str(), &end);
  return (end && end != v.c_str() && *end == 0) ? d : def;
}

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : s) {
    if (ch == sep) { out.push_back(cur); cur.clear(); }
    else cur += ch;
  }
  if (!cur.empty() || !out.empty()) out.push_back(cur);
  return out;
}

std::vector<double> Fields(const std::string& item) {
  std::vector<double> v;
  for (const std::string& f : Split(item, ':')) v.push_back(std::atof(f.c_str()));
  return v;
}

std::string F4(double v) {
  char b[64];
  std::snprintf(b, sizeof b, "%.4f", v);
  return b;
}

kernel::StairRules Rules(const std::map<std::string, std::string>& o) {
  kernel::StairRules r;
  r.max_riser = Num(o, "maxriser", r.max_riser);
  r.min_riser = Num(o, "minriser", r.min_riser);
  r.min_tread = Num(o, "mintread", r.min_tread);
  r.min_width = Num(o, "minwidth", r.min_width);
  r.blondel_min = Num(o, "blondelmin", r.blondel_min);
  r.blondel_max = Num(o, "blondelmax", r.blondel_max);
  return r;
}

std::string OptionLine(const kernel::StairOption& o) {
  return std::to_string(o.risers) + " risers x " + F4(o.riser) + ", tread " + F4(o.tread) + ", run " + F4(o.run);
}

void StairCheckCmd(CommandContext& ctx) {
  const auto o = TakeOptionTokens(ctx);
  const kernel::StairRules rules = Rules(o);
  const double riser = Num(o, "riser", 0.18), tread = Num(o, "tread", 0.28), width = Num(o, "width", rules.min_width);
  const kernel::StairCheck c = kernel::CheckStair(riser, tread, width, rules);
  ctx.Print("StairCheck: riser " + F4(riser) + " tread " + F4(tread) + " width " + F4(width) + " 2R+T=" + F4(c.blondel) + " -> " + (c.ok ? "PASS" : "FAIL"));
  for (const std::string& v : c.violations) ctx.Print("  violation: " + v);
}

void StairFitCmd(CommandContext& ctx) {
  const auto o = TakeOptionTokens(ctx);
  const kernel::StairRules rules = Rules(o);
  const double rise = Num(o, "rise", 0), tread = Num(o, "tread", 0);
  const int risers = static_cast<int>(Num(o, "risers", 0));
  if (risers > 0 && tread > 0) {  // a story-linked stair: keep the riser count, follow the new story height
    const kernel::StairRelink r = kernel::RelinkStairToStory(risers, tread, rise, rules);
    ctx.Print("StairFit: " + std::to_string(risers) + " risers over rise " + F4(rise) + " -> riser " + F4(r.riser) + " 2R+T=" + F4(r.blondel) + " " + (r.valid ? "VALID" : "INVALID"));
    for (const std::string& why : r.reasons) ctx.Print("  reason: " + why);
    if (!r.fixes.empty()) ctx.Print("  fix: " + OptionLine(r.fixes.front()));
    return;
  }
  const std::vector<kernel::StairOption> opts = kernel::ProposeStairOptions(rise, rules, tread, 0);
  ctx.Print("StairFit: rise " + F4(rise) + " -> " + std::to_string(opts.size()) + " option(s)");
  if (!opts.empty()) ctx.Print("  first: " + OptionLine(opts.front()));
}

void RampPlanCmd(CommandContext& ctx) {
  const auto o = TakeOptionTokens(ctx);
  const kernel::RampPlan p = kernel::PlanRamp(Num(o, "rise", 0), Num(o, "maxslope", 1.0 / 12.0), Num(o, "maxrun", 9.0), Num(o, "landing", 1.5));
  if (!p.ok) { ctx.Warn("RampPlan: need a positive Rise, MaxSlope, MaxRun and a non-negative Landing"); return; }
  ctx.Print("RampPlan: " + std::to_string(p.flights) + " flight(s), " + std::to_string(p.landings) + " landing(s), run per flight " + F4(p.run_per_flight) +
            ", total run " + F4(p.total_run) + ", footprint " + F4(p.total_length) + ", slope " + F4(p.slope));
}

void RoofPitchCmd(CommandContext& ctx) {
  const auto o = TakeOptionTokens(ctx);
  kernel::RoofPitch p;
  if (o.count("degrees")) p = kernel::PitchFromDegrees(Num(o, "degrees", 0));
  else if (o.count("percent")) p = kernel::PitchFromPercent(Num(o, "percent", 0));
  else if (o.count("rise") && o.count("run")) p = kernel::PitchFromRiseRun(Num(o, "rise", 0), Num(o, "run", 0));
  else if (o.count("elevation") && o.count("distance")) p = kernel::PitchFromElevation(Num(o, "eave", 0), Num(o, "elevation", 0), Num(o, "distance", 0));
  if (!p.valid) { ctx.Warn("RoofPitchConv: give Degrees=, Percent=, Rise= and Run=, or Elevation= and Distance= (Eave=); a vertical plane or a zero run is not a pitch"); return; }
  ctx.Print("RoofPitchConv: " + F4(p.degrees) + " degrees = " + F4(p.percent) + " percent = " + F4(p.rise_per_run) + " rise per run");
}

void ZoneAreaCmd(CommandContext& ctx) {
  const auto o = TakeOptionTokens(ctx);
  std::vector<ArchPt2> poly;
  for (const std::string& v : Split(OptionOr(o, "poly"), '/')) {
    const std::vector<std::string> xy = Split(v, ',');
    if (xy.size() >= 2) poly.push_back({std::atof(xy[0].c_str()), std::atof(xy[1].c_str())});
  }
  if (poly.size() < 3) { ctx.Warn("ZoneArea: Poly=x,y/x,y/x,y/... needs at least three vertices"); return; }
  std::vector<kernel::ZoneElement> els;
  for (const std::string& item : Split(OptionOr(o, "elem"), '/')) {
    const std::vector<std::string> f = Split(item, ':');
    if (f.size() < 2) continue;
    const std::string e = ToLower(f[1]);
    els.push_back({std::atof(f[0].c_str()), e == "none" ? kernel::ZoneEffect::None : e == "reduce" ? kernel::ZoneEffect::ReduceArea : kernel::ZoneEffect::Subtract});
  }
  const kernel::ZoneQuantities q = kernel::ComputeZone(poly, Num(o, "height", 0), els, Num(o, "minelem", 0));
  ctx.Print("ZoneArea: gross area " + F4(q.gross_area) + ", net area " + F4(q.net_area) + ", volume " + F4(q.volume) + " (" + std::to_string(q.elements_counted) + " element(s) counted)");
  if (o.count("limit1") && o.count("ceiling")) {
    const std::vector<double> c = Fields(OptionOr(o, "ceiling")), l1 = Fields(OptionOr(o, "limit1")), l2 = Fields(OptionOr(o, "limit2"));
    if (c.size() >= 3 && l1.size() >= 2) {
      kernel::LowCeilingRule rule;
      rule.limit1 = l1[0]; rule.pct1 = l1[1];
      if (l2.size() >= 2) { rule.two_limits = true; rule.limit2 = l2[0]; rule.pct2 = l2[1]; }
      const kernel::LowCeilingResult r = kernel::ZoneAreaUnderLowCeilings(poly, c[0], c[1], c[2], rule);
      ctx.Print("ZoneArea: under limit 1 " + F4(r.area_below1) + ", under limit 2 " + F4(r.area_below2) + ", counted area " + F4(r.counted_area));
    }
  }
  if (o.count("recess")) {
    std::vector<kernel::WallRecess> rec;
    for (const std::string& item : Split(OptionOr(o, "recess"), '/')) {
      const std::vector<double> f = Fields(item);
      if (f.size() >= 2) rec.push_back({f[0], f[1]});
    }
    const bool any = ToLower(OptionOr(o, "rule", "and")) == "or";
    ctx.Print("ZoneArea: recess area added " + F4(kernel::RecessAddedArea(rec, Num(o, "mindepth", 0), Num(o, "minrecessarea", 0), any ? kernel::RecessRule::DepthOrArea : kernel::RecessRule::DepthAndArea)));
  }
}

void WallQuantitiesCmd(CommandContext& ctx) {
  const auto o = TakeOptionTokens(ctx);
  std::vector<kernel::WallOpening> ops;
  for (const std::string& item : Split(OptionOr(o, "open"), '/')) {
    const std::vector<double> f = Fields(item);  // x0:width:sill:height
    if (f.size() >= 4) ops.push_back({f[0], f[1], f[2], f[3]});
  }
  const double h1 = Num(o, "h1", 2.8);
  const kernel::WallQuantitiesResult q = kernel::ComputeWallQuantities(Num(o, "length", 0), h1, Num(o, "h2", h1), Num(o, "thickness", 0.2), ops);
  ctx.Print("WallQuantities: gross face " + F4(q.gross_side_area) + ", net face " + F4(q.net_side_area) + ", gross volume " + F4(q.gross_volume) + ", net volume " + F4(q.net_volume) +
            ", " + std::to_string(q.openings_counted) + " opening(s)" + (q.ok ? "" : " (OVERLAPPING openings: quantities are not reliable)"));
}

}  // namespace calc

void RegisterArchCommands(CommandEngine& e) {
  Reg(e, "Wall", WallFactory());
  Reg(e, "Door", Make<OpeningCommand>(false));
  Reg(e, "Window", Make<OpeningCommand>(true));
  Reg(e, "ArchSlab", SlabFactory());
  Reg(e, "Roof", Make<RoofCommand>());
  Reg(e, "Stair", StairFactory());
  Reg(e, "Column", Make<ColumnCommand>());
  Reg(e, "Beam", BeamFactory());
  Reg(e, "Bolt", Make<FastenerCommand>(ArchType::Bolt), CommandStatus::Implemented,
      "Generated from a small starter table of ISO-metric-style sizes (M6/M8/M10/M12) - not a real fastener-standard database. The hex head is approximated as a circle circumscribing its across-flats width, not a true hex prism. The shank now cuts a real ISO 724 coarse-pitch helical thread (CoarseThreadPitch(), ArchComponents.cpp) at an approximate thread depth.");
  Reg(e, "Nut", Make<FastenerCommand>(ArchType::Nut), CommandStatus::Implemented,
      "Same starter size table and round-head approximation as Bolt; now cuts a real matching threaded bore (same ISO 724 pitch/radii as Bolt, so the two mate) instead of the old plain, unbored solid.");
  Reg(e, "Washer", Make<FastenerCommand>(ArchType::Washer), CommandStatus::Implemented,
      "Same starter size table as Bolt/Nut; a real hollow ring (outer cylinder minus bore), unlike Bolt/Nut's solid approximation.");
  Reg(e, "IBeam", Make<StructShapeCommand>(ArchType::IBeam), CommandStatus::Implemented,
      "Generated from a small starter table of plausible section dimensions, not a specific AISC/Eurocode designation - a union of three boxes (two flanges, one web), not a rolled-shape fillet profile.");
  Reg(e, "Channel", Make<StructShapeCommand>(ArchType::Channel), CommandStatus::Implemented,
      "Same starter-table caveat as IBeam; a C-shaped union of three boxes.");
  // "AngleIron", not "Angle": cmd_analyze.cpp already registers "Angle" as
  // the three-point angle-measurement command (analyze_script.txt) - same
  // reason ArchSlab isn't just "Slab" (see SlabFactory's own registration
  // above, "Slab" already being a mesh-tools command).
  Reg(e, "AngleIron", Make<StructShapeCommand>(ArchType::Angle), CommandStatus::Implemented,
      "Same starter-table caveat as IBeam; an L-shaped union of two boxes.");
  Reg(e, "Duct", Make<MepRunCommand>(ArchType::Duct), CommandStatus::Implemented,
      "A straight run extruded along its centreline like Beam, built SOLID (not a hollow duct wall) - round or rectangular. See SizeDuct for the flow-based sizing heuristic.");
  // "MepPipe", not "Pipe": cmd_surface.cpp already registers "Pipe" as the
  // tube-around-a-curve surface command (surface_script.txt) - same
  // ArchSlab-vs-Slab naming reason as AngleIron above.
  Reg(e, "MepPipe", Make<MepRunCommand>(ArchType::Pipe), CommandStatus::Implemented,
      "A straight round run extruded along its centreline like Beam, built SOLID (not a hollow pipe wall/bore). See SizePipe for the flow-based sizing heuristic.");
  Reg(e, "Conduit", Make<MepRunCommand>(ArchType::Conduit), CommandStatus::Implemented,
      "A straight round run extruded along its centreline like Beam, built SOLID (not a hollow conduit wall/raceway).");
  Reg(e, "SizeDuct", Make<MepSizeCommand>(ArchType::Duct), CommandStatus::Implemented,
      "Re-derives a Duct's diameter from an airflow (CFM) input via a fixed ~6 m/s velocity assumption (area = flow/velocity, diameter from area) - a plausible starting-point heuristic, explicitly NOT an ASHRAE code-compliance calculation (real duct sizing depends on friction loss, fittings, and noise criteria this project has no license or basis to reproduce).");
  Reg(e, "SizePipe", Make<MepSizeCommand>(ArchType::Pipe), CommandStatus::Implemented,
      "Re-derives a Pipe's diameter from a flow (GPM) input via a fixed ~1.5 m/s velocity assumption, the same heuristic-only caveat as SizeDuct (explicitly NOT an NEC/plumbing-code-compliant calculation).");
  Reg(e, "StairCheck", Immediate(calc::StairCheckCmd), CommandStatus::Implemented,
      "Riser=, Tread=, Width= against MaxRiser=/MinRiser=/MinTread=/MinWidth= and the Blondel range 2R+T (BlondelMin=/BlondelMax=); prints PASS or FAIL with one line per broken rule. The limits are inputs, not a building code.");
  Reg(e, "StairFit", Immediate(calc::StairFitCmd), CommandStatus::Implemented,
      "Rise= [Tread=]: counts the riser counts that satisfy the stair rules and prints the one with the fewest risers. With Risers= and Tread= it relinks an existing stair to a new story height (Rise=): the riser follows, and an invalid result is flagged with the nearest valid fix instead of being changed silently.");
  Reg(e, "RampPlan", Immediate(calc::RampPlanCmd), CommandStatus::Implemented,
      "Rise=, MaxSlope= (rise per run, default 1/12), MaxRun= (longest flight, default 9), Landing= (default 1.5): the fewest equal flights and landings that climb Rise at no more than MaxSlope.");
  Reg(e, "RoofPitchConv", Immediate(calc::RoofPitchCmd), CommandStatus::Implemented,
      "Converts a roof pitch between degrees, percent and rise per run; accepts Degrees=, Percent=, Rise= with Run=, or Elevation= with Distance= and Eave=. Negative pitches (down-sloping planes) are allowed.");
  Reg(e, "ZoneArea", Immediate(calc::ZoneAreaCmd), CommandStatus::Implemented,
      "Poly=x,y/x,y/... Height= [Elem=area:subtract|reduce|none/...] [MinElem=]: shoelace area, net area and volume of a zone. Ceiling=h0:gx:gy with Limit1=height:fraction [Limit2=height:fraction] reduces area under low ceilings; Recess=depth:area/... with MinDepth=, MinRecessArea= and Rule=and|or adds wall recesses.");
  Reg(e, "WallQuantities", Immediate(calc::WallQuantitiesCmd), CommandStatus::Implemented,
      "Length= H1= [H2= for a sloping top] Thickness= Open=x0:width:sill:height/...: gross and net face area and volume of a wall with rectangular openings (clipped to the wall; overlapping openings are flagged).");
  Reg(e, "ArchEdit", Make<ArchEditCommand>());
  Reg(e, "ArchDelete", OnSelection("Select an architectural component to delete", [](CommandContext& ctx, const std::vector<ObjectId>& objs) {
        int removed = 0;
        for (ObjectId id : objs) {
          arch::ArchComponent c;
          if (arch::FindArchComponentByObject(ctx.Doc(), id, c)) {
            ctx.Doc().BeginChange("ArchDelete");
            if (arch::DeleteArchComponent(ctx.Doc(), c.id)) ++removed;
          }
        }
        ctx.Print("ArchDelete: removed " + std::to_string(removed) + " component(s) (and any openings hosted on a removed Wall).");
      }));
  Reg(e, "ArchSchedule", Immediate([](CommandContext& ctx) {
        auto opts = TakeOptionTokens(ctx);
        std::string csv_path = OptionOr(opts, "csv", "");
        std::vector<arch::ArchComponent> list = arch::LoadArch(ctx.Doc());
        std::map<ArchType, int> counts;
        for (const arch::ArchComponent& c : list) counts[c.type]++;
        ctx.Print("Architectural schedule (" + std::to_string(list.size()) + " component(s)):");
        for (const std::string& name : arch::ArchTypeNames()) {
          ArchType t;
          arch::ParseArchType(name, t);
          if (IsMechOrMep(t) || !counts.count(t)) continue;
          ctx.Print("  " + name + ": " + std::to_string(counts[t]));
        }
        // Mechanical/MEP: a real bill-of-materials, not just a count -
        // group by MechLabel() (size + dimensions) so "3x M8x40 Bolt" and
        // "2x 6m IBeam IB200" style quantities come out as separate rows,
        // matching the count-only style's own per-type breakdown above but
        // with the parameters that make a BOM row actually useful.
        std::vector<std::pair<std::string, int>> bom;  // preserves first-seen order
        for (const arch::ArchComponent& c : list) {
          if (!IsMechOrMep(c.type)) continue;
          std::string label = MechLabel(c);
          auto it = std::find_if(bom.begin(), bom.end(), [&](const auto& p) { return p.first == label; });
          if (it == bom.end()) bom.push_back({label, 1});
          else ++it->second;
        }
        if (!bom.empty()) {
          ctx.Print("Mechanical / MEP bill of materials:");
          for (const auto& [label, qty] : bom) ctx.Print("  " + std::to_string(qty) + "x " + label);
        }
        if (!csv_path.empty()) {
          std::ofstream f(csv_path);
          f << "Item,Qty\n";
          for (const std::string& name : arch::ArchTypeNames()) {
            ArchType t;
            arch::ParseArchType(name, t);
            if (IsMechOrMep(t) || !counts.count(t)) continue;
            f << name << "," << counts[t] << "\n";
          }
          for (const auto& [label, qty] : bom) f << "\"" << label << "\"," << qty << "\n";
          ctx.Print("ArchSchedule: CSV written to " + csv_path);
        }
      }), CommandStatus::Implemented,
      "Wall/Door/Window/Slab/Roof/Stair/Column/Beam are reported as a per-type count, exactly as before. Bolt/Nut/Washer/IBeam/Channel/Angle/Duct/Pipe/Conduit are reported as a real bill of materials, grouped by size/dimensions (e.g. '3x M8 x 40mm Bolt') - the same 'baked snapshot, not live-linked' honesty as BillOfMaterials elsewhere in this app, not an associative table. Csv=path writes the same rows as a CSV file, matching BillOfMaterials's own CSV idiom.");
}

}  // namespace dino8::app
