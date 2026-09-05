// File commands: New, Open, Save, SaveAs, Import, Export, Exit...
#include "commands/cmd_common.h"
#include "io/File3dm.h"

#include <cctype>

namespace dino8::app {

namespace {

const std::vector<std::string> kModelExts = {".3dm", ".obj", ".stl", ".ply", ".dxf"};
const std::vector<std::string> kExportExts = {".3dm", ".obj", ".stl", ".ply", ".dxf", ".svg", ".pdf"};

void SaveTo(CommandContext& ctx, const std::string& path) {
  std::string err;
  if (!ctx.App().SaveDocument(path, err)) ctx.Warn(err);
}

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
        if (auto p = ctx.Engine().TakePendingInput()) { SaveTo(ctx, *p); return; }
        app.ShowFileDialog("Save model as", kExportExts, true, [&app](const std::string& path) { std::string err; if (!app.SaveDocument(path, err)) app.Notify(err); });
      }));
  Reg(e, "SaveSmall", Immediate([](CommandContext& ctx) { if (ctx.Doc().Path().empty()) ctx.Engine().Execute("SaveAs"); else SaveTo(ctx, ctx.Doc().Path()); }), CommandStatus::Partial, "Dino 8 never stores render meshes, so every save is already small.");
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
      }), CommandStatus::Partial, "Saves a normal .3dm you can open as a starting point.");
  Reg(e, "Import", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        if (auto p = ctx.Engine().TakePendingInput()) { std::string err; if (!app.ImportFile(*p, err)) ctx.Warn(err); else ctx.Print("Imported " + *p); return; }
        app.ShowFileDialog("Import", kModelExts, false, [&app](const std::string& path) { std::string err; if (!app.ImportFile(path, err)) app.Notify(err); });
      }));
  Reg(e, "Insert", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Import"); }), CommandStatus::Partial, "Imports the file's objects; block instances are planned.");
  Reg(e, "Export", OnSelection("Select objects to export", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Application& app = ctx.App();
        for (ObjectId id : ids) ctx.Doc().Select(id, true);
        if (auto p = ctx.Engine().TakePendingInput()) { std::string err; if (!app.ExportSelected(*p, err)) ctx.Warn(err); else ctx.Print("Exported " + *p); return; }
        app.ShowFileDialog("Export selected", kExportExts, true, [&app](const std::string& path) { std::string err; if (!app.ExportSelected(path, err)) app.Notify(err); else app.Notify("Exported " + path); });
      }));
  Reg(e, "ExportSelected", OnSelection("Select objects to export", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Application& app = ctx.App();
        for (ObjectId id : ids) ctx.Doc().Select(id, true);
        app.ShowFileDialog("Export selected", kExportExts, true, [&app](const std::string& path) { std::string err; if (!app.ExportSelected(path, err)) app.Notify(err); else app.Notify("Exported " + path); });
      }));
  Reg(e, "ExportWithOrigin", OnSelection("Select objects to export", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) ctx.Doc().Select(id, true);
        ctx.Engine().Execute("Export");
      }), CommandStatus::Partial, "Exports without re-basing the origin.");
  Reg(e, "Exit", Immediate([](CommandContext& ctx) { ctx.App().RequestQuit(); }));
  Reg(e, "Notes", Immediate([](CommandContext& ctx) { ctx.App().Panels().notes = true; }));
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
