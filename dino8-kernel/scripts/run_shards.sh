#!/usr/bin/env bash
# Run dino8_kernel_tests as N parallel shard processes and merge the result.
#
#   scripts/run_shards.sh N                 run all tests in N shards (N processes at once)
#   scripts/run_shards.sh --check N         do not run anything: verify, using DINO8_TEST_LIST=1, that the
#                                           N shards are disjoint and their union equals the unsharded list
#   DINO8_TEST_FILTER=<substr> scripts/run_shards.sh N     only tests whose name contains <substr>
#   DINO8_TEST_BIN=/path/to/dino8_kernel_tests             binary (default: ../build/tests/dino8_kernel_tests)
#   DINO8_SHARD_KEEP=1                      keep the per-shard logs (their directory is printed)
#
# Shard k of N is DINO8_TEST_SHARD=k/N: every N-th registered test starting at index k (see
# tests/test_registry.h). Each shard's stdout/stderr are captured separately and printed in shard order
# when it finishes, so the merged output is not interleaved. Exit status is nonzero if any shard exits
# nonzero, is killed by a signal, or prints no "all checks passed" line (a crash that lost its exit code).
#
# On the shared 4-core machine wrap it like any other test run: gate.sh run scripts/run_shards.sh 2
set -u
trap '' PIPE

HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${DINO8_TEST_BIN:-$HERE/../build/tests/dino8_kernel_tests}"

MODE=run
if [ "${1:-}" = "--check" ]; then MODE=check; shift; fi
N="${1:-}"
case "$N" in ''|*[!0-9]*|0) echo "usage: $0 [--check] N   (N >= 1)" >&2; exit 2;; esac
[ -x "$BIN" ] || { echo "run_shards: test binary not found or not executable: $BIN" >&2; exit 2; }

WORK="$(mktemp -d)"
cleanup() { if [ "${DINO8_SHARD_KEEP:-0}" = 1 ]; then echo "run_shards: logs kept in $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT

if [ "$MODE" = check ]; then
  DINO8_TEST_LIST=1 "$BIN" 2>/dev/null | LC_ALL=C sort > "$WORK/all.txt" || true
  : > "$WORK/union.txt"
  for ((k = 0; k < N; k++)); do
    DINO8_TEST_SHARD="$k/$N" DINO8_TEST_LIST=1 "$BIN" 2>/dev/null | LC_ALL=C sort > "$WORK/s$k.txt" || true
    echo "shard $k/$N lists $(wc -l < "$WORK/s$k.txt") tests"
    cat "$WORK/s$k.txt" >> "$WORK/union.txt"
  done
  LC_ALL=C sort "$WORK/union.txt" > "$WORK/union_sorted.txt"
  total=$(wc -l < "$WORK/all.txt"); un=$(wc -l < "$WORK/union_sorted.txt")
  dups=$(LC_ALL=C uniq -d "$WORK/union_sorted.txt" | wc -l)
  echo "unsharded list: $total names; union of $N shards: $un names; names listed by more than one shard: $dups"
  # Duplicate names can exist in the unsharded list itself; compare as sorted multisets.
  if [ "$total" -gt 0 ] && cmp -s "$WORK/all.txt" "$WORK/union_sorted.txt"; then
    echo "run_shards --check: OK (union of shards == unsharded list, as multisets)"; exit 0
  fi
  echo "run_shards --check: MISMATCH" >&2; diff "$WORK/all.txt" "$WORK/union_sorted.txt" | head -20 >&2; exit 1
fi

start=$(date +%s)
pids=()
for ((k = 0; k < N; k++)); do
  ( s=$(date +%s)
    DINO8_TEST_SHARD="$k/$N" "$BIN" > "$WORK/out.$k" 2> "$WORK/err.$k"
    echo "$? $(( $(date +%s) - s ))" > "$WORK/rc.$k" ) &
  pids+=($!)
done
wait "${pids[@]}"

rc=0
total_ok=0
for ((k = 0; k < N; k++)); do
  read -r src secs < "$WORK/rc.$k" || { src=255; secs=0; }
  nok=$(grep -c '^ok: ' "$WORK/out.$k" || true)
  nfail=$(grep -c '^FAILED: ' "$WORK/err.$k" || true)
  done_line=$(grep -c '^all checks passed' "$WORK/out.$k" || true)
  total_ok=$((total_ok + nok))
  echo "================ shard $k/$N: exit $src, ${secs}s, $nok ok lines, $nfail FAILED lines ================"
  cat "$WORK/out.$k"
  if [ -s "$WORK/err.$k" ]; then echo "---- shard $k/$N stderr ----"; cat "$WORK/err.$k"; fi
  if [ "$src" != 0 ] || [ "$done_line" != 1 ]; then rc=1; echo "run_shards: shard $k/$N FAILED (exit $src, 'all checks passed' lines: $done_line)"; fi
done
echo "run_shards: $N shard(s), $total_ok ok lines in total, wall $(( $(date +%s) - start ))s, result: $([ $rc = 0 ] && echo PASS || echo FAIL)"
exit $rc
