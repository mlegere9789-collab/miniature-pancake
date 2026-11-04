// DwgCompare / XrefCompare: a local, viewport-overlaid two-file geometric
// diff, in the spirit of AutoCAD's DWG Compare / Xref Compare, built on top
// of Dino 8's existing DWG/DXF/.3dm readers (io/FileExchange.h, io/File3dm.h)
// and the existing per-object colour-override mechanism every renderer path
// already checks (SceneObject::color_by_layer / Document::EffectiveColor -
// see viewport/Viewport.cpp) rather than a new rendering path.
//
// What this is NOT: a byte-exact, ID-tracking diff like the undo system's
// StateDelta (Document.h) gets for free within one session. Two
// independently-authored/re-exported files have no shared object identity,
// so matching objects across them is a heuristic - see "Matching heuristic"
// below and its disclosed limits.
#pragma once

#include <string>

#include "doc/Document.h"

namespace dino8::app {

// How many objects landed in each bucket after RunDwgCompare/RunXrefCompare.
struct CompareStats {
  int unchanged = 0;  // identical geometry+layer on both sides (not overlaid - nothing to show)
  int modified = 0;   // paired but geometry differs (tinted orange on the current/new object)
  int added = 0;      // only in the "new" set (tinted green)
  int removed = 0;    // only in the "old" set (copied in as a locked, dashed, red ghost)
};

// Loads `path` (any format Dino 8 already reads for Open: .3dm/.dxf/.dwg/
// .obj/.stl/.ply/.igs/.iges/.stp/.step) into `out`, which is a throwaway
// Document, not the current one - the same "load into a scratch Document,
// then copy what's wanted" shape AttachWorksession (session/Worksession.cpp)
// already uses for .3dm. Returns false and fills `error` on failure or an
// unrecognized extension.
bool LoadCompareSource(Document& out, const std::string& path, std::string& error);

// DwgCompare Path=<file>: diffs the CURRENTLY OPEN document (treated as the
// "new"/current version) against `path` (treated as the "old"/baseline
// version, loaded fresh into a scratch Document - the current document is
// never switched away from). Colours added/modified objects in place via
// the existing colour-override mechanism and adds locked, dashed ghost
// copies of removed objects on a dedicated "Compare: Removed" layer. Returns
// false and fills `error` if `path` can't be read.
//
// Matching heuristic (disclosed limits): object ids are per-document, so
// two independently loaded files cannot be diffed by id the way the
// in-session undo stack (StateDelta, Document.h) can. Instead:
//   1. Each candidate object gets a fingerprint from its kind, layer name,
//      and its tessellated geometry (Display().points/lines/triangles),
//      quantized to 1e-3 model units and order-normalized (sorted) so it is
//      tolerant of re-export noise, direction reversal and vertex-order
//      differences but sensitive to any real coordinate change.
//   2. Objects whose fingerprint matches exactly on both sides are
//      "unchanged" and are not touched or overlaid at all.
//   3. Remaining objects are paired by nearest bounding-box center among
//      same-layer candidates (greedy nearest-neighbor) into "modified"
//      pairs; anything left over is "added" (new-only) or "removed"
//      (old-only).
// This is a best-effort heuristic, not a true diff algorithm: a heavily
// edited object can easily fail to pair with its own former self (showing
// up as one "removed" + one "added" instead of one "modified"), and an
// object that happens to share a layer and a similar bounding box with an
// unrelated object can be paired with it incorrectly. Treat the overlay as
// a fast visual triage aid, not a certified change list.
bool RunDwgCompare(Document& doc, const std::string& path, std::string& error, CompareStats* stats = nullptr);

// XrefCompare Alias=<name-or-path>: Dino 8 has no live external-reference
// link (Worksession/AttachWorksession copies a referenced file's objects
// into the document once, tagged "Dino8.Reference" = its source path - see
// session/Worksession.h). So "compare the xref against its source" means:
// re-read that reference model's own source file fresh from disk right now,
// and diff it against the objects already copied into `doc` under that
// exact reference model (identified by ReferenceModel::path/alias), using
// the same fingerprint/pairing/colouring machinery as RunDwgCompare above.
// This deliberately does not duplicate RunDwgCompare's logic: an xref
// attachment is structurally just "another file's objects in the scene",
// so the compare is the identical algorithm scoped to one reference
// model's objects instead of the whole document. Returns false (with
// `error` set) if no attached reference model matches `alias_or_path`.
bool RunXrefCompare(Document& doc, const std::string& alias_or_path, std::string& error, CompareStats* stats = nullptr);

// Removes every object tagged by a previous RunDwgCompare/RunXrefCompare
// (the "Dino8.CompareOverlay" removed-ghosts and the "Compare: Removed"
// layer) and restores added/modified objects' original by-layer colouring,
// clearing their "Dino8.CompareState" tag. Returns the number of objects
// touched (ghosts removed + tinted objects restored).
int ClearDwgCompare(Document& doc);

}  // namespace dino8::app
