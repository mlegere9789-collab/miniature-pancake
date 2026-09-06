// Session commands: 3D digitizer (Dig*), Worksession/LimitReferenceModel
// (attaching other .3dm files as locked reference models), and Snapshots
// (named, full-document restore points independent of Undo/Redo).
#include "commands/cmd_common.h"

#include <algorithm>
#include <cctype>

#include "session/Digitizer.h"
#include "session/Worksession.h"

namespace dino8::app {

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

CommandFactory Say(const std::string& text, bool warn = false) {
  return Immediate([text, warn](CommandContext& ctx) { if (warn) ctx.Warn(text); else ctx.Print(text); });
}

// ---------------------------------------------------------------------------
// Digitizer (DigConnect/DigDisconnect/DigListPorts/DigCalibrate/DigPoint/
// DigScale/DigPause/DigResume). Protocol=Ascii opens a real serial port;
// Protocol=File and Protocol=Simulated are headless stand-ins that make the
// feature testable without hardware (see session/Digitizer.h).
// ---------------------------------------------------------------------------

std::string DescribeDigitizer() {
  Digitizer& dig = Digitizer::Instance();
  if (!dig.Connected()) return "Digitizer: not connected. Use DigConnect.";
  std::string s = std::string("Digitizer: connected, protocol ") + DigitizerProtocolName(dig.Protocol());
  if (dig.Protocol() == DigitizerProtocol::Ascii) s += ", port " + dig.Port() + " @ " + std::to_string(dig.Baud()) + " baud";
  else if (dig.Protocol() == DigitizerProtocol::File) s += ", file " + dig.Port();
  if (dig.Paused()) s += ", paused";
  s += dig.Calibration().set ? ", calibrated" : ", not calibrated";
  return s;
}

// DigConnect Ascii|File|Simulated [port-or-path] [baud]
class DigConnectCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Protocol (Ascii/File/Simulated)", "Simulated");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (!protocol_) {
      DigitizerProtocol p;
      if (!ParseDigitizerProtocol(t, p)) { ctx.Warn("DigConnect: unknown protocol '" + t + "'"); Finish(); return; }
      protocol_ = p;
      if (*protocol_ == DigitizerProtocol::Simulated) { Connect(ctx); return; }
      if (auto next = ctx.Engine().TakePendingInput()) { OnText(ctx, *next); return; }
      WantText(*protocol_ == DigitizerProtocol::Ascii ? "Serial port (e.g. /dev/ttyUSB0 or COM3)" : "File to read points from");
      return;
    }
    port_ = t;
    if (*protocol_ != DigitizerProtocol::Ascii) { Connect(ctx); return; }
    if (auto next = ctx.Engine().TakePendingInput()) { OnNumber(ctx, std::strtod(next->c_str(), nullptr)); return; }
    WantText("Baud rate", "9600");
  }
  void OnNumber(CommandContext& ctx, double v) override {
    baud_ = static_cast<int>(v);
    Connect(ctx);
  }
  void OnEnter(CommandContext& ctx) override { if (protocol_ && *protocol_ == DigitizerProtocol::Ascii && !port_.empty()) Connect(ctx); else Finish(); }

 private:
  void Connect(CommandContext& ctx) {
    Digitizer& dig = Digitizer::Instance();
    std::string error;
    bool ok = false;
    switch (*protocol_) {
      case DigitizerProtocol::Simulated: dig.ConnectSimulated(); ok = true; break;
      case DigitizerProtocol::File: ok = dig.ConnectFile(port_, error); break;
      case DigitizerProtocol::Ascii: ok = dig.ConnectSerial(port_, baud_ > 0 ? baud_ : 9600, error); break;
    }
    if (!ok) ctx.Warn("DigConnect: " + error);
    else ctx.Print(DescribeDigitizer());
    Finish();
  }
  std::optional<DigitizerProtocol> protocol_;
  std::string port_;
  int baud_ = 0;
};

