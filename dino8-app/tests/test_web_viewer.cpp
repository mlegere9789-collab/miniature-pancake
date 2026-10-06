// Unit test for PARITY_MAP.md's "Cloud model viewer / app builder
// (ShapeDiver equivalent)" item (src/io/WebViewer.h) - before this there
// was no web-viewer or embed code of any kind anywhere in the source.
//
// Checks the generated HTML's embedded geometry against an INDEPENDENTLY
// computed reference (the same MeshBrepClosed tessellation ExportWebViewer
// itself calls, run again here from scratch) rather than just asserting
// "some output was produced" - a genuine cross-check, the same discipline
// test_compute_server.cpp's own byte-level response checks use. A separate,
// real-browser check (tests/web_viewer_browser_check.js, driven through the
// pre-installed headless Chromium via Playwright) is NOT part of this
// ctest target - this repo's test toolchain is C++-only - but is kept as a
// reproducible, documented artifact; see that script's own comment.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <opennurbs.h>

#include "doc/Document.h"
#include "doc/SceneObject.h"
#include "dino8/kernel/brep.h"
#include "geom/BrepMesher.h"
#include "io/WebViewer.h"

using dino8::app::Document;
using dino8::app::ExportWebViewer;
using dino8::app::SceneObject;
using dino8::kernel::Brep;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

// A real, trimmed box brep - ON_BrepBox's own genuine topology (six real
// trim loops), the same construction cmd_solids.cpp's BoxCommand and
// tests/test_brep_mesher.cpp's own box fixture both use. kernel::Brep::Box()
// is deliberately NOT used here: its own header comment discloses it as
// six untrimmed NewFace(int) surfaces with no real trim loops at all - a
// narrow construction proven elsewhere (general_boolean_sweep.cpp) for the
// boolean pipeline specifically, not something MeshBrepFaces' trim-loop
// walking can tessellate (it legitimately produces zero faces for one -
// confirmed directly while writing this test, not assumed).
Brep MakeBoxBrep(double x0, double y0, double z0, double x1, double y1, double z1) {
  const ON_3dPoint corners[8] = {
      ON_3dPoint(x0, y0, z0), ON_3dPoint(x1, y0, z0), ON_3dPoint(x1, y1, z0), ON_3dPoint(x0, y1, z0),
      ON_3dPoint(x0, y0, z1), ON_3dPoint(x1, y0, z1), ON_3dPoint(x1, y1, z1), ON_3dPoint(x0, y1, z1)};
  ON_Brep* raw = ON_BrepBox(corners);
  Brep k;
  if (raw) { k.raw() = *raw; delete raw; }
  return k;
}

std::string ReadWholeFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Counts the comma-separated numbers inside the first `[...]` that follows
// `key` in `html` (e.g. key="positions:" finds "positions:[1,2,3]" and
// returns 3) - just enough ad hoc parsing to check the embedded data
// without pulling in a JSON parser for a test, the same "simple string
// finds, not a real parser" approach test_compute_server.cpp's own checks
// use for HTTP responses.
int CountArrayEntries(const std::string& html, const std::string& key) {
  const size_t key_pos = html.find(key);
  if (key_pos == std::string::npos) return -1;
  const size_t open = html.find('[', key_pos);
  const size_t close = html.find(']', open);
  if (open == std::string::npos || close == std::string::npos) return -1;
  const std::string inner = html.substr(open + 1, close - open - 1);
  if (inner.empty()) return 0;
  int commas = 0;
  for (char c : inner) if (c == ',') ++commas;
  return commas + 1;
}

size_t CountOccurrences(const std::string& haystack, const std::string& needle) {
  size_t count = 0, pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) { ++count; pos += needle.size(); }
  return count;
}

}  // namespace

