// File commands: New, Open, Save, SaveAs, Import, Export, Exit...
#include "commands/cmd_common.h"
#include "io/File3dm.h"
#include "io/FileExchange.h"
#include "io/FileIgesStep.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>

namespace dino8::app {

namespace {

namespace fs = std::filesystem;

std::string LowerExt(const std::string& path) {
  std::string e = fs::path(path).extension().string();
  for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

const std::vector<std::string> kModelExts = {".3dm", ".obj", ".stl", ".ply", ".dxf", ".dwg", ".igs", ".iges", ".stp", ".step"};
const std::vector<std::string> kExportExts = {".3dm", ".obj", ".stl", ".ply", ".dxf", ".dwg", ".svg", ".pdf", ".igs", ".iges", ".stp", ".step"};

void SaveTo(CommandContext& ctx, const std::string& path) {
  std::string err;
  if (!ctx.App().SaveDocument(path, err)) ctx.Warn(err);
}

// Drains the pending-input tokens for Export/SaveAs, pulling out an
// AcadSchemes-style "Version=" token (see cmd_remaining.cpp's AcadSchemes)
// as a one-off override for just this export, and returning the single
// remaining token as the path - same tolerant "any order, last plain token
// wins" parsing "Print"'s own Scale=/path tokens already use. Returns
// nullopt (falling back to a file dialog) when no path token was given.
std::optional<std::string> TakePathAndVersion(CommandContext& ctx, std::string& scheme_override) {
  std::optional<std::string> path;
  while (auto t = ctx.Engine().TakePendingInput()) {
    std::string lower = *t;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.rfind("version=", 0) == 0) { scheme_override = t->substr(8); continue; }
    path = *t;
  }
  return path;
}

// Runs `fn` (an Export/SaveAs call) with the document's AcadSchemes default
// (DocumentSettings::dwg_export_scheme) temporarily replaced by
// `scheme_key` - a normalized key from NormalizeAcadScheme(), or empty to
// leave the document's own persistent scheme in effect - and restored right
// after, so a one-off "Version=" on Export/SaveAs never changes the
// document's own AcadSchemes setting for later exports.
void ExportWithSchemeOverride(Document& doc, const std::string& scheme_key, const std::function<void()>& fn) {
  const std::string prev = doc.Settings().dwg_export_scheme;
  if (!scheme_key.empty()) doc.Settings().dwg_export_scheme = scheme_key;
  fn();
  doc.Settings().dwg_export_scheme = prev;
}

// Same extension dispatch as Application::ExportSelected, but against a
// caller-supplied scratch document (ExportWithOrigin below builds one with
// the objects re-based to a new origin) instead of the live document.
bool ExportDocument(const Document& doc, const std::string& path, std::string& error) {
  const std::string ext = LowerExt(path);
  if (ext == ".3dm") return Save3dm(doc, path, error);
  if (ext == ".dxf") return ExportDxf(doc, path, true, error);
  if (ext == ".dwg") return ExportDwg(doc, path, true, error);
  if (ext == ".ply") return ExportPly(doc, path, true, error);
  if (ext == ".igs" || ext == ".iges") return ExportIges(doc, path, true, error);
  if (ext == ".stp" || ext == ".step") return ExportStep(doc, path, true, error);
  return ExportMeshFile(doc, path, true, error);
}

// Free function (not a Command method) so the fire-and-forget file-dialog
// callback below never has to close over `this`: the Command that started
// it is destroyed (Finish() + AfterCallback()) as soon as OnPoint returns,
// same as every other dialog-based export in this file.
void RunExportWithOrigin(Application& app, Document& doc, const std::vector<ObjectId>& ids, Point3d origin, const std::string& path) {
  if (path.empty()) return;
  const std::string ext = LowerExt(path);
  std::string error;
  bool ok;
  if (ext == ".svg" || ext == ".pdf") {
    ok = app.ExportDrawing(path, true, 0.0, error);
  } else {
    Document tmp;
    tmp.Layers() = doc.Layers();
    const ON_Xform xf = ON_Xform::TranslationTransformation(ON_3dPoint::Origin - origin);
    for (ObjectId id : ids) {
      const SceneObject* o = doc.Find(id);
      if (!o) continue;
      SceneObject dup = *o;
      dup.id = kNoObject;
      dup.selected = true;
      dup.Transform(xf);
      tmp.Add(std::move(dup));
    }
    ok = ExportDocument(tmp, path, error);
  }
  if (ok) app.Notify("Exported " + path + " (origin at " + FormatPoint(origin) + ")");
  else app.Notify(error);
}

// ExportWithOrigin: select objects, pick a point to become the new origin,
// then export copies of the selection translated so that point lands at
// (0,0,0) - the vector/CAD interchange formats (.3dm/.obj/.stl/.dxf/.ply/
// .igs/.stp) support this exactly since it's a plain object translation.
// SVG/PDF (page-space drawings of the *view*, not object-space geometry)
// have no origin to re-base, so those two extensions fall back to a plain
// Export of the view.
class ExportWithOriginCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to export", 1); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    for (ObjectId id : ids_) ctx.Doc().Select(id, true);
    WantPoint("Point to use as the new origin");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (auto path = ctx.Engine().TakePendingInput()) { RunExportWithOrigin(ctx.App(), ctx.Doc(), ids_, p, *path); Finish(); return; }
    Application& app = ctx.App();
    Document& doc = ctx.Doc();
    std::vector<ObjectId> ids = ids_;
    app.ShowFileDialog("Export selected (with origin)", kExportExts, true, [&app, &doc, ids, p](const std::string& path) { RunExportWithOrigin(app, doc, ids, p, path); });
    Finish();
  }
  std::vector<ObjectId> ids_;
};

}  // namespace

