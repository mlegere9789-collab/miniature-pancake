// Worksession: attaching other .3dm files as locked, read-only reference
// models (Rhino's Worksession command / panel), with LimitReferenceModel
// restricting a model to the objects inside a picked box, and a session
// .rws JSON file that lists the attached paths so a session can be saved
// and restored (Rhino's own worksession files, kept as plain-text JSON
// here rather than Rhino's binary format).
#pragma once

#include <string>

#include "doc/Document.h"

namespace dino8::app {

// Loads `path` and copies its objects into `doc` as one reference model:
// locked (greyed via the existing locked-object display tint), tagged
// "Dino8.Reference" = `path` so Save skips them again, on a new locked
// layer named for the file. When `has_limit` is set, only objects whose
// bounding box intersects [limit_min, limit_max] are copied - real
// filtering, not a whole-file load followed by hiding.
// Returns the number of objects attached, or -1 on error (see `error`).
int AttachWorksession(Document& doc, const std::string& path, std::string& error, bool has_limit = false,
                     kernel::Point3d limit_min = kernel::Point3d(0, 0, 0),
                     kernel::Point3d limit_max = kernel::Point3d(0, 0, 0));

// Removes every object of the reference model matching `alias_or_path`
// (case-insensitive path or alias; "*"/"all" matches every attached
// model) and its ReferenceModels() entry. Returns the objects removed.
int DetachWorksession(Document& doc, const std::string& alias_or_path);

// Re-filters an already-attached model down to the objects that
// intersect [limit_min, limit_max] (LimitReferenceModel run after the
// fact); the rest are removed from the document. Returns the number of
// objects removed.
int LimitWorksessionModel(Document& doc, const std::string& alias_or_path, kernel::Point3d limit_min,
                         kernel::Point3d limit_max);

// Re-reads `alias_or_path`'s matching reference model(s) ("*"/"all"
// reloads every attached model) from their source file on disk and
// replaces their currently-attached objects with what the file now
// contains - the manual refresh AttachWorksession's own header comment
// names as missing (there is still no live link: nothing watches the
// source file, so an edit there has no effect until this is called).
// The model's own alias, layer and limit box (if any) are preserved and
// re-applied to the freshly-loaded objects. Returns the number of
// objects now attached across every model reloaded (post-reload, not a
// delta), or -1 on error (see `error`) when `alias_or_path` names no
// attached model, or isn't "*"/"all" and its one file fails to load -
// in either of those two failure cases nothing is changed. Reloading
// "*"/"all" instead skips (and reports via `error`, continuing with the
// rest) any one model whose file fails to load, leaving that one
// model's prior objects attached unchanged.
int ReloadWorksession(Document& doc, const std::string& alias_or_path, std::string& error);

// The session file: a JSON array of {"path", "alias"} for every currently
// attached reference model.
bool SaveWorksessionFile(const Document& doc, const std::string& path, std::string& error);
// Attaches every model listed in `path`'s .rws file that isn't already
// attached (matched by path). Returns the number of models attached (not
// objects - see AttachWorksession's return for that).
int LoadWorksessionFile(Document& doc, const std::string& path, std::string& error);

}  // namespace dino8::app
