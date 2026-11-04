#pragma once

// ACIS text save-file (.sat) and binary save-file (.sab) reader and writer -
// written clean-room from the record layouts listed in docs/ACIS_SAT_NOTES.md
// (read that file for exactly which public documents were used, what could and
// could not be verified, and which layouts are assumptions).
//
// SAB status, stated plainly: NO public prose describing the SAB byte layout
// was reachable (the only public document used mentions .sab by name and gives
// no layout). Every SAB token tag value, the header layout and the
// bool/enum encoding are therefore UNVERIFIED recollection and first-principles
// choices, listed in acis_sat.cpp ("SAB wire format") and in the notes. The SAB
// writer is tested against THIS reader only; its output has never been read by
// any other product and must not be described as ACIS-compatible.
//
// Round-1 scope: solid and sheet bodies made of planes, cones (including
// cylinders), spheres and tori, bounded by straight and circular/elliptical
// edges. Everything else is REFUSED with a reason string - a body that cannot
// be converted faithfully is never returned in a degraded form.

#include <iosfwd>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"

namespace dino8::kernel {

// Only the layouts documented in the public sources listed in
// docs/ACIS_SAT_NOTES.md are accepted by default (bodies, lumps, shells, faces,
// loops, coedges, edges, vertices, points, plane-surface, straight-curve).
// cone-surface / sphere-surface / torus-surface / ellipse-curve are NOT in
// those sources; their field layouts here come from recollection and are
// UNVERIFIED, so they are refused unless the caller opts in.
struct AcisSatOptions {
  bool allow_unverified_layouts = false;
};

struct AcisSatReadResult {
  // One Brep per `body` entity that converted completely.
  std::vector<Brep> bodies;
  // Non-fatal notes: skipped bodies (with the reason), tolerated oddities.
  std::vector<std::string> warnings;
  // False when the file was refused outright (bad/missing header, unsupported
  // version, truncated data) or when it contained bodies but none could be
  // converted; `error` then says why. True when at least one body converted.
  bool ok = false;
  std::string error;
};

struct AcisSatWriteResult {
  bool ok = false;
  std::string error;
  std::vector<std::string> warnings;
};

// Parses a .sat text file. Never throws for malformed/unsupported input; the
// reason is returned in `error` / `warnings`.
AcisSatReadResult ReadAcisSat(const std::string& path, const AcisSatOptions& options = {});
AcisSatReadResult ReadAcisSat(std::istream& in, const AcisSatOptions& options = {});

// Writes the bodies as an ACIS 7.0 (version 700) text save file. A body whose
// geometry cannot be expressed with the supported entities (NURBS surfaces,
// non-circular edge curves, ...) makes the whole call fail with a reason
// naming the body and face; nothing is written in that case.
AcisSatWriteResult WriteAcisSat(const std::string& path, const std::vector<Brep>& bodies, const AcisSatOptions& options = {});
AcisSatWriteResult WriteAcisSat(std::ostream& out, const std::vector<Brep>& bodies, const AcisSatOptions& options = {});

// ---- Binary (.sab) ------------------------------------------------------
// Same entity/record layer as the text format (identical hardening, identical
// refusals); only the token encoding differs. Hostile input (bad signature,
// truncation, lengths or counts that exceed the data, unknown token tags,
// non-finite numbers, files over 128 MiB or over 2 Mi tokens / 1 Mi records,
// cyclic chains) is refused with a reason in `error` - never a crash. The tag
// table is UNVERIFIED (see the top of this header).
AcisSatReadResult ReadAcisSab(const std::string& path, const AcisSatOptions& options = {});
AcisSatReadResult ReadAcisSab(std::istream& in, const AcisSatOptions& options = {});

// Writes the bodies as a version-700 binary save file using the UNVERIFIED tag
// table. Same refusals as WriteAcisSat; nothing is written on refusal.
AcisSatWriteResult WriteAcisSab(const std::string& path, const std::vector<Brep>& bodies, const AcisSatOptions& options = {});
AcisSatWriteResult WriteAcisSab(std::ostream& out, const std::vector<Brep>& bodies, const AcisSatOptions& options = {});

// Detects the encoding from the first bytes (the "ACIS BinaryFile" signature
// means SAB, anything else is tried as SAT text) and reads accordingly.
AcisSatReadResult ReadAcis(const std::string& path, const AcisSatOptions& options = {});
AcisSatReadResult ReadAcis(std::istream& in, const AcisSatOptions& options = {});

// True when `bytes` (at least the first 15) begin with the SAB signature.
bool LooksLikeAcisSab(const std::string& bytes);

}  // namespace dino8::kernel
