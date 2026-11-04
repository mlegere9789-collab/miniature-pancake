// A small AutoLISP-style Lisp dialect (PARITY_MAP.md's "AutoLISP-equivalent
// command scripting language"). It is a SUBSET of AutoLISP, not Autodesk's
// interpreter: dynamic scoping with (defun f (args / locals) ...), the usual
// special forms and list / string / math functions, (command ...) driving the
// app's command line, (defun c:name () ...) defining a new typed command, and
// a few drawing queries (entlast, ssget "X", sslength, ssname). It has no
// dependency on the rest of the app: the app plugs in through LispHost.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace dino8::lisp {

struct Value;
using List = std::vector<Value>;
struct Func {
  std::vector<std::string> params;
  std::vector<std::string> locals;
  List body;
};

struct Value {
  enum class T { Nil, True, Int, Real, Str, Sym, List, Subr, Lambda };
  T t = T::Nil;
  long long i = 0;
  double r = 0;
  std::string s;                 // string text, symbol name (lowercase), subr name
  std::shared_ptr<List> l;       // list elements
  std::shared_ptr<Func> f;       // lambda
  bool dotted = false;           // a List of exactly two elements printed and taken apart as a dotted pair (a . b)
  bool pickset = false;          // a List that is a selection set: ssadd / ssdel change it in place
};

class LispError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// One DXF-style group of an entity: (code . value) or, for a point, (code x y z).
struct DxfItem {
  enum class K { Str, Real, Int, Point };
  int code = 0;
  K k = K::Str;
  std::string s;
  double x = 0, y = 0, z = 0;    // Real: x; Point: x y z
  long long i = 0;
};

// What the interpreter needs from the application. Unset members make the
// matching function report "not available" instead of crashing.
struct LispHost {
  // (command "Box" "0,0,0" ...): the pieces, "" meaning Enter. Returns false
  // when the command line failed.
  std::function<bool(const std::vector<std::string>&)> command;
  std::function<void(const std::string&)> print;                 // one finished output line
  std::function<std::optional<long long>()> last_entity;         // (entlast)
  std::function<std::vector<long long>()> all_entities;          // (ssget "X")
  std::function<void(const std::string& cmd_name)> on_define_command;  // (defun c:name ...)
  std::function<bool(const std::string& path, std::string& text)> read_file;  // (load "file.lsp")
  // Entity access (entget / entmod / entdel / entnext). Entity names are the object ids.
  std::function<bool(long long id, std::vector<DxfItem>& out)> ent_get;      // false: no such entity
  std::function<std::string(long long id, const std::vector<DxfItem>& items)> ent_mod;  // "" ok, else the reason it was refused
  std::function<bool(long long id)> ent_del;
  std::function<std::optional<long long>(std::optional<long long> after)> ent_next;  // nullopt after: the first entity
  // The next queued typed input for getint / getreal / getpoint / getstring / getkword (nullopt: none left).
  std::function<std::optional<std::string>()> next_input;
};

class Interp {
 public:
  Interp();
  LispHost host;

  // Evaluates every top-level form in `text`; returns the last value. Throws
  // LispError on a syntax or run-time error (forms before it keep their effects).
  Value EvalText(const std::string& text);
  // Calls the user function `name` (e.g. "c:foo") with no arguments.
  Value CallNamed(const std::string& name);
  bool IsDefined(const std::string& name) const;
  // How a value prints at the command line (AutoLISP prin1 style).
  static std::string Format(const Value& v, bool quote_strings);

  // Parsing helpers (also used by `read`).
  static List ParseAll(const std::string& text);
  // "(setq n (getint \"n: \")) 5 7": splits a line that starts with "(" into its leading
  // balanced forms and the typed input that follows them (the queue the get* functions
  // read). A line that does not start with "(" or never balances is all `forms`.
  static void SplitInput(const std::string& text, std::string& forms, std::string& rest);

  Value Eval(const Value& v);
  Value Apply(const Value& fn, List args);
  std::map<std::string, Value>& Globals() { return globals_; }

  // Flushes any partial princ line.
  void FlushOutput();

 private:
  friend struct Builtins;
  Value EvalList(const Value& form);
  Value CallUser(const Func& f, List args);
  void Emit(const std::string& text);

  std::map<std::string, Value> globals_;
  int init_bits_ = 0;             // (initget bits "Keyword ...") applies to the next get* call only
  std::string init_kw_;
  std::string out_buf_;
  int depth_ = 0;
  long long steps_ = 0;
};

}  // namespace dino8::lisp
