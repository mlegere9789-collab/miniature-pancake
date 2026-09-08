#!/usr/bin/env bash
# Runs every tutorial script in this directory through the real Dino8
# binary, the same way tests/smoke.sh exercises the regression scripts, so
# the docs/site/tutorials.html command sequences are provably accurate
# rather than aspirational.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-$HERE/../../build/Dino8}"
TMP="$(mktemp -d)"
export XDG_CONFIG_HOME="$TMP/config"
mkdir -p "$XDG_CONFIG_HOME"
trap 'rm -rf "$TMP"' EXIT

fail=0
for script in "$HERE"/[0-9][0-9]_*.txt; do
  name="$(basename "$script")"
  work="$TMP/$name"
  sed -e "s|@TMP@|$TMP|g" -e "s|@FLOWFILE@|$HERE/flow_linear_array.dflow|g" "$script" > "$work"
  echo "=== $name ==="
  if command -v xvfb-run >/dev/null 2>&1; then
    OUT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$work" 2>&1)"
  else
    OUT="$("$BIN" --smoke 200 --script "$work" 2>&1)"
  fi
  status=$?
  echo "$OUT" | grep -E "FAIL|^ok " || true
  if [ $status -ne 0 ] || echo "$OUT" | grep -q FAIL; then
    echo "FAIL: $name (exit $status)"
    echo "$OUT" | tail -30
    fail=1
  else
    echo "PASS: $name"
  fi
done
exit $fail
