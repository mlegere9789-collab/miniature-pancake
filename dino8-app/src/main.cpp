// Dino 8 entry point: creates the window and GL context, initialises ImGui
// with docking, and runs the application frame loop.
//
// Command line:
//   --smoke N     render N frames and exit (used by headless QC under Xvfb)
//   --script FILE run each line of FILE as a command after start-up
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
//
// Script lines starting with '@' are synthetic input for UI tests:
//   @move X Y | @down [button] | @up [button] | @click X Y [button]
//   @world VIEW X Y Z      move the mouse to a world point in a viewport
//   @clickworld VIEW X Y Z click a world point in a viewport
//   @drag X0 Y0 X1 Y1      left-drag (window/crossing select)
//   @key NAME | @text STR | @wait N | @expect_selected N | @expect_objects N
//   FILE.3dm      open a model on start-up

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gl/gl_loader.h"
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "app/Application.h"
#include "app/Settings.h"
#include "doc/Document.h"
#include "ui/Theme.h"
#include "util/ThreadPool.h"
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

}  // namespace

int main(int argc, char** argv) {
  int smoke_frames = -1;
  int stress_count = -1;
  int cull_test_far_count = -1;
  std::string script_path;
  std::string open_path;
  std::string screenshot_path;
  std::string cull_screenshot_path;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--smoke") == 0 && i + 1 < argc) smoke_frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--stress") == 0 && i + 1 < argc) stress_count = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--cull-test") == 0 && i + 1 < argc) cull_test_far_count = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--cull-screenshot") == 0 && i + 1 < argc) cull_screenshot_path = argv[++i];
    else if (std::strcmp(argv[i], "--script") == 0 && i + 1 < argc) script_path = argv[++i];
    else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot_path = argv[++i];
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
  glfwWindowHint(GLFW_SAMPLES, 4);
  if (smoke_frames >= 0) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

  GLFWwindow* window = glfwCreateWindow(1600, 900, "Dino 8", nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "Could not create an OpenGL 3.3 window\n");
    glfwTerminate();
    return 1;
  }
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
  // Window layout persists in the user's config directory (not in smoke runs).
  const std::string ini_path = dino8::app::ConfigDirectory() + "/layout.ini";
  const bool has_layout = smoke_frames < 0 && std::filesystem::exists(ini_path);
  io.IniFilename = smoke_frames < 0 ? ini_path.c_str() : nullptr;
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
  app.headless = smoke_frames >= 0;
  app.smoke_mode = smoke_frames >= 0;
  std::string error;
  if (!app.Init(ExeDir(argv[0]), error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  dino8::app::ApplyDinoTheme(app.ui_scale, static_cast<dino8::app::ThemeMode>(app.theme_mode), app.accent_color);
  if (!error.empty()) std::fprintf(stderr, "warning: %s\n", error.c_str());

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
    // A hidden smoke-test window never receives OS focus; tell ImGui it
    // is focused so synthetic keyboard input is not discarded.
    if (smoke_frames >= 0) io.AddFocusEvent(true);
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
      for (const std::string& line : app.Engine().History()) std::printf("history: %s\n", line.c_str());
      break;
    }
  }

  app.Shutdown();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return exit_code;
}
