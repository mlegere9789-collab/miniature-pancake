#!/bin/bash
# Runs an ASan+UBSan build of dino8_kernel_tests on named filters (or one shard of the whole suite) and judges the output.
#   parity/tools/ci_sanitize.sh <test-binary> <subset|shard:K/N> [filter-file]
#   subset     one run per line of parity/tools/sanitizer_subset.txt (or the given file), DINO8_TEST_FILTER=<line>
#   shard:K/N  the whole suite, DINO8_TEST_SHARD=K/N (nightly)
# A run FAILS on: any AddressSanitizer error, a test FAILED line, a timeout/crash, or a UBSan "runtime error" whose location
# is in this repository's own code (dino8-kernel/, dino8-app/). UBSan findings located in fetched third-party code
# (openNURBS, manifold) are listed and counted but do not fail the run: they are not ours to change here. Leak detection is off
# (openNURBS keeps process-lifetime tables) - the point is out-of-bounds, use-after-free, overflow, misaligned and null access.
# The build uses -fsanitize-recover=address and halt_on_error=0, so one finding does not hide every later test of the run;
# each distinct report is counted and its SUMMARY line listed.
set -u
BIN=$(readlink -f "${1:?test binary}"); MODE=${2:?subset or shard:K/N}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LIST=${3:-$ROOT/parity/tools/sanitizer_subset.txt}
export ASAN_OPTIONS=detect_leaks=0:strict_string_checks=1:detect_stack_use_after_return=1:abort_on_error=0:halt_on_error=0
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0
W=$(mktemp -d); cd "$W" || exit 2
# run_one returns 0 when the run has a PROBLEM (it is used as "run_one ... && bad=1"), 1 when clean.
run_one() {  # label env-assignment...
  local label=$1; shift
  env "$@" DINO8_TEST_TIMING=1 DINO8_TEST_WATCHDOG=1500 timeout 3300 "$BIN" > "$W/$label.log" 2> "$W/$label.err"
  local rc=$?
  local ok failed asan ub_own ub_3p
  ok=$(grep -c '^ok' "$W/$label.log"); failed=$(grep -c '^FAILED' "$W/$label.err")
  asan=$(grep -c 'ERROR: AddressSanitizer' "$W/$label.err")
  ub_own=$(grep 'runtime error:' "$W/$label.err" | grep -v '/_deps/' | grep -cE '/dino8-(kernel|app)/')
  ub_3p=$(( $(grep -c 'runtime error:' "$W/$label.err") - ub_own ))
  echo "run $label: exit=$rc ok=$ok FAILED=$failed asan=$asan ubsan_own=$ub_own ubsan_thirdparty=$ub_3p"
  grep -E '^FAILED|ERROR: AddressSanitizer|SUMMARY: ' "$W/$label.err" | cut -c1-300 | sort | uniq -c | head -20
  # the first AddressSanitizer report in full (error line, access, stack), so a finding can be read without the raw log
  awk '/ERROR: AddressSanitizer/{on=1} on{print "   asan| " substr($0,1,220); if(++n>=26) exit}' "$W/$label.err"
  grep 'runtime error:' "$W/$label.err" | grep -v '/_deps/' | grep -E '/dino8-(kernel|app)/' | cut -c1-300 | sort | uniq -c | sort -rn | head -20
  grep 'runtime error:' "$W/$label.err" | awk '!(!/_deps/ && /\/dino8-(kernel|app)\//)' | sed 's/^.*\/_deps\///' | cut -c1-200 | sort | uniq -c | sort -rn | head -8 | sed 's/^/   third-party: /'
  [ $rc -ne 0 ] || [ "$failed" -ne 0 ] || [ "$asan" -ne 0 ] || [ "$ub_own" -ne 0 ]
}
bad=0
case "$MODE" in
  subset)
    while IFS= read -r f; do
      f=${f%%#*}; f=$(echo "$f" | tr -d ' \t'); [ -z "$f" ] && continue
      run_one "f_$(echo "$f" | tr -c 'A-Za-z0-9' _)" DINO8_TEST_FILTER="$f" && bad=1
      grep '^timing:' "$W"/f_*.log 2>/dev/null | sort -k3 -nr | head -3
    done < "$LIST" ;;
  shard:*)
    run_one "shard_$(echo "${MODE#shard:}" | tr -c '0-9' _)" DINO8_TEST_SHARD="${MODE#shard:}" && bad=1
    grep '^timing:' "$W"/shard_*.log 2>/dev/null | sort -k3 -nr | head -8 ;;
  *) echo "bad mode $MODE"; exit 2 ;;
esac
echo "---- end ----"
rm -rf "$W"
exit $bad
