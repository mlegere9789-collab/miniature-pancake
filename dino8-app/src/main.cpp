// Dino 8 entry point: creates the window and GL context, initialises ImGui
// with docking, and runs the application frame loop.
//
// Command line:
//   --smoke N     render N frames and exit (used by headless QC under Xvfb)
//   --script FILE run each line of FILE as a command after start-up
//   --screenshot FILE.ppm   save the final frame (used with --smoke)
//
// Script lines starting with '@' are synthetic input for UI tests:
//   @move X Y | @down [button] | @up [button] | @click X Y [button]
//   @world VIEW X Y Z      move the mouse to a world point in a viewport
//   @clickworld VIEW X Y Z click a world point in a viewport
//   @drag X0 Y0 X1 Y1      left-drag (window/crossing select)
//   @key NAME | @text STR | @wait N | @expect_selected N | @expect_objects N
//   FILE.3dm      open a model on start-up

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
#include "ui/Theme.h"

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

}  // namespace

int main(int argc, char** argv) {
  int smoke_frames = -1;
  std::string script_path;
  std::string open_path;
  std::string screenshot_path;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--smoke") == 0 && i + 1 < argc) smoke_frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--script") == 0 && i + 1 < argc) script_path = argv[++i];
    else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot_path = argv[++i];
    else if (std::strcmp(argv[i], "--version") == 0) { std::printf("Dino 8 %s\n", DINO8_VERSION); return 0; }
    else if (argv[i][0] != '-') open_path = argv[i];
  }

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
  std::string error;
  if (!app.Init(ExeDir(argv[0]), error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
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
          dino8::app::Viewport* vp = app.FindViewport(view);
          double px = 0, py = 0;
          if (vp && vp->WorldToPixel(dino8::kernel::Point3d(x, y, z), px, py)) {
            const double sx = vp->ScreenX() + px, sy = vp->ScreenY() + py;
            if (cmd == "world") expand({"@move " + std::to_string(sx) + " " + std::to_string(sy), "@wait 1"});
            else expand({"@click " + std::to_string(sx) + " " + std::to_string(sy) + " 0"});
          } else {
            std::fprintf(stderr, "script: viewport %s not found or point off-screen\n", view.c_str());
          }
        }
        else if (cmd == "dragworld") {
          std::string view; double x0, y0, z0, x1, y1, z1; ss >> view >> x0 >> y0 >> z0 >> x1 >> y1 >> z1;
          dino8::app::Viewport* vp = app.FindViewport(view);
          double ax, ay, bx, by;
          if (vp && vp->WorldToPixel(dino8::kernel::Point3d(x0, y0, z0), ax, ay) && vp->WorldToPixel(dino8::kernel::Point3d(x1, y1, z1), bx, by)) {
            expand({"@drag " + std::to_string(vp->ScreenX() + ax) + " " + std::to_string(vp->ScreenY() + ay) + " " + std::to_string(vp->ScreenX() + bx) + " " + std::to_string(vp->ScreenY() + by)});
          }
        }
        else if (cmd == "drag") { float x0, y0, x1, y1; ss >> x0 >> y0 >> x1 >> y1; expand({"@move " + std::to_string(x0) + " " + std::to_string(y0), "@wait 1", "@down 0", "@wait 1", "@move " + std::to_string((x0 + x1) / 2) + " " + std::to_string((y0 + y1) / 2), "@wait 1", "@move " + std::to_string(x1) + " " + std::to_string(y1), "@wait 2", "@up 0", "@wait 1"}); }
        else if (cmd == "key") { std::string n; ss >> n; io.AddKeyEvent(key_of(n), true); expand({"@keyup " + n}); }
        else if (cmd == "keyup") { std::string n; ss >> n; io.AddKeyEvent(key_of(n), false); }
        else if (cmd == "text") { std::string rest; std::getline(ss, rest); if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1); io.AddInputCharactersUTF8(rest.c_str()); }
        else if (cmd == "wait") { ss >> wait_frames; }
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
    glClearColor(0.11f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
