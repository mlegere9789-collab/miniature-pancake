#!/usr/bin/env bash
# Headless functional QC for Dino 8: runs a command script through the real
# app under Xvfb and checks the resulting history/object counts.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-$HERE/../build/Dino8}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/script.txt" <<EOS
Box 0,0,0 20,20,0 10
Sphere 10,10,10 8
SelAll
BooleanDifference
SelAll
Volume
SelNone
Circle 40,0,0 5
ExtrudeCrv 0,0,0 12
SelAll
Save $TMP/test.3dm
New
Open $TMP/test.3dm
SelAll
List
Undo
Redo
Line 0,0,0 10,10,0
SelCrv
Length
SelAll
Export $TMP/test.obj
EOS

RUNNER=""
if ! xset q >/dev/null 2>&1; then RUNNER="xvfb-run -a -s -screen 0 1600x900x24"; fi
OUT="$($RUNNER "$BIN" --smoke 200 --script "$TMP/script.txt" 2>&1)" || { echo "$OUT"; echo "FAIL: app exited non-zero"; exit 1; }
echo "$OUT" | grep "^smoke:" || { echo "$OUT"; echo "FAIL: no smoke line"; exit 1; }
fail=0
check() { if echo "$OUT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
check "1055 commands loaded" "catalog has all 1055 commands"
check "BooleanDifference:" "boolean difference ran"
check "Volume = " "volume measured"
check "Extruded 1 object" "extrusion created a solid"
check "history: Command: Save" "save command ran"
check "gl_error=0" "no OpenGL errors"
echo "$OUT" | grep -E "^(smoke|history)" | tail -40
exit $fail
