// Native .3dm read/write (through OpenNURBS' ONX_Model) plus OBJ / STL
// mesh import and export.
#pragma once

#include <string>

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

}  // namespace dino8::app
