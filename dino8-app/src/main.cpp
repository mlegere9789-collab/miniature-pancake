// Dino 8 entry point: creates the window and GL context, initialises ImGui
// with docking, and runs the application frame loop.
//
// Command line:
//   --smoke N     render N frames and exit (used by headless QC under Xvfb)
//   --script FILE run each line of FILE as a command after start-up.
//                 Given without --smoke, this is the supported *batch
//                 scripting* mode: the window is created hidden (still
//                 needs a GL context - a real display, or Xvfb+llvmpipe on
//                 Linux CI/servers, per docs/BATCH_SCRIPTING.md), the file's
//                 commands run once each, and the process exits on its own
//                 the instant the script finishes (exit code 0, or 2 if an
//                 `@expect_*` check failed) instead of falling into the
//                 normal interactive loop. Combine with --smoke N to also
//                 render/capture frames (the pre-existing headless QC use).
//   --screenshot FILE.ppm   save the final frame (used with --smoke)
//   --stress N    add N simple boxes to a fresh document, time object
//                 creation / display-mesh warmup / viewport picking / an
//                 undo snapshot, print one "stress: ..." line, then exit
//                 (or continue into --smoke's frame loop if both are given).
//                 See tests/stress.sh and tests/performance_notes.md.
//   --cull-test N     build a small near cluster plus N far-away boxes,
//                     frame the camera on the near cluster only, render it
//                     once and print one "cull_test: ..." line reporting
//                     Viewport::DrawObjects' frustum-cull candidate count
//                     (respects DINO8_DISABLE_FRUSTUM_CULL). Pair with
//                     --cull-screenshot to also capture the render. See
//                     tests/cull_test.sh.
//   --cull-screenshot FILE.bmp   with --cull-test: write the viewport's
//                 own render (Viewport::CaptureToFile) to FILE.bmp.
//   --serve PORT  start the minimal compute server (net/ComputeServer.h) on
//                 PORT (0 asks the OS for a free ephemeral port - see the
//                 "serve: listening on port N" line this prints) and run
//                 headless, exactly like a pure --script batch run: each
//                 POST /run request's body is run as a Lua script, and each
//                 POST /run/python request's body as a Python script
//                 (dino8 module), against the same LuaEngine/PythonEngine
//                 the command line uses, with the captured print() output
//                 coming back as the response body. See docs/COMPUTE_SERVER.md
//                 and PARITY_MAP.md's "Cloud/network compute service" item.
//   --serve-max-requests N   with --serve: exit after N requests have been
//                 serviced instead of running until killed (used by
//                 tests/smoke.sh for a deterministic, self-terminating run).
//   --serve-token [NAME:]TOKEN   with --serve: require every request to
//                 carry a matching "Authorization: Bearer TOKEN" header,
//                 rejecting any other request with 401 before it ever
//                 reaches the script engine. Omit for the previous, fully
//                 open behavior. Repeatable: each occurrence registers one
//                 more acceptable token, so distinct callers can each carry
//                 their own (real per-caller credentials, not one shared
//                 secret for everyone - still no rotation/expiry/revocation,
//                 and still no TLS between the token and the wire). A
//                 "NAME:" prefix on a token names its caller (e.g.
//                 "--serve-token ci:abc123"), echoed back as "caller" in a
//                 /run[/python] JSON response (Accept: application/json) so
//                 a caller can confirm which credential authenticated it;
//                 a bare TOKEN with no prefix works exactly as before
//                 (anonymous, no "caller" field in the JSON response).
//
// Script lines starting with '@' are synthetic input for UI tests:
//   @move X Y | @down [button] | @up [button] | @click X Y [button]
//   @world VIEW X Y Z      move the mouse to a world point in a viewport
//   @clickworld VIEW X Y Z click a world point in a viewport
//   @forcehoverfeed X Y Z TOKEN   test-only: force CommandEngine's hover
//                 point to X,Y,Z and immediately feed TOKEN as typed text,
//                 bypassing ImGui's own viewport-hover detection (see its
//                 own comment below for why)
//   @drag X0 Y0 X1 Y1      left-drag (window/crossing select)
//   @key NAME | @text STR | @wait N | @expect_selected N | @expect_objects N
//   FILE.3dm      open a model on start-up

#if defined(_MSC_VER)
// Windows crash reporting: SetUnhandledExceptionFilter (installed first
// thing in main()) prints the exception code and faulting address of any
// exception left unhandled on any thread to stderr before the process dies,
// so a crash in a --smoke/--script run shows up as a real line in the CI
// log instead of a silent non-zero exit. WIN32_LEAN_AND_MEAN/NOMINMAX and
// including windows.h before GLFW's own header is the standard order that
// avoids APIENTRY/CALLBACK macro redefinition conflicts.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gl/gl_loader.h"
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "app/Application.h"
#include "app/Settings.h"
#include "doc/Document.h"
#include "net/ComputeServer.h"
#include "platform/Accessibility.h"
#include "platform/Clipboard.h"
#include "plugins/PluginPanel.h"
#include "ui/Panels.h"
#include "ui/Theme.h"
#include "util/ThreadPool.h"
#include "util/json_mini.h"
#include "viewport/Viewport.h"

