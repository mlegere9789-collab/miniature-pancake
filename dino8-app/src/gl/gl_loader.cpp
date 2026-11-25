#include "gl/gl_loader.h"

#include <GLFW/glfw3.h>

#include <string>

namespace dino8::gl {

#define DINO8_GL_DEFINE(ret, name, ...) PFN_##name name = nullptr;
DINO8_GL_FUNCS(DINO8_GL_DEFINE)
#undef DINO8_GL_DEFINE

namespace {
std::string g_last_error;
}

bool Load() {
  g_last_error.clear();
#define DINO8_GL_LOAD(ret, name, ...)                                              \
  name = reinterpret_cast<PFN_##name>(glfwGetProcAddress("gl" #name));            \
  if (name == nullptr) {                                                           \
    g_last_error += "missing gl" #name " ";                                        \
  }
  DINO8_GL_FUNCS(DINO8_GL_LOAD)
#undef DINO8_GL_LOAD
  return g_last_error.empty();
}

const char* LastError() { return g_last_error.c_str(); }

}  // namespace dino8::gl
