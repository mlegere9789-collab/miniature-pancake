// A self-contained HTML + WebGL model viewer export - PARITY_MAP.md's
// "Cloud model viewer / app builder (ShapeDiver equivalent)" item.
//
// Honestly scoped: this is the "viewer" half, and a local one at that -
// a single static .html file with every object's geometry and color baked
// in, no external script/CDN/network dependency of any kind (it renders
// identically whether opened from disk or served from a web host). It is
// NOT the "cloud" half (no hosting, no sharable URL this code produces on
// its own). The "app builder" half now has a real, if narrow, slice closed:
// each exported part gets its own Scale slider a viewer visitor can drag to
// rebuild the model's own geometry (not just orbit a frozen snapshot) - a
// transform-only rebuild about that part's own centroid, not a re-run of
// its original construction parameters, and no way to change a part's
// actual shape, only its scale. See io/WebViewer.cpp's own top comment for
// the exact construction.
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
