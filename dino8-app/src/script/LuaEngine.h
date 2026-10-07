// Embedded Lua 5.4 scripting with a rhinoscriptsyntax-like `rs` module.
//
// Rhino scripts with RhinoScript / Python; Dino 8 cannot ship CPython in a
// small offline installer, so it embeds Lua (a ~300 KB, plain-C
// interpreter) and exposes the familiar rs.* names. Scripts run in a Lua
// coroutine so rs.GetPoint / rs.GetObjects / rs.GetString can suspend the
// script while the user picks in a viewport: the RunScript command stays
// active, forwards the pick, and resumes the coroutine. Script tokens typed
// after the file name ("RunScript t.lua 5,5,5") feed those prompts exactly
// like they feed any other command.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "doc/Document.h"

struct lua_State;

namespace dino8::app {

class Application;

// One rs.* function: the registration table doubles as the reference the
// Help > Scripting Reference panel shows, so the two never drift apart.
struct RsFunctionDoc {
  const char* name;       // "AddPoint"
  const char* signature;  // "rs.AddPoint(x, y, z) or rs.AddPoint({x,y,z})"
  const char* doc;        // one line
};

// What a suspended script is waiting for.
enum class ScriptWant { Nothing, Point, Objects, Text, Number, Integer };

struct ScriptRequest {
  ScriptWant want = ScriptWant::Nothing;
  std::string prompt;
  int min_objects = 1;
  bool single_object = false;  // rs.GetObject: resume with one id, not a list
  std::optional<double> default_number;
  std::optional<std::string> default_text;
};

class LuaEngine {
 public:
  explicit LuaEngine(Application& app);
  ~LuaEngine();
  LuaEngine(const LuaEngine&) = delete;
  LuaEngine& operator=(const LuaEngine&) = delete;

  // Starts `code` in a fresh coroutine and runs it until it finishes, fails
  // or suspends on an rs.Get* prompt. Returns false when it failed to
  // compile or raised an error (already printed as "! Script error: ...").
  // `chunk_name` is what error messages call the script ("t.lua").
  bool Start(const std::string& code, const std::string& chunk_name);
  // Reads the file and calls Start(). Also usable for command (.txt) files:
  // those are run line by line through the command engine instead.
  bool StartFile(const std::string& path);
  // Compiles `expr` as an expression first ("= 2+3" prints 5) and as a
  // statement otherwise.
  bool StartExpression(const std::string& expr);

  bool Running() const { return thread_ != nullptr; }
  bool Suspended() const { return thread_ != nullptr && request_.want != ScriptWant::Nothing; }
  const ScriptRequest& Request() const { return request_; }

  // Resumes a suspended script with the value it asked for. Each returns
  // false when the script then failed.
  bool ResumePoint(kernel::Point3d p);
  bool ResumeObjects(const std::vector<ObjectId>& ids);
  bool ResumeText(const std::string& text);
  bool ResumeNumber(double v);
  bool ResumeNil();   // Enter / nothing picked
  void Abort();       // Esc: drop the coroutine

  // Output lines produced by the last run (print / errors), for the editor.
  const std::vector<std::string>& LastOutput() const { return output_; }
  void Print(const std::string& line);

  // Every rs.* function, for the reference panel.
  static const std::vector<RsFunctionDoc>& ApiDocs();

  Application& App() { return app_; }
  // True while the coroutine is executing Lua (rs.* calls arrive from it).
  bool InScript() const { return in_script_; }
  // Called by rs.Get*: records the request; the caller then yields.
  void SetRequest(ScriptRequest r) { request_ = std::move(r); }

 private:
  bool Resume(int nargs);
  void Finish(bool ok);
  bool StartThread(const std::string& code, const std::string& chunk_name, bool as_expression);

  Application& app_;
  lua_State* L_ = nullptr;        // main state (globals persist across runs)
  lua_State* thread_ = nullptr;   // running coroutine, null when idle
  int thread_ref_ = 0;            // registry anchor for thread_
  ScriptRequest request_;
  std::vector<std::string> output_;
  std::string chunk_name_;
  bool in_script_ = false;
  bool print_results_ = false;    // "= expr": print the returned values
};

}  // namespace dino8::app
