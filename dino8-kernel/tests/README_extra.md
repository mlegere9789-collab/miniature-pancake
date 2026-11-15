# Kernel tests: `tests/extra/`, sharding and listing

`tests/test_basic.cpp` is one ~99,500-line translation unit: any edit recompiles all of it (minutes), and every
agent appending tests at its end conflicts with every other. New tests should instead go in their own small file.

## Adding a test (no edit to test_basic.cpp)

Create `dino8-kernel/tests/extra/<anything>.cpp`:

```cpp
#include "dino8/kernel/curve.h"
#include "test_registry.h"

D8_EXTRA_TEST(MyFeatureWorks) {
  d8test::Check(1 + 1 == 2, "my feature: arithmetic still works");
}
```

* Every `extra/*.cpp` is globbed by `tests/CMakeLists.txt` (`CONFIGURE_DEPENDS`), so a plain `ninja` picks up a new
  file; no cmake edit, no edit to `test_basic.cpp`.
* Registered name is `extra::MyFeatureWorks`. Use unique names across all of `extra/` (a duplicate is a link error).
* `d8test::Check(cond, what)` prints the same `ok: ...` / `FAILED: ...` lines as test_basic.cpp's `Check`, and its
  failures make the binary exit 1. An uncaught exception in a test is reported as one failed check, as for
  test_basic's `RunTest`.
* The tests run from `main()` just before `ON::End`, sorted by name (static-initialisation order across files is
  unspecified, so they are sorted to keep the order, and shard assignment, deterministic). `ON::Begin` has already
  run; do not call it yourself.
* Anonymous-namespace helpers in test_basic.cpp are not visible from `extra/`; put what you need in your own file
  (or a header next to `test_registry.h`).

## Filtering, listing, sharding (environment variables of the test binary)

| variable | effect |
| --- | --- |
| `DINO8_TEST_FILTER=<substr>` | only tests whose name contains `<substr>` (as before; `ON::*` always run) |
| `DINO8_TEST_LIST=1` | print the registered test names, one per line (stdout), run nothing, print no summary |
| `DINO8_TEST_SHARD=k/n` | of the tests that pass the filter, in registration order (test_basic.cpp's `RunTest` calls in `main()` order, then the extras sorted by name), run only indexes `i` with `i % n == k`; `0 <= k < n`, else exit 2 |

`ON::Begin` / `ON::End` run in every shard and are never counted or listed. The three combine freely
(`DINO8_TEST_FILTER=Fillet DINO8_TEST_SHARD=1/3`; `DINO8_TEST_LIST=1` lists exactly the tests that would run).

## `scripts/run_shards.sh`

```
dino8-kernel/scripts/run_shards.sh N          # N shard processes in parallel, outputs merged in shard order
dino8-kernel/scripts/run_shards.sh --check N  # no test runs: verifies with DINO8_TEST_LIST that the N shards are
                                              # disjoint and their union equals the unsharded list
DINO8_TEST_BIN=...  DINO8_TEST_FILTER=...  DINO8_SHARD_KEEP=1   # optional
```

It exits nonzero if any shard exits nonzero or does not print `all checks passed`. On the shared machine run it
through the gate (`gate.sh run dino8-kernel/scripts/run_shards.sh 2`); N processes means N cores.

Sharding is round-robin by registration order, not by cost: wall time is the slowest shard. Tests that are slow
and adjacent in `main()` spread out, but a few very slow tests can still dominate; measured numbers are in the
commit message / hand-off report, not promised here. Tests are assumed independent of each other (each shard
skips the other shards' tests); a test that silently relies on state left by an earlier test would behave
differently in a shard.