void RegisterFileCommands(CommandEngine& e) {
  Reg(e, "New", Immediate([](CommandContext& ctx) { ctx.App().NewDocument(true); }));
  Reg(e, "Open", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        if (auto p = ctx.Engine().TakePendingInput()) { std::string err; if (!app.OpenDocument(*p, err)) ctx.Warn(err); return; }
        app.ConfirmDiscard([&app]() { app.ShowFileDialog("Open model", kModelExts, false, [&app](const std::string& path) { std::string err; if (!app.OpenDocument(path, err)) app.Notify(err); }); });
      }));
  Reg(e, "Revert", Immediate([](CommandContext& ctx) { std::string p = ctx.Doc().Path(); if (p.empty()) { ctx.Warn("Document has never been saved"); return; } std::string err; if (!ctx.App().OpenDocument(p, err)) ctx.Warn(err); }));
  Reg(e, "Save", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        if (auto p = ctx.Engine().TakePendingInput()) { SaveTo(ctx, *p); return; }
        if (!ctx.Doc().Path().empty()) { SaveTo(ctx, ctx.Doc().Path()); return; }
        app.ShowFileDialog("Save model", {".3dm"}, true, [&app](const std::string& path) { std::string err; if (!app.SaveDocument(path, err)) app.Notify(err); });
      }));
  Reg(e, "SaveAs", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        std::string scheme_input;
        if (auto p = TakePathAndVersion(ctx, scheme_input)) {
          std::string normalized;
          if (!scheme_input.empty()) {
            normalized = NormalizeAcadScheme(scheme_input);
            if (normalized.empty()) { ctx.Warn("SaveAs: unrecognized Version=" + scheme_input + " (see AcadSchemes for valid values)"); return; }
          }
          ExportWithSchemeOverride(ctx.Doc(), normalized, [&] { SaveTo(ctx, *p); });
          return;
        }
        app.ShowFileDialog("Save model as", kExportExts, true, [&app](const std::string& path) { std::string err; if (!app.SaveDocument(path, err)) app.Notify(err); });
      }), CommandStatus::Implemented,
      "A Version=13/14/2000/2004/2010/2013/2018 token overrides the document's AcadSchemes default just for this "
      "DWG/DXF save (other extensions ignore it); with no path token, opens the save-as dialog instead.");
  Reg(e, "SaveSmall", Immediate([](CommandContext& ctx) { if (ctx.Doc().Path().empty()) ctx.Engine().Execute("SaveAs"); else SaveTo(ctx, ctx.Doc().Path()); }), CommandStatus::Implemented, "Dino 8 never stores render meshes, so every save is already small.");
  Reg(e, "IncrementalSave", Immediate([](CommandContext& ctx) {
        std::string p = ctx.Doc().Path();
        if (p.empty()) { ctx.Engine().Execute("SaveAs"); return; }
        static int n = 0;
        std::string stem = p.substr(0, p.size() - 4);
        SaveTo(ctx, stem + "_" + std::to_string(++n) + ".3dm");
      }));
  Reg(e, "SaveAsTemplate", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        app.ShowFileDialog("Save template", {".3dm"}, true, [&app](const std::string& path) { std::string err; if (!app.SaveDocument(path, err)) app.Notify(err); });
      }), CommandStatus::Implemented, "Saves a normal .3dm you can open as a starting point.");
  Reg(e, "Import", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        if (auto p = ctx.Engine().TakePendingInput()) { std::string err; if (!app.ImportFile(*p, err)) ctx.Warn(err); else ctx.Print("Imported " + *p); return; }
        app.ShowFileDialog("Import", kModelExts, false, [&app](const std::string& path) { std::string err; if (!app.ImportFile(path, err)) app.Notify(err); });
      }));
  Reg(e, "Export", OnSelection("Select objects to export", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Application& app = ctx.App();
        for (ObjectId id : ids) ctx.Doc().Select(id, true);
        std::string scheme_input;
        if (auto p = TakePathAndVersion(ctx, scheme_input)) {
          std::string normalized;
          if (!scheme_input.empty()) {
            normalized = NormalizeAcadScheme(scheme_input);
            if (normalized.empty()) { ctx.Warn("Export: unrecognized Version=" + scheme_input + " (see AcadSchemes for valid values)"); return; }
          }
          std::string err;
          bool ok = false;
          ExportWithSchemeOverride(ctx.Doc(), normalized, [&] { ok = app.ExportSelected(*p, err); });
          if (!ok) ctx.Warn(err); else ctx.Print("Exported " + *p + (normalized.empty() ? "" : " (Version=" + normalized + ")"));
          return;
        }
        app.ShowFileDialog("Export selected", kExportExts, true, [&app](const std::string& path) { std::string err; if (!app.ExportSelected(path, err)) app.Notify(err); else app.Notify("Exported " + path); });
      }), CommandStatus::Implemented,
      "A Version=13/14/2000/2004/2010/2013/2018 token overrides the document's AcadSchemes default just for this "
      "DWG/DXF export (other extensions ignore it); with no path token, opens the export dialog instead.");
  Reg(e, "ExportSelected", OnSelection("Select objects to export", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Application& app = ctx.App();
        for (ObjectId id : ids) ctx.Doc().Select(id, true);
        app.ShowFileDialog("Export selected", kExportExts, true, [&app](const std::string& path) { std::string err; if (!app.ExportSelected(path, err)) app.Notify(err); else app.Notify("Exported " + path); });
      }));
  Reg(e, "ExportWithOrigin", Make<ExportWithOriginCommand>(), CommandStatus::Implemented, "Translates copies of the selection so the picked point lands at 0,0,0 before writing them; SVG/PDF (page-space view drawings, not object-space geometry) export the plain view instead.");
  Reg(e, "Exit", Immediate([](CommandContext& ctx) { ctx.App().RequestQuit(); }));
  Reg(e, "Notes", Immediate([](CommandContext& ctx) { ctx.App().Panels().notes = true; }));
  Reg(e, "ActivityLog", Immediate([](CommandContext& ctx) { ctx.App().Panels().activity_log = true; }));
  // ActivityExport: writes the document's full Activity Log (see
  // Document::ActivityLog / RecordActivityLogEntry) as CSV - the same
  // format BillOfMaterials' "csv=" option writes above - so it opens
  // cleanly in a spreadsheet. Optional first argument is the output path;
  // Enter with no argument writes next to the document (or to the current
  // directory for an unsaved document).
  Reg(e, "ActivityExport", Immediate([](CommandContext& ctx) {
        std::string path;
        if (auto t = ctx.Engine().TakePendingInput()) path = *t;
        if (path.empty()) {
          path = ctx.Doc().Path().empty() ? "activity_log.csv" : ctx.Doc().Path() + ".activity_export.csv";
        }
        ctx.Doc().FlushPendingHistory();  // include the very last edit, even if it hasn't hit another BeginChange yet
        std::ofstream f(path);
        if (!f) { ctx.Warn("ActivityExport: could not write " + path); return; }
        f << "Timestamp (UTC),Action,Detail\n";
        auto csv_escape = [](const std::string& s) {
          std::string out = "\"";
          for (char c : s) { if (c == '"') out += "\"\""; else out += c; }
          out += "\"";
          return out;
        };
        for (const auto& entry : ctx.Doc().ActivityLog()) {
          f << csv_escape(entry.timestamp_utc) << "," << csv_escape(entry.label) << "," << csv_escape(entry.summary) << "\n";
        }
        ctx.Print("ActivityExport: " + std::to_string(ctx.Doc().ActivityLog().size()) + " entr" +
                   (ctx.Doc().ActivityLog().size() == 1 ? "y" : "ies") + " written to " + path);
      }),
      CommandStatus::Implemented,
      "Exports the local Activity Log (a persisted, labeled record of every edit made to this document - Dino 8's analogue of AutoCAD's Activity Insights) to CSV. No cross-machine/user sync, single-user local history only.");
  Reg(e, "DocumentProperties", Immediate([](CommandContext& ctx) { ctx.App().Panels().document_properties = true; }));
  Reg(e, "Units", Immediate([](CommandContext& ctx) { ctx.App().Panels().document_properties = true; }));
  Reg(e, "Audit3dmFile", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        app.ShowFileDialog("Audit .3dm file", {".3dm"}, false, [&app](const std::string& path) {
          Document tmp; std::string err;
          if (Load3dm(tmp, path, err)) app.Notify(path + ": " + std::to_string(tmp.ObjectCount()) + " objects, " + std::to_string(tmp.Layers().size()) + " layers" + (err.empty() ? "" : " (" + err + ")"));
          else app.Notify(err);
        });
      }));
  // Print: vector PDF of the active view. "Print file.pdf" and an optional
  // "Scale=1" token (page mm per document unit; 0 = fit to page) can be
  // given on the command line; otherwise a save dialog asks for the file.
  Reg(e, "Print", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        double scale = 0.0;
        std::optional<std::string> path;
        while (auto t = ctx.Engine().TakePendingInput()) {
          const std::string lower = [&] { std::string s = *t; for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }();
          if (lower.rfind("scale=", 0) == 0) scale = std::atof(t->c_str() + 6);
          else if (!path) path = *t;
        }
        if (path) { std::string err; if (!app.ExportDrawing(*path, false, scale, err)) ctx.Warn(err); return; }
        app.ShowFileDialog("Print to PDF", {".pdf", ".svg"}, true, [&app, scale](const std::string& p) { std::string err; if (!app.ExportDrawing(p, false, scale, err)) app.Notify(err); });
      }), CommandStatus::Implemented, "Writes a vector PDF (or SVG) of the active view; Scale=<mm per unit> forces a print scale.");
}

}  // namespace dino8::app
