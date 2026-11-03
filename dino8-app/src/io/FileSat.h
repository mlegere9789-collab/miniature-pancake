// ACIS SAT (.sat, text save file) exchange - a thin app layer over the kernel's
// ReadAcisSat/WriteAcisSat (dino8-kernel/src/acis_sat.cpp). Read
// dino8-kernel/docs/ACIS_SAT_NOTES.md for exactly what is verified against
// public documentation and what is only self-consistent: no real ACIS-written
// file was available when this was written, so every summary printed here says
// so instead of presenting the format support as proven.
#pragma once

#include <string>

#include "doc/Document.h"

namespace dino8::app {

// Reads every body of a .sat file as a Brep object. Entities whose field
// layouts are documented in public prose (planes, straight curves, topology
// records) are tried first; if the file also holds cone/sphere/torus/ellipse
// entities (layouts that are UNVERIFIED, from recollection) the file is read a
// second time with those layouts allowed and the summary says which bodies
// depend on them. On success `summary` is a multi-line report (counts per
// body, the verified/unverified note, every refusal with its reason); on
// failure it holds the refusal reason(s).
bool ImportSat(Document& doc, const std::string& path, std::string& summary);

// Writes the (selected, visible) Brep objects as an ACIS 7.0 text save file.
// Objects the writer cannot express (meshes, curves, SubDs, NURBS-surface
// bodies, ...) are left out and listed with a reason; if nothing can be
// written the call fails. On success `error` is NOT an error: it holds the
// multi-line summary (what was written, which layouts are UNVERIFIED, what
// was left out and why) for the caller to print.
bool ExportSat(const Document& doc, const std::string& path, bool selected_only, std::string& error);

}  // namespace dino8::app
