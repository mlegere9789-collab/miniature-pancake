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
bool Save3dm(const Document& doc, const std::string& path, std::string& error, bool include_reference_objects = true);
bool ImportMeshFile(Document& doc, const std::string& path, std::string& error);
bool ExportMeshFile(const Document& doc, const std::string& path, bool selected_only, std::string& error);

}  // namespace dino8::app
