// File commands: New, Open, Save, SaveAs, Import, Export, Exit...
#include "commands/cmd_common.h"
#include "io/DigitalSignature.h"
#include "io/File3dm.h"
#include "io/FileExchange.h"
#include "io/FileIgesStep.h"
#include "io/FileSat.h"
#include "viewport/Viewport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

const std::vector<std::string> kModelExts = {".3dm", ".obj", ".stl", ".3mf", ".gltf", ".glb", ".ply", ".dxf", ".dwg", ".igs", ".iges", ".stp", ".step", ".sat", ".sab", ".ifc", ".xyz", ".pts", ".las"};
const std::vector<std::string> kExportExts = {".3dm", ".obj", ".stl", ".3mf", ".gltf", ".glb", ".ply", ".dxf", ".dwg", ".svg", ".pdf", ".igs", ".iges", ".stp", ".step", ".sat", ".sab", ".ifc", ".xyz", ".pts", ".las"};

void SaveTo(CommandContext& ctx, const std::string& path, int dm_version = 0) {
  std::string err;
  if (!ctx.App().SaveDocument(path, err, dm_version)) ctx.Warn(err);
}

// Parses a SaveAs/Export "Version=" token as a .3dm archive version number
// (see Save3dm's own doc comment, io/File3dm.h, for the full version/
// Rhino-release table) - a plain non-negative integer, unlike the AcadSchemes
// token the same option already carries for DWG/DXF. Returns false (and
// warns through `label`) for anything else, the same "refuse rather than
// silently ignore" shape NormalizeAcadScheme's own failure already has.
bool ParseDm3dmVersion(CommandContext& ctx, const std::string& label, const std::string& text, int& out_version) {
  if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); })) {
    ctx.Warn(label + ": Version=" + text + " is not a valid .3dm archive version number (expected e.g. 4, 5, 50, 60, 70, 80)");
    return false;
  }
  out_version = std::atoi(text.c_str());
  return true;
}

// Drains the pending-input tokens for Export/SaveAs, pulling out an
// AcadSchemes-style "Version=" token (see cmd_remaining.cpp's AcadSchemes)
// as a one-off override for just this export, and returning the single
// remaining token as the path - same tolerant "any order, last plain token
// wins" parsing "Print"'s own Scale=/path tokens already use. Returns
// nullopt (falling back to a file dialog) when no path token was given.
std::optional<std::string> TakePathAndVersion(CommandContext& ctx, std::string& scheme_override, MeshExportOptions* mesh = nullptr,
                                              bool* mesh_options_bad = nullptr) {
  std::optional<std::string> path;
  double units_factor = 1.0;
  while (auto t = ctx.Engine().TakePendingInput()) {
    std::string lower = *t;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.rfind("version=", 0) == 0) { scheme_override = t->substr(8); continue; }
    if (mesh) {
      // Export's mesh-format tokens (.stl / .obj / .ply): Format=Ascii|Binary|BinaryBigEndian, Scale=<factor>,
      // Units=<mm|cm|m|km|in|ft|mi|um>, Tolerance=<chord tolerance>, PerObject=Yes|No, Group=Layer|Object (OBJ). A bad value warns and sets
      // *mesh_options_bad so the export is refused rather than written with a silently different setting.
      auto bad = [&](const std::string& why) { ctx.Warn("Export: " + why); if (mesh_options_bad) *mesh_options_bad = true; };
      auto number = [&](const std::string& text, double& out) {
        char* end = nullptr;
        out = std::strtod(text.c_str(), &end);
        return end != text.c_str() && *end == '\0' && out > 0.0 && std::isfinite(out);
      };
      if (lower.rfind("format=", 0) == 0) {
        const std::string v = lower.substr(7);
        if (v == "ascii" || v == "text") mesh->format = MeshExportFormat::Ascii;
        else if (v == "binary" || v == "binarylittleendian" || v == "binary_little_endian") mesh->format = MeshExportFormat::Binary;
        else if (v == "binarybigendian" || v == "binary_big_endian") mesh->format = MeshExportFormat::BinaryBigEndian;
        else bad("Format=" + t->substr(7) + " is not Ascii, Binary or BinaryBigEndian");
        continue;
      }
      if (lower.rfind("scale=", 0) == 0) {
        if (!number(lower.substr(6), mesh->scale)) bad("Scale=" + t->substr(6) + " is not a positive number");
        continue;
      }
      if (lower.rfind("units=", 0) == 0) {
        if (!MeshExportUnitFactor(ctx.Doc(), t->substr(6), units_factor)) bad("Units=" + t->substr(6) + " is not a known unit (mm, cm, m, km, in, ft, mi, um)");
        else mesh->units_name = t->substr(6);
        continue;
      }
      if (lower.rfind("tolerance=", 0) == 0) {
        if (!number(lower.substr(10), mesh->chord_tolerance)) bad("Tolerance=" + t->substr(10) + " is not a positive number");
        continue;
      }
      if (lower.rfind("group=", 0) == 0) {
        const std::string v = lower.substr(6);
        if (v == "layer") mesh->group_by_layer = true;
        else if (v == "object") mesh->group_by_layer = false;
        else bad("Group=" + t->substr(6) + " is not Layer or Object");
        continue;
      }
      if (lower.rfind("perobject=", 0) == 0) {
        const std::string v = lower.substr(10);
        if (v == "yes" || v == "true" || v == "1") mesh->per_object = true;
        else if (v == "no" || v == "false" || v == "0") mesh->per_object = false;
        else bad("PerObject=" + t->substr(10) + " is not Yes or No");
        continue;
      }
    }
    path = *t;
  }
  if (mesh) mesh->scale *= units_factor;
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
  if (ext == ".sat") return ExportSat(doc, path, true, error);
  if (ext == ".sab") return ExportSab(doc, path, true, error);  // on success `error` holds the summary (printed by the caller)
  if (ext == ".ifc") return ExportIfc(doc, path, true, error);
  if (ext == ".xyz") return ExportXyz(doc, path, true, error);
  if (ext == ".pts") return ExportPts(doc, path, true, error);
  if (ext == ".las") return ExportLas(doc, path, true, error);
  return ExportMeshFile(doc, path, true, error);
}

