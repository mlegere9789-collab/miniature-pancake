// Machine-readable test evidence (master plan, section 7.5): ONE JSON object per line for every check and every test.
//
// When the environment variable DINO8_TEST_JSONL names a file, the test runner appends to it (the file is created when
// missing and never truncated, so several processes - one per filter, one per shard - can share it):
//
//   {"kind":"run","suite":"kernel","pid":123,"sha":"<commit>","shard":"3/12","filter":"Hairline"}
//   {"kind":"check","suite":"kernel","pid":123,"test":"TestCurveLength","name":"<check label>","result":"ok","n":3,"ms":0.012}
//   {"kind":"test","suite":"kernel","pid":123,"test":"TestCurveLength","name":"TestCurveLength","result":"ok",
//        "checks":4,"failed":0,"idx":17,"ms":1.234}
//   {"kind":"summary","suite":"kernel","pid":123,"tests":1,"checks":4,"ok":4,"failed":0,"thrown":0,"ms":1.5}
//
// check.result is "ok" or "fail". test.result is "ok" (no failed check), "fail" (at least one failed check) or "throw"
// (the test function ended with an uncaught exception; its message is in "error"). `n` numbers the checks of one test from
// 1, `ms` of a check is the time since the previous check (or the start of the test), `idx` of a test is its position among
// the tests that passed DINO8_TEST_FILTER (the number DINO8_TEST_SHARD=k/n takes modulo n). The summary line is written at
// normal process exit; a run that died (crash, watchdog kill) has none, which the validator
// (parity/tools/evidence_summary.py) reports. `sha` is DINO8_EVIDENCE_SHA, else GITHUB_SHA, when either is set.
//
// Printed counts, for comparison: "ok" check lines are the stdout lines starting "ok", failed checks are the stderr lines
// starting "FAILED" (one more per test that threw). The test names "ON::..." (library begin/end) are infrastructure and
// are never recorded.
//
// With DINO8_TEST_JSONL unset nothing is opened or written and the console output and the exit code are unchanged.
// Header-only on purpose: any test main (dino8_kernel_tests, dino8_continuity2_tests) includes it without linking more.
// Define D8EV_SUITE before the include to name the suite (default "kernel").
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <process.h>
#define D8EV_GETPID() static_cast<long>(_getpid())
#else
#include <unistd.h>
#define D8EV_GETPID() static_cast<long>(getpid())
#endif

#ifndef D8EV_SUITE
#define D8EV_SUITE "kernel"
#endif

