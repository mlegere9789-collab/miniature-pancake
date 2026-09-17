#!/usr/bin/env bash
# Frustum-culling QC for Dino 8: proves Viewport::DrawObjects' view-frustum
# cull (Viewport.cpp, see the "Frustum culling" section and
# tests/performance_notes.md's "No LOD or frustum culling" item it closes)
# only removes draw calls, never removes anything that should still be
# visible. Runs the app's own --cull-test hook (main.cpp's RunCullTest)
# twice - once normally, once with DINO8_DISABLE_FRUSTUM_CULL=1 (the same
# binary's escape hatch, forcing the old "every object is a candidate"
# behaviour) - and checks:
#   (a) the "cull_test:" candidate count drops far below the object count
#       with the cull on, and covers every object with it off;
#   (b) the near cluster the camera is framed on is never itself
#       under-counted (candidates >= visible_expected with the cull on -
#       nothing that should be visible got dropped from the candidate list
#       in the first place);
#   (c) the two runs' screenshots (this viewport's own render, not the
#       whole composited window) are pixel-identical - the direct proof
#       that what actually got drawn did not change, regardless of what
#       the cull skipped.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-$HERE/../build/Dino8}"
FAR="${2:-4000}"
TMP="$(mktemp -d)"
export XDG_CONFIG_HOME="$TMP/config"
mkdir -p "$XDG_CONFIG_HOME"
trap 'rm -rf "$TMP"' EXIT

run() {
  # $1: extra env assignments (space-separated VAR=val), or "" for none.
  # $2: screenshot path.
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
    env $1 "$BIN" --cull-test "$FAR" --cull-screenshot "$2" 2>&1
  else
    env $1 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --cull-test "$FAR" --cull-screenshot "$2" 2>&1
  fi
}

echo "Dino 8 frustum-cull test: $FAR far objects"
ON="$(run "" "$TMP/cull_on.bmp")"
echo "$ON" | grep "^cull_test:" || { echo "$ON"; echo "FAIL: no cull_test line (cull-on run)"; exit 1; }
echo "$ON" | grep "^cull_test:"
OFF="$(run "DINO8_DISABLE_FRUSTUM_CULL=1" "$TMP/cull_off.bmp")"
echo "$OFF" | grep "^cull_test:" || { echo "$OFF"; echo "FAIL: no cull_test line (cull-off run)"; exit 1; }
echo "$OFF" | grep "^cull_test:"

fail=0

TOTAL_ON="$(echo "$ON" | grep -o 'total=[0-9]*' | cut -d= -f2)"
CAND_ON="$(echo "$ON" | grep -o 'candidates=[0-9]*' | cut -d= -f2)"
VISIBLE="$(echo "$ON" | grep -o 'visible_expected=[0-9]*' | cut -d= -f2)"
TOTAL_OFF="$(echo "$OFF" | grep -o 'total=[0-9]*' | cut -d= -f2)"
CAND_OFF="$(echo "$OFF" | grep -o 'candidates=[0-9]*' | cut -d= -f2)"

if [ -n "$CAND_OFF" ] && [ -n "$TOTAL_OFF" ] && [ "$CAND_OFF" = "$TOTAL_OFF" ]; then
  echo "ok   DINO8_DISABLE_FRUSTUM_CULL=1 makes every object a candidate ($CAND_OFF == $TOTAL_OFF)"
else
  echo "FAIL disabled-cull candidate count ($CAND_OFF) does not equal the object count ($TOTAL_OFF)"; fail=1
fi

if [ -n "$CAND_ON" ] && [ -n "$VISIBLE" ] && [ "$CAND_ON" -ge "$VISIBLE" ]; then
  echo "ok   the cull kept at least the whole near cluster as candidates ($CAND_ON >= $VISIBLE)"
else
  echo "FAIL the cull dropped part of the near cluster from the candidate list ($CAND_ON < $VISIBLE)"; fail=1
fi

if [ -n "$CAND_ON" ] && [ -n "$TOTAL_ON" ] && [ "$TOTAL_ON" -gt 0 ] && \
   python3 -c "import sys; sys.exit(0 if $CAND_ON < $TOTAL_ON * 0.5 else 1)"; then
  echo "ok   the cull actually dropped candidates ($CAND_ON of $TOTAL_ON objects, far below half)"
else
  echo "FAIL the cull did not meaningfully reduce the candidate count ($CAND_ON of $TOTAL_ON)"; fail=1
fi

if [ -s "$TMP/cull_on.bmp" ] && [ -s "$TMP/cull_off.bmp" ] && cmp -s "$TMP/cull_on.bmp" "$TMP/cull_off.bmp"; then
  echo "ok   cull-on and cull-off screenshots are pixel-identical (nothing visible went missing or appeared)"
else
  echo "FAIL cull-on and cull-off screenshots differ (or one is missing) - the cull changed what got rendered"; fail=1
fi

exit $fail
