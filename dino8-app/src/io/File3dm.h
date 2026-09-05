// Native .3dm read/write (through OpenNURBS' ONX_Model) plus OBJ / STL
// mesh import and export.
#pragma once

#include <string>

#include "doc/Document.h"

namespace dino8::app {

bool Load3dm(Document& doc, const std::string& path, std::string& error);
bool Save3dm(const Document& doc, const std::string& path, std::string& error);
bool ImportMeshFile(Document& doc, const std::string& path, std::string& error);
bool ExportMeshFile(const Document& doc, const std::string& path, bool selected_only, std::string& error);

}  // namespace dino8::app
