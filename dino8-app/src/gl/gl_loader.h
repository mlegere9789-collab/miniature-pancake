// Minimal, dependency-free OpenGL 3.3 core function loader.
//
// Why not glad/glew: both need either a generator step or an extra vendored
// dependency. Dino 8 needs a small, fixed set of GL functions (buffers,
// shaders, VAOs, framebuffers) that are resolved at runtime through
// glfwGetProcAddress on every platform, so a hand-written table is simpler,
// fully portable, and has no build-time moving parts.
//
// Usage: call dino8::gl::Load() once after the GL context is current.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

// Types that pre-3.0 system headers (notably Windows' GL/gl.h) don't define.
#ifndef GL_VERSION_2_0
typedef char GLchar;
#endif
#ifndef GL_VERSION_1_5
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;
#endif

// Constants missing from GL 1.1 headers.
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_POLYGON_OFFSET_FILL
#define GL_POLYGON_OFFSET_FILL 0x8037
#endif
#ifndef GL_LINE_SMOOTH
#define GL_LINE_SMOOTH 0x0B20
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

namespace dino8::gl {

#define DINO8_GL_FUNCS(X)                                                                   \
  X(void, GenBuffers, GLsizei, GLuint*)                                                     \
  X(void, DeleteBuffers, GLsizei, const GLuint*)                                            \
  X(void, BindBuffer, GLenum, GLuint)                                                       \
  X(void, BufferData, GLenum, GLsizeiptr, const void*, GLenum)                              \
  X(void, GenVertexArrays, GLsizei, GLuint*)                                                \
  X(void, DeleteVertexArrays, GLsizei, const GLuint*)                                       \
  X(void, BindVertexArray, GLuint)                                                          \
  X(void, EnableVertexAttribArray, GLuint)                                                  \
  X(void, DisableVertexAttribArray, GLuint)                                                 \
  X(void, VertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)      \
  X(GLuint, CreateShader, GLenum)                                                           \
  X(void, ShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*)                \
  X(void, CompileShader, GLuint)                                                            \
  X(void, GetShaderiv, GLuint, GLenum, GLint*)                                              \
  X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                             \
  X(void, DeleteShader, GLuint)                                                             \
  X(GLuint, CreateProgram, void)                                                            \
  X(void, AttachShader, GLuint, GLuint)                                                     \
  X(void, LinkProgram, GLuint)                                                              \
  X(void, GetProgramiv, GLuint, GLenum, GLint*)                                             \
  X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                            \
  X(void, UseProgram, GLuint)                                                               \
  X(void, DeleteProgram, GLuint)                                                            \
  X(GLint, GetUniformLocation, GLuint, const GLchar*)                                       \
  X(void, UniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*)                      \
  X(void, Uniform1f, GLint, GLfloat)                                                        \
  X(void, Uniform1i, GLint, GLint)                                                          \
  X(void, Uniform2f, GLint, GLfloat, GLfloat)                                               \
  X(void, Uniform3f, GLint, GLfloat, GLfloat, GLfloat)                                      \
  X(void, Uniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat)                             \
  X(void, GenFramebuffers, GLsizei, GLuint*)                                                \
  X(void, DeleteFramebuffers, GLsizei, const GLuint*)                                       \
  X(void, BindFramebuffer, GLenum, GLuint)                                                  \
  X(void, FramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint)                      \
  X(void, GenRenderbuffers, GLsizei, GLuint*)                                               \
  X(void, DeleteRenderbuffers, GLsizei, const GLuint*)                                      \
  X(void, BindRenderbuffer, GLenum, GLuint)                                                 \
  X(void, RenderbufferStorage, GLenum, GLenum, GLsizei, GLsizei)                            \
  X(void, FramebufferRenderbuffer, GLenum, GLenum, GLenum, GLuint)                          \
  X(GLenum, CheckFramebufferStatus, GLenum)                                                 \
  X(void, ActiveTexture, GLenum)

#define DINO8_GL_DECLARE(ret, name, ...) \
  typedef ret(APIENTRY* PFN_##name)(__VA_ARGS__); \
  extern PFN_##name name;
DINO8_GL_FUNCS(DINO8_GL_DECLARE)
#undef DINO8_GL_DECLARE

// Resolves every function above through glfwGetProcAddress. Returns false
// (and leaves a description in LastError()) if any required function is
// missing - i.e. the context is older than OpenGL 3.3.
bool Load();
const char* LastError();

}  // namespace dino8::gl

// Convenience aliases so rendering code reads like ordinary GL.
#define glGenBuffers dino8::gl::GenBuffers
#define glDeleteBuffers dino8::gl::DeleteBuffers
#define glBindBuffer dino8::gl::BindBuffer
#define glBufferData dino8::gl::BufferData
#define glGenVertexArrays dino8::gl::GenVertexArrays
#define glDeleteVertexArrays dino8::gl::DeleteVertexArrays
#define glBindVertexArray dino8::gl::BindVertexArray
#define glEnableVertexAttribArray dino8::gl::EnableVertexAttribArray
#define glDisableVertexAttribArray dino8::gl::DisableVertexAttribArray
#define glVertexAttribPointer dino8::gl::VertexAttribPointer
#define glCreateShader dino8::gl::CreateShader
#define glShaderSource dino8::gl::ShaderSource
#define glCompileShader dino8::gl::CompileShader
#define glGetShaderiv dino8::gl::GetShaderiv
#define glGetShaderInfoLog dino8::gl::GetShaderInfoLog
#define glDeleteShader dino8::gl::DeleteShader
#define glCreateProgram dino8::gl::CreateProgram
#define glAttachShader dino8::gl::AttachShader
#define glLinkProgram dino8::gl::LinkProgram
#define glGetProgramiv dino8::gl::GetProgramiv
#define glGetProgramInfoLog dino8::gl::GetProgramInfoLog
#define glUseProgram dino8::gl::UseProgram
#define glDeleteProgram dino8::gl::DeleteProgram
#define glGetUniformLocation dino8::gl::GetUniformLocation
#define glUniformMatrix4fv dino8::gl::UniformMatrix4fv
#define glUniform1f dino8::gl::Uniform1f
#define glUniform1i dino8::gl::Uniform1i
#define glUniform2f dino8::gl::Uniform2f
#define glUniform3f dino8::gl::Uniform3f
#define glUniform4f dino8::gl::Uniform4f
#define glGenFramebuffers dino8::gl::GenFramebuffers
#define glDeleteFramebuffers dino8::gl::DeleteFramebuffers
#define glBindFramebuffer dino8::gl::BindFramebuffer
#define glFramebufferTexture2D dino8::gl::FramebufferTexture2D
#define glGenRenderbuffers dino8::gl::GenRenderbuffers
#define glDeleteRenderbuffers dino8::gl::DeleteRenderbuffers
#define glBindRenderbuffer dino8::gl::BindRenderbuffer
#define glRenderbufferStorage dino8::gl::RenderbufferStorage
#define glFramebufferRenderbuffer dino8::gl::FramebufferRenderbuffer
#define glCheckFramebufferStatus dino8::gl::CheckFramebufferStatus
#define glActiveTexture dino8::gl::ActiveTexture
