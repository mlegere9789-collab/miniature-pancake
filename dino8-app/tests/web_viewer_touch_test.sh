#!/usr/bin/env bash
# Touch support of the exported web viewer: export a viewer with the real app
# (ExportWebViewer), check the touch markers in the HTML, and - when node +
# Playwright + a Chromium are available - drive real touch input through
# tests/web_viewer_touch_check.js.
# Prints "ok   pwv_..." / "FAIL pwv_..." / "skip ..." lines; non-zero if any FAIL.
# Usage: bash web_viewer_touch_test.sh <path to Dino8 binary>
trap '' PIPE
set -euo pipefail
echo() { builtin echo "$@" 2>/dev/null || true; }
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:?usage: web_viewer_touch_test.sh <Dino8 binary>}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }

printf 'Box 0,0,0 20,20,0 10\nSphere 30,10,5 6\nExportWebViewer Path=%s/viewer.html\n' "$W" > "$W/s.txt"
mkdir -p "$W/cfg"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  OUT="$(XDG_CONFIG_HOME="$W/cfg" HOME="$W/cfg" timeout 900 "$BIN" --smoke 20 --script "$W/s.txt" 2>&1)" || true
else
  OUT="$(XDG_CONFIG_HOME="$W/cfg" HOME="$W/cfg" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 20 --script "$W/s.txt" 2>&1)" || true
fi
if [ ! -f "$W/viewer.html" ]; then bad "pwv_export: ExportWebViewer wrote no file"; exit 1; fi
ok "pwv_export: ExportWebViewer wrote the viewer ($(wc -c < "$W/viewer.html" | tr -d ' ') bytes)"
mark() { if grep -qF -- "$1" "$W/viewer.html"; then ok "$2"; else bad "$2"; fi; }
mark 'name="viewport"' "pwv_viewport_meta: the exported viewer declares a mobile viewport"
mark 'touch-action:none' "pwv_touch_action: the canvas disables browser gestures (touch-action:none)"
mark "addEventListener('pointerdown'" "pwv_pointer_events: orbit/zoom run on Pointer Events (mouse, touch and pen share one path)"
mark '@media (pointer:coarse)' "pwv_coarse_ui: the Scale controls get larger hit targets on touch screens"
if grep -qE "addEventListener\('mouse(down|move|up)'" "$W/viewer.html"; then bad "pwv_no_mouse_only: mouse-only handlers remain"; else ok "pwv_no_mouse_only: no mouse-only drag handlers remain"; fi
if grep -qE '<script[^>]*src=|https?://' "$W/viewer.html"; then bad "pwv_self_contained: the viewer references an external URL"; else ok "pwv_self_contained: still a single file with no external URL"; fi

if command -v node >/dev/null 2>&1 && node -e "require('playwright')" >/dev/null 2>&1; then
  if TOUCH="$(cd "$HERE" && timeout 300 node web_viewer_touch_check.js "$W/viewer.html" 2>&1)"; then
    ok "pwv_real_touch: Chromium touch emulation - one-finger orbit, two-finger pinch zoom, mouse and sliders still work ($(grep -c '^ok' <<<"$TOUCH" || true) checks)"
  else builtin echo "$TOUCH" | tail -15 >&2; bad "pwv_real_touch: touch gestures did not work in Chromium"; fi
else
  echo "skip pwv_real_touch (node with Playwright not available)"
fi
exit $fail
