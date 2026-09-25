// Embedded Python 3 scripting with a RhinoCommon-flavoured `dino8` module,
// alongside the Lua-based `rs.*` engine (see LuaEngine.h).
//
// Unlike Lua (vendored from source, always available), Python support is
// optional: it links against whatever Python 3 development install is on
// the build machine (see CMakeLists.txt), so DINO8_HAVE_PYTHON may not be
// defined. When it isn't, every method below is a harmless no-op that
// returns false / prints nothing, and PythonAvailable() is false - callers
// (cmd_misc.cpp's RunPythonScript) check that to print an honest message
// instead of pretending the engine ran.
//
// Interactive limitation (read before assuming parity with Lua): Lua
// scripts run in a real Lua coroutine, so rs.GetPoint/rs.GetObjects can
// yield the C stack and let RunScript resume them later with a picked
// value. CPython has no equivalent of "suspend an arbitrary nested call
// stack and resume it later" without either stackful coroutines (a whole
// separate C stack + context switch per script, akin to greenlets) or
// running the script on its own OS thread and blocking it on a condition
// variable while the UI thread pumps. Neither was implemented here for
// time reasons: Python scripts run start-to-finish, synchronously, in
// Begin() - dino8.GetPoint()-style interactive prompts are not offered by
// the module at all. Everything else (building geometry, editing
// properties, running commands, printing) works the same run, just without
// mid-script pauses for a viewport pick.
#pragma once

#include <string>
#include <vector>

namespace dino8::app {

class Application;

class PythonEngine {
 public:
  explicit PythonEngine(Application& app);
  ~PythonEngine();
  PythonEngine(const PythonEngine&) = delete;
  PythonEngine& operator=(const PythonEngine&) = delete;

  // True when this build was linked against a Python 3 development install
  // (DINO8_HAVE_PYTHON) - false means every method below does nothing.
  static bool Available();

  // Runs `code` to completion (no coroutine suspension - see the file
  // comment above). Returns false when it failed to compile or raised an
  // exception (already printed as "! Python error: ..."). `chunk_name` is
  // what error messages/tracebacks call the script ("t.py").
  bool Start(const std::string& code, const std::string& chunk_name);
  // Reads the file and calls Start().
  bool StartFile(const std::string& path);
  // Evaluates `expr` as an expression first ("= 2+3" prints 5) and runs it
  // as a statement otherwise, matching LuaEngine::StartExpression.
  bool StartExpression(const std::string& expr);

  // Output lines produced by the last run (print() / errors), for the
  // Script Editor. Mirrors LuaEngine::LastOutput/Print.
  const std::vector<std::string>& LastOutput() const { return output_; }
  void Print(const std::string& line);

  Application& App() { return app_; }

  // Called by the embedded module's sys.stdout/stderr shim (one call per
  // Python-side write()); buffers partial writes and forwards to Print()
  // a full line at a time. Public only because the pybind11 module glue
  // lives in an anonymous namespace in the .cpp and needs to reach it -
  // scripts never call this directly.
  void FeedStdout(const std::string& text);

 private:
  bool Run(const std::string& code, const std::string& chunk_name, bool as_expression);

  Application& app_;
  std::vector<std::string> output_;
  std::string print_buffer_;
};

}  // namespace dino8::app