int main() {
  // ---- Nothing to export: a point-only / empty document refuses --------
  {
    Document doc;
    std::string error;
    Check(!ExportWebViewer(doc, "unused.html", false, error), "an empty document is refused rather than writing an empty viewer");
    doc.Add(SceneObject::MakePoint(dino8::kernel::Point3d(1, 2, 3)));
    Check(!ExportWebViewer(doc, "unused.html", false, error),
          "a document with only a point (no triangle-mesh-able geometry) is refused");
  }
  {
    Document doc;
    doc.Add(SceneObject::MakeBrep(MakeBoxBrep(0, 0, 0, 1, 1, 1)));
    std::string error;
    Check(!ExportWebViewer(doc, "unused.html", /*selected_only=*/true, error),
          "Selected=Yes with nothing actually selected is refused, not silently exported empty");
  }

  // ---- One box: cross-check the embedded geometry against an
  // independently-recomputed reference tessellation -----------------------
  {
    Document doc;
    const Brep box = MakeBoxBrep(0, 0, 0, 2, 3, 4);
    doc.Add(SceneObject::MakeBrep(box));
    const std::string path = "test_web_viewer_box.html";
    std::string error;
    const bool exported_ok = ExportWebViewer(doc, path, false, error);
    const std::string msg = "ExportWebViewer writes a real file for a single box [error: " + error + "]";
    Check(exported_ok, msg.c_str());

    const std::string html = ReadWholeFile(path);
    Check(!html.empty(), "the written file is non-empty");
    Check(html.find("<canvas") != std::string::npos, "the output is a real HTML page with a <canvas> element");
    Check(html.find("script src=") == std::string::npos,
          "the output has no external <script src=...> - genuinely self-contained, not just claiming to be");
    Check(CountOccurrences(html, "color:[") == 1, "exactly one part for one object");

    dino8::app::BrepMeshOptions opt;
    opt.chord_tolerance = 0.01;
    const dino8::kernel::Mesh reference = dino8::app::MeshBrepClosed(box.raw(), opt);
    int expected_triangles = 0;
    for (int i = 0; i < reference.FaceCount(); ++i) expected_triangles += reference.raw().m_F[i].IsQuad() ? 2 : 1;

    const int got_vertices = CountArrayEntries(html, "positions:") / 3;
    const int got_triangles = CountArrayEntries(html, "indices:") / 3;
    Check(got_vertices == reference.VertexCount(),
          "the embedded vertex count matches an independently-recomputed tessellation of the same box exactly");
    Check(got_triangles == expected_triangles,
          "the embedded triangle count (quads counted as 2 triangles) matches the independent reference exactly");
    Check(got_vertices > 0 && got_triangles > 0, "sanity: the box actually produced real geometry, not an empty part");

    std::remove(path.c_str());
  }

  // ---- Two objects: two parts, Selected=Yes only exports the selection --
  {
    Document doc;
    const dino8::app::ObjectId id_a = doc.Add(SceneObject::MakeBrep(MakeBoxBrep(0, 0, 0, 1, 1, 1)));
    doc.Add(SceneObject::MakeBrep(MakeBoxBrep(5, 5, 5, 6, 6, 6)));
    const std::string path = "test_web_viewer_two.html";
    std::string error;
    Check(ExportWebViewer(doc, path, false, error), "ExportWebViewer writes a file for two objects");
    Check(CountOccurrences(ReadWholeFile(path), "color:[") == 2, "two objects become exactly two parts");
    std::remove(path.c_str());

    doc.Select(id_a, true);
    Check(ExportWebViewer(doc, path, /*selected_only=*/true, error), "Selected=Yes exports with exactly one object selected");
    Check(CountOccurrences(ReadWholeFile(path), "color:[") == 1, "Selected=Yes exports only the selected object, not both");
    std::remove(path.c_str());
  }

  // ---- A hidden object is skipped, same as every other exporter --------
  {
    Document doc;
    const dino8::app::ObjectId id = doc.Add(SceneObject::MakeBrep(MakeBoxBrep(0, 0, 0, 1, 1, 1)));
    doc.Find(id)->visible = false;
    std::string error;
    Check(!ExportWebViewer(doc, "unused.html", false, error), "a hidden object is excluded, leaving nothing to export");
  }

  // ---- Leaves a real fixture on disk for the separate, real-browser check
  // (tests/web_viewer_browser_check.py) to open - not cleaned up here, same
  // "leave the fixture for the next tool in the chain" shape
  // dwg_fixture_gen.c's own generated fixtures already have. ----------------
  {
    Document doc;
    doc.Add(SceneObject::MakeBrep(MakeBoxBrep(0, 0, 0, 1, 1, 1)));
    doc.Add(SceneObject::MakeBrep(MakeBoxBrep(2, 0, 0, 3, 1, 1)));
    std::string error;
    Check(ExportWebViewer(doc, "web_viewer_browser_fixture.html", false, error),
          "a real two-box fixture is written for the separate browser-level check");
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
