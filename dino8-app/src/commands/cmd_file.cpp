// File commands: New, Open, Save, SaveAs, Import, Export, Exit...
#include "commands/cmd_common.h"
#include "io/File3dm.h"

namespace dino8::app {

namespace {

const std::vector<std::string> kModelExts = {".3dm", ".obj", ".stl"};

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
        app.ShowFileDialog("Save model as", {".3dm", ".obj", ".stl"}, true, [&app](const std::string& path) { std::string err; if (!app.SaveDocument(path, err)) app.Notify(err); });
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
        app.ShowFileDialog("Export selected", {".3dm", ".obj", ".stl"}, true, [&app](const std::string& path) { std::string err; if (!app.ExportSelected(path, err)) app.Notify(err); else app.Notify("Exported " + path); });
      }));
  Reg(e, "ExportSelected", OnSelection("Select objects to export", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Application& app = ctx.App();
        for (ObjectId id : ids) ctx.Doc().Select(id, true);
        app.ShowFileDialog("Export selected", {".3dm", ".obj", ".stl"}, true, [&app](const std::string& path) { std::string err; if (!app.ExportSelected(path, err)) app.Notify(err); else app.Notify("Exported " + path); });
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
  Reg(e, "Print", Immediate([](CommandContext& ctx) { ctx.Print("Print: export to OBJ/STL or capture the viewport; direct printing is planned."); }), CommandStatus::Partial);
}

}  // namespace dino8::app
