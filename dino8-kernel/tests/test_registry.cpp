#include "test_registry.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace d8test {

namespace {

std::vector<std::pair<std::string, TestFn>>& Registry() {
  static std::vector<std::pair<std::string, TestFn>> r;  // function-local: safe at static-init time
  return r;
}

struct ShardSpec {
  int k = 0;
  int n = 1;
};

ShardSpec ParseShard() {
  ShardSpec s;
  const char* v = std::getenv("DINO8_TEST_SHARD");
  if (!v || !*v) return s;
  int k = -1, n = 0;
  if (std::sscanf(v, "%d/%d", &k, &n) != 2 || n < 1 || k < 0 || k >= n) {
    std::fprintf(stderr, "DINO8_TEST_SHARD='%s' is not k/n with 0 <= k < n; refusing to run\n", v);
    std::exit(2);
  }
  s.k = k;
  s.n = n;
  return s;
}

}  // namespace

int& Failures() {
  static int f = 0;
  return f;
}

void Check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++Failures();
  } else {
    std::printf("ok: %s\n", what);
  }
}

bool ListMode() {
  const char* v = std::getenv("DINO8_TEST_LIST");
  return v && *v && std::strcmp(v, "0") != 0;
}

bool ShouldRun(const char* name) {
  const bool infra = std::strncmp(name, "ON::", 4) == 0;
  if (infra) return !ListMode();
  if (const char* filter = std::getenv("DINO8_TEST_FILTER")) {
    if (*filter && std::strstr(name, filter) == nullptr) return false;
  }
  static const ShardSpec shard = ParseShard();
  static long index = 0;  // position among the tests that passed the filter, in registration order
  const long mine = index++;
  if (mine % shard.n != shard.k) return false;
  if (ListMode()) {
    std::printf("%s\n", name);
    return false;
  }
  return true;
}

Registrar::Registrar(const char* name, TestFn fn) { Registry().emplace_back(name, fn); }

void RunExtras() {
  auto sorted = Registry();
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& t : sorted) {
    if (!ShouldRun(t.first.c_str())) continue;
    try {
      t.second();
    } catch (const std::exception& e) {
      std::fprintf(stderr, "FAILED: %s threw an uncaught exception instead of completing (%s)\n", t.first.c_str(),
                   e.what());
      ++Failures();
    } catch (...) {
      std::fprintf(stderr, "FAILED: %s threw an uncaught non-std::exception instead of completing\n",
                   t.first.c_str());
      ++Failures();
    }
  }
}

}  // namespace d8test
