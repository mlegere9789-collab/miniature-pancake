#!/usr/bin/env bash
# Large-assembly stress test for Dino 8: builds N simple boxes through
# Dino8's own --stress hook (see main.cpp's RunStressTest) and reports
# object-creation, display-mesh-warmup, viewport-pick and undo-snapshot
# timings. Unlike tests/smoke.sh this is not pass/fail QC - it prints
# numbers for tests/performance_notes.md and (optionally) checks pick
# latency against a budget so a real regression still fails CI.
#
# Usage:
#   tests/stress.sh [N] [BIN]
#   tests/stress.sh --compare [N] [BIN]   also runs with the spatial grid
#                                         and the parallel warmup disabled,
#                                         for a same-binary before/after
#                                         (DINO8_DISABLE_PICK_GRID /
#                                         DINO8_DISABLE_PARALLEL_WARMUP -
#                                         see Viewport.cpp / main.cpp).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

COMPARE=0
if [ "${1:-}" = "--compare" ]; then COMPARE=1; shift; fi
N="${1:-30000}"
BIN="${2:-$HERE/../build/Dino8}"
TMP="$(mktemp -d)"
export XDG_CONFIG_HOME="$TMP/config"
mkdir -p "$XDG_CONFIG_HOME"
trap 'rm -rf "$TMP"' EXIT

run() {
  # $1: extra env assignments (space-separated VAR=val), or "" for none.
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
    env $1 "$BIN" --stress "$N" 2>&1
  else
    env $1 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --stress "$N" 2>&1
  fi
}

echo "Dino 8 stress test: $N objects"
OUT="$(run "")"
echo "$OUT" | grep "^stress:" || { echo "$OUT"; echo "FAIL: no stress line (app crashed or exited early)"; exit 1; }
echo "$OUT" | grep "^stress:"

fail=0
PICK_MS="$(echo "$OUT" | grep -o 'pick_ms=[0-9.]*' | cut -d= -f2)"
# Budget: a single hover pick should stay well under one frame (16.7ms at
# 60fps) even at this object count, or picking itself is the frame-rate
# bottleneck again. 8ms leaves headroom for the rest of the frame (draw
# calls, ImGui, other viewports).
if [ -n "$PICK_MS" ] && python3 -c "import sys; sys.exit(0 if float('$PICK_MS') < 8.0 else 1)"; then
  echo "ok   pick_ms ($PICK_MS) is under the 8ms-per-hover-pick budget at $N objects"
else
  echo "FAIL pick_ms ($PICK_MS) exceeds the 8ms budget at $N objects"; fail=1
fi

if [ "$COMPARE" = "1" ]; then
  echo
  echo "-- same binary, spatial grid and parallel warmup disabled (the old behavior) --"
  BEFORE="$(run "DINO8_DISABLE_PICK_GRID=1 DINO8_DISABLE_PARALLEL_WARMUP=1")"
  echo "$BEFORE" | grep "^stress:" || { echo "$BEFORE"; echo "FAIL: no stress line in the disabled-grid run"; exit 1; }
  echo "$BEFORE" | grep "^stress:"
  echo
  echo "-- summary --"
  echo "with grid + parallel warmup:    $(echo "$OUT" | grep '^stress:')"
  echo "brute force + serial warmup:    $(echo "$BEFORE" | grep '^stress:')"
fi

exit $fail
