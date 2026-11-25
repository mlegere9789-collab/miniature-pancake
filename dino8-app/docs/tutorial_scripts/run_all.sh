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
# @TMP@/@FLOWFILE@ get baked as literal TEXT into a generated script file
# (Save/Export/Open/GrasshopperPlayer command lines), not passed as a
# bash-level argv to $BIN - so they get none of Git-for-Windows' automatic
# MSYS<->Windows path translation an argv would. On Windows the raw "/tmp/
# xxx" text resolves as "current drive's root\tmp\xxx", never the real
# scratch dir, so every Save/Export/Open/GrasshopperPlayer using it fails
# silently (see tests/smoke.sh's own TMPW/HEREW comment for the same
# reasoning it already applies everywhere else). cygpath -m gives the
# real, OS-native path; on Linux/macOS (no cygpath) this is a no-op.
if command -v cygpath >/dev/null 2>&1; then TMPW="$(cygpath -m "$TMP")"; else TMPW="$TMP"; fi
if command -v cygpath >/dev/null 2>&1; then HEREW="$(cygpath -m "$HERE")"; else HEREW="$HERE"; fi

fail=0
for script in "$HERE"/[0-9][0-9]_*.txt; do
  name="$(basename "$script")"
  work="$TMP/$name"
  sed -e "s|@TMP@|$TMPW|g" -e "s|@FLOWFILE@|$HEREW/flow_linear_array.dflow|g" "$script" > "$work"
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
