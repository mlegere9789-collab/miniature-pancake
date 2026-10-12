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
// `archive_version` (default 0, meaning "newest this OpenNURBS build
// knows how to write", every call site before this parameter existed)
// is passed straight through to ONX_Model::Write - a plain OpenNURBS
// archive version number (2/3/4/5, or any multiple of 10 from 50 up),
// not an AcadSchemes-style string key.
bool Save3dm(const Document& doc, const std::string& path, std::string& error, bool include_reference_objects = true,
             int archive_version = 0);
bool ImportMeshFile(Document& doc, const std::string& path, std::string& error);
bool ExportMeshFile(const Document& doc, const std::string& path, bool selected_only, std::string& error);

}  // namespace dino8::app
