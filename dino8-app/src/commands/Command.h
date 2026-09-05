// The interactive command interface. A command is a small state machine:
// Begin() asks for its first input (a point, objects, a number, text, or
// just Enter) via the Want* helpers; the engine feeds input back through
// the On*() callbacks as the user picks in a viewport or types on the
// command line; the command calls Finish() when done.
//
// This mirrors how Rhino's own command line works (prompt + options +
// Enter/Esc), so scripts and muscle memory transfer directly.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "doc/Document.h"
#include "viewport/Viewport.h"

namespace dino8::app {

class Application;
class CommandEngine;

enum class Want { Nothing, Point, Objects, Number, Text, Enter };

// A command-line option shown after the prompt, e.g. "Radius=5" or
// "Mode=Lines". Typing the option name (or clicking it) triggers OnOption.
struct OptionSpec {
  std::string name;                  // e.g. "Radius"
  std::string value;                 // current value text ("5", "Yes", "")
  std::vector<std::string> choices;  // for list options ("Yes","No")
  bool numeric = false;              // value is a number the user can type
  bool toggle = false;               // clicking flips Yes/No
};

class CommandContext {
 public:
  CommandContext(Application& app, Document& doc, CommandEngine& engine)
      : app_(app), doc_(doc), engine_(engine) {}

  Document& Doc() { return doc_; }
  Application& App() { return app_; }
  CommandEngine& Engine() { return engine_; }

  Viewport* ActiveViewport();
  std::vector<std::unique_ptr<Viewport>>& Viewports();
  SnapSettings& Snaps();
  DocumentSettings& Settings() { return doc_.Settings(); }

  // Output to the command history.
  void Print(const std::string& line);
  void Warn(const std::string& line);

  // Transient preview geometry drawn in every viewport until cleared.
  std::vector<float>& PreviewLines();
  std::vector<float>& PreviewPoints();
  void ClearPreview();
  void AddPreviewLine(kernel::Point3d a, kernel::Point3d b);
  void AddPreviewPolyline(const std::vector<kernel::Point3d>& pts, bool closed = false);
  void AddPreviewCurve(const kernel::NurbsCurve& curve);
  void AddPreviewPoint(kernel::Point3d p);

  // The last point picked (used for @relative input, ortho, distance).
  std::optional<kernel::Point3d> LastPoint() const;
  void SetLastPoint(std::optional<kernel::Point3d> p);
  // Where the cursor currently is on the CPlane, if hovering a viewport.
  std::optional<kernel::Point3d> HoverPoint() const;

  // Objects currently selected in the document.
  std::vector<ObjectId> Selected() { return doc_.SelectedIds(); }

  // Whether the command was launched with the "-" (script/no-dialog) prefix.
  bool ScriptMode() const;

  void RequestRedraw();
  void ZoomExtentsAll();

 private:
  Application& app_;
  Document& doc_;
  CommandEngine& engine_;
};

class Command {
 public:
  virtual ~Command() = default;

  virtual void Begin(CommandContext& ctx) = 0;
  virtual void OnPoint(CommandContext&, kernel::Point3d) {}
  virtual void OnNumber(CommandContext&, double) {}
  virtual void OnText(CommandContext&, const std::string&) {}
  virtual void OnObjects(CommandContext&, const std::vector<ObjectId>&) {}
  virtual void OnEnter(CommandContext&) {}
  virtual void OnOption(CommandContext&, const std::string& /*name*/, const std::string& /*value*/) {}
  virtual void OnHover(CommandContext&, kernel::Point3d) {}
  virtual void OnCancel(CommandContext&) {}

  // Current request to the user.
  Want want = Want::Nothing;
  std::string prompt = "Command:";
  std::vector<OptionSpec> options;
  bool finished = false;
  // For Want::Objects: accept the pre-selection immediately if non-empty.
  bool accept_preselection = true;
  int min_objects = 1;
  // For Want::Number: a default the user can accept with Enter.
  std::optional<double> default_number;
  std::optional<std::string> default_text;

  void Finish() { finished = true; }

 protected:
  void WantPoint(const std::string& p) { want = Want::Point; prompt = p; }
  void WantObjects(const std::string& p, int min_count = 1) { want = Want::Objects; prompt = p; min_objects = min_count; }
  void WantNumber(const std::string& p, std::optional<double> def = std::nullopt) { want = Want::Number; prompt = p; default_number = def; }
  void WantText(const std::string& p, std::optional<std::string> def = std::nullopt) { want = Want::Text; prompt = p; default_text = def; }
  void WantEnter(const std::string& p) { want = Want::Enter; prompt = p; }
};

using CommandFactory = std::function<std::unique_ptr<Command>()>;

// Helper base for commands that need no interaction at all.
class ImmediateCommand : public Command {
 public:
  explicit ImmediateCommand(std::function<void(CommandContext&)> fn) : fn_(std::move(fn)) {}
  void Begin(CommandContext& ctx) override {
    fn_(ctx);
    Finish();
  }

 private:
  std::function<void(CommandContext&)> fn_;
};

// Helper base for "select objects, then act" commands.
class SelectThenActCommand : public Command {
 public:
  SelectThenActCommand(std::string prompt, std::function<void(CommandContext&, const std::vector<ObjectId>&)> fn,
                       int min_count = 1)
      : prompt_(std::move(prompt)), fn_(std::move(fn)), min_(min_count) {}
  void Begin(CommandContext&) override { WantObjects(prompt_, min_); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    fn_(ctx, ids);
    Finish();
  }

 private:
  std::string prompt_;
  std::function<void(CommandContext&, const std::vector<ObjectId>&)> fn_;
  int min_;
};

// Small helpers shared by many commands.
std::string FormatPoint(kernel::Point3d p);
std::string FormatNumber(double v);

}  // namespace dino8::app
