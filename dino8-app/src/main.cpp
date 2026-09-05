// Dino 8 entry point: creates the window and GL context, initialises ImGui
// with docking, and runs the application frame loop.
//
// Command line:
//   --smoke N     render N frames and exit (used by headless QC under Xvfb)
//   --script FILE run each line of FILE as a command after start-up
//   --screenshot FILE.ppm   save the final frame (used with --smoke)
//   FILE.3dm      open a model on start-up

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gl/gl_loader.h"
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "app/Application.h"
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
  io.IniFilename = nullptr;  // layout is rebuilt deterministically; no stray .ini files
  float xscale = 1.0f, yscale = 1.0f;
  glfwGetWindowContentScale(window, &xscale, &yscale);
  const float ui_scale = xscale > 0 ? xscale : 1.0f;
  dino8::app::ApplyDinoTheme(ui_scale);
  ImFontConfig font_cfg;
  font_cfg.SizePixels = 16.0f * ui_scale;
  io.Fonts->AddFontDefault(&font_cfg);
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  dino8::app::Application app;
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

  int frame = 0;
  int exit_code = 0;
  while (!glfwWindowShouldClose(window) && !app.WantsQuit()) {
    glfwPollEvents();
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
      glfwWaitEventsTimeout(0.1);
      continue;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Feed one script line per frame after the UI has settled.
    if (frame > 2 && script_cursor < script_lines.size()) {
      app.Engine().Execute(script_lines[script_cursor++]);
    }
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