// Export with PerObject=Yes: every selected, visible object that yields a mesh is written to its OWN file,
// <stem>_<n>_<name>.<ext> next to `path` (n counts the written files from 1; the name is the object's name with every
// character outside letters, digits, '-' and '_' replaced by '_'). Objects that give no mesh (curves, points) are skipped.
// Returns the number of files written; the paths are appended to `written`.
int ExportPerObject(CommandContext& ctx, const std::string& path, std::vector<std::string>& written, std::string& error) {
  const fs::path p(path);
  int n = 0;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!o.selected || !ctx.Doc().IsObjectVisible(o)) continue;
    Document sub;
    sub.Layers() = ctx.Doc().Layers();
    SceneObject copy = o;
    copy.selected = true;
    sub.Add(std::move(copy));
    std::string name = o.name;
    for (char& c : name) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    const std::string leaf = p.stem().string() + "_" + std::to_string(n + 1) + (name.empty() ? "" : "_" + name) + p.extension().string();
    const std::string out = (p.parent_path() / leaf).string();
    std::string e;
    if (ExportDocument(sub, out, e)) {
      ++n;
      written.push_back(out);
    } else {
      std::error_code ec;
      fs::remove(out, ec);
      error = e;
    }
  }
  return n;
}

// Free function (not a Command method) so the fire-and-forget file-dialog
// callback below never has to close over `this`: the Command that started
// it is destroyed (Finish() + AfterCallback()) as soon as OnPoint returns,
// same as every other dialog-based export in this file.
//
// `plane` is the construction plane that was active when the origin was picked: its x and y axes (not its own origin, which the picked
// point replaces) give the orientation of the written file's axes, so an object sits against the new frame exactly as it sat against
// the construction plane. A world-aligned plane leaves a plain translation.
ON_Xform OriginFrameTransform(Point3d origin, const ConstructionPlane& plane) {
  Vector3d x = plane.x_axis, y = plane.y_axis;
  Vector3d z = ON_CrossProduct(x, y);
  if (!x.Unitize() || !z.Unitize()) return ON_Xform::TranslationTransformation(ON_3dPoint::Origin - origin);  // degenerate plane: translation only
  y = ON_CrossProduct(z, x);
  y.Unitize();
  ON_Xform xf = ON_Xform::IdentityTransformation;
  const Vector3d axes[3] = {x, y, z};
  for (int r = 0; r < 3; ++r) {
    xf.m_xform[r][0] = axes[r].x;
    xf.m_xform[r][1] = axes[r].y;
    xf.m_xform[r][2] = axes[r].z;
    xf.m_xform[r][3] = -(axes[r].x * origin.x + axes[r].y * origin.y + axes[r].z * origin.z);
  }
  return xf;
}