// DigCalibrate: reads 3 points from the connected digitizer (origin, a
// point on +X, a point on +Y) and solves the rigid transform into the
// active CPlane, exactly as Rhino's DigCalibrate does for a 3-point setup.
class DigCalibrateCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    Digitizer& dig = Digitizer::Instance();
    if (!dig.Connected()) { ctx.Warn("DigCalibrate: the digitizer is not connected. Use DigConnect first."); Finish(); return; }
    ReadNext(ctx, "Digitize the origin point");
  }

 private:
  void ReadNext(CommandContext& ctx, const std::string& prompt) {
    ctx.Print(prompt + " (Protocol=Simulated: click in the viewport; Protocol=File/Ascii: reads the next point automatically)");
    Digitizer& dig = Digitizer::Instance();
    if (dig.Protocol() == DigitizerProtocol::Simulated) { WantPoint(prompt); return; }
    DigitizerPoint p;
    if (!dig.ReadPoint(p)) { ctx.Warn("DigCalibrate: no point available from the digitizer"); Finish(); return; }
    Got(ctx, p.raw);
  }
  void OnPoint(CommandContext& ctx, kernel::Point3d p) override {
    Digitizer::Instance().FeedSimulatedPoint(p);
    DigitizerPoint dp;
    Digitizer::Instance().ReadPoint(dp);
    Got(ctx, dp.raw);
  }
  void Got(CommandContext& ctx, kernel::Point3d raw) {
    if (!have_origin_) { origin_ = raw; have_origin_ = true; ReadNext(ctx, "Digitize a point on the +X axis"); return; }
    if (!have_x_) { x_point_ = raw; have_x_ = true; ReadNext(ctx, "Digitize a point on the +Y axis (or the +Y side of the plane)"); return; }
    const ON_Plane cplane = ActivePlane(ctx);
    DigitizerCalibration cal;
    cal.set = true;
    cal.origin = origin_;
    cal.x_axis = x_point_ - origin_;
    cal.y_axis = raw - origin_;
    cal.target_origin = cplane.origin;
    cal.target_x = cplane.xaxis;
    cal.target_y = cplane.yaxis;
    Digitizer::Instance().Calibration() = cal;
    ctx.Print("DigCalibrate: calibrated to the active CPlane");
    Finish();
  }
  kernel::Point3d origin_{0, 0, 0}, x_point_{0, 0, 0};
  bool have_origin_ = false, have_x_ = false;
};

// DigScale [value]: sets the device-to-model unit scale applied to raw
// digitizer readings before calibration (e.g. a caliper-style digitizer
// reporting inches into a millimetre model).
class DigScaleCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("New scale <" + FormatNumber(Digitizer::Instance().UnitScale()) + ">", FormatNumber(Digitizer::Instance().UnitScale()));
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (!end || *end != 0 || v <= 0) { ctx.Warn("DigScale: '" + t + "' is not a positive number"); Finish(); return; }
    Digitizer::Instance().SetUnitScale(v);
    ctx.Print("DigScale: scale set to " + FormatNumber(Digitizer::Instance().UnitScale()));
    Finish();
  }
  void OnNumber(CommandContext& ctx, double v) override { OnText(ctx, FormatNumber(v)); }
  void OnEnter(CommandContext& ctx) override { ctx.Print("DigScale: current scale " + FormatNumber(Digitizer::Instance().UnitScale())); Finish(); }
};

// ---------------------------------------------------------------------------
// Worksession: Attach/Detach/List/Save/Load. Objects are copied in (locked,
// tagged "Dino8.Reference") rather than kept as a live link to the source
// file - see session/Worksession.h for why and for what Save3dm then skips.
// ---------------------------------------------------------------------------

class WorksessionCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    options = {{"Attach", "", {}, false, false}, {"Detach", "", {}, false, false}, {"List", "", {}, false, false},
               {"Save", "", {}, false, false}, {"Load", "", {}, false, false}};
    if (auto t = ctx.Engine().TakePendingInput()) { OnOption(ctx, *t, ""); return; }
    WantEnter("Worksession (Attach/Detach/List/Save/Load)");
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override {
    const std::string l = Lower(n);
    if (l == "list") { List(ctx); Finish(); return; }
    if (l != "attach" && l != "detach" && l != "save" && l != "load") { ctx.Warn("Worksession: unknown option '" + n + "'"); Finish(); return; }
    action_ = l;
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText(action_ == "detach" ? "Model to detach (path, alias, or All)" : "File path");
  }
  void OnText(CommandContext& ctx, const std::string& text) override {
    if (action_.empty()) { OnOption(ctx, text, ""); return; }
    Document& doc = ctx.Doc();
    std::string error;
    if (action_ == "attach") {
      doc.BeginChange("Worksession Attach");
      const int n = AttachWorksession(doc, text, error);
      if (n < 0) ctx.Warn("Worksession: " + error);
      else ctx.Print("Worksession: attached " + text + " (" + std::to_string(n) + " object(s))");
    } else if (action_ == "detach") {
      doc.BeginChange("Worksession Detach");
      const int n = DetachWorksession(doc, text);
      ctx.Print("Worksession: detached " + std::to_string(n) + " object(s)");
    } else if (action_ == "save") {
      if (!SaveWorksessionFile(doc, text, error)) ctx.Warn("Worksession: " + error);
      else ctx.Print("Worksession: saved " + text);
    } else if (action_ == "load") {
      const int n = LoadWorksessionFile(doc, text, error);
      if (n == 0 && !error.empty()) ctx.Warn("Worksession: " + error);
      else ctx.Print("Worksession: attached " + std::to_string(n) + " model(s) from " + text);
    }
    Finish();
  }
  void OnEnter(CommandContext& ctx) override { List(ctx); Finish(); }

 private:
  void List(CommandContext& ctx) {
    const auto& models = ctx.Doc().ReferenceModels();
    ctx.Print("Worksession: " + std::to_string(models.size()) + " attached reference model(s)");
    for (const ReferenceModel& m : models)
      ctx.Print("  " + m.alias + " (" + m.path + "): " + std::to_string(m.object_ids.size()) + " object(s)" + (m.has_limit_box ? ", limited" : ""));
  }
  std::string action_;
};

class LimitReferenceModelCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Reference model to limit (path or alias)");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (name_.empty()) { name_ = t; WantPoint("First corner of the box to keep"); return; }
  }
  void OnPoint(CommandContext& ctx, kernel::Point3d p) override {
    ctx.SetLastPoint(p);
    if (!a_) { a_ = p; WantPoint("Other corner of the box to keep"); return; }
    const kernel::Point3d mn(std::min(a_->x, p.x), std::min(a_->y, p.y), std::min(a_->z, p.z));
    const kernel::Point3d mx(std::max(a_->x, p.x), std::max(a_->y, p.y), std::max(a_->z, p.z));
    ctx.Doc().BeginChange("LimitReferenceModel");
    const int removed = LimitWorksessionModel(ctx.Doc(), name_, mn, mx);
    ctx.Print("LimitReferenceModel: " + std::to_string(removed) + " object(s) removed from '" + name_ + "'");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, kernel::Point3d h) override {
    if (!a_) return;
    ctx.ClearPreview();
    ctx.AddPreviewLine(*a_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  std::string name_;
  std::optional<kernel::Point3d> a_;
};

// ---------------------------------------------------------------------------
// Snapshots: named, full-document restore points independent of Undo/Redo
// (Document::SaveNamedSnapshot et al.) - same Save/Restore/Delete/List shape
// as NamedSelections/NamedCPlane (cmd_state.cpp's NamedSetCommand).
// ---------------------------------------------------------------------------

class SnapshotsCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    options = {{"Save", "", {}, false, false}, {"Restore", "", {}, false, false}, {"Delete", "", {}, false, false}, {"List", "", {}, false, false}};
    if (auto t = ctx.Engine().TakePendingInput()) { OnOption(ctx, *t, ""); return; }
    WantEnter("Snapshots (Save/Restore/Delete/List)");
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override {
    const std::string l = Lower(n);
    if (l == "list") { List(ctx); Finish(); return; }
    if (l != "save" && l != "restore" && l != "delete") { ctx.Warn("Snapshots: unknown option '" + n + "'"); Finish(); return; }
    action_ = l;
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Name");
  }
  void OnText(CommandContext& ctx, const std::string& name) override {
    if (action_.empty()) { OnOption(ctx, name, ""); return; }
    Document& doc = ctx.Doc();
    if (action_ == "save") { doc.SaveNamedSnapshot(name); ctx.Print("Snapshot '" + name + "' saved (" + std::to_string(doc.Objects().size()) + " object(s))"); }
    else if (action_ == "restore") { if (!doc.RestoreNamedSnapshot(name)) ctx.Warn("No snapshot '" + name + "'"); else ctx.Print("Snapshot '" + name + "' restored"); }
    else if (action_ == "delete") { if (!doc.DeleteNamedSnapshot(name)) ctx.Warn("No snapshot '" + name + "'"); else ctx.Print("Snapshot '" + name + "' deleted"); }
    Finish();
  }
  void OnEnter(CommandContext& ctx) override { List(ctx); Finish(); }

 private:
  void List(CommandContext& ctx) {
    const std::vector<std::string> names = ctx.Doc().NamedSnapshotNames();
    ctx.Print(std::to_string(names.size()) + " snapshot(s)");
    for (const std::string& n : names) ctx.Print("  " + n);
  }
  std::string action_;
};

}  // namespace

