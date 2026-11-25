// Native .3dm read/write (through OpenNURBS' ONX_Model) plus OBJ / STL
// mesh import and export.
#pragma once

#include <string>
#include <vector>

#include "doc/Document.h"

namespace dino8::app {

bool Load3dm(Document& doc, const std::string& path, std::string& error);
// `include_reference_objects` (default true, matching every call site
// before Worksession existed) writes every object; passing false - what
// Application::SaveDocument's plain Save uses - skips objects tagged
// "Dino8.Reference" (Worksession's attached, read-only reference models),
// so a reference model is not silently duplicated into a document that
// merely referenced it, only exported when asked for explicitly.
// `version` (default 0) is the target .3dm archive version, passed straight
// through to `ONX_Model::Write()` - 0 means "current/latest", or a caller
// can name an exact archive version (2, 3, 4, 5, 50, 60, 70, 80, ...; see
// that function's own doc comment in opennurbs_extensions.h for the full
// version/Rhino-release table) to write a file older Rhino releases can
// open, same as the kernel's own `Model::Save(path, version)` already does.
// An out-of-range `version` is rejected with a named error rather than
// silently substituted - see Save3dm's own definition for why that matters.
bool Save3dm(const Document& doc, const std::string& path, std::string& error, bool include_reference_objects = true, int version = 0);
bool ImportMeshFile(Document& doc, const std::string& path, std::string& error);
bool ExportMeshFile(const Document& doc, const std::string& path, bool selected_only, std::string& error);

// Options of the mesh export formats (.stl / .obj / .ply), set from the Export command's tokens
// (Format=Ascii|Binary|BinaryBigEndian, Scale=<factor>, Units=<name>, Tolerance=<chord>, PerObject=Yes, Group=Layer|Object for OBJ) and in effect for the
// ExportMeshFile / ExportPly calls made while a ScopedMeshExportOptions is alive; every field's default is what these
// exporters always did (STL binary, PLY ascii, no scaling, chord tolerance 0.01, one file).
enum class MeshExportFormat { Default, Ascii, Binary, BinaryBigEndian };
struct MeshExportOptions {
  MeshExportFormat format = MeshExportFormat::Default;  // STL: Default = Binary; PLY: Default = Ascii; OBJ is always text
  double scale = 1.0;                                    // positions multiplied by this (document units -> file units)
  double chord_tolerance = 0.01;                         // tessellation chord tolerance of Breps and Surfaces, in document units
  bool per_object = false;                               // one file per object (the Export command writes <stem>_<n>_<name>.<ext>)
  bool group_by_layer = false;                           // OBJ: one "o" group per layer (its objects merged) instead of one per object
  std::string units_name;                                // the Units= name as typed, for the file header / report; empty = the document's unit
};
// What the last mesh export (STL: solid check and unit; OBJ: unit) has to report, one line per entry; cleared at the start of every
// ExportMeshFile call. The Export and StlOut commands print it after "Exported <path>".
const std::vector<std::string>& LastMeshExportReport();
const MeshExportOptions& ActiveMeshExportOptions();
class ScopedMeshExportOptions {
 public:
  explicit ScopedMeshExportOptions(const MeshExportOptions& options);
  ~ScopedMeshExportOptions();
  ScopedMeshExportOptions(const ScopedMeshExportOptions&) = delete;
  ScopedMeshExportOptions& operator=(const ScopedMeshExportOptions&) = delete;

 private:
  MeshExportOptions saved_;
};
// Options of the OBJ import, set from the Import command's tokens (Units=<name>, Scale=<factor>, UpAxis=Y|Z) and in effect for the
// ImportMeshFile calls made while a ScopedMeshImportOptions is alive. A coordinate (x, y, z) of the file becomes scale * (x, y, z) in
// document units, or scale * (x, -z, y) when the file's up axis is Y (a +90 degree turn about X: the file's Y axis becomes the
// document's Z axis). The defaults are what the importer always did: no scaling, Z up.
struct MeshImportOptions {
  double scale = 1.0;
  bool y_up = false;
};
const MeshImportOptions& ActiveMeshImportOptions();
class ScopedMeshImportOptions {
 public:
  explicit ScopedMeshImportOptions(const MeshImportOptions& options);
  ~ScopedMeshImportOptions();
  ScopedMeshImportOptions(const ScopedMeshImportOptions&) = delete;
  ScopedMeshImportOptions& operator=(const ScopedMeshImportOptions&) = delete;

 private:
  MeshImportOptions saved_;
};
// Multiplier that converts a length written in `units` (the file's unit) to the document's unit system; false when the name is not
// recognised (same names as MeshExportUnitFactor).
bool MeshImportUnitFactor(const Document& doc, const std::string& units, double& factor);

// Multiplier that converts a length in the document's unit system to `units` ("mm", "cm", "m", "km", "in", "ft", "mi", "um",
// or the full unit names the document settings use); false when the name is not recognised.
bool MeshExportUnitFactor(const Document& doc, const std::string& units, double& factor);

}  // namespace dino8::app