namespace d8ev {

namespace detail {

using Clock = std::chrono::steady_clock;

struct State {
  std::mutex mu;
  std::FILE* file = nullptr;
  bool tried = false;
  std::string test;  // the test being run ("" outside any test)
  bool in_test = false;
  long test_checks = 0;
  long test_failed = 0;
  long idx = -1;          // set by NoteSelected() for the next TestBegin()
  long test_idx = -1;     // idx of the running test
  long tests = 0, checks = 0, ok = 0, failed = 0, thrown = 0;
  Clock::time_point t_run, t_test, t_last;
};

inline State& S() {
  static State s;
  return s;
}

// Appends `s` as a JSON string literal (quotes included). Control characters are escaped, bytes that are not valid UTF-8 become U+FFFD.
inline void Quote(std::string& o, const char* s) {
  o.push_back('"');
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s ? s : "");
  while (*p) {
    const unsigned char c = *p;
    if (c == '"') { o += "\\\""; ++p; }
    else if (c == '\\') { o += "\\\\"; ++p; }
    else if (c < 0x20) {
      if (c == '\n') o += "\\n";
      else if (c == '\t') o += "\\t";
      else if (c == '\r') o += "\\r";
      else { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned>(c)); o += b; }
      ++p;
    } else if (c < 0x80) { o.push_back(static_cast<char>(c)); ++p; }
    else {
      int len = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
      bool good = len > 0;
      for (int i = 1; good && i < len; ++i) good = (p[i] & 0xC0) == 0x80;  // stops at the terminating NUL, never reads past it
      if (good && len == 3) {
        const unsigned cp = ((c & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        good = cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF);
      }
      if (good && len == 4) {
        const unsigned cp = ((c & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
        good = cp >= 0x10000 && cp <= 0x10FFFF;
      }
      if (good) { o.append(reinterpret_cast<const char*>(p), static_cast<size_t>(len)); p += len; }
      else { o += "\\ufffd"; ++p; }
    }
  }
  o.push_back('"');
}

inline double MsSince(Clock::time_point t0, Clock::time_point t1) {
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// Common start of every line; the caller appends its own fields and the closing brace.
inline void Head(std::string& o, const char* kind) {
  o += "{\"kind\":\"";
  o += kind;
  o += "\",\"suite\":";
  Quote(o, D8EV_SUITE);
  char b[48];
  std::snprintf(b, sizeof b, ",\"pid\":%ld", D8EV_GETPID());
  o += b;
}

inline void Emit(State& s, std::string& line) {  // caller holds s.mu
  if (!s.file) return;
  line += "}\n";
  std::fwrite(line.data(), 1, line.size(), s.file);
  std::fflush(s.file);  // one line per write, and nothing lost if the process dies in the next test
}

inline void Summary() {
  State& s = S();
  std::lock_guard<std::mutex> g(s.mu);
  if (!s.file) return;
  std::string o;
  Head(o, "summary");
  char b[200];
  std::snprintf(b, sizeof b, ",\"tests\":%ld,\"checks\":%ld,\"ok\":%ld,\"failed\":%ld,\"thrown\":%ld,\"ms\":%.3f", s.tests, s.checks, s.ok,
                s.failed, s.thrown, MsSince(s.t_run, Clock::now()));
  o += b;
  Emit(s, o);
  std::fclose(s.file);
  s.file = nullptr;
}

// Opens the file on first use (caller holds s.mu). Returns false when evidence is off or the file cannot be opened.
inline bool Open(State& s) {
  if (s.file) return true;
  if (s.tried) return false;
  s.tried = true;
  const char* path = std::getenv("DINO8_TEST_JSONL");
  if (!path || !*path) return false;
  s.file = std::fopen(path, "a");
  if (!s.file) {
    std::fprintf(stderr, "DINO8_TEST_JSONL: cannot open '%s' for appending; no evidence will be written\n", path);
    return false;
  }
  s.t_run = s.t_last = Clock::now();
  std::string o;
  Head(o, "run");
  const char* sha = std::getenv("DINO8_EVIDENCE_SHA");
  if (!sha || !*sha) sha = std::getenv("GITHUB_SHA");
  if (sha && *sha) { o += ",\"sha\":"; Quote(o, sha); }
  const char* shard = std::getenv("DINO8_TEST_SHARD");
  if (shard && *shard) { o += ",\"shard\":"; Quote(o, shard); }
  const char* filter = std::getenv("DINO8_TEST_FILTER");
  if (filter && *filter) { o += ",\"filter\":"; Quote(o, filter); }
  Emit(s, o);
  std::atexit(&Summary);  // registered after S() was constructed, so it runs before the state is destroyed
  return true;
}

inline bool Infra(const char* name) { return name && std::strncmp(name, "ON::", 4) == 0; }

}  // namespace detail

// test_registry.cpp's ShouldRun() tells the evidence log which registration index the test it just accepted has.
inline void NoteSelected(long idx) {
  detail::State& s = detail::S();
  std::lock_guard<std::mutex> g(s.mu);
  s.idx = idx;
}

// A test function is about to run.
inline void TestBegin(const char* name) {
  detail::State& s = detail::S();
  std::lock_guard<std::mutex> g(s.mu);
  if (!detail::Open(s) || detail::Infra(name)) return;
  s.test = name ? name : "";
  s.in_test = true;
  s.test_checks = s.test_failed = 0;
  s.test_idx = s.idx;
  s.idx = -1;
  s.t_test = s.t_last = detail::Clock::now();
}

// The test function returned (threw=false) or ended in an uncaught exception whose message is `error`.
inline void TestEnd(const char* name, bool threw, const char* error = nullptr) {
  detail::State& s = detail::S();
  std::lock_guard<std::mutex> g(s.mu);
  if (!s.file || detail::Infra(name) || !s.in_test) return;
  const char* result = threw ? "throw" : (s.test_failed > 0 ? "fail" : "ok");
  std::string o;
  detail::Head(o, "test");
  o += ",\"test\":";
  detail::Quote(o, s.test.c_str());
  o += ",\"name\":";
  detail::Quote(o, s.test.c_str());
  o += ",\"result\":\"";
  o += result;
  o += "\"";
  char b[160];
  std::snprintf(b, sizeof b, ",\"checks\":%ld,\"failed\":%ld", s.test_checks, s.test_failed);
  o += b;
  if (s.test_idx >= 0) { std::snprintf(b, sizeof b, ",\"idx\":%ld", s.test_idx); o += b; }
  std::snprintf(b, sizeof b, ",\"ms\":%.3f", detail::MsSince(s.t_test, detail::Clock::now()));
  o += b;
  if (threw) { o += ",\"error\":"; detail::Quote(o, error ? error : "non-std exception"); ++s.thrown; }
  detail::Emit(s, o);
  ++s.tests;
  s.in_test = false;
  s.test.clear();
}

// One check, in the test that is running. Mirrors the console line the runner printed for it.
inline void Check(bool ok, const char* what) {
  detail::State& s = detail::S();
  std::lock_guard<std::mutex> g(s.mu);
  if (!detail::Open(s)) return;
  const auto now = detail::Clock::now();
  ++s.checks;
  if (ok) ++s.ok; else ++s.failed;
  ++s.test_checks;
  if (!ok) ++s.test_failed;
  std::string o;
  detail::Head(o, "check");
  o += ",\"test\":";
  detail::Quote(o, s.test.c_str());
  o += ",\"name\":";
  detail::Quote(o, what);
  o += ok ? ",\"result\":\"ok\"" : ",\"result\":\"fail\"";
  char b[96];
  std::snprintf(b, sizeof b, ",\"n\":%ld,\"ms\":%.3f", s.test_checks, detail::MsSince(s.t_last, now));
  o += b;
  detail::Emit(s, o);
  s.t_last = now;
}

}  // namespace d8ev