void RegisterSessionCommands(CommandEngine& e) {
  Reg(e, "DigConnect", Make<DigConnectCommand>(), CommandStatus::Partial,
      "Connects Protocol=Ascii (real serial port), Protocol=File (replays 'x y z [button]' lines from a text file - a headless test mode) or Protocol=Simulated (the next viewport click becomes a digitized point).");
  Reg(e, "DigDisconnect", Immediate([](CommandContext& ctx) {
        Digitizer::Instance().Disconnect();
        ctx.Print("DigDisconnect: disconnected");
      }));
  Reg(e, "DigListPorts", Immediate([](CommandContext& ctx) {
        const std::vector<std::string> ports = Digitizer::EnumeratePorts();
        ctx.Print("DigListPorts: " + std::to_string(ports.size()) + " port(s) found");
        for (const std::string& p : ports) ctx.Print("  " + p);
      }), CommandStatus::Partial, "Lists serial devices that currently exist and open successfully.");
  Reg(e, "DigCalibrate", Make<DigCalibrateCommand>(), CommandStatus::Partial,
      "3-point calibration (origin, +X point, +Y point) into the active CPlane.");
  Reg(e, "DigPoint", Immediate([](CommandContext& ctx) {
        Digitizer& dig = Digitizer::Instance();
        if (!dig.Connected()) { ctx.Warn("DigPoint: the digitizer is not connected. Use DigConnect first."); return; }
        DigitizerPoint p;
        if (!dig.ReadPoint(p)) { ctx.Warn("DigPoint: no point available from the digitizer"); return; }
        const kernel::Point3d model = dig.ToModel(p.raw);
        AddObject(ctx, SceneObject::MakePoint(model), "DigPoint");
        ctx.Print("DigPoint: digitized " + FormatPoint(model) + (p.button ? (" (button " + std::to_string(p.button) + ")") : ""));
      }), CommandStatus::Partial, "Reads the next point from the connected digitizer (calibrated and unit-scaled) and adds a point object.");
  Reg(e, "DigScale", Make<DigScaleCommand>(), CommandStatus::Partial,
      "Sets the device-to-model unit scale applied to raw digitizer readings, before calibration.");
  Reg(e, "DigPause", Immediate([](CommandContext& ctx) { Digitizer::Instance().SetPaused(true); ctx.Print("DigPause: digitizing paused"); }));
  Reg(e, "DigResume", Immediate([](CommandContext& ctx) { Digitizer::Instance().SetPaused(false); ctx.Print("DigResume: digitizing resumed"); }));
  Reg(e, "DigStatus", Immediate([](CommandContext& ctx) { ctx.Print(DescribeDigitizer()); }));

  // ---- Worksession / LimitReferenceModel --------------------------------
  Reg(e, "Worksession", Make<WorksessionCommand>(), CommandStatus::Partial,
      "Attach/Detach/List/Save/Load reference models (other .3dm files, copied in locked). Copies objects in rather than a live link, unlike Rhino's worksessions.");
  Reg(e, "LimitReferenceModel", Make<LimitReferenceModelCommand>(), CommandStatus::Partial,
      "Re-filters an attached reference model down to the objects whose bounding box intersects two picked corner points.");

  // ---- Snapshots ----------------------------------------------------------
  Reg(e, "Snapshots", Make<SnapshotsCommand>(), CommandStatus::Partial,
      "Save/Restore/Delete/List named, full-document restore points, independent of Undo/Redo.");
}

}  // namespace dino8::app
