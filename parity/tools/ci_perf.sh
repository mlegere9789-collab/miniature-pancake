#!/bin/bash
# Runs the kernel benchmark harness and compares it with the stored baseline. Used by .github/workflows/nightly.yml and
# by the throwaway-ref template kverify-perf.yml, so both measure and judge identically.
#   parity/tools/ci_perf.sh <bench-binary> <out.json> [reps]
# Environment: PERF_BASELINE (default parity/perf_baseline.json; missing = measure only), PERF_STRICT_GATES=1 to fail on a
# missed Appendix B gate. Exit status is perf_compare.py's (1 = regression / missing / errored metric).
set -u
BIN=${1:?bench binary}; OUT=${2:?output json}; REPS=${3:-5}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BASE=${PERF_BASELINE:-$ROOT/parity/perf_baseline.json}
commit=$(git -C "$ROOT" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
"$BIN" --commit "$commit" --reps "$REPS" --out "$OUT"
brc=$?
echo "bench exit=$brc"
echo "---- metrics json ----"; cat "$OUT"; echo "---- end metrics json ----"
echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //') x$(nproc)  $(uname -r)"
if [ ! -f "$BASE" ]; then echo "no baseline at $BASE: measured only"; exit $brc; fi
extra=()
[ -n "${PERF_STRICT_GATES:-}" ] && extra+=(--strict-gates)
[ -n "${GITHUB_STEP_SUMMARY:-}" ] && extra+=(--summary "$GITHUB_STEP_SUMMARY")
python3 -I "$ROOT/parity/tools/perf_compare.py" --current "$OUT" --baseline "$BASE" "${extra[@]}"
crc=$?
echo "compare exit=$crc"
[ $brc -ne 0 ] && exit $brc
exit $crc
