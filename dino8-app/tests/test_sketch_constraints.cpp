// Unit test for sketch/Constraints.cpp's SolveAll() unknown-count guard.
//
// dino8.constraints is stored as plain document user text (see
// Constraints.h's own doc comment) - a string field a crafted/corrupted
// .3dm can set to anything LoadConstraints is willing to parse.
// LoadConstraints only checks that each constraint carries enough
// points/radius_objects for its own type (see its own doc comment); it
// never caps how many *distinct* constraints - and therefore how many
// distinct solver unknowns - the document can list, even when every one
// of them references an object that doesn't actually exist. Before
// SolveAll's own kMaxSketchSolveVariables guard, that unknown count (n)
// fed straight into an n*n Levenberg-Marquardt normal-equations matrix,
// allocated and Gaussian-eliminated (O(n^2)/O(n^3)) up to 60 times -
// unbounded CPU/memory from nothing but a document's own user text, the
// same "untrusted file count reaches unbounded work" class already fixed
// for OFF/LAS/PLY import elsewhere in this codebase.
//
// This test builds a constraint list with more distinct radius-object
// unknowns than any real sketch would ever need (but still small enough
// that even the unguarded O(n^3) solve finishes in well under a second,
// so this stays a fast, deterministic test rather than a timing gamble)
// and checks that SolveAll refuses it outright instead of attempting the
// solve.
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "doc/Document.h"
#include "sketch/Constraints.h"

using dino8::app::ConstructionPlane;
using dino8::app::Document;
using dino8::app::ObjectId;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

// Builds a "dino8.constraints" blob (see Constraints.cpp's SaveConstraints
// for the exact shape) holding `count` independent Radius constraints,
// each against its own nonexistent object id - the cheapest possible way
// to inflate SolveAll's own distinct-unknown count (one radius unknown per
// constraint, no shared points to collide on) without needing any real
// scene geometry at all.
std::string BuildManyRadiusConstraints(int count) {
  std::ostringstream out;
  out << "[";
  for (int i = 0; i < count; ++i) {
    out << (i ? "," : "") << "{\"id\":" << (i + 1)
        << ",\"type\":\"Radius\",\"value\":5,\"fx\":0,\"fy\":0,\"fz\":0,\"points\":[],"
           "\"radius_objects\":[" << (1000 + i) << "]}";
  }
  out << "]";
  return out.str();
}
}  // namespace

int main() {
  Document doc;
  const int kOverLimit = 300;  // above Constraints.cpp's kMaxSketchSolveVariables (256)
  doc.UserText()["dino8.constraints"] = BuildManyRadiusConstraints(kOverLimit);

  const std::vector<dino8::sketch::Constraint> loaded = dino8::sketch::LoadConstraints(doc);
  Check(static_cast<int>(loaded.size()) == kOverLimit,
        "LoadConstraints accepts every well-formed Radius constraint regardless of count");

  ConstructionPlane cplane;
  std::vector<std::string> report;
  const bool ok = dino8::sketch::SolveAll(doc, cplane, report);
  Check(!ok, "SolveAll refuses a constraint list with more unknowns than its own cap");
  bool mentions_too_many = false;
  for (const std::string& line : report) {
    if (line.find("Too many constraint unknowns") != std::string::npos) mentions_too_many = true;
  }
  Check(mentions_too_many, "SolveAll's report names the too-many-unknowns guard, not a real solve attempt");

  // A list comfortably under the cap still solves normally - the guard
  // must not reject ordinary, legitimately-sized sketches.
  Document small_doc;
  const int kUnderLimit = 10;
  small_doc.UserText()["dino8.constraints"] = BuildManyRadiusConstraints(kUnderLimit);
  std::vector<std::string> small_report;
  const bool small_ok = dino8::sketch::SolveAll(small_doc, cplane, small_report);
  Check(small_ok, "SolveAll still solves an ordinary, well-under-the-cap constraint list");

  std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
