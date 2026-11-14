// Spike R1 boundary probe: a small C++17 program (NOT the Dino 8 application) that drives the
// Rust operation log through the generated C ABI.
//
// It checks behavior (round trip, errors, handle safety, panic containment, exception policy for
// callbacks, crash recovery, threads) and measures the cost of crossing the boundary. Build it
// plain, or under -fsanitize=address,undefined or thread; see ../scripts/run_probe.sh and
// CMakeLists.txt.
//
// Usage: probe_test [--ops N] [--file-ops N] [--fsync-ops N] [--threads T] [--dir PATH]
//                   [--no-bench] [--verbose]

#include "dino8_probe.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define D8_PROBE_POSIX 0
#else
#define D8_PROBE_POSIX 1
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

// ---------------------------------------------------------------------------------------------
// Tiny test harness
// ---------------------------------------------------------------------------------------------

int g_checks = 0;
int g_failed = 0;
bool g_verbose = false;

#define CHECK(cond)                                                                  \
  do {                                                                               \
    ++g_checks;                                                                      \
    if (!(cond)) {                                                                   \
      ++g_failed;                                                                    \
      std::fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                                \
  } while (0)

#define CHECK_EQ(a, b)                                                                    \
  do {                                                                                    \
    ++g_checks;                                                                           \
    const auto va_ = (a);                                                                 \
    const auto vb_ = (b);                                                                 \
    if (!(va_ == vb_)) {                                                                  \
      ++g_failed;                                                                         \
      std::fprintf(stderr, "CHECK_EQ FAILED %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, \
                   __LINE__, #a, #b, static_cast<long long>(va_),                         \
                   static_cast<long long>(vb_));                                          \
    }                                                                                     \
  } while (0)

using Clock = std::chrono::steady_clock;

int64_t ns_since(Clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
}

// ---------------------------------------------------------------------------------------------
// RAII wrapper: the C++ side owns the handle and closes it exactly once.
// ---------------------------------------------------------------------------------------------

class Log {
 public:
  explicit Log(const char* path = nullptr, uint32_t flags = 0) {
    D8Error err = D8_ERROR_INIT;
    D8Status st = d8_log_open(path, flags, &h_, &err);
    if (st != D8_OK) {
      throw std::runtime_error(std::string("d8_log_open: ") + d8_status_name(st) + ": " +
                               err.message);
    }
  }
  ~Log() { close(); }
  Log(const Log&) = delete;
  Log& operator=(const Log&) = delete;

  D8LogHandle handle() const { return h_; }
  D8Status close() {
    D8Error err = D8_ERROR_INIT;
    return d8_log_close(&h_, &err);  // zeroes h_, so a second close is a no-op
  }

 private:
  D8LogHandle h_{0};
};

D8ObjectId make_id(uint64_t a, uint64_t b) {
  D8ObjectId id;
  std::memcpy(id.bytes, &a, 8);
  std::memcpy(id.bytes + 8, &b, 8);
  return id;
}

// A representative command record: 64-byte parameter blob, two input ids, one output id.
struct OpFixture {
  std::array<uint8_t, 64> params{};
  std::array<D8ObjectId, 2> in{};
  std::array<D8ObjectId, 1> out{};
  D8OpRecord rec{};

  OpFixture() {
    for (size_t i = 0; i < params.size(); ++i) params[i] = static_cast<uint8_t>(i * 7 + 1);
    rec.command_id = 1;
    rec.tool_version = 800;
    rec.random_seed = 0x9E3779B97F4A7C15ULL;
    rec.params = params.data();
    rec.params_len = params.size();
    rec.inputs = in.data();
    rec.n_inputs = in.size();
    rec.outputs = out.data();
    rec.n_outputs = out.size();
    vary(0);
  }
  // Cheap per-call variation so that stored content differs between records.
  void vary(uint64_t k) {
    std::memcpy(params.data(), &k, 8);
    in[0] = make_id(k, 1);
    in[1] = make_id(k, 2);
    out[0] = make_id(k, 3);
    rec.command_id = static_cast<uint32_t>(k % 1070);
  }
};

// ---------------------------------------------------------------------------------------------
// Behavior tests
// ---------------------------------------------------------------------------------------------

void test_abi_and_names() {
  CHECK_EQ(d8_abi_version(), static_cast<uint32_t>(D8_ABI_VERSION));
  CHECK(std::strcmp(d8_status_name(D8_OK), "D8_OK") == 0);
  CHECK(std::strcmp(d8_status_name(D8_ERR_PANIC), "D8_ERR_PANIC") == 0);
  CHECK(std::strcmp(d8_status_name(12345), "D8_ERR_UNKNOWN") == 0);
  CHECK_EQ(d8_probe_nothing(41), 42u);
  D8Error err = D8_ERROR_INIT;
  CHECK_EQ(d8_probe_noop(&err), D8_OK);
  CHECK_EQ(err.code, D8_OK);
  CHECK_EQ(err.message[0], '\0');
  CHECK_EQ(d8_probe_noop(nullptr), D8_OK);  // the error record is optional
}

void test_roundtrip() {
  Log log;
  D8Error err = D8_ERROR_INIT;

  OpFixture f;
  f.vary(12345);
  f.rec.command_id = 777;
  uint64_t seq = 0;
  CHECK_EQ(d8_log_append(log.handle(), &f.rec, &seq, &err), D8_OK);
  CHECK_EQ(seq, 1u);

  uint8_t pbuf[128];
  D8ObjectId ibuf[4], obuf[4];
  D8OpRead rd{};
  rd.params_buf = pbuf;
  rd.params_cap = sizeof pbuf;
  rd.inputs_buf = ibuf;
  rd.inputs_cap = 4;
  rd.outputs_buf = obuf;
  rd.outputs_cap = 4;
  CHECK_EQ(d8_log_read(log.handle(), 1, &rd, &err), D8_OK);
  CHECK_EQ(rd.seq, 1u);
  CHECK_EQ(rd.kind, static_cast<uint32_t>(D8_KIND_OP));
  CHECK_EQ(rd.command_id, 777u);
  CHECK_EQ(rd.tool_version, 800u);
  CHECK_EQ(rd.random_seed, 0x9E3779B97F4A7C15ULL);
  CHECK_EQ(rd.params_len, f.params.size());
  CHECK(std::memcmp(pbuf, f.params.data(), f.params.size()) == 0);
  CHECK_EQ(rd.n_inputs, 2u);
  CHECK(std::memcmp(&ibuf[0], &f.in[0], 16) == 0 && std::memcmp(&ibuf[1], &f.in[1], 16) == 0);
  CHECK_EQ(rd.n_outputs, 1u);
  CHECK(std::memcmp(&obuf[0], &f.out[0], 16) == 0);

  // Undo marker: records which operation it undoes; a second undo has nothing left.
  uint64_t marker = 0, undone = 0;
  CHECK_EQ(d8_log_undo_marker(log.handle(), &marker, &undone, &err), D8_OK);
  CHECK_EQ(marker, 2u);
  CHECK_EQ(undone, 1u);
  rd = D8OpRead{};
  rd.params_buf = pbuf;
  rd.params_cap = sizeof pbuf;
  CHECK_EQ(d8_log_read(log.handle(), 2, &rd, &err), D8_OK);
  CHECK_EQ(rd.kind, static_cast<uint32_t>(D8_KIND_UNDO));
  CHECK_EQ(rd.ref_seq, 1u);
  CHECK_EQ(d8_log_undo_marker(log.handle(), nullptr, nullptr, &err), D8_ERR_NOTHING_TO_UNDO);
  CHECK_EQ(err.code, D8_ERR_NOTHING_TO_UNDO);

  uint64_t len = 0;
  CHECK_EQ(d8_log_len(log.handle(), &len, &err), D8_OK);
  CHECK_EQ(len, 2u);
  D8LogInfo info{};
  CHECK_EQ(d8_log_info(log.handle(), &info, &err), D8_OK);
  CHECK_EQ(info.records, 2u);
  CHECK_EQ(info.live_ops, 0u);
  CHECK_EQ(info.segments, 1u);
  CHECK_EQ(info.wal_bytes, 0u);
  CHECK_EQ(d8_log_sync(log.handle(), &err), D8_OK);  // no-op for an in-memory log
}

void test_errors() {
  Log log;
  D8Error err = D8_ERROR_INIT;
  OpFixture f;
  uint64_t seq = 0;

  // NULL arguments are reported, never dereferenced.
  CHECK_EQ(d8_log_append(log.handle(), nullptr, &seq, &err), D8_ERR_NULL_ARG);
  CHECK(std::strstr(err.message, "op") != nullptr);
  CHECK_EQ(d8_log_append(log.handle(), &f.rec, nullptr, &err), D8_OK);  // out_seq is optional
  CHECK_EQ(d8_log_open(nullptr, 0, nullptr, &err), D8_ERR_NULL_ARG);
  D8OpRecord bad = f.rec;
  bad.params = nullptr;  // non-zero length with a NULL pointer
  CHECK_EQ(d8_log_append(log.handle(), &bad, &seq, &err), D8_ERR_NULL_ARG);
  bad = f.rec;
  bad.params_len = D8_MAX_PARAMS_LEN + 1;
  CHECK_EQ(d8_log_append(log.handle(), &bad, &seq, &err), D8_ERR_LIMIT);
  bad = f.rec;
  bad.params_len = SIZE_MAX / 2 + 7;  // absurd length is refused before any slice is formed
  CHECK_EQ(d8_log_append(log.handle(), &bad, &seq, &err), D8_ERR_LIMIT);
  CHECK_EQ(d8_log_append_batch(log.handle(), &f.rec, 0, nullptr, &err), D8_ERR_INVALID_ARG);
  {
    D8LogHandle tmp{0};
    CHECK_EQ(d8_log_open(nullptr, 0xF0, &tmp, &err), D8_ERR_INVALID_ARG);  // unknown flags
    CHECK_EQ(tmp.id, 0u);
  }

  // Missing record, then a buffer that is too small: the true sizes come back for a retry.
  uint8_t small[8];
  D8OpRead rd{};
  rd.params_buf = small;
  rd.params_cap = sizeof small;
  CHECK_EQ(d8_log_read(log.handle(), 99, &rd, &err), D8_ERR_NOT_FOUND);
  CHECK_EQ(err.detail, 99);
  CHECK_EQ(d8_log_read(log.handle(), 0, &rd, &err), D8_ERR_NOT_FOUND);
  CHECK_EQ(d8_log_read(log.handle(), 1, &rd, &err), D8_ERR_BUFFER_TOO_SMALL);
  CHECK_EQ(rd.params_len, f.params.size());
  CHECK_EQ(rd.n_inputs, 2u);
  CHECK_EQ(rd.n_outputs, 1u);
  std::vector<uint8_t> big(rd.params_len);
  std::vector<D8ObjectId> ins(rd.n_inputs), outs(rd.n_outputs);
  rd.params_buf = big.data();
  rd.params_cap = big.size();
  rd.inputs_buf = ins.data();
  rd.inputs_cap = ins.size();
  rd.outputs_buf = outs.data();
  rd.outputs_cap = outs.size();
  CHECK_EQ(d8_log_read(log.handle(), 1, &rd, &err), D8_OK);  // retry with exact sizes

  // The message is always NUL terminated and fits the record, even for a long path.
  std::string long_path(1000, 'x');
  D8LogHandle tmp{0};
  CHECK(d8_log_open(("/nonexistent_dir_for_probe/" + long_path).c_str(), 0, &tmp, &err) == D8_ERR_IO);
  CHECK(std::memchr(err.message, '\0', sizeof err.message) != nullptr);
  CHECK(err.detail != 0);  // the OS error number
  CHECK_EQ(tmp.id, 0u);
}

void test_handle_safety() {
  D8Error err = D8_ERROR_INIT;
  D8LogHandle stale{0};
  {
    Log log;
    stale = log.handle();
    CHECK(stale.id != 0);
    CHECK_EQ(log.close(), D8_OK);
    CHECK_EQ(log.close(), D8_OK);  // the wrapper zeroed its copy: closing again is a no-op
  }
  OpFixture f;
  uint64_t seq = 0;
  // A stale copy of the handle is rejected with a status, not undefined behavior.
  CHECK_EQ(d8_log_append(stale, &f.rec, &seq, &err), D8_ERR_BAD_HANDLE);
  CHECK_EQ(d8_log_len(stale, &seq, &err), D8_ERR_BAD_HANDLE);
  D8LogHandle copy = stale;
  CHECK_EQ(d8_log_close(&copy, &err), D8_ERR_BAD_HANDLE);
  CHECK_EQ(copy.id, 0u);
  // Zero and forged handles likewise.
  D8LogHandle zero{0}, forged{0xDEADBEEFCAFEF00DULL};
  CHECK_EQ(d8_log_append(zero, &f.rec, &seq, &err), D8_ERR_BAD_HANDLE);
  CHECK_EQ(d8_log_append(forged, &f.rec, &seq, &err), D8_ERR_BAD_HANDLE);
  // A new log may reuse the slot but gets a different id; the stale copy stays dead.
  Log fresh;
  CHECK(fresh.handle().id != stale.id);
  CHECK_EQ(d8_log_append(stale, &f.rec, &seq, &err), D8_ERR_BAD_HANDLE);
  CHECK_EQ(d8_log_append(fresh.handle(), &f.rec, &seq, &err), D8_OK);
}

#if D8_PROBE_POSIX
// Silences stderr (the default Rust panic message goes there) unless --verbose.
class StderrSilencer {
 public:
  StderrSilencer() {
    if (g_verbose) return;
    std::fflush(stderr);
    saved_ = dup(2);
    int nul = open("/dev/null", O_WRONLY);
    if (nul >= 0) {
      dup2(nul, 2);
      close(nul);
    }
  }
  ~StderrSilencer() {
    if (saved_ >= 0) {
      std::fflush(stderr);
      dup2(saved_, 2);
      close(saved_);
    }
  }

 private:
  int saved_ = -1;
};
#else
class StderrSilencer {};
#endif

void test_panic_containment() {
  Log log, other;
  D8Error err = D8_ERROR_INIT;
  OpFixture f;
  uint64_t seq = 0;
  CHECK_EQ(d8_log_append(log.handle(), &f.rec, &seq, &err), D8_OK);

  const uint64_t drops_before = d8_probe_drop_count();
  bool exception_escaped = false;
  struct Case {
    uint32_t mode;
    const char* expect;
  };
  const Case cases[] = {{0, "static message"}, {1, "formatted message"}, {2, "non-string"}};
  for (const Case& c : cases) {
    D8Status st = D8_OK;
    {
      StderrSilencer quiet;
      try {
        st = d8_probe_trigger_panic(log.handle(), c.mode, &err);
      } catch (...) {
        exception_escaped = true;  // a panic that unwound into C++ would land here
      }
    }
    CHECK(!exception_escaped);
    CHECK_EQ(st, D8_ERR_PANIC);
    CHECK_EQ(err.code, D8_ERR_PANIC);
    CHECK(std::strstr(err.message, "panic caught at the C boundary") != nullptr);
    CHECK(std::strstr(err.message, c.expect) != nullptr);
    if (g_verbose) std::printf("  panic mode %u -> %s\n", c.mode, err.message);
    // The same handle keeps working: these panics left no state behind.
    CHECK_EQ(d8_log_append(log.handle(), &f.rec, &seq, &err), D8_OK);
  }
  // Destructors of Rust locals ran while the panic unwound inside the guard.
  CHECK_EQ(d8_probe_drop_count() - drops_before, 3u);

  // A panic while the log's internal lock is held poisons that log: it refuses work with a
  // status (fail-safe), can still be closed, and does not affect other logs.
  D8Status st = D8_OK;
  {
    StderrSilencer quiet;
    try {
      st = d8_probe_trigger_panic(log.handle(), 3, &err);
    } catch (...) {
      exception_escaped = true;
    }
  }
  CHECK(!exception_escaped);
  CHECK_EQ(st, D8_ERR_PANIC);
  CHECK_EQ(d8_log_append(log.handle(), &f.rec, &seq, &err), D8_ERR_POISONED);
  D8OpRead rd{};
  CHECK_EQ(d8_log_read(log.handle(), 1, &rd, &err), D8_ERR_POISONED);
  CHECK_EQ(d8_log_append(other.handle(), &f.rec, &seq, &err), D8_OK);
  CHECK_EQ(log.close(), D8_OK);
  CHECK_EQ(d8_log_append(log.handle(), &f.rec, &seq, &err), D8_ERR_BAD_HANDLE);
  CHECK_EQ(d8_probe_trigger_panic(other.handle(), 99, &err), D8_ERR_INVALID_ARG);
}

#if D8_PROBE_POSIX
// Runs fn in a forked child with output discarded; returns the raw wait status.
int run_in_child(void (*fn)()) {
  std::fflush(nullptr);
  pid_t pid = fork();
  if (pid == 0) {
    int nul = open("/dev/null", O_WRONLY);
    dup2(nul, 1);
    dup2(nul, 2);
    fn();
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return status;
}

void child_unguarded_panic() { d8_probe_unguarded_panic(); }

void test_unguarded_panic_aborts() {
  // What a boundary function WITHOUT catch_unwind would do: the Rust runtime aborts the process.
  // It must never unwind into C++ frames.
  int status = run_in_child(child_unguarded_panic);
  CHECK(WIFSIGNALED(status));
  CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  if (g_verbose) std::printf("  unguarded panic: child terminated by signal %d\n", WTERMSIG(status));
}
#endif

// A visitor that records what it sees.
struct VisitCtx {
  std::vector<uint64_t> seqs;
  uint64_t stop_at = 0;      // return non-zero at this sequence number
  bool reenter = false;      // call back into the library from inside the callback
  D8LogHandle handle{0};
  bool content_ok = true;
  int reenter_failures = 0;
  bool throw_exception = false;
  bool catch_exception = true;
};

extern "C" int32_t d8_probe_test_visit(void* user, const D8OpView* op) {
  auto* ctx = static_cast<VisitCtx*>(user);
  try {
    ctx->seqs.push_back(op->seq);
    if (op->kind == D8_KIND_OP) {
      uint64_t k = 0;
      std::memcpy(&k, op->params, 8);
      if (op->params_len != 64 || op->n_inputs != 2 || op->n_outputs != 1 ||
          std::memcmp(&op->inputs[0], make_id(k, 1).bytes, 16) != 0 ||
          op->command_id != static_cast<uint32_t>(k % 1070)) {
        ctx->content_ok = false;
      }
    }
    if (ctx->reenter) {
      // The library holds no lock while the callback runs, so it may be called back.
      uint64_t len = 0;
      D8Error err = D8_ERROR_INIT;
      if (d8_log_len(ctx->handle, &len, &err) != D8_OK) ++ctx->reenter_failures;
      OpFixture f;
      if (d8_log_append(ctx->handle, &f.rec, nullptr, &err) != D8_OK) ++ctx->reenter_failures;
    }
    if (ctx->throw_exception && op->seq == 3) throw std::runtime_error("kernel failure");
    return op->seq == ctx->stop_at ? 5 : 0;
  } catch (...) {
    if (!ctx->catch_exception) throw;  // only used in the forked abort test
    return -1;                         // policy: never let an exception cross into Rust
  }
}

void test_visit_callbacks() {
  Log log;
  D8Error err = D8_ERROR_INIT;
  OpFixture f;
  for (uint64_t k = 1; k <= 10; ++k) {
    f.vary(k);
    CHECK_EQ(d8_log_append(log.handle(), &f.rec, nullptr, &err), D8_OK);
  }
  VisitCtx ctx;
  CHECK_EQ(d8_log_visit(log.handle(), 1, 1000, d8_probe_test_visit, &ctx, &err), D8_OK);
  CHECK_EQ(ctx.seqs.size(), 10u);
  CHECK(ctx.content_ok);
  for (size_t i = 0; i < ctx.seqs.size(); ++i) CHECK_EQ(ctx.seqs[i], i + 1);

  ctx = VisitCtx{};
  ctx.stop_at = 4;
  CHECK_EQ(d8_log_visit(log.handle(), 2, 10, d8_probe_test_visit, &ctx, &err), D8_ERR_CALLBACK);
  CHECK_EQ(err.detail, 5);
  CHECK_EQ(ctx.seqs.size(), 3u);  // 2, 3, 4

  // The callback may call back into the library: no lock is held during the callback.
  ctx = VisitCtx{};
  ctx.reenter = true;
  ctx.handle = log.handle();
  CHECK_EQ(d8_log_visit(log.handle(), 1, 10, d8_probe_test_visit, &ctx, &err), D8_OK);
  CHECK_EQ(ctx.reenter_failures, 0);
  uint64_t len = 0;
  CHECK_EQ(d8_log_len(log.handle(), &len, &err), D8_OK);
  CHECK_EQ(len, 20u);

  // Exception policy: the C++ trampoline catches and converts, so Rust only sees an int.
  ctx = VisitCtx{};
  ctx.throw_exception = true;
  CHECK_EQ(d8_log_visit(log.handle(), 1, 10, d8_probe_test_visit, &ctx, &err), D8_ERR_CALLBACK);
  CHECK_EQ(err.detail, -1);
  CHECK_EQ(d8_log_visit(log.handle(), 1, 10, nullptr, &ctx, &err), D8_ERR_NULL_ARG);
  CHECK_EQ(d8_log_visit(log.handle(), 0, 10, d8_probe_test_visit, &ctx, &err), D8_ERR_INVALID_ARG);
}

#if D8_PROBE_POSIX
void child_escaping_exception() {
  Log log;
  D8Error err = D8_ERROR_INIT;
  OpFixture f;
  for (uint64_t k = 1; k <= 5; ++k) {
    f.vary(k);
    d8_log_append(log.handle(), &f.rec, nullptr, &err);
  }
  VisitCtx ctx;
  ctx.throw_exception = true;
  ctx.catch_exception = false;  // the mistake: let the exception escape the callback
  d8_log_visit(log.handle(), 1, 5, d8_probe_test_visit, &ctx, &err);
}

void test_escaping_exception_aborts() {
  // What happens if a C++ exception escapes a callback into Rust frames: the process aborts
  // (Rust marks extern "C" frames as unable to unwind); it is never silently swallowed.
  int status = run_in_child(child_escaping_exception);
  if (g_verbose) {
    if (WIFSIGNALED(status)) {
      std::printf("  escaping C++ exception: child terminated by signal %d\n", WTERMSIG(status));
    } else {
      std::printf("  escaping C++ exception: child exited with code %d\n", WEXITSTATUS(status));
    }
  }
  CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}
#endif

std::string temp_path(const std::string& dir, const char* tag) {
  return dir + "/d8probe_" + tag + "_" + std::to_string(
#if D8_PROBE_POSIX
                                              static_cast<long long>(getpid())
#else
                                              0LL
#endif
                                                  ) +
         ".d8log";
}

long long file_size(const std::string& path) {
  std::FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp) return -1;
  std::fseek(fp, 0, SEEK_END);
  long long n = std::ftell(fp);
  std::fclose(fp);
  return n;
}

void test_file_recovery(const std::string& dir) {
  const std::string path = temp_path(dir, "recover");
  std::remove(path.c_str());
  D8Error err = D8_ERROR_INIT;
  OpFixture f;
  {
    Log log(path.c_str(), D8_LOG_SYNC_WRITE | D8_LOG_TRUNCATE);
    for (uint64_t k = 1; k <= 1000; ++k) {
      f.vary(k);
      CHECK_EQ(d8_log_append(log.handle(), &f.rec, nullptr, &err), D8_OK);
    }
    for (int i = 0; i < 3; ++i) CHECK_EQ(d8_log_undo_marker(log.handle(), nullptr, nullptr, &err), D8_OK);
    D8LogInfo info{};
    CHECK_EQ(d8_log_info(log.handle(), &info, &err), D8_OK);
    CHECK_EQ(info.records, 1003u);
    CHECK_EQ(info.live_ops, 997u);
    CHECK(info.wal_bytes > 1003u * 100);
  }
  const long long full = file_size(path);
  CHECK(full > 0);
  {  // clean reopen: everything is back, the undo state included
    Log log(path.c_str(), D8_LOG_SYNC_WRITE);
    D8LogInfo info{};
    CHECK_EQ(d8_log_info(log.handle(), &info, &err), D8_OK);
    CHECK_EQ(info.records, 1003u);
    CHECK_EQ(info.recovered_records, 1003u);
    CHECK_EQ(info.live_ops, 997u);
    CHECK_EQ(info.discarded_tail_bytes, 0u);
    uint64_t marker = 0, undone = 0;
    CHECK_EQ(d8_log_undo_marker(log.handle(), &marker, &undone, &err), D8_OK);
    CHECK_EQ(marker, 1004u);
    CHECK_EQ(undone, 997u);
  }
#if D8_PROBE_POSIX
  // Simulate a crash in the middle of the last write: cut 7 bytes off the file.
  CHECK_EQ(truncate(path.c_str(), static_cast<off_t>(file_size(path) - 7)), 0);
  {
    Log log(path.c_str(), D8_LOG_SYNC_WRITE);
    D8LogInfo info{};
    CHECK_EQ(d8_log_info(log.handle(), &info, &err), D8_OK);
    CHECK_EQ(info.records, 1003u);  // the 1004th (torn) record is gone, nothing else
    CHECK(info.discarded_tail_bytes > 0);
    CHECK_EQ(d8_log_undo_marker(log.handle(), nullptr, nullptr, &err), D8_OK);  // appendable again
  }
#endif
  // A file that is not a log is refused and left untouched.
  {
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    const char junk[] = "this is definitely not a Dino 8 log file";
    std::fwrite(junk, 1, sizeof junk, fp);
    std::fclose(fp);
    D8LogHandle h{0};
    CHECK_EQ(d8_log_open(path.c_str(), 0, &h, &err), D8_ERR_CORRUPT);
    CHECK_EQ(file_size(path), static_cast<long long>(sizeof junk));
  }
  std::remove(path.c_str());
}

void test_threads(int nthreads) {
  Log log;
  const int per_thread = 50000;
  std::vector<std::vector<uint64_t>> got(nthreads);
  std::vector<std::thread> pool;
  std::atomic<int> failures{0};
  for (int t = 0; t < nthreads; ++t) {
    pool.emplace_back([&, t] {
      OpFixture f;
      D8Error err = D8_ERROR_INIT;
      got[t].reserve(per_thread);
      for (int i = 0; i < per_thread; ++i) {
        f.vary((static_cast<uint64_t>(t) << 32) | static_cast<uint64_t>(i));
        uint64_t seq = 0;
        if (d8_log_append(log.handle(), &f.rec, &seq, &err) != D8_OK) ++failures;
        got[t].push_back(seq);
      }
    });
  }
  for (auto& th : pool) th.join();
  CHECK_EQ(failures.load(), 0);
  std::vector<uint64_t> all;
  for (auto& v : got) {
    CHECK(std::is_sorted(v.begin(), v.end()));  // each thread sees increasing numbers
    all.insert(all.end(), v.begin(), v.end());
  }
  std::sort(all.begin(), all.end());
  const uint64_t total = static_cast<uint64_t>(nthreads) * per_thread;
  CHECK_EQ(all.size(), total);
  bool contiguous = true;
  for (uint64_t i = 0; i < total && contiguous; ++i) contiguous = all[i] == i + 1;
  CHECK(contiguous);  // unique and gap free
  // Spot-check content: record at seq s must carry the payload its writer produced.
  D8Error err = D8_ERROR_INIT;
  uint8_t pbuf[64];
  D8ObjectId ibuf[2], obuf[1];
  int bad = 0;
  for (int t = 0; t < nthreads; ++t) {
    for (int i = 0; i < per_thread; i += 997) {
      D8OpRead rd{};
      rd.params_buf = pbuf;
      rd.params_cap = sizeof pbuf;
      rd.inputs_buf = ibuf;
      rd.inputs_cap = 2;
      rd.outputs_buf = obuf;
      rd.outputs_cap = 1;
      uint64_t k = (static_cast<uint64_t>(t) << 32) | static_cast<uint64_t>(i);
      if (d8_log_read(log.handle(), got[t][i], &rd, &err) != D8_OK) {
        ++bad;
        continue;
      }
      uint64_t stored = 0;
      std::memcpy(&stored, pbuf, 8);
      if (stored != k || std::memcmp(&ibuf[0], make_id(k, 1).bytes, 16) != 0) ++bad;
    }
  }
  CHECK_EQ(bad, 0);
}

// ---------------------------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------------------------

struct Pct {
  double p50 = 0, p90 = 0, p99 = 0, p999 = 0, max = 0;
  size_t over_100us = 0, over_1ms = 0;
};

Pct percentiles(std::vector<uint32_t>& ns) {
  Pct p;
  if (ns.empty()) return p;
  auto at = [&](double q) {
    size_t k = static_cast<size_t>(q * static_cast<double>(ns.size() - 1));
    std::nth_element(ns.begin(), ns.begin() + static_cast<std::ptrdiff_t>(k), ns.end());
    return static_cast<double>(ns[k]) / 1000.0;
  };
  p.p50 = at(0.50);
  p.p90 = at(0.90);
  p.p99 = at(0.99);
  p.p999 = at(0.999);
  p.max = static_cast<double>(*std::max_element(ns.begin(), ns.end())) / 1000.0;
  for (uint32_t v : ns) {
    p.over_100us += v > 100000u;
    p.over_1ms += v > 1000000u;
  }
  return p;
}

volatile uint64_t g_sink = 0;

double timer_overhead_ns() {
  const int n = 2000000;
  auto t0 = Clock::now();
  int64_t acc = 0;
  for (int i = 0; i < n; ++i) {
    auto a = Clock::now();
    auto b = Clock::now();
    acc += std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
  }
  g_sink = static_cast<uint64_t>(acc);
  return static_cast<double>(ns_since(t0)) / n / 2.0;  // one Clock::now() call
}

struct BenchResult {
  double mean_us = 0;
  Pct pct;
};

BenchResult g_append_mem;
BenchResult g_append_mem_timed;

void report(const char* name, size_t n, double total_ms, const Pct* pct = nullptr) {
  std::printf("[bench] %-28s n=%-9zu total_ms=%10.2f mean_us=%9.4f", name, n, total_ms,
              total_ms * 1000.0 / static_cast<double>(n));
  if (pct) {
    std::printf(" p50_us=%.3f p90_us=%.3f p99_us=%.3f p999_us=%.3f max_us=%.3f over100us=%zu over1ms=%zu",
                pct->p50, pct->p90, pct->p99, pct->p999, pct->max, pct->over_100us, pct->over_1ms);
  }
  std::printf("\n");
  std::fflush(stdout);
}

void bench_floor(size_t n) {
  D8Error err = D8_ERROR_INIT;
  uint64_t acc = 0;
  auto t0 = Clock::now();
  for (size_t i = 0; i < n; ++i) acc += d8_probe_nothing(i);
  double ms = static_cast<double>(ns_since(t0)) / 1e6;
  g_sink = acc;
  report("bare_call(no guard)", n, ms);

  t0 = Clock::now();
  for (size_t i = 0; i < n; ++i) acc += static_cast<uint64_t>(d8_probe_noop(&err));
  ms = static_cast<double>(ns_since(t0)) / 1e6;
  g_sink = acc;
  report("guarded_noop(catch+err)", n, ms);

  Log log;
  uint64_t len = 0;
  t0 = Clock::now();
  for (size_t i = 0; i < n; ++i) {
    acc += static_cast<uint64_t>(d8_log_len(log.handle(), &len, &err));
    acc += len;
  }
  ms = static_cast<double>(ns_since(t0)) / 1e6;
  g_sink = acc;
  report("handle_lookup+len", n, ms);
}

// Times every call of an empty guarded function. Its tail is the noise floor of this machine
// (scheduler preemption, interrupts): nothing the library does can be faster or steadier.
void bench_noise_floor(size_t n) {
  D8Error err = D8_ERROR_INIT;
  std::vector<uint32_t> lat(n);
  auto t0 = Clock::now();
  for (size_t i = 0; i < n; ++i) {
    auto a = Clock::now();
    d8_probe_noop(&err);
    auto b = Clock::now();
    lat[i] = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
  }
  double ms = static_cast<double>(ns_since(t0)) / 1e6;
  Pct p = percentiles(lat);
  report("noise_floor(empty call,timed)", n, ms, &p);
}

// Appends n operations to an in-memory log; timed=true records every call for percentiles.
BenchResult bench_append(const char* name, size_t n, bool timed) {
  Log log;
  D8Error err = D8_ERROR_INIT;
  OpFixture f;
  uint64_t seq = 0;
  size_t failures = 0;
  BenchResult res;
  std::vector<uint32_t> lat;
  if (timed) lat.resize(n);
  auto t0 = Clock::now();
  if (timed) {
    for (size_t i = 0; i < n; ++i) {
      f.vary(i);
      auto a = Clock::now();
      D8Status st = d8_log_append(log.handle(), &f.rec, &seq, &err);
      auto b = Clock::now();
      failures += st != D8_OK;
      lat[i] = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
    }
  } else {
    for (size_t i = 0; i < n; ++i) {
      f.vary(i);
      failures += d8_log_append(log.handle(), &f.rec, &seq, &err) != D8_OK;
    }
  }
  double ms = static_cast<double>(ns_since(t0)) / 1e6;
  CHECK_EQ(failures, 0u);
  CHECK_EQ(seq, static_cast<uint64_t>(n));
  res.mean_us = ms * 1000.0 / static_cast<double>(n);
  if (timed) {
    // Where do the slow calls sit? Calls at an arena segment boundary would point at the library;
    // calls scattered at random point at the scheduler of this shared machine.
    const size_t per_segment = (4u << 20) / 160u;
    size_t shown = 0;
    for (size_t i = 0; i < n && shown < 8; ++i) {
      if (lat[i] > 500000u) {
        std::printf("[slow]  call #%zu took %.1f us (position in segment: %zu of %zu)\n", i,
                    lat[i] / 1000.0, i % per_segment, per_segment);
        ++shown;
      }
    }
    // The two growth paths of the log: a new 4 MiB arena segment and a new 8192-entry index page.
    auto slow_path = [&](size_t every, const char* what) {
      std::vector<uint32_t> v;
      for (size_t i = every; i < n; i += every) v.push_back(lat[i]);
      if (v.empty()) return;
      std::sort(v.begin(), v.end());
      std::printf("[info]  first append of each %s (%zu samples): median %.2f us, max %.2f us\n", what,
                  v.size(), v[v.size() / 2] / 1000.0, v.back() / 1000.0);
    };
    slow_path(per_segment, "4 MiB arena segment");
    slow_path(8192, "8192-entry index page");
    res.pct = percentiles(lat);
  }
  report(name, n, ms, timed ? &res.pct : nullptr);
  D8LogInfo info{};
  d8_log_info(log.handle(), &info, &err);
  if (!timed) {
    std::printf("[info]  in-memory arena: %.1f MiB in %" PRIu64 " segments, %.1f bytes/record\n",
                static_cast<double>(info.bytes_stored) / (1024.0 * 1024.0), info.segments,
                static_cast<double>(info.bytes_stored) / static_cast<double>(n));
  }
  return res;
}

void bench_batch(size_t n, size_t batch) {
  Log log;
  D8Error err = D8_ERROR_INIT;
  std::vector<OpFixture> fx(batch);
  std::vector<D8OpRecord> recs(batch);
  for (size_t i = 0; i < batch; ++i) {
    fx[i].vary(i);
    recs[i] = fx[i].rec;
  }
  size_t failures = 0;
  auto t0 = Clock::now();
  for (size_t done = 0; done < n; done += batch) {
    failures += d8_log_append_batch(log.handle(), recs.data(), batch, nullptr, &err) != D8_OK;
  }
  double ms = static_cast<double>(ns_since(t0)) / 1e6;
  CHECK_EQ(failures, 0u);
  char name[64];
  std::snprintf(name, sizeof name, "append_batch(%zu ops/call)", batch);
  report(name, n, ms);
}

void bench_read(size_t n) {
  Log log;
  D8Error err = D8_ERROR_INIT;
  OpFixture f;
  for (size_t i = 0; i < n; ++i) {
    f.vary(i);
    d8_log_append(log.handle(), &f.rec, nullptr, &err);
  }
  uint8_t pbuf[64];
  D8ObjectId ibuf[2], obuf[1];
  size_t failures = 0;
  auto t0 = Clock::now();
  for (size_t i = 1; i <= n; ++i) {
    D8OpRead rd{};
    rd.params_buf = pbuf;
    rd.params_cap = sizeof pbuf;
    rd.inputs_buf = ibuf;
    rd.inputs_cap = 2;
    rd.outputs_buf = obuf;
    rd.outputs_cap = 1;
    failures += d8_log_read(log.handle(), i, &rd, &err) != D8_OK;
    g_sink = rd.seq;
  }
  double ms = static_cast<double>(ns_since(t0)) / 1e6;
  CHECK_EQ(failures, 0u);
  report("read_back(sequential)", n, ms);
}

void bench_file(const std::string& dir, const char* name, uint32_t mode, size_t n, bool report_sync) {
  const std::string path = temp_path(dir, name);
  std::remove(path.c_str());
  {
    Log log(path.c_str(), mode | D8_LOG_TRUNCATE);
    D8Error err = D8_ERROR_INIT;
    OpFixture f;
    size_t failures = 0;
    auto t0 = Clock::now();
    for (size_t i = 0; i < n; ++i) {
      f.vary(i);
      failures += d8_log_append(log.handle(), &f.rec, nullptr, &err) != D8_OK;
    }
    double ms = static_cast<double>(ns_since(t0)) / 1e6;
    CHECK_EQ(failures, 0u);
    char label[64];
    std::snprintf(label, sizeof label, "append_file(%s)", name);
    report(label, n, ms);
    if (report_sync) {
      auto s0 = Clock::now();
      CHECK_EQ(d8_log_sync(log.handle(), &err), D8_OK);
      std::printf("[bench] %-28s one fdatasync after %zu buffered appends: %.3f ms\n", "log_sync", n,
                  static_cast<double>(ns_since(s0)) / 1e6);
    }
  }
  std::printf("[info]  log file: %.1f MiB (%.1f bytes/record)\n",
              static_cast<double>(file_size(path)) / (1024.0 * 1024.0),
              static_cast<double>(file_size(path)) / static_cast<double>(n));
  std::remove(path.c_str());
}

void bench_contended(int nthreads, size_t n_total) {
  Log log;
  std::vector<std::thread> pool;
  std::atomic<size_t> failures{0};
  const size_t per = n_total / static_cast<size_t>(nthreads);
  auto t0 = Clock::now();
  for (int t = 0; t < nthreads; ++t) {
    pool.emplace_back([&, t] {
      OpFixture f;
      D8Error err = D8_ERROR_INIT;
      size_t bad = 0;
      for (size_t i = 0; i < per; ++i) {
        f.vary((static_cast<uint64_t>(t) << 32) | i);
        bad += d8_log_append(log.handle(), &f.rec, nullptr, &err) != D8_OK;
      }
      failures += bad;
    });
  }
  for (auto& th : pool) th.join();
  double ms = static_cast<double>(ns_since(t0)) / 1e6;
  CHECK_EQ(failures.load(), 0u);
  char name[64];
  std::snprintf(name, sizeof name, "append_%d_threads(shared)", nthreads);
  report(name, per * static_cast<size_t>(nthreads), ms);
}

}  // namespace

int main(int argc, char** argv) {
  size_t ops = 1000000, file_ops = 200000, fsync_ops = 2000;
  int threads = 4;
  bool run_bench = true;
  std::string dir = ".";
  if (const char* t = std::getenv("TMPDIR")) dir = t;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
    if (a == "--ops") ops = std::strtoull(next(), nullptr, 10);
    else if (a == "--file-ops") file_ops = std::strtoull(next(), nullptr, 10);
    else if (a == "--fsync-ops") fsync_ops = std::strtoull(next(), nullptr, 10);
    else if (a == "--threads") threads = std::atoi(next());
    else if (a == "--dir") dir = next();
    else if (a == "--no-bench") run_bench = false;
    else if (a == "--verbose") g_verbose = true;
    else {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return 2;
    }
  }

  std::printf("== Dino 8 FFI probe (C++17 test program) ==\n");
  std::printf("abi: header=0x%08x library=0x%08x\n", static_cast<unsigned>(D8_ABI_VERSION),
              static_cast<unsigned>(d8_abi_version()));

  struct Test {
    const char* name;
    void (*fn)();
  };
  const Test tests[] = {
      {"abi_and_names", test_abi_and_names},
      {"roundtrip", test_roundtrip},
      {"errors", test_errors},
      {"handle_safety", test_handle_safety},
      {"panic_containment", test_panic_containment},
#if D8_PROBE_POSIX
      {"unguarded_panic_aborts", test_unguarded_panic_aborts},
#endif
      {"visit_callbacks", test_visit_callbacks},
#if D8_PROBE_POSIX
      {"escaping_exception_aborts", test_escaping_exception_aborts},
#endif
  };
  for (const Test& t : tests) {
    int before = g_failed;
    try {
      t.fn();
    } catch (const std::exception& e) {
      ++g_failed;
      std::fprintf(stderr, "test %s threw: %s\n", t.name, e.what());
    } catch (...) {
      ++g_failed;
      std::fprintf(stderr, "test %s threw a non-standard exception\n", t.name);
    }
    std::printf("[test] %-28s %s\n", t.name, g_failed == before ? "ok" : "FAILED");
    std::fflush(stdout);
  }
  try {
    int before = g_failed;
    test_file_recovery(dir);
    std::printf("[test] %-28s %s\n", "file_recovery", g_failed == before ? "ok" : "FAILED");
    before = g_failed;
    test_threads(threads);
    std::printf("[test] %-28s %s (%d threads)\n", "threads", g_failed == before ? "ok" : "FAILED", threads);
  } catch (const std::exception& e) {
    ++g_failed;
    std::fprintf(stderr, "test threw: %s\n", e.what());
  }

  bool gate_ok = true;
  if (run_bench && ops > 0) {
    std::printf("\n-- measurements (ops=%zu, record = 48-byte header + 64-byte params + 2 inputs + 1 output) --\n", ops);
    std::printf("[info]  clock call overhead: %.1f ns per Clock::now() (included in the timed percentiles)\n",
                timer_overhead_ns());
    bench_floor(std::max<size_t>(ops, 1000000) * 5);
    bench_noise_floor(std::max<size_t>(ops, 1000000) * 5);
    g_append_mem = bench_append("append_mem(mean)", ops, false);
    g_append_mem_timed = bench_append("append_mem(per-call timed)", ops, true);
    bench_batch(ops, 1000);
    bench_read(std::min<size_t>(ops, 1000000));
    bench_contended(threads, ops);
    bench_file(dir, "buffered", D8_LOG_SYNC_BUFFERED, std::min(ops, file_ops), true);
    bench_file(dir, "write", D8_LOG_SYNC_WRITE, std::min(ops, file_ops), false);
    if (fsync_ops > 0) bench_file(dir, "fsync", D8_LOG_SYNC_FSYNC, fsync_ops, false);

    gate_ok = g_append_mem.mean_us < 1000.0 && g_append_mem_timed.pct.p999 < 1000.0;
    std::printf("\nGATE append overhead < 1 ms per call: %s (mean %.4f us, p99.9 %.3f us, max %.3f us)\n",
                gate_ok ? "PASS" : "FAIL", g_append_mem.mean_us, g_append_mem_timed.pct.p999,
                g_append_mem_timed.pct.max);
  }
  CHECK(gate_ok);

  std::printf("SUMMARY checks=%d failed=%d\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
