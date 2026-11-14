// Self-registration + sharding support for dino8_kernel_tests.
//
// New kernel tests go in their own small translation units under
// tests/extra/*.cpp (globbed by tests/CMakeLists.txt) instead of being
// appended to the ~99,000-line test_basic.cpp, so adding one needs no edit to
// test_basic.cpp (no merge conflict, and only that small file recompiles).
// See tests/README_extra.md.
//
//   #include "test_registry.h"
//   D8_EXTRA_TEST(MyFeatureWorks) {
//     d8test::Check(1 + 1 == 2, "my feature: arithmetic still works");
//   }
//
// The test is registered under the name "extra::MyFeatureWorks".
#pragma once

namespace d8test {

// Same output format as test_basic.cpp's Check(): "ok: <what>" on stdout,
// "FAILED: <what>" on stderr. Failures are counted in Failures().
void Check(bool condition, const char* what);
int& Failures();

// Central gate used by BOTH test_basic.cpp's RunTest() and the extras runner.
// Returns true if the test named `name` should run now, honouring
//   DINO8_TEST_LIST=1        print the name (stdout) and return false (nothing runs)
//   DINO8_TEST_FILTER=<sub>  only names containing <sub>
//   DINO8_TEST_SHARD=k/n     of the tests that pass the filter, in registration
//                            order, only every n-th one starting at index k (0 <= k < n)
// Names starting with "ON::" are infrastructure: always run (never listed,
// never counted for sharding), exactly as before.
bool ShouldRun(const char* name);

// True when DINO8_TEST_LIST=1 (main() uses it to suppress the final summary).
bool ListMode();

using TestFn = void (*)();
struct Registrar {
  Registrar(const char* name, TestFn fn);
};

// Runs every registered extra test, sorted by name (static-init order across
// translation units is unspecified; sorting makes the order, and therefore the
// shard assignment, deterministic). Called from main() in test_basic.cpp.
void RunExtras();

}  // namespace d8test

#define D8_EXTRA_TEST(Name)                                                   \
  static void d8_extra_test_##Name();                                         \
  static ::d8test::Registrar d8_extra_registrar_##Name("extra::" #Name,       \
                                                       d8_extra_test_##Name); \
  static void d8_extra_test_##Name()
