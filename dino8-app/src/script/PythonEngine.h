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
// Interactive prompts (dino8.GetPoint): Lua scripts run in a real Lua
// coroutine, so rs.GetPoint can yield the C stack and let RunScript resume
// it later with a picked value. CPython has no equivalent of "suspend an
// arbitrary nested call stack and resume it later" without either stackful
// coroutines (a whole separate C stack + context switch per script, akin to
// greenlets) or running the script on its own OS thread and blocking it on a
// condition variable while the UI thread pumps. This engine takes the
// second approach: Start()/StartFile()/StartExpression() run the script on
// a dedicated worker thread (ThreadMain) and block the calling thread until
// that worker either finishes or suspends inside dino8.GetPoint() (see
// WaitForPoint, called from the embedded module's GetPoint binding in the
// .cpp). Exactly one of the UI thread and the worker thread is ever
// runnable at a time - ResumePoint()/ResumeNil()/Abort() wake the worker and
// then block the caller again the same way Start() did - so there is no
// concurrent access to the Document, the Python interpreter state, or this
// engine's own output_/print_buffer_ from two threads at once; the thread
// only exists to get a real suspend point, not for parallelism. The GIL is
// released (py::gil_scoped_release) for the duration of that block so nothing
// is ever left holding it while idle. Scope: dino8.GetPoint(),
// dino8.GetString(), dino8.GetReal() and dino8.GetInteger() are wired up
// this way (mirroring rs.GetPoint/rs.GetString/rs.GetReal/rs.GetInteger);
// rs.GetObjects-equivalent remains unported - a real but narrower gap than
// "no interactive prompts at all".
#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "script/LuaEngine.h"  // reuses ScriptWant/ScriptRequest, the same suspend-request shape LuaEngine already defines

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

  // Starts `code` on the worker thread and blocks until it either finishes
  // or suspends on a dino8.GetPoint() call. Returns false when it finished
  // and failed to compile or raised an exception (already printed as
  // "! Python error: ..."); true otherwise (finished successfully, or
  // suspended - check Suspended()/Request() to tell which). `chunk_name` is
  // what error messages/tracebacks call the script ("t.py").
  bool Start(const std::string& code, const std::string& chunk_name);
  // Reads the file and calls Start().
  bool StartFile(const std::string& path);
  // Evaluates `expr` as an expression first ("= 2+3" prints 5) and runs it
  // as a statement otherwise, matching LuaEngine::StartExpression.
  bool StartExpression(const std::string& expr);

  // True while the worker thread is alive (running or suspended on GetPoint).
  bool Running() const;
  // True while the worker thread is specifically blocked inside GetPoint().
  bool Suspended() const;
  const ScriptRequest& Request() const { return request_; }

  // Resumes a suspended script with the point/text/number it asked for, or
  // with "Enter/nothing given" (ResumeNil - dino8.GetPoint()/dino8.GetString()/
  // dino8.GetReal()/dino8.GetInteger() then return None, unless the call had
  // a default - see WaitForText/WaitForNumber). Each blocks until the script
  // next finishes or suspends again, exactly like Start(), and returns the
  // same way. False if nothing was suspended.
  bool ResumePoint(kernel::Point3d p);
  bool ResumeText(const std::string& text);
  bool ResumeNumber(double v);
  bool ResumeNil();
  void Abort();  // Esc: cancel the suspended script, it stops with a RuntimeError the script could catch (uncaught, prints "Script cancelled: ...")

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

  // What dino8.GetPoint()/dino8.GetString() got woken up with. Returned by
  // value (not a py::object) so this header stays usable without
  // DINO8_HAVE_PYTHON.
  struct PointWait {
    bool got_point = false;
    bool cancelled = false;
    kernel::Point3d point;
  };
  struct TextWait {
    bool got_text = false;
    bool cancelled = false;
    std::string text;
  };
  struct NumberWait {
    bool got_number = false;
    bool cancelled = false;
    double number = 0;
  };
  // Called only from the worker thread, by the embedded module's GetPoint/
  // GetString/GetReal/GetInteger bindings (PyGetPoint/PyGetString/PyGetReal/
  // PyGetInteger, in the .cpp): records the prompt, wakes anyone waiting in
  // Start()/Resume*() on the UI thread, and blocks until
  // ResumePoint/ResumeText/ResumeNumber/ResumeNil/Abort supplies an answer.
  PointWait WaitForPoint(const std::string& prompt);
  // `default_text`, if set, is what a bare Enter on the command line
  // supplies (CommandEngine.cpp routes it straight to OnText instead of
  // OnEnter - see Command::WantText) - so ResumeNil here only means no
  // default was given either, matching rs.GetString(prompt) pushing nil.
  TextWait WaitForText(const std::string& prompt, const std::optional<std::string>& default_text);
  // Mirrors WaitForText above for dino8.GetReal()/dino8.GetInteger() -
  // `is_integer` only affects rounding (see ResumeNumber), the suspend/
  // resume mechanics are identical. `default_number`, if set, is what a
  // bare Enter on the command line supplies (Command::WantNumber), same
  // relationship WaitForText's own `default_text` has to Command::WantText.
  NumberWait WaitForNumber(const std::string& prompt, const std::optional<double>& default_number, bool is_integer);

 private:
  enum class State { Idle, Running, Suspended, Finished };

  bool LaunchThread(const std::string& code, const std::string& chunk_name, bool as_expression);
  void ThreadMain(std::string code, std::string chunk_name, bool as_expression);
  // Blocks the calling (UI) thread until the worker reaches Suspended or
  // Finished; joins and returns final_ok_ in the Finished case.
  bool WaitUntilSuspendedOrFinished();

  Application& app_;
  std::vector<std::string> output_;
  std::string print_buffer_;

  std::thread worker_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  State state_ = State::Idle;
  ScriptRequest request_;
  bool resume_ready_ = false;
  bool resume_is_value_ = false;  // false means ResumeNil (Enter/nothing given), not a real point/text/number answer
  kernel::Point3d resume_point_;
  std::string resume_text_;
  double resume_number_ = 0;
  bool abort_requested_ = false;
  bool final_ok_ = true;
  bool cancelled_ = false;  // set by WaitForPoint right before throwing, read back in ThreadMain's catch
  std::string chunk_name_;
};

}  // namespace dino8::app
