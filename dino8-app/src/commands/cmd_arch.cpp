// Commands for the parametric architectural components in arch/
// ArchComponents.h (Wall/Door/Window/Slab/Roof/Stair/Column/Beam - Rhino
// has none of these; see AUDIT.md's declined-feature rows). Each creation
// command is a short point/number picking sequence in the style of
// cmd_solids.cpp's BoxCommand; ArchEdit re-opens an existing component's
// parameters by picking one of its built objects in the viewport.
#include <algorithm>
#include <map>

#include "arch/ArchComponents.h"
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

}  // namespace

void RegisterArchCommands(CommandEngine& e) {
  Reg(e, "Wall", WallFactory());
  Reg(e, "Door", Make<OpeningCommand>(false));
  Reg(e, "Window", Make<OpeningCommand>(true));
  Reg(e, "ArchSlab", SlabFactory());
  Reg(e, "Roof", Make<RoofCommand>());
  Reg(e, "Stair", StairFactory());
  Reg(e, "Column", Make<ColumnCommand>());
  Reg(e, "Beam", BeamFactory());
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
        std::vector<arch::ArchComponent> list = arch::LoadArch(ctx.Doc());
        std::map<ArchType, int> counts;
        for (const arch::ArchComponent& c : list) counts[c.type]++;
        ctx.Print("Architectural schedule (" + std::to_string(list.size()) + " component(s)):");
        for (const std::string& name : arch::ArchTypeNames()) {
          ArchType t;
          arch::ParseArchType(name, t);
          if (counts.count(t)) ctx.Print("  " + name + ": " + std::to_string(counts[t]));
        }
      }), CommandStatus::Implemented, "Prints a count of every Wall/Door/Window/Slab/Roof/Stair/Column/Beam in the document.");
}

}  // namespace dino8::app