namespace {

void GlfwErrorCallback(int code, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

std::string ExeDir(const char* argv0) {
  std::error_code ec;
  std::filesystem::path p = std::filesystem::absolute(argv0, ec);
  if (ec) return ".";
  return p.parent_path().string();
}

// ScreenCaptureToFile: writes the whole default framebuffer (every viewport
// and panel, as just composited by ImGui) as a 24-bit BMP. `rgb` is w*h*3
// bytes, bottom-up (matching glReadPixels), which is also how BMP rows are
// ordered, so no flip is needed here.
bool WriteWindowCaptureBmp(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const int row = (w * 3 + 3) & ~3;
  const unsigned int data_size = static_cast<unsigned int>(row) * static_cast<unsigned int>(h);
  const unsigned int file_size = 54 + data_size;
  unsigned char hdr[54] = {'B', 'M'};
  auto put32 = [&](int at, unsigned int v) { for (int i = 0; i < 4; ++i) hdr[at + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff); };
  auto put16 = [&](int at, unsigned int v) { hdr[at] = static_cast<unsigned char>(v & 0xff); hdr[at + 1] = static_cast<unsigned char>((v >> 8) & 0xff); };
  put32(2, file_size); put32(10, 54); put32(14, 40); put32(18, static_cast<unsigned int>(w)); put32(22, static_cast<unsigned int>(h));
  put16(26, 1); put16(28, 24); put32(34, data_size);
  std::fwrite(hdr, 1, 54, f);
  std::vector<unsigned char> line(static_cast<size_t>(row), 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const unsigned char* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
      line[static_cast<size_t>(x) * 3] = p[2]; line[static_cast<size_t>(x) * 3 + 1] = p[1]; line[static_cast<size_t>(x) * 3 + 2] = p[0];
    }
    std::fwrite(line.data(), 1, line.size(), f);
  }
  std::fclose(f);
  return true;
}

// Builds a single unit-cube mesh box centered at `center` - the stress
// test's per-object geometry. Deliberately not the full BoxCommand path in
// cmd_solids.cpp (Brep + trims + tolerance-driven tessellation): the point
// of --stress is to measure what the document/viewport/undo layers cost at
// N objects, not to re-benchmark solid modelling, so each object is as
// cheap as a real object can be while still exercising a real DisplayCache
// (triangles + normals) through SceneObject::EnsureDisplay.
dino8::app::SceneObject MakeStressBox(dino8::kernel::Point3d center, double half_size) {
  dino8::kernel::Mesh m;
  ON_Mesh& r = m.raw();
  const double x[2] = {center.x - half_size, center.x + half_size};
  const double y[2] = {center.y - half_size, center.y + half_size};
  const double z[2] = {center.z - half_size, center.z + half_size};
  for (int k = 0; k < 2; ++k)
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i) r.SetVertex(k * 4 + j * 2 + i, ON_3dPoint(x[i], y[j], z[k]));
  const int f[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
  for (int i = 0; i < 6; ++i) r.SetQuad(i, f[i][0], f[i][1], f[i][2], f[i][3]);
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return dino8::app::SceneObject::MakeMesh(m);
}

// Runs the --stress workload against `app`'s current (freshly-cleared)
// document: N boxes on a compact 3D grid, timing exactly the four things
// tests/performance_notes.md reports on:
//   - create_ms:  Document::Add x N (object list growth, not geometry cost)
//   - warmup_ms:  SceneObject::EnsureDisplay x N, parallelized with
//                 dino8::app::ParallelFor unless DINO8_DISABLE_PARALLEL_WARMUP
//                 is set (each object's mutable display cache is its own -
//                 see util/ThreadPool.h for why that is safe to parallelize)
//   - pick_ms:    average Viewport::PickObject latency over kPickSamples
//                 hover-style picks, accelerated by the ObjectGrid in
//                 spatial/ObjectGrid.h unless DINO8_DISABLE_PICK_GRID is set
//   - undo_ms:    a single Document::BeginChange call - the full-document
//                 snapshot Undo takes before every command, which stays
//                 O(objects) regardless of anything in this file (see the
//                 "still doesn't scale" section of performance_notes.md)
void RunStressTest(dino8::app::Application& app, int object_count) {
  using Clock = std::chrono::steady_clock;
  auto elapsed_ms = [](Clock::time_point since) {
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
  };

  dino8::app::Document& doc = app.Doc();
  doc.Clear();

  // A compact cube grid: side^3 >= object_count, boxes 1 unit apart center
  // to center (0.4 half-size, so neighbours don't touch) - dense enough
  // that a pick ray genuinely has to thread through many candidate cells,
  // not spread so far apart that every object lands in its own grid cell.
  const int side = std::max(1, static_cast<int>(std::ceil(std::cbrt(static_cast<double>(object_count)))));
  const auto create_start = Clock::now();
  int placed = 0;
  for (int k = 0; k < side && placed < object_count; ++k) {
    for (int j = 0; j < side && placed < object_count; ++j) {
      for (int i = 0; i < side && placed < object_count; ++i) {
        const dino8::kernel::Point3d center(i * 1.0, j * 1.0, k * 1.0);
        doc.Add(MakeStressBox(center, 0.4));
        ++placed;
      }
    }
  }
  const double create_ms = elapsed_ms(create_start);

  const bool serial_warmup = std::getenv("DINO8_DISABLE_PARALLEL_WARMUP") != nullptr;
  std::vector<dino8::app::SceneObject>& objects = doc.Objects();
  const auto warmup_start = Clock::now();
  if (serial_warmup) {
    for (dino8::app::SceneObject& o : objects) o.EnsureDisplay(0.02, 0.05);
  } else {
    dino8::app::ParallelFor(objects.size(), [&](std::size_t i) { objects[i].EnsureDisplay(0.02, 0.05); });
  }
  const double warmup_ms = elapsed_ms(warmup_start);

  dino8::app::Viewport* vp = app.ActiveViewport();
  double pick_ms = -1.0;
  if (vp) {
    vp->GetCamera().ZoomExtents(dino8::kernel::BoundingBox{dino8::kernel::Point3d(-1, -1, -1),
                                                            dino8::kernel::Point3d(side + 1.0, side + 1.0, side + 1.0)},
                                vp->Aspect());
    constexpr int kPickSamples = 200;
    const auto pick_start = Clock::now();
    for (int s = 0; s < kPickSamples; ++s) {
      // Sweep across the viewport rather than picking the same pixel every
      // time, so the grid's DDA march visits a realistically varied set of
      // cells instead of one memoized path.
      const double px = (s % 40) * 20.0 + 5.0;
      const double py = ((s / 40) % 20) * 20.0 + 5.0;
      vp->PickObject(doc, px, py, 6.0);
    }
    pick_ms = elapsed_ms(pick_start) / kPickSamples;
  }

  const auto undo_start = Clock::now();
  doc.BeginChange("StressUndoSnapshot");
  const double undo_ms = elapsed_ms(undo_start);

  std::printf("stress: objects=%d create_ms=%.3f warmup_ms=%.3f pick_ms=%.4f undo_ms=%.3f grid=%s parallel_warmup=%s\n",
              placed, create_ms, warmup_ms, pick_ms, undo_ms,
              std::getenv("DINO8_DISABLE_PICK_GRID") ? "off" : "on", serial_warmup ? "off" : "on");
}

// --cull-test N: an honest, same-binary-shaped proof (like --stress's grid
// A/B above) that Viewport::DrawObjects' frustum cull (Viewport.cpp, see
// the "Frustum culling" section and tests/performance_notes.md's "No LOD
// or frustum culling" item it closes) only removes draw calls, never
// removes anything that should still be visible.
//
// tests/cull_test.sh runs this binary twice - once normally, once with
// DINO8_DISABLE_FRUSTUM_CULL=1 (the escape hatch Viewport.cpp's cull
// checks, which forces every object back onto every frame's candidate
// list, i.e. exactly the pre-cull behaviour in the same binary) - and
// checks that:
//   (a) the printed "cull_test:" line's candidate count is far below the
//       object count with the cull on, and exactly equal to it with the
//       cull disabled (proof the cull is actually doing something
//       measurable, not just a no-op broad phase);
//   (b) the two runs' `screenshot_path` files (this viewport's own render,
//       written via Viewport::CaptureToFile, not the whole composited
//       ImGui window) are pixel-identical (proof nothing that should be
//       visible went missing, and nothing that shouldn't be visible
//       appeared, regardless of what the cull skipped).
//
// A fixed 27-box cluster sits at the origin - what the camera below
// actually frames with ZoomExtents - while `far_count` more boxes sit a
// million units away, guaranteed outside that frustum; a passing run's
// candidate count should land near 27 (plus a little grid-cell slack),
// not near far_count + 27.
void RunCullTest(dino8::app::Application& app, int far_count, const std::string& screenshot_path) {
  dino8::app::Document& doc = app.Doc();
  doc.Clear();

  constexpr int kVisibleSide = 3;  // 27 boxes: small, but enough for a real grid with several cells.
  for (int k = 0; k < kVisibleSide; ++k)
    for (int j = 0; j < kVisibleSide; ++j)
      for (int i = 0; i < kVisibleSide; ++i) doc.Add(MakeStressBox(dino8::kernel::Point3d(i * 1.0, j * 1.0, k * 1.0), 0.4));
  const int visible_count = kVisibleSide * kVisibleSide * kVisibleSide;
  const dino8::kernel::BoundingBox visible_box{dino8::kernel::Point3d(-1, -1, -1),
                                                dino8::kernel::Point3d(kVisibleSide + 1.0, kVisibleSide + 1.0, kVisibleSide + 1.0)};

  constexpr double kFarOffset = 1.0e6;  // world units from the origin - well outside the frustum framed below
  for (int n = 0; n < far_count; ++n) {
    doc.Add(MakeStressBox(dino8::kernel::Point3d(kFarOffset + (n % 100) * 2.0, kFarOffset + (n / 100) * 2.0, kFarOffset), 0.4));
  }

  dino8::app::Viewport* vp = app.ActiveViewport();
  if (!vp) { std::printf("cull_test: total=0 candidates=0 visible_expected=%d far_count=%d error=no_active_viewport\n", visible_count, far_count); return; }
  vp->SetMode(dino8::app::DisplayMode::Shaded);
  // Frame only the near cluster - not doc's full extent, which would also
  // include the far boxes and defeat the point of this test.
  vp->GetCamera().ZoomExtents(visible_box, vp->Aspect());

  dino8::app::Viewport::FrameContext ctx = app.MakeFrameContext();
  vp->Render(app.Renderer(), ctx);
  const dino8::app::Viewport::FrustumCullStats stats = vp->LastFrustumCullStats();

  if (!screenshot_path.empty()) {
    std::string err;
    if (!vp->CaptureToFile(screenshot_path, err)) std::fprintf(stderr, "cull_test: screenshot failed: %s\n", err.c_str());
  }

  std::printf("cull_test: total=%zu candidates=%zu visible_expected=%d far_count=%d cull=%s\n",
              stats.total_objects, stats.draw_candidates, visible_count, far_count,
              std::getenv("DINO8_DISABLE_FRUSTUM_CULL") ? "off" : "on");
}

// Compares two strings in time independent of where they first differ, so
// the compute server's --serve-token check (the only gate in front of
// arbitrary script execution) doesn't leak the token one byte at a time to
// another local process timing repeated guesses against the loopback port.
bool ConstantTimeEquals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  return diff == 0;
}

// Reads one boolean-ish query parameter ("geometry=1" or "geometry=true")
// out of a raw HTTP query string ("a=1&geometry=1&b=2") - not a general
// query-string parser (no URL-decoding, no repeated-key handling), just
// enough for GET /objects's own single `?geometry=1` flag below.
bool ComputeQueryFlagSet(const std::string& query, const std::string& key) {
  size_t pos = 0;
  while (pos < query.size()) {
    const size_t amp = query.find('&', pos);
    const std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
    const size_t eq = pair.find('=');
    if (eq != std::string::npos && pair.substr(0, eq) == key) {
      const std::string value = pair.substr(eq + 1);
      if (value == "1" || value == "true") return true;
    }
    if (amp == std::string::npos) break;
    pos = amp + 1;
  }
  return false;
}