bool IsWorldAlignedPlane(const ConstructionPlane& plane) {
  return plane.x_axis.x == 1.0 && plane.x_axis.y == 0.0 && plane.x_axis.z == 0.0 && plane.y_axis.x == 0.0 && plane.y_axis.y == 1.0 && plane.y_axis.z == 0.0;
}

void RunExportWithOrigin(Application& app, Document& doc, const std::vector<ObjectId>& ids, Point3d origin, const ConstructionPlane& plane, const std::string& path) {
  if (path.empty()) return;
  const std::string ext = LowerExt(path);
  std::string error;
  bool ok;
  if (ext == ".svg" || ext == ".pdf") {
    ok = app.ExportDrawing(path, true, 0.0, error);
  } else {
    Document tmp;
    tmp.Layers() = doc.Layers();
    const ON_Xform xf = OriginFrameTransform(origin, plane);
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
  if (ok) {
    app.Notify("Exported " + path + " (origin at " + FormatPoint(origin) +
               (IsWorldAlignedPlane(plane) ? std::string() : ", axes of the construction plane x " + FormatPoint(plane.x_axis) + " y " + FormatPoint(plane.y_axis)) + ")");
    if (!error.empty()) app.Notify(error);  // ExportSat reports its summary through `error` on success
  } else app.Notify(error);
}

// ExportWithOrigin: select objects, pick a point to become the new origin,
// then export copies of the selection re-expressed in the frame made of that
// point and the axes of the active construction plane (a world-aligned plane
// gives a plain translation: the point lands at (0,0,0)) - the vector/CAD interchange formats (.3dm/.obj/.stl/.dxf/.ply/
// .igs/.stp/.ifc/.xyz/.pts/.las) support this exactly since it's a plain
// object translation.
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
    ConstructionPlane plane;  // world aligned unless a viewport with its own construction plane is active
    if (Viewport* vp = ctx.ActiveViewport()) plane = vp->CPlane();
    if (auto path = ctx.Engine().TakePendingInput()) { RunExportWithOrigin(ctx.App(), ctx.Doc(), ids_, p, plane, *path); Finish(); return; }
    Application& app = ctx.App();
    Document& doc = ctx.Doc();
    std::vector<ObjectId> ids = ids_;
    app.ShowFileDialog("Export selected (with origin)", kExportExts, true, [&app, &doc, ids, p, plane](const std::string& path) { RunExportWithOrigin(app, doc, ids, p, plane, path); });
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
          if (LowerExt(*p) == ".3dm") {
            int dm_version = 0;
            if (!scheme_input.empty() && !ParseDm3dmVersion(ctx, "SaveAs", scheme_input, dm_version)) return;
            SaveTo(ctx, *p, dm_version);
            return;
          }
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
      "DWG/DXF save; for a .3dm path, Version=<archive number> (e.g. 4, 5, 50, 60, 70, 80) instead targets that exact "
      ".3dm archive version so older Rhino releases can open it (see Save3dm's own doc comment, io/File3dm.h); any "
      "other extension ignores it; with no path token, opens the save-as dialog instead.");
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
  // Import <path> [Units=<mm|cm|m|km|in|ft|mi|um>] [Scale=<factor>] [UpAxis=Y|Z]: the three tokens are OBJ settings. Units= names the length
  // unit the file's coordinates are written in (they are converted to the document's unit), Scale= multiplies them further, UpAxis=Y
  // says the file is Y-up (it is turned to the document's Z up). A bad value, or one of the tokens for another format, refuses the
  // import with a message rather than ignoring the setting.
  Reg(e, "Import", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        MeshImportOptions opts;
        double units_factor = 1.0;
        bool have_opts = false, bad = false;
        std::optional<std::string> path;
        while (auto t = ctx.Engine().TakePendingInput()) {
          std::string lower = *t;
          for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          auto refuse = [&](const std::string& why) { ctx.Warn("Import: " + why); bad = true; };
          if (lower.rfind("units=", 0) == 0) {
            have_opts = true;
            if (!MeshImportUnitFactor(ctx.Doc(), t->substr(6), units_factor)) refuse("Units=" + t->substr(6) + " is not a known unit (mm, cm, m, km, in, ft, mi, um)");
          } else if (lower.rfind("scale=", 0) == 0) {
            have_opts = true;
            char* end = nullptr;
            const std::string v = t->substr(6);
            opts.scale = std::strtod(v.c_str(), &end);
            if (end == v.c_str() || *end != '\0' || !(opts.scale > 0.0) || !std::isfinite(opts.scale)) refuse("Scale=" + v + " is not a positive number");
          } else if (lower.rfind("upaxis=", 0) == 0) {
            have_opts = true;
            const std::string v = lower.substr(7);
            if (v == "y") opts.y_up = true;
            else if (v == "z") opts.y_up = false;
            else refuse("UpAxis=" + t->substr(7) + " is not Y or Z");
          } else if (!path) {
            path = *t;
          }
        }
        if (bad) return;
        opts.scale *= units_factor;
        auto run = [have_opts, opts](Application& a, const std::string& file, std::string& err) {
          if (have_opts && LowerExt(file) != ".obj") { err = "Import: Units=, Scale= and UpAxis= apply to .obj files only"; return false; }
          ScopedMeshImportOptions scoped(opts);
          return a.ImportFile(file, err);
        };
        if (path) { std::string err; if (!run(app, *path, err)) ctx.Warn(err); return; }  // ImportFile itself notifies Imported <path>
        app.ShowFileDialog("Import", kModelExts, false, [&app, run](const std::string& file) { std::string err; if (!run(app, file, err)) app.Notify(err); });
      }), CommandStatus::Implemented,
      "Imports a model file. For .obj: Units=<unit> names the unit of the file's coordinates, Scale=<factor> multiplies them, UpAxis=Y turns a Y-up file to the document's Z up.");
  Reg(e, "Export", OnSelection("Select objects to export", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Application& app = ctx.App();
        for (ObjectId id : ids) ctx.Doc().Select(id, true);
        std::string scheme_input;
        MeshExportOptions mesh_options;
        bool mesh_options_bad = false;
        if (auto p = TakePathAndVersion(ctx, scheme_input, &mesh_options, &mesh_options_bad)) {
          if (mesh_options_bad) return;  // already warned; nothing written
          if (LowerExt(*p) == ".3dm") {
            int dm_version = 0;
            if (!scheme_input.empty() && !ParseDm3dmVersion(ctx, "Export", scheme_input, dm_version)) return;
            std::string err;
            bool ok = app.ExportSelected(*p, err, dm_version);
            if (!ok) ctx.Warn(err); else ctx.Print("Exported " + *p + (dm_version == 0 ? "" : " (Version=" + std::to_string(dm_version) + ")"));
            return;
          }
          std::string normalized;
          if (!scheme_input.empty()) {
            normalized = NormalizeAcadScheme(scheme_input);
            if (normalized.empty()) { ctx.Warn("Export: unrecognized Version=" + scheme_input + " (see AcadSchemes for valid values)"); return; }
          }
          std::string err;
          bool ok = false;
          const std::string ext = LowerExt(*p);
          const bool is_mesh_ext = ext == ".stl" || ext == ".obj" || ext == ".ply";
          if (mesh_options.group_by_layer && ext != ".obj") { ctx.Warn("Export: Group=Layer applies to .obj files only"); return; }
          if (mesh_options.per_object && !is_mesh_ext) { ctx.Warn("Export: PerObject=Yes applies to .stl, .obj and .ply files only"); return; }
          std::vector<std::string> written;
          {
            ScopedMeshExportOptions scoped(mesh_options);
            if (mesh_options.per_object) {
              ExportWithSchemeOverride(ctx.Doc(), normalized, [&] { ok = ExportPerObject(ctx, *p, written, err) > 0; });
            } else {
              ExportWithSchemeOverride(ctx.Doc(), normalized, [&] { ok = app.ExportSelected(*p, err); });
            }
          }
          if (!ok) { ctx.Warn(err.empty() ? "Export: nothing to export" : err); return; }
          if (mesh_options.per_object) {
            ctx.Print("Exported " + std::to_string(written.size()) + " files, one per object:");
            for (const std::string& w : written) ctx.Print("  " + w);
          } else {
            ctx.Print("Exported " + *p + (normalized.empty() ? "" : " (Version=" + normalized + ")"));
            if (is_mesh_ext) for (const std::string& line : LastMeshExportReport()) ctx.Print(line);
          }
          return;
        }
        app.ShowFileDialog("Export selected", kExportExts, true, [&app](const std::string& path) { std::string err; if (!app.ExportSelected(path, err)) app.Notify(err); else app.Notify("Exported " + path); });
      }), CommandStatus::Implemented,
      "A Version=13/14/2000/2004/2010/2013/2018 token overrides the document's AcadSchemes default just for this "
      "DWG/DXF export; for a .3dm path, Version=<archive number> (e.g. 4, 5, 50, 60, 70, 80) instead targets that "
      "exact .3dm archive version so older Rhino releases can open it; any other extension ignores it; with no "
      "path token, opens the export dialog instead.");
  Reg(e, "ExportSelected", OnSelection("Select objects to export", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Application& app = ctx.App();
        for (ObjectId id : ids) ctx.Doc().Select(id, true);
        app.ShowFileDialog("Export selected", kExportExts, true, [&app](const std::string& path) { std::string err; if (!app.ExportSelected(path, err)) app.Notify(err); else app.Notify("Exported " + path); });
      }));
  // STLOut / IgesExport: the fixed-format export commands of AutoCAD (STLOUT, IGESEXPORT). They write the selection to one
  // format only: the file name must end in the command's extension (.stl; .igs or .iges), otherwise nothing is written and the
  // command says why. With no path token a file dialog asks for the file. StlOut also takes Export's mesh tokens
  // (Format=Ascii|Binary|BinaryBigEndian, Units=, Scale=, Tolerance=); both announce "Exported <path>" when the file is done.
  Reg(e, "StlOut", OnSelection("Select solids or meshes to write as STL", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Application& app = ctx.App();
        for (ObjectId id : ids) ctx.Doc().Select(id, true);
        std::string unused_scheme;
        MeshExportOptions mesh_options;
        bool mesh_options_bad = false;
        if (auto p = TakePathAndVersion(ctx, unused_scheme, &mesh_options, &mesh_options_bad)) {
          if (mesh_options_bad) return;
          if (LowerExt(*p) != ".stl") { ctx.Warn("StlOut: " + *p + " is not a .stl file name; nothing written"); return; }
          std::string err;
          bool ok = false;
          {
            ScopedMeshExportOptions scoped(mesh_options);
            ok = app.ExportSelected(*p, err);
          }
          if (!ok) { ctx.Warn(err.empty() ? "StlOut: nothing to export" : err); return; }
          ctx.Print("Exported " + *p);
          for (const std::string& line : LastMeshExportReport()) ctx.Print(line);
          return;
        }
        app.ShowFileDialog("Write STL file", {".stl"}, true, [&app](const std::string& path) { std::string err; if (!app.ExportSelected(path, err)) app.Notify(err); else app.Notify("Exported " + path); });
      }), CommandStatus::Implemented,
      "Writes the selected solids, surfaces and meshes to a stereolithography .stl file (binary by default; Format=Ascii, Units=, Scale= and Tolerance= as for Export).");
  Reg(e, "IgesExport", OnSelection("Select objects to write as IGES", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Application& app = ctx.App();
        for (ObjectId id : ids) ctx.Doc().Select(id, true);
        std::string unused_scheme;
        if (auto p = TakePathAndVersion(ctx, unused_scheme)) {
          const std::string ext = LowerExt(*p);
          if (ext != ".igs" && ext != ".iges") { ctx.Warn("IgesExport: " + *p + " is not a .igs or .iges file name; nothing written"); return; }
          std::string err;
          if (!app.ExportSelected(*p, err)) ctx.Warn(err.empty() ? "IgesExport: nothing to export" : err); else ctx.Print("Exported " + *p);
          return;
        }
        app.ShowFileDialog("Write IGES file", {".igs", ".iges"}, true, [&app](const std::string& path) { std::string err; if (!app.ExportSelected(path, err)) app.Notify(err); else app.Notify("Exported " + path); });
      }), CommandStatus::Implemented,
      "Writes the selection to a new .igs or .iges file (the same IGES writer as Export) and announces it when done.");
  Reg(e, "ExportWithOrigin", Make<ExportWithOriginCommand>(), CommandStatus::Implemented, "Writes copies of the selection re-expressed in the frame of the picked point and the active construction plane (the picked point lands at 0,0,0, the plane's axes become X, Y, Z; a world plane is a plain translation) before writing them; SVG/PDF (page-space view drawings, not object-space geometry) export the plain view instead.");
  // ExportStepAp242: a separate command from Export/SaveAs's own .stp/.step
  // dispatch (which stays AP214, unchanged, so no existing round trip
  // breaks) because AP214 and AP242 share the same file extension - this is
  // the only way to pick AP242's own tessellated-geometry schema on write.
  // Import/Open need no equivalent command: ImportStep (io/FileIgesStep.cpp)
  // auto-detects AP242 from the file's own FILE_SCHEMA and reads it back
  // via ImportStepAp242 either way.
  Reg(e, "ExportStepAp242", OnSelection("Select objects to export", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Application& app = ctx.App();
        Document& doc = ctx.Doc();
        for (ObjectId id : ids) doc.Select(id, true);
        auto run = [&doc](const std::string& path) -> std::pair<bool, std::string> { std::string err; bool ok = ExportStepAp242(doc, path, true, err); return {ok, err}; };
        if (auto p = ctx.Engine().TakePendingInput()) { auto [ok, err] = run(*p); if (!ok) ctx.Warn(err); else ctx.Print("Exported " + *p); return; }
        app.ShowFileDialog("Export selected (STEP AP242)", {".stp", ".step"}, true, [&app, run](const std::string& path) { auto [ok, err] = run(path); if (ok) app.Notify("Exported " + path); else app.Notify(err); });
      }), CommandStatus::Implemented,
      "Writes the selection as a real STEP AP242 (ISO 10303-242) file - COORDINATES_LIST/TRIANGULATED_FACE tessellated "
      "geometry, not AP214's B-rep entities - tessellating Breps/Surfaces/SubDs exactly like ExportIfc. Plain Export/"
      "SaveAs to .stp/.step still writes AP214.");
  Reg(e, "Exit", Immediate([](CommandContext& ctx) { ctx.App().RequestQuit(); }));
  // Notes ["text"]: with text, replaces the document notes (saved in the 3dm); without, opens the Notes panel. Either way the
  // current notes are reported on one line so a script can read them back.
  Reg(e, "Notes", Immediate([](CommandContext& ctx) {
        if (auto tok = ctx.Engine().TakePendingInput()) { ctx.Doc().Notes() = *tok; ctx.Doc().Touch(); }
        else ctx.App().Panels().notes = true;
        ctx.Print("Notes: " + (ctx.Doc().Notes().empty() ? std::string("(empty)") : ctx.Doc().Notes()));
      }), CommandStatus::Implemented, "Notes [\"text\"]: sets the notes saved with the model (3dm), or opens the Notes panel when no text is typed; always reports the current notes.");
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
  // Units [Millimeters|Centimeters|Meters|Inches|Feet]: with a name, sets the model units (long or singular names and the abbreviations
  // mm cm m in ft are accepted); without, opens Document Properties. Either way one line reports units and both tolerances.
  Reg(e, "Units", Immediate([](CommandContext& ctx) {
        DocumentSettings& s = ctx.Settings();
        if (auto tok = ctx.Engine().TakePendingInput()) {
          std::string w = *tok;
          for (char& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          static const char* const kNames[][3] = {{"Millimeters", "millimeter", "mm"}, {"Centimeters", "centimeter", "cm"}, {"Meters", "meter", "m"},
                                                  {"Inches", "inch", "in"}, {"Feet", "foot", "ft"}};
          const char* found = nullptr;
          for (const auto& n : kNames) {
            std::string full = n[0];
            for (char& c : full) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (w == full || w == n[1] || w == n[2]) found = n[0];
          }
          if (!found) { ctx.Warn("Units: unknown unit system '" + *tok + "' (use Millimeters, Centimeters, Meters, Inches or Feet)"); return; }
          s.unit_system = found;
          ctx.Doc().Touch();
        } else {
          ctx.App().Panels().document_properties = true;
        }
        ctx.Print("Units: " + s.unit_system + ", absolute tolerance " + FormatNumber(s.absolute_tolerance) + ", angle tolerance " + FormatNumber(s.angle_tolerance_degrees) + " degrees");
      }), CommandStatus::Implemented, "Units [name]: sets the model unit system (Millimeters, Centimeters, Meters, Inches, Feet), or opens Document Properties when no name is typed; always reports units and tolerances.");
  // SetTolerance <absolute> [angle degrees]: the document's absolute tolerance (at least 1e-8) and, when given, angle tolerance (at least 0.001).
  Reg(e, "SetTolerance", Immediate([](CommandContext& ctx) {
        DocumentSettings& s = ctx.Settings();
        auto number = [&](const std::string& t, double& out) { char* end = nullptr; const double v = std::strtod(t.c_str(), &end); if (end == t.c_str() || *end) return false; out = v; return true; };
        double absolute = s.absolute_tolerance, angle = s.angle_tolerance_degrees;
        std::optional<std::string> a = ctx.Engine().TakePendingInput();
        std::optional<std::string> g = ctx.Engine().TakePendingInput();
        if (a && !number(*a, absolute)) { ctx.Warn("SetTolerance: expected a number for the absolute tolerance, got '" + *a + "'"); return; }
        if (g && !number(*g, angle)) { ctx.Warn("SetTolerance: expected a number for the angle tolerance, got '" + *g + "'"); return; }
        if (a) { s.absolute_tolerance = std::max(1e-8, absolute); if (g) s.angle_tolerance_degrees = std::max(0.001, angle); ctx.Doc().Touch(); }
        ctx.Print("Units: " + s.unit_system + ", absolute tolerance " + FormatNumber(s.absolute_tolerance) + ", angle tolerance " + FormatNumber(s.angle_tolerance_degrees) + " degrees");
      }), CommandStatus::Implemented, "SetTolerance <absolute> [angle degrees]: sets the document absolute tolerance and optionally the angle tolerance; with no number only reports them.");
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
  // DigitalSign: signs an already-exported file with this machine's RSA-2048
  // signing key (generated once, on first use, at
  // <ConfigDirectory()>/signing_key.json), writing a "<path>.sig" sidecar
  // next to it - see io/DigitalSignature.h for the actual RSA/SHA-256/
  // PKCS#1v1.5 scheme. The path argument names any file on disk (typically
  // one this app just exported), not a Dino 8 document.
  Reg(e, "DigitalSign", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        auto sign = [&app](const std::string& path) {
          RsaPrivateKey key;
          std::string err;
          if (!LoadOrCreateSigningKey(key, err)) { app.Notify("DigitalSign: " + err); return; }
          if (!WriteSignatureSidecar(path, key, err)) { app.Notify("DigitalSign: " + err); return; }
          app.Notify("DigitalSign: wrote " + path + ".sig");
        };
        if (auto p = ctx.Engine().TakePendingInput()) { sign(*p); return; }
        app.ShowFileDialog("Select a file to sign", {}, false, sign);
      }), CommandStatus::Implemented,
      "Writes path.sig next to the given file, an RSA-2048/SHA-256/PKCS#1v1.5 signature from this machine's own "
      "signing key (generated on first use). VerifySignature checks it back.");
  // VerifySignature: checks a file against the "<path>.sig" sidecar
  // DigitalSign wrote for it (any matching .sig, from this machine or
  // another - verification only needs the sidecar's own embedded public
  // key, not this machine's private one). Reports the signer's public-key
  // fingerprint on success so it can be compared against a value the
  // signer published elsewhere - this alone proves nothing about identity,
  // the same "no certificate authority here" honesty VerifySignatureSidecar
  // itself documents.
  Reg(e, "VerifySignature", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        auto verify = [&app](const std::string& path) {
          std::string fingerprint, err;
          if (!VerifySignatureSidecar(path, "", fingerprint, err)) { app.Notify("VerifySignature: FAILED (" + err + ")"); return; }
          app.Notify("VerifySignature: OK, signed by key fingerprint " + fingerprint.substr(0, 16) + "...");
        };
        if (auto p = ctx.Engine().TakePendingInput()) { verify(*p); return; }
        app.ShowFileDialog("Select a file to verify", {}, false, verify);
      }), CommandStatus::Implemented, "Verifies a file against the path.sig sidecar DigitalSign wrote for it.");
}

}  // namespace dino8::app
