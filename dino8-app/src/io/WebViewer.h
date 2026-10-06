// A self-contained HTML + WebGL model viewer export - PARITY_MAP.md's
// "Cloud model viewer / app builder (ShapeDiver equivalent)" item.
//
// Honestly scoped: this is the "viewer" half only, and a local one at that -
// a single static .html file with every object's geometry and color baked
// in, no external script/CDN/network dependency of any kind (it renders
// identically whether opened from disk or served from a web host). It is
// NOT the "cloud" half (no hosting, no sharable URL this code produces on
// its own) and NOT the "app builder" half (no exposed parametric inputs a
// viewer visitor could tweak to rebuild the model - this is a frozen
// snapshot, not a live app). See io/WebViewer.cpp's own top comment for the
// exact construction.
#pragma once

#include <string>

#include "doc/Document.h"

namespace dino8::app {

// Exports every visible object in `doc` (or, if `selected_only`, every
// selected one) as one self-contained .html file at `path`. Point/curve/
// surface/brep/mesh/SubD objects are each tessellated into triangles the
// same way ExportMeshFile's OBJ/STL path already does (same per-kind
// tolerance choices); a kind with nothing to show (an empty document, or a
// selection with no exportable geometry) returns false with `error` set
// rather than writing a viewer with nothing in it.
bool ExportWebViewer(const Document& doc, const std::string& path, bool selected_only, std::string& error);

}  // namespace dino8::app