// Reads a 3-element JSON array ("[x,y,z]") into a Point3d for POST
// /objects below - false (p left unchanged) if `v` isn't an array of
// exactly 3 numbers, the same honest-refusal shape the rest of that
// route uses rather than defaulting unset coordinates to 0.
bool ComputeJsonPoint3d(const dino8::json::Value& v, dino8::kernel::Point3d& p) {
  if (!v.IsArray() || v.Size() != 3) return false;
  for (size_t i = 0; i < 3; ++i) if (v[i].type != dino8::json::Value::Type::Number) return false;
  p = dino8::kernel::Point3d(v[0].number, v[1].number, v[2].number);
  return true;
}

// Minimal string escaping for the compute server's JSON responses (GET
// /objects, and /run[/python]'s Accept: application/json form) - the same
// local, dependency-free convention every other JSON writer in this
// codebase already uses (see doc/BlockInstances.cpp, drafting/Table.cpp,
// io/DigitalSignature.cpp), rather than a single shared writer none of
// them centralize either.
std::string ComputeJsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
        else out += static_cast<char>(c);
    }
  }
  return out;
}

}  // namespace

#if defined(_MSC_VER)
namespace {
LONG WINAPI Dino8UnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
  std::fprintf(stderr, "UNHANDLED EXCEPTION: code=0x%08lX address=%p\n",
               static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
               info->ExceptionRecord->ExceptionAddress);
  std::fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace
#endif

int main(int argc, char** argv) {
#if defined(_MSC_VER)
  // See the windows.h include comment near the top of this file: report any
  // unhandled exception to stderr before dying. Installed first so it also
  // covers GLFW/GL init.
  SetUnhandledExceptionFilter(Dino8UnhandledExceptionFilter);
#endif
  // Unbuffered stdout/stderr: when --smoke/--script is piped (never a TTY),
  // the CRT fully buffers stdout by default on both glibc and MSVC, so any
  // crash (segfault/access violation, no atexit) silently discards every
  // line printed since the buffer last flushed instead of surfacing them -
  // exactly the failure mode that made a real Windows-only crash further
  // down the command pipeline look like "the process produced zero output
  // then exited non-zero" in CI logs. Line-buffered still batches syscalls
  // reasonably (one flush per printed line, not one per crash), but nothing
  // observable is ever lost to a crash again.
  std::setvbuf(stdout, nullptr, _IOLBF, 4096);
  std::setvbuf(stderr, nullptr, _IOLBF, 4096);
  int smoke_frames = -1;
  int stress_count = -1;
  int cull_test_far_count = -1;
  std::string script_path;
  std::string open_path;
  std::string screenshot_path;
  std::string cull_screenshot_path;
  int serve_port = -1;
  int serve_max_requests = -1;
  // Each --serve-token occurrence becomes one (name, token) pair - name
  // empty for a bare "TOKEN" with no "NAME:" prefix. Checked in order, first
  // match wins (see compute_handler below); an empty vector means the
  // server stays fully open, exactly as when there was only ever one
  // optional shared token.
  std::vector<std::pair<std::string, std::string>> serve_tokens;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--smoke") == 0 && i + 1 < argc) smoke_frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--stress") == 0 && i + 1 < argc) stress_count = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--cull-test") == 0 && i + 1 < argc) cull_test_far_count = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--cull-screenshot") == 0 && i + 1 < argc) cull_screenshot_path = argv[++i];
    else if (std::strcmp(argv[i], "--script") == 0 && i + 1 < argc) script_path = argv[++i];
    else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot_path = argv[++i];
    else if (std::strcmp(argv[i], "--serve") == 0 && i + 1 < argc) serve_port = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--serve-max-requests") == 0 && i + 1 < argc) serve_max_requests = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--serve-token") == 0 && i + 1 < argc) {
      const std::string spec = argv[++i];
      const size_t colon = spec.find(':');
      if (colon == std::string::npos) serve_tokens.emplace_back("", spec);
      else serve_tokens.emplace_back(spec.substr(0, colon), spec.substr(colon + 1));
    }
    else if (std::strcmp(argv[i], "--version") == 0) { std::printf("Dino 8 %s\n", DINO8_VERSION); return 0; }
    else if (argv[i][0] != '-') open_path = argv[i];
  }
  // --stress implies headless/offscreen like --smoke, unless --smoke was
  // also given explicitly (then the caller wants to see stress objects in
  // the following smoke frames too). A handful of frames run first so
  // ImGui has laid out real viewport sizes (PickObject needs a non-1x1
  // Viewport::Aspect()) before RunStressTest fires on frame 3, mirroring
  // the `frame > 2` gate the script runner below already uses for the
  // same reason. --cull-test behaves the same way, for the same reason
  // (RunCullTest also needs a real Viewport::Aspect() for ZoomExtents).
  const bool stress_only = (stress_count >= 0 || cull_test_far_count >= 0) && smoke_frames < 0;
  if (stress_only) smoke_frames = 4;

  // A real, 100%-reproducible Windows CI hang has been seen starting right
  // here: a process launched this way produced *zero* further output (not
  // even a crash message) until the job's own timeout killed it - see
  // RHINO8_KILLER_AUDIT.md row L. stdout/stderr are already unbuffered
  // above, so if this line is ever missing from a hung run's log, the hang
  // is before glfwInit(); if it's present but startup finished (below)
  // never prints, the hang is inside glfwInit()/glfwCreateWindow()/the GL
  // context/ImGui setup that follows.
  if (smoke_frames >= 0 || !script_path.empty()) std::fprintf(stderr, "dino8: starting GLFW init\n");
  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit()) {
    std::fprintf(stderr, "Could not initialise GLFW\n");
    return 1;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  // 4x MSAA is a rendering-quality nicety, not something headless/offscreen
  // QC needs - and some CI runners' virtual/software GPU can't satisfy an
  // NSGL pixel format request that combines it with a 3.3 core profile at
  // all (confirmed on GitHub's macOS Actions runners: "NSGL: Failed to
  // find a suitable pixel format" with samples=4, which resolves clean
  // with samples=0), so skip it in --smoke/--stress/--cull-test mode
  // rather than fail to even open a window.
  if (smoke_frames < 0) glfwWindowHint(GLFW_SAMPLES, 4);
  // Hidden window for --smoke (headless QC) and for pure --script batch runs
  // alike - a batch job has no user to look at a window, and creating one
  // visible would steal focus / flash on screen for the run's duration.
  if (smoke_frames >= 0 || !script_path.empty()) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  // Windows (and X11): size the window in screen pixels scaled by the
  // monitor's content scale, and let GLFW rescale it when it is dragged to
  // a monitor with a different DPI. The process is per-monitor-v2 DPI
  // aware (resources/dino8.manifest), so without this hint the OS would
  // hand the window the same pixel count on a 200% monitor and the UI
  // would render at half size there. ImGui's font atlas is still built
  // once at the startup scale (see ui_scale below); a mid-session DPI
  // change rescales the framebuffer but not the font - a known remaining
  // gap. Ignored on platforms without per-monitor scaling (macOS).
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

  if (smoke_frames >= 0 || !script_path.empty()) std::fprintf(stderr, "dino8: GLFW initialised, creating window\n");
  GLFWwindow* window = glfwCreateWindow(1600, 900, "Dino 8", nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "Could not create an OpenGL 3.3 window\n");
    glfwTerminate();
    return 1;
  }
  if (smoke_frames >= 0 || !script_path.empty()) std::fprintf(stderr, "dino8: window created, loading GL\n");
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  if (!dino8::gl::Load()) {
    std::fprintf(stderr, "Could not load OpenGL functions: %s\n", dino8::gl::LastError());
    return 1;
  }
  glfwMaximizeWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  // Window layout persists in the user's config directory - not in smoke
  // runs, and not in batch --script runs either (a hidden, unattended batch
  // job has no real layout to save, and must never clobber the user's own
  // saved interactive layout.ini).
  const bool interactive_run = smoke_frames < 0 && script_path.empty() && serve_port < 0;
  const std::string ini_path = dino8::app::ConfigDirectory() + "/layout.ini";
  const bool has_layout = interactive_run && std::filesystem::exists(ini_path);
  io.IniFilename = interactive_run ? ini_path.c_str() : nullptr;
  float xscale = 1.0f, yscale = 1.0f;
  glfwGetWindowContentScale(window, &xscale, &yscale);
  float ui_scale = xscale > 0 ? xscale : 1.0f;
  dino8::app::ApplyDinoTheme(ui_scale);
  ImFontConfig font_cfg;
  font_cfg.SizePixels = 16.0f * ui_scale;
  io.Fonts->AddFontDefault(&font_cfg);
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  dino8::app::Application app;
  app.ui_scale = ui_scale;
  app.has_saved_layout = has_layout;
  app.native_window = window;
  // A pure --script run (no --smoke) is just as unattended as a --smoke
  // run - its window is hidden too (see the GLFW_VISIBLE hint above) - so it
  // needs the same headless treatment: ShowFileDialog's real OS file picker
  // blocks the calling thread until a human clicks it (Application.cpp:557),
  // which would hang a batch job forever the first time a script line calls
  // a bare Save/Open with no path already supplied; ConfirmDiscard's
  // unsaved-changes prompt has the same problem. Batch scripts are expected
  // to always pass paths explicitly (matching how --script already has to
  // for --smoke QC scripts), so headless=true only changes behavior for the
  // case that would otherwise hang.
  app.headless = !interactive_run;
  app.smoke_mode = !interactive_run;
  std::string error;
  if (!app.Init(ExeDir(argv[0]), error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  dino8::app::ApplyDinoTheme(app.ui_scale, static_cast<dino8::app::ThemeMode>(app.theme_mode), app.accent_color);
  if (!error.empty()) std::fprintf(stderr, "warning: %s\n", error.c_str());

  // AT-SPI2 accessibility bridge for the command line (see
  // platform/Accessibility.h and docs/ACCESSIBILITY.md) - started here,
  // same as --smoke mode, so a headless run under Xvfb is just as
  // accessible-tree-queryable as an interactive one; the frame loop below
  // keeps its published text in sync every frame.
  dino8::platform::InitAccessibility("Dino8");

  // In --smoke and batch --script mode (no console for a human to watch,
  // this is what CI/automation reads): flush each history line to stdout
  // the instant CommandEngine records it, not after app.Frame()/Execute()
  // returns. A command's own Begin()/handler runs strictly *after*
  // CommandEngine::Print() already recorded "Command: X" but *before*
  // control ever gets back to the history_printed loop below - so a crash
  // inside a command's own logic (a real, hard, non-C++-exception crash the
  // DINO8_GUARD in CommandEngine.cpp cannot catch) used to produce zero
  // stdout output no matter how the earlier buffering/incremental-print
  // fixes were tuned, because that print-after-the-fact loop was simply
  // never reached. This makes the very next crashing CI/batch run show
  // exactly which command was executing at the moment of any such crash.
  if (!interactive_run) {
    // app.Init() above already ran the command catalog through Print() (the
    // "Command catalog: N commands loaded" line) before this hook existed
    // to catch it live - flush whatever's already in History() once, right
    // now, so it isn't silently dropped (the on_print_line guard on the
    // catch-up loops below only helps for lines recorded *after* this
    // point).
    for (const std::string& line : app.Engine().History()) {
      std::printf("history: %s\n", line.c_str());
    }
    std::fflush(stdout);
    app.Engine().on_print_line = [](const std::string& line) {
      std::printf("history: %s\n", line.c_str());
      std::fflush(stdout);
    };
  }

  if (!open_path.empty()) {
    std::string e;
    if (!app.OpenDocument(open_path, e)) std::fprintf(stderr, "%s\n", e.c_str());
  }
  std::vector<std::string> script_lines;
  if (!script_path.empty()) {
    std::ifstream in(script_path);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line[0] != '#') script_lines.push_back(line);
    }
  }
  size_t script_cursor = 0;
  int wait_frames = 0;
  // How much of app.Engine().History() has already been printed. Printing
  // was previously done in one single batch after the whole script/smoke
  // run finished (see the old loop at the bottom of this function) - which
  // meant a crash anywhere during script execution produced ZERO stdout
  // output no matter how stdout was buffered, because the printf calls
  // themselves were never reached, not because output was lost in a
  // buffer. That made every mid-script crash (see e.g. the Windows-only
  // DWG round-trip crash tests/smoke.sh once hit) completely undiagnosable
  // from CI logs. Printing each new history line as soon as it appears
  // means a crash always shows every command that ran before it.
  // Starts at History().size(), not 0: the block above already flushed
  // everything recorded up to and including hook registration.
  size_t history_printed = app.Engine().History().size();

  // --serve: the minimal compute server (net/ComputeServer.h). Started
  // here, after app.Init() (so app.Engine() already exists) and before the
  // frame loop, exactly like script_lines above is prepared before the
  // loop feeds it one line per frame. compute_handler runs a POST /run
  // request's body as a Lua script against the running document's own
  // LuaEngine, or a POST /run/python request's body as a Python script
  // against PythonEngine - the same two engines the command line and
  // RunScript/RunPythonScript already use - and returns the captured
  // print() output as the response. A script that suspends on an
  // rs.Get*/dino8.Get*-style prompt (either engine - see script/
  // PythonEngine.h for how PythonEngine's own worker-thread suspend works)
  // can't be satisfied over a synchronous HTTP request, so that case is
  // aborted (Abort()) and reported as a compute error in the response body
  // instead of hanging the connection. Each --serve-token given requires a
  // matching "Authorization: Bearer TOKEN" header on every request (any one
  // of the registered tokens, each optionally named - "NAME:TOKEN" - so
  // distinct callers can each hold their own), checked here before either
  // engine ever sees the body.
  dino8::app::ComputeServer compute_server;
  int serve_requests_handled = 0;
  if (serve_port >= 0) {
    std::string serve_error;
    if (!compute_server.Start(serve_port, serve_error)) {
      std::fprintf(stderr, "%s\n", serve_error.c_str());
      return 1;
    }
    std::printf("serve: listening on port %d\n", compute_server.Port());
    std::fflush(stdout);
  }
  // JSON-wraps a script run's result when the request asked for it (an
  // `Accept: application/json` header, checked by the caller below) instead
  // of the plain `print()`-output-as-body every caller got before this -
  // PARITY_MAP.md's own disclosed "no geometry (de)serialization format at
  // all - a script gets and returns plain text" gap, narrowed for the
  // response side here and for GET /objects (below) on the request side.
  // Still plain text by default: a caller that never asks for JSON sees no
  // behavior change at all.
  auto json_wrap_output = [](bool ok, const std::vector<std::string>& output, const std::string& error_line, const std::string& caller_name) {
    std::string body = "{\"ok\":";
    body += ok ? "true" : "false";
    body += ",\"output\":[";
    for (size_t i = 0; i < output.size(); ++i) {
      if (i) body += ',';
      body += '"';
      body += ComputeJsonEscape(output[i]);
      body += '"';
    }
    body += ']';
    if (!error_line.empty()) { body += ",\"error\":\""; body += ComputeJsonEscape(error_line); body += '"'; }
    if (!caller_name.empty()) { body += ",\"caller\":\""; body += ComputeJsonEscape(caller_name); body += '"'; }
    body += "}\n";
    return body;
  };
  const dino8::app::ComputeHandler compute_handler = [&app, &serve_tokens, json_wrap_output](const dino8::app::HttpRequest& req) {
    dino8::app::HttpResponse resp;
    // TryParseHttpRequest hands back the request-line path verbatim,
    // query string and all ("/objects?geometry=1") - split it once here so
    // every route comparison below matches on the bare path, with `query`
    // available for GET /objects's own `?geometry=1` flag.
    const size_t qpos = req.path.find('?');
    const std::string path = qpos == std::string::npos ? req.path : req.path.substr(0, qpos);
    const std::string query = qpos == std::string::npos ? std::string() : req.path.substr(qpos + 1);
    const bool is_run = path == "/run" || path == "/run/python";
    const bool is_objects = path == "/objects";
    if ((is_run && req.method != "POST") ||
        (is_objects && req.method != "GET" && req.method != "POST")) {
      resp.status = 405;
      resp.body = "Dino 8 compute service: /run and /run/python take POST, /objects takes GET or POST\n";
      return resp;
    }
    // Checked against every registered --serve-token in order (constant-time
    // per candidate, same as the single-token version always did) rather
    // than, say, hashing the header into a lookup map - there are at most a
    // handful of tokens for a local-automation tool like this, so a linear
    // scan costs nothing a caller would notice and keeps every comparison
    // genuinely constant-time against its own candidate, with no shared
    // table lookup leaking anything about *which* candidate it resembles.
    std::string caller_name;  // stays "" for an unauthenticated server or an anonymous (no "NAME:") token
    if (!serve_tokens.empty()) {
      const auto it = req.headers.find("authorization");
      bool matched = false;
      if (it != req.headers.end()) {
        for (const auto& [name, token] : serve_tokens) {
          if (ConstantTimeEquals(it->second, "Bearer " + token)) { caller_name = name; matched = true; break; }
        }
      }
      if (!matched) {
        resp.status = 401;
        resp.body = "Dino 8 compute service: missing or incorrect Authorization: Bearer token\n";
        return resp;
      }
    }
    // GET /objects: a real, if minimal, structured geometry wire format -
    // the id/type/name/layer/bounding-box of every object currently in the
    // running document, as JSON - rather than only the plain print() text
    // /run[/python] return. `?geometry=1` additionally carries each
    // object's own geometry for four kinds: point (coordinates), mesh
    // (vertices/faces), curve (degree/control points/weights/knots), and
    // surface (the same shape as curve, in each of its two directions) -
    // the exact NURBS definition in both the curve and surface cases,
    // enough to reconstruct the object exactly, not just sampled points.
    // Every other kind (polysurface, SubD, point cloud) still gets
    // "geometry":null - a Brep's multiple trimmed faces (each with its own
    // trimming curves) and a SubD's control-cage topology are a
    // substantially larger undertaking than one untrimmed surface's single
    // control grid, not attempted here. POST /objects (right below) closes
    // the other half - sending one of these same four shapes back in adds
    // that exact geometry to the document, the first way to get geometry
    // *into* this server as structured data rather than Lua/Python source.
    if (path == "/objects" && req.method == "GET") {
      const dino8::app::Document& doc = app.Doc();
      const bool want_geometry = ComputeQueryFlagSet(query, "geometry");
      std::string body = "[";
      bool first = true;
      for (const dino8::app::SceneObject& o : doc.Objects()) {
        if (!first) body += ',';
        first = false;
        const std::string layer = (o.layer_index >= 0 && o.layer_index < static_cast<int>(doc.Layers().size()))
                                       ? doc.LayerFullPath(o.layer_index)
                                       : "";
        body += "{\"id\":" + std::to_string(o.id) + ",\"type\":\"" + ComputeJsonEscape(dino8::app::ObjectKindName(o.kind)) +
                "\",\"name\":\"" + ComputeJsonEscape(o.name) + "\",\"layer\":\"" + ComputeJsonEscape(layer) + "\",\"bbox\":";
        dino8::kernel::BoundingBox bb;
        if (doc.BoundingBoxOf({o.id}, bb)) {
          body += "{\"min\":[" + std::to_string(bb.min.x) + "," + std::to_string(bb.min.y) + "," + std::to_string(bb.min.z) +
                  "],\"max\":[" + std::to_string(bb.max.x) + "," + std::to_string(bb.max.y) + "," + std::to_string(bb.max.z) + "]}";
        } else {
          body += "null";
        }
        if (want_geometry) {
          body += ",\"geometry\":";
          if (o.kind == dino8::app::ObjectKind::Point) {
            body += "{\"point\":[" + std::to_string(o.point.x) + "," + std::to_string(o.point.y) + "," + std::to_string(o.point.z) + "]}";
          } else if (o.kind == dino8::app::ObjectKind::Mesh && o.mesh) {
            const ON_Mesh& m = o.mesh->raw();
            std::string verts = "[";
            for (int i = 0; i < m.VertexCount(); ++i) {
              if (i) verts += ',';
              const ON_3dPoint v = m.Vertex(i);
              verts += "[" + std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z) + "]";
            }
            verts += ']';
            std::string faces = "[";
            for (int i = 0; i < m.FaceCount(); ++i) {
              if (i) faces += ',';
              const ON_MeshFace& f = m.m_F[i];
              faces += "[" + std::to_string(f.vi[0]) + "," + std::to_string(f.vi[1]) + "," + std::to_string(f.vi[2]);
              if (f.IsQuad()) faces += "," + std::to_string(f.vi[3]);
              faces += ']';
            }
            faces += ']';
            body += "{\"vertices\":" + verts + ",\"faces\":" + faces + "}";
          } else if (o.kind == dino8::app::ObjectKind::Curve && o.curve) {
            // A NURBS curve's degree/control points/weights/knots are
            // exactly the information OpenNURBS itself stores - enough to
            // reconstruct the curve exactly, not an approximation sampled
            // down to points the way the mesh/point payloads above are a
            // dead end for anything that needs the real curve back.
            const dino8::kernel::NurbsCurve& c = *o.curve;
            const bool rational = c.IsRational();
            std::string cvs = "[";
            for (int i = 0; i < c.ControlPointCount(); ++i) {
              if (i) cvs += ',';
              const dino8::kernel::Point3d p = c.ControlPointAt(i);
              cvs += "[" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + "]";
            }
            cvs += ']';
            std::string knots = "[";
            for (int i = 0; i < c.KnotCount(); ++i) {
              if (i) knots += ',';
              knots += std::to_string(c.KnotAt(i));
            }
            knots += ']';
            body += "{\"degree\":" + std::to_string(c.Degree()) + ",\"rational\":" + (rational ? "true" : "false") +
                    ",\"control_points\":" + cvs;
            if (rational) {
              std::string weights = "[";
              for (int i = 0; i < c.ControlPointCount(); ++i) {
                if (i) weights += ',';
                weights += std::to_string(c.WeightAt(i));
              }
              weights += ']';
              body += ",\"weights\":" + weights;
            }
            body += ",\"knots\":" + knots + "}";
          } else if (o.kind == dino8::app::ObjectKind::Surface && o.surface) {
            // A single untrimmed NURBS surface's own degree/control grid/
            // weights/knots in each of its two directions - the exact
            // OpenNURBS definition, same "reconstruct it exactly, not a
            // sampled approximation" contract the curve payload above
            // already has. `control_points` is flattened in the same
            // `u * v_count + v` order `NurbsSurface::FromControlGrid()`
            // itself takes (see that method's own comment) - POST
            // /objects below passes this array straight through to it, so
            // a GET round-tripped straight into a POST reconstructs the
            // identical surface, not just an equivalent-looking one.
            const dino8::kernel::NurbsSurface& s = *o.surface;
            const int cu = s.CVCountU(), cv = s.CVCountV();
            const bool rational = s.IsRational();
            std::string cvs = "[";
            for (int i = 0; i < cu; ++i) {
              for (int j = 0; j < cv; ++j) {
                if (i || j) cvs += ',';
                const dino8::kernel::Point3d p = s.ControlPointAt(i, j);
                cvs += "[" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + "]";
              }
            }
            cvs += ']';
            auto knot_array = [&](int direction) {
              std::string k = "[";
              for (int i = 0; i < s.KnotCount(direction); ++i) {
                if (i) k += ',';
                k += std::to_string(s.KnotAt(direction, i));
              }
              return k + ']';
            };
            body += "{\"degree_u\":" + std::to_string(s.DegreeU()) + ",\"degree_v\":" + std::to_string(s.DegreeV()) +
                    ",\"u_count\":" + std::to_string(cu) + ",\"v_count\":" + std::to_string(cv) +
                    ",\"rational\":" + (rational ? "true" : "false") + ",\"control_points\":" + cvs;
            if (rational) {
              std::string weights = "[";
              for (int i = 0; i < cu; ++i) {
                for (int j = 0; j < cv; ++j) {
                  if (i || j) weights += ',';
                  weights += std::to_string(s.WeightAt(i, j));
                }
              }
              weights += ']';
              body += ",\"weights\":" + weights;
            }
            body += ",\"knots_u\":" + knot_array(0) + ",\"knots_v\":" + knot_array(1) + "}";
          } else {
            body += "null";
          }
        }
        body += '}';
      }
      body += "]\n";
      resp.content_type = "application/json";
      resp.body = body;
      return resp;
    }
    // POST /objects: the other half of the geometry wire format GET
    // /objects?geometry=1 (above) reads - sends one of that same route's
    // own four shapes (point/mesh/curve/surface) back in as a JSON request
    // body and adds it to the document, the first way geometry can get *into*
    // this server as structured data rather than Lua/Python source text.
    // Uses the existing util/json_mini.h reader (previously only ever fed
    // trusted local config files - its own depth cap already bounds a
    // maliciously-nested body, and ComputeServer's 8 MiB request-size cap
    // already bounds the body's total size, so reusing it here against
    // network input adds no new unguarded trust boundary).
    if (path == "/objects" && req.method == "POST") {
      dino8::json::Value root;
      std::string parse_error;
      if (!dino8::json::Parse(req.body, root, parse_error) || !root.IsObject()) {
        resp.status = 400;
        resp.content_type = "application/json";
        resp.body = "{\"ok\":false,\"error\":\"malformed JSON body: " +
                    ComputeJsonEscape(parse_error.empty() ? "expected a JSON object" : parse_error) + "\"}\n";
        return resp;
      }
      const std::string type = root["type"].AsString();
      dino8::app::Document& doc = app.Doc();
      dino8::app::ObjectId new_id = dino8::app::kNoObject;
      std::string error = "unknown or missing \"type\" (expected \"point\", \"mesh\", \"curve\" or \"surface\")";
      if (type == "point") {
        dino8::kernel::Point3d p;
        if (ComputeJsonPoint3d(root["point"], p)) {
          doc.BeginChange("compute: AddPoint");
          new_id = doc.Add(dino8::app::SceneObject::MakePoint(p));
        } else {
          error = "point: expected \"point\": [x, y, z]";
        }
      } else if (type == "mesh") {
        const dino8::json::Value& verts_json = root["vertices"];
        const dino8::json::Value& faces_json = root["faces"];
        bool ok = verts_json.IsArray() && faces_json.IsArray() && verts_json.Size() > 0 && faces_json.Size() > 0;
        std::vector<dino8::kernel::Point3d> verts;
        if (ok) {
          verts.reserve(verts_json.Size());
          for (size_t i = 0; ok && i < verts_json.Size(); ++i) {
            dino8::kernel::Point3d p;
            if (ComputeJsonPoint3d(verts_json[i], p)) verts.push_back(p); else ok = false;
          }
        }
        dino8::kernel::Mesh m;
        if (ok) {
          ON_Mesh& r = m.raw();
          for (size_t i = 0; i < verts.size(); ++i) r.SetVertex(static_cast<int>(i), verts[i]);
          for (size_t f = 0; ok && f < faces_json.Size(); ++f) {
            const dino8::json::Value& face = faces_json[f];
            if (!face.IsArray() || (face.Size() != 3 && face.Size() != 4)) { ok = false; break; }
            int idx[4] = {0, 0, 0, 0};
            for (size_t k = 0; k < face.Size(); ++k) {
              const dino8::json::Value& vi = face[k];
              const int v = static_cast<int>(vi.number);
              if (vi.type != dino8::json::Value::Type::Number || v < 0 || v >= static_cast<int>(verts.size())) { ok = false; break; }
              idx[k] = v;
            }
            if (!ok) break;
            if (face.Size() == 3) r.SetTriangle(static_cast<int>(f), idx[0], idx[1], idx[2]);
            else r.SetQuad(static_cast<int>(f), idx[0], idx[1], idx[2], idx[3]);
          }
        }
        if (ok) {
          m.raw().ComputeFaceNormals();
          m.raw().ComputeVertexNormals();
          doc.BeginChange("compute: AddMesh");
          new_id = doc.Add(dino8::app::SceneObject::MakeMesh(m));
        } else {
          error = "mesh: expected non-empty \"vertices\" ([x,y,z] each) and \"faces\" (3 or 4 valid 0-based indices each)";
        }
      } else if (type == "curve") {
        const dino8::json::Value& cvs_json = root["control_points"];
        const dino8::json::Value& degree_json = root["degree"];
        const int degree = static_cast<int>(degree_json.number);
        bool ok = degree_json.type == dino8::json::Value::Type::Number && degree >= 1 &&
                   cvs_json.IsArray() && static_cast<int>(cvs_json.Size()) >= degree + 1;
        std::vector<dino8::kernel::Point3d> cvs;
        if (ok) {
          cvs.reserve(cvs_json.Size());
          for (size_t i = 0; ok && i < cvs_json.Size(); ++i) {
            dino8::kernel::Point3d p;
            if (ComputeJsonPoint3d(cvs_json[i], p)) cvs.push_back(p); else ok = false;
          }
        }
        if (ok) {
          dino8::kernel::NurbsCurve c = dino8::kernel::NurbsCurve::FromControlPoints(cvs, degree);
          const dino8::json::Value& knots_json = root["knots"];
          if (knots_json.IsArray()) {
            ok = static_cast<int>(knots_json.Size()) == c.KnotCount();
            for (int i = 0; ok && i < c.KnotCount(); ++i) {
              const dino8::json::Value& kv = knots_json[static_cast<size_t>(i)];
              ok = kv.type == dino8::json::Value::Type::Number && c.SetKnotAt(i, kv.number) == dino8::kernel::Result::Ok;
            }
          }
          const dino8::json::Value& weights_json = root["weights"];
          if (ok && weights_json.IsArray()) {
            ok = static_cast<int>(weights_json.Size()) == c.ControlPointCount();
            for (int i = 0; ok && i < c.ControlPointCount(); ++i) {
              const dino8::json::Value& wv = weights_json[static_cast<size_t>(i)];
              ok = wv.type == dino8::json::Value::Type::Number && c.SetWeightAt(i, wv.number) == dino8::kernel::Result::Ok;
            }
          }
          if (ok) {
            doc.BeginChange("compute: AddCurve");
            new_id = doc.Add(dino8::app::SceneObject::MakeCurve(c));
          }
        }
        if (new_id == dino8::app::kNoObject) {
          error = "curve: expected an integer \"degree\">=1, at least degree+1 \"control_points\" ([x,y,z] each), "
                  "an optional \"knots\" with exactly degree+control_points-1 entries, and an optional \"weights\" "
                  "with exactly one entry per control point";
        }
      } else if (type == "surface") {
        // The other half of the surface payload GET /objects?geometry=1
        // (above) reads - same shape, same "FromControlGrid() takes this
        // exact flat array" round-trip contract curve's own POST branch
        // above already has. `control_points` is u_count*v_count entries
        // flattened in FromControlGrid's own `u * v_count + v` order.
        const dino8::json::Value& du_json = root["degree_u"];
        const dino8::json::Value& dv_json = root["degree_v"];
        const dino8::json::Value& uc_json = root["u_count"];
        const dino8::json::Value& vc_json = root["v_count"];
        const dino8::json::Value& cvs_json = root["control_points"];
        const int du = static_cast<int>(du_json.number), dv = static_cast<int>(dv_json.number);
        const int uc = static_cast<int>(uc_json.number), vc = static_cast<int>(vc_json.number);
        bool ok = du_json.type == dino8::json::Value::Type::Number && dv_json.type == dino8::json::Value::Type::Number &&
                  uc_json.type == dino8::json::Value::Type::Number && vc_json.type == dino8::json::Value::Type::Number &&
                  du >= 1 && dv >= 1 && uc >= du + 1 && vc >= dv + 1 &&
                  cvs_json.IsArray() && static_cast<int>(cvs_json.Size()) == uc * vc;
        std::vector<dino8::kernel::Point3d> cvs;
        if (ok) {
          cvs.reserve(cvs_json.Size());
          for (size_t i = 0; ok && i < cvs_json.Size(); ++i) {
            dino8::kernel::Point3d p;
            if (ComputeJsonPoint3d(cvs_json[i], p)) cvs.push_back(p); else ok = false;
          }
        }
        if (ok) {
          dino8::kernel::NurbsSurface s = dino8::kernel::NurbsSurface::FromControlGrid(cvs, uc, vc, du, dv);
          for (int dir = 0; dir < 2 && ok; ++dir) {
            const dino8::json::Value& knots_json = root[dir == 0 ? "knots_u" : "knots_v"];
            if (!knots_json.IsArray()) continue;
            ok = static_cast<int>(knots_json.Size()) == s.KnotCount(dir);
            for (int i = 0; ok && i < s.KnotCount(dir); ++i) {
              const dino8::json::Value& kv = knots_json[static_cast<size_t>(i)];
              ok = kv.type == dino8::json::Value::Type::Number && s.SetKnotAt(dir, i, kv.number) == dino8::kernel::Result::Ok;
            }
          }
          const dino8::json::Value& weights_json = root["weights"];
          if (ok && weights_json.IsArray()) {
            ok = static_cast<int>(weights_json.Size()) == uc * vc;
            for (int i = 0; ok && i < uc; ++i) {
              for (int j = 0; ok && j < vc; ++j) {
                const dino8::json::Value& wv = weights_json[static_cast<size_t>(i * vc + j)];
                ok = wv.type == dino8::json::Value::Type::Number && s.SetWeightAt(i, j, wv.number) == dino8::kernel::Result::Ok;
              }
            }
          }
          if (ok) {
            doc.BeginChange("compute: AddSurface");
            new_id = doc.Add(dino8::app::SceneObject::MakeSurface(s));
          }
        }
        if (new_id == dino8::app::kNoObject) {
          error = "surface: expected integers \"degree_u\">=1/\"degree_v\">=1 and \"u_count\">=degree_u+1/"
                  "\"v_count\">=degree_v+1, \"control_points\" (u_count*v_count [x,y,z] entries, flattened "
                  "u*v_count+v), an optional \"knots_u\"/\"knots_v\", and an optional \"weights\" with exactly "
                  "one entry per control point";
        }
      }
      resp.content_type = "application/json";
      if (new_id == dino8::app::kNoObject) {
        resp.status = 400;
        resp.body = "{\"ok\":false,\"error\":\"" + ComputeJsonEscape(error) + "\"}\n";
      } else {
        resp.body = "{\"ok\":true,\"id\":" + std::to_string(new_id) + "}\n";
      }
      return resp;
    }
    if (path == "/run") {
      const bool ok = app.Lua().Start(req.body, "compute-request");
      const bool suspended = app.Lua().Suspended();
      if (suspended) app.Lua().Abort();
      const std::string error_line = suspended ? "script requires interactive input (rs.Get*), which the compute server cannot satisfy" : "";
      const auto accept = req.headers.find("accept");
      if (accept != req.headers.end() && accept->second.find("application/json") != std::string::npos) {
        resp.content_type = "application/json";
        resp.body = json_wrap_output(ok && !suspended, app.Lua().LastOutput(), error_line, caller_name);
      } else {
        std::string out;
        for (const std::string& line : app.Lua().LastOutput()) { out += line; out += '\n'; }
        if (suspended) out += "! compute error: " + error_line + "\n";
        resp.body = out;
      }
      resp.status = (ok && !suspended) ? 200 : 500;
      return resp;
    }
    if (path == "/run/python") {
      if (!dino8::app::PythonEngine::Available()) {
        resp.status = 500;
        resp.body = "Dino 8 compute service: this build was compiled without a Python 3 development install (no DINO8_HAVE_PYTHON)\n";
        return resp;
      }
      const bool ok = app.Python().Start(req.body, "compute-request");
      const bool suspended = app.Python().Suspended();
      if (suspended) app.Python().Abort();
      const std::string error_line = suspended ? "script requires interactive input (dino8.GetPoint/GetString/GetReal/GetInteger/GetObject/GetObjects), which the compute server cannot satisfy" : "";
      const auto accept = req.headers.find("accept");
      if (accept != req.headers.end() && accept->second.find("application/json") != std::string::npos) {
        resp.content_type = "application/json";
        resp.body = json_wrap_output(ok && !suspended, app.Python().LastOutput(), error_line, caller_name);
      } else {
        std::string out;
        for (const std::string& line : app.Python().LastOutput()) { out += line; out += '\n'; }
        if (suspended) out += "! compute error: " + error_line + "\n";
        resp.body = out;
      }
      resp.status = (ok && !suspended) ? 200 : 500;
      return resp;
    }
    resp.status = 404;
    resp.body = "Dino 8 compute service: unknown path (supported: POST /run, POST /run/python, GET /objects)\n";
    return resp;
  };

  int frame = 0;
  int exit_code = 0;
  while (!app.WantsQuit()) {
    glfwPollEvents();
    if (glfwWindowShouldClose(window)) {
      // Route the window close button through the unsaved-changes prompt.
      glfwSetWindowShouldClose(window, GLFW_FALSE);
      app.RequestQuit();
    }
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
      glfwWaitEventsTimeout(0.1);
      continue;
    }
    // A hidden smoke-test or batch-script window never receives OS focus;
    // tell ImGui it is focused so synthetic keyboard input / typed script
    // command text is not discarded.
    if (!interactive_run) io.AddFocusEvent(true);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (stress_count >= 0 && frame == 3) RunStressTest(app, stress_count);
    if (cull_test_far_count >= 0 && frame == 3) RunCullTest(app, cull_test_far_count, cull_screenshot_path);

    // Feed one script line per frame after the UI has settled.
    if (frame > 2 && script_cursor < script_lines.size() && wait_frames == 0) {
      const std::string line = script_lines[script_cursor++];
      if (!line.empty() && line[0] == '@') {
        std::istringstream ss(line.substr(1));
        std::string cmd;
        ss >> cmd;
        auto key_of = [](const std::string& n) {
          if (n == "Enter") return ImGuiKey_Enter;
          if (n == "Escape") return ImGuiKey_Escape;
          if (n == "Delete") return ImGuiKey_Delete;
          if (n == "Tab") return ImGuiKey_Tab;
          if (n == "Backspace") return ImGuiKey_Backspace;
          if (n == "Space") return ImGuiKey_Space;
          if (n == "F1") return ImGuiKey_F1;
          if (n == "Up") return ImGuiKey_UpArrow;
          if (n == "Down") return ImGuiKey_DownArrow;
          if (n == "Z") return ImGuiKey_Z;
          return ImGuiKey_None;
        };
        auto expand = [&](std::initializer_list<std::string> lines) {
          script_lines.insert(script_lines.begin() + static_cast<long>(script_cursor), lines.begin(), lines.end());
        };
        if (cmd == "move") { float x, y; ss >> x >> y; io.AddMousePosEvent(x, y); }
        else if (cmd == "down") { int b = 0; ss >> b; io.AddMouseButtonEvent(b, true); }
        else if (cmd == "up") { int b = 0; ss >> b; io.AddMouseButtonEvent(b, false); }
        else if (cmd == "click") { float x, y; int b = 0; ss >> x >> y >> b; expand({"@move " + std::to_string(x) + " " + std::to_string(y), "@wait 1", "@down " + std::to_string(b), "@wait 1", "@up " + std::to_string(b), "@wait 1"}); }
        else if (cmd == "world" || cmd == "clickworld") {
          std::string view; double x, y, z; ss >> view >> x >> y >> z;
          int btn = 0; ss >> btn;  // optional mouse button for clickworld (0 left, 1 right, 2 middle)
          dino8::app::Viewport* vp = app.FindViewport(view);
          double px = 0, py = 0;
          if (vp && vp->WorldToPixel(dino8::kernel::Point3d(x, y, z), px, py)) {
            const double sx = vp->ScreenX() + px, sy = vp->ScreenY() + py;
            if (std::getenv("DINO8_UI_DEBUG")) std::fprintf(stderr, "[script] %s %s %g,%g,%g -> vp(%g,%g) px(%g,%g)\n", cmd.c_str(), view.c_str(), x, y, z, vp->ScreenX(), vp->ScreenY(), px, py);
            if (cmd == "world") expand({"@move " + std::to_string(sx) + " " + std::to_string(sy), "@wait 1"});
            else expand({"@click " + std::to_string(sx) + " " + std::to_string(sy) + " " + std::to_string(btn)});
          } else {
            std::fprintf(stderr, "script: viewport %s not found or point off-screen\n", view.c_str());
          }
        }
        else if (cmd == "forcehoverfeed") {
          // Test-only: sets CommandEngine's hover_point_ directly (bypassing
          // ImGui's own per-frame viewport-hover detection entirely - this
          // harness's headless Xvfb runs never seem to report a viewport as
          // "hovered" even with the mouse cursor moved, via @move/@world, to
          // a pixel geometrically inside its rect, so that route can't
          // reproduce "a hover point happens to be set" deterministically
          // here) and immediately feeds one text token to the engine in the
          // same step - before this frame's own Draw() call has a chance to
          // recompute and overwrite hover_point_ from the (always-unhovered
          // here) real mouse state, the way it would if @forcehover-ing and
          // feeding the token were left as two separate script lines a
          // frame apart. This is what lets a script deterministically
          // reproduce "a command mid-Want::Point sees a bare number while
          // hover_point_ happens to be set" (CommandEngine::FeedText's
          // "distance toward the cursor" branch, and
          // Command::NumberIsLiteralValue()'s opt-out of it - see
          // RHINO8_KILLER_AUDIT.md row K / commit da0a4eb) on any platform
          // this harness runs on, not only whichever one's incidental
          // ImGui/GLFW hover state happens to hit it.
          double x = 0, y = 0, z = 0; std::string tok;
          ss >> x >> y >> z >> tok;
          app.Engine().FeedHover(dino8::kernel::Point3d(x, y, z));
          app.Engine().FeedText(tok);
        }
        else if (cmd == "dragworld") {
          std::string view; double x0, y0, z0, x1, y1, z1; ss >> view >> x0 >> y0 >> z0 >> x1 >> y1 >> z1;
          int btn = 0; ss >> btn;  // optional mouse button (0 left, 1 right, 2 middle - right/middle drag the camera)
          dino8::app::Viewport* vp = app.FindViewport(view);
          double ax, ay, bx, by;
          if (vp && vp->WorldToPixel(dino8::kernel::Point3d(x0, y0, z0), ax, ay) && vp->WorldToPixel(dino8::kernel::Point3d(x1, y1, z1), bx, by)) {
            expand({"@drag " + std::to_string(vp->ScreenX() + ax) + " " + std::to_string(vp->ScreenY() + ay) + " " + std::to_string(vp->ScreenX() + bx) + " " + std::to_string(vp->ScreenY() + by) + " " + std::to_string(btn)});
          }
        }
        else if (cmd == "drag") {
          float x0, y0, x1, y1; ss >> x0 >> y0 >> x1 >> y1;
          int btn = 0; ss >> btn;  // optional mouse button, default left (0)
          expand({"@move " + std::to_string(x0) + " " + std::to_string(y0), "@wait 1", "@down " + std::to_string(btn), "@wait 1", "@move " + std::to_string((x0 + x1) / 2) + " " + std::to_string((y0 + y1) / 2), "@wait 1", "@move " + std::to_string(x1) + " " + std::to_string(y1), "@wait 2", "@up " + std::to_string(btn), "@wait 1"});
        }
        else if (cmd == "key") { std::string n; ss >> n; io.AddKeyEvent(key_of(n), true); expand({"@keyup " + n}); }
        else if (cmd == "keyup") { std::string n; ss >> n; io.AddKeyEvent(key_of(n), false); }
        else if (cmd == "text") { std::string rest; std::getline(ss, rest); if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1); io.AddInputCharactersUTF8(rest.c_str()); }
        else if (cmd == "wait") { ss >> wait_frames; }
        else if (cmd == "waitfile") {
          // Blocks the script - not the AT-SPI2 accessibility bridge, which
          // keeps being pumped below so it keeps answering queries - until
          // an external process creates a file at this path. Unlike "wait
          // N frames", this lets an external test deterministically
          // synchronize against a specific point in a headless run without
          // racing real wall-clock time against however fast this
          // machine's frames happen to render (see the only user of this
          // directive, tests/smoke_accessibility.py).
          std::string path; ss >> path;
          while (!std::filesystem::exists(path)) {
            dino8::platform::PumpAccessibilityEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          }
        }
        else if (cmd == "snap") {
          std::string name; int on = 1; ss >> name >> on;
          dino8::app::SnapSettings& sn = app.Snaps();
          bool* flag = name == "end" ? &sn.end : name == "mid" ? &sn.mid : name == "cen" ? &sn.cen : name == "point" ? &sn.point : name == "near" ? &sn.near_ : name == "vertex" ? &sn.vertex : name == "int" ? &sn.int_ : name == "perp" ? &sn.perp : name == "tan" ? &sn.tan : name == "quad" ? &sn.quad : name == "grid" ? &sn.grid_snap : name == "ortho" ? &sn.ortho : nullptr;
          if (flag) *flag = on != 0;
        }
        else if (cmd == "expect_selected") { size_t n; ss >> n; const size_t got = app.Doc().SelectedCount(); std::printf("%s expect_selected %zu (got %zu)\n", got == n ? "ok  " : "FAIL", n, got); if (got != n) exit_code = 2; }
        else if (cmd == "expect_objects") { size_t n; ss >> n; const size_t got = app.Doc().ObjectCount(); std::printf("%s expect_objects %zu (got %zu)\n", got == n ? "ok  " : "FAIL", n, got); if (got != n) exit_code = 2; }
      } else {
        app.Engine().Execute(line);
      }
    }
    if (wait_frames > 0) --wait_frames;
    app.Frame();
    dino8::platform::UpdateAccessibility(app.Engine().Prompt(), app.CommandInput(), app.Engine().History(),
                                          dino8::app::LastMenuBarAccessibleTree(),
                                          dino8::app::CommandOptionsAccessibleTree(app),
                                          dino8::app::LayersPanelAccessibleTree(app),
                                          dino8::app::PropertiesPanelAccessibleTree(app),
                                          dino8::app::ViewportsAccessibleTree(app),
                                          dino8::app::ActivityLogAccessibleTree(app),
                                          dino8::app::NamedViewsAccessibleTree(app),
                                          dino8::app::NamedCPlanesAccessibleTree(app),
                                          dino8::app::LinetypesAccessibleTree(app),
                                          dino8::app::MaterialsAccessibleTree(app),
                                          dino8::app::ClippingPlanesAccessibleTree(app),
                                          dino8::app::LayoutsAccessibleTree(app),
                                          dino8::app::BlockManagerAccessibleTree(app),
                                          dino8::app::LayerStateManagerAccessibleTree(app),
                                          dino8::app::DocumentUserTextAccessibleTree(app),
                                          dino8::app::LightsAccessibleTree(app),
                                          dino8::app::AnnotationStylesAccessibleTree(app),
                                          dino8::app::DocumentNotesAccessibleTree(app),
                                          dino8::app::EnvironmentsAccessibleTree(app),
                                          dino8::app::AuditResultsAccessibleTree(app),
                                          dino8::app::UndoHistoryAccessibleTree(app),
                                          dino8::app::RedoHistoryAccessibleTree(app),
                                          dino8::app::HatchPatternsAccessibleTree(),
                                          dino8::plugins::PluginsAccessibleTree(),
                                          dino8::app::CommandListAccessibleTree(app),
                                          dino8::app::CommandAliasesAccessibleTree(app),
                                          dino8::app::KeyboardShortcutsAccessibleTree(app),
                                          dino8::app::BuiltinShortcutsAccessibleTree(app),
                                          dino8::app::DocumentPropertiesAccessibleTree(app),
                                          dino8::app::TexturesAccessibleTree(app),
                                          dino8::app::DisplayAccessibleTree(app));
    // Catch up history_printed to whatever on_print_line already flushed
    // live as each line was recorded (see its own comment on why that has
    // to happen from inside CommandEngine::Print(), not here) - printing
    // again here too would print every line twice. When on_print_line isn't
    // set (normal interactive use, no console to flush to), this is the
    // only printer, same as before.
    for (const auto& hist = app.Engine().History(); history_printed < hist.size(); ++history_printed) {
      if (!app.Engine().on_print_line) std::printf("history: %s\n", hist[history_printed].c_str());
    }

    ImGui::Render();
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    const ImVec4 clear = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    glClearColor(clear.x, clear.y, clear.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (auto cap = app.TakeWindowCaptureRequest()) {
      std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 3);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
      if (WriteWindowCaptureBmp(*cap, w, h, pixels)) app.Notify("Saved " + *cap);
      else app.Notify("Cannot write " + *cap);
    }
    const bool last_smoke_frame = smoke_frames >= 0 && frame + 1 >= smoke_frames && script_cursor >= script_lines.size();
    if (last_smoke_frame && !screenshot_path.empty()) {
      std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 3);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
      if (FILE* f = std::fopen(screenshot_path.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) std::fwrite(&pixels[static_cast<size_t>(y) * w * 3], 1, static_cast<size_t>(w) * 3, f);
        std::fclose(f);
      }
    }
    glfwSwapBuffers(window);

    ++frame;
    if (smoke_frames >= 0 && frame >= smoke_frames && script_cursor >= script_lines.size()) {
      // Report a few facts the QC script checks.
      std::printf("smoke: frames=%d objects=%zu commands=%zu gl_error=%u\n", frame, app.Doc().ObjectCount(),
                  app.Engine().Registry().size(), static_cast<unsigned>(glGetError()));
      // Same on_print_line-already-handled-it guard as above - every history
      // line has already been printed live if the hook is set; this is only
      // the catch-up path when it isn't.
      for (const auto& hist = app.Engine().History(); history_printed < hist.size(); ++history_printed) {
        if (!app.Engine().on_print_line) std::printf("history: %s\n", hist[history_printed].c_str());
      }
      break;
    }
    // Pure batch-script mode (--script without --smoke): exit the instant
    // the script has fully drained (no lines left, and no @wait/@click/@drag
    // expansion still pending) instead of falling into the normal
    // interactive loop - see docs/BATCH_SCRIPTING.md. wait_frames == 0 is
    // the same "truly finished, not just between two expanded lines" guard
    // the per-frame line-feed gate above uses.
    if (smoke_frames < 0 && !script_path.empty() && script_cursor >= script_lines.size() && wait_frames == 0) {
      std::printf("script: done objects=%zu commands=%zu\n", app.Doc().ObjectCount(), app.Engine().Registry().size());
      break;
    }
    // --serve: service at most one waiting connection per frame (timeout 0
    // - never blocks the frame loop) so this coexists with everything else
    // the loop does, exactly like the --stress/--cull-test/script-feeding
    // hooks above it. --serve-max-requests bounds the run for
    // tests/smoke.sh the same way pure batch-script mode bounds itself on
    // running out of script lines, above.
    if (serve_port >= 0) {
      if (compute_server.PollOnce(compute_handler, /*timeout_ms=*/0)) ++serve_requests_handled;
      if (serve_max_requests >= 0 && serve_requests_handled >= serve_max_requests) {
        std::printf("serve: done requests=%d\n", serve_requests_handled);
        break;
      }
    }
  }

  app.Shutdown();
  // Stops the X11 CLIPBOARD-selection-owner thread (Linux) cleanly; a no-op
  // on Windows/macOS, where clipboard ownership isn't held by a background
  // thread. See src/platform/Clipboard.h.
  dino8::platform::ShutdownClipboard();
  // Unregisters from the AT-SPI2 registry and closes its D-Bus connection
  // (Linux, when built with atspi-2/gio-2.0); a no-op everywhere else. See
  // src/platform/Accessibility.h.
  dino8::platform::ShutdownAccessibility();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return exit_code;
}
