# Shared helpers of the UX sections (UXSNAP, UXDOC, UXSEL2, UXI18N). Sourced by each section into the harness shell; every function
# takes the label prefix it reports under, so one section never depends on another section's state.
#   ux_launch SCRIPT [ENV...]   runs the app headless on SCRIPT and keeps the output in UX_OUT (empty and a FAIL line when it never finished)
#   ux_lines BLOCK              the output lines of one block (between "UXBEGIN_BLOCK" and the next UXBEGIN_), without the echo of the typed
#                               command lines and without the "history: " prefix
#   ux_eq PREFIX BLOCK LABEL EXPECTED...   the block's lines are exactly EXPECTED (one argument per line)
#   ux_has PREFIX BLOCK LABEL NEEDLE       the block contains NEEDLE
ux_ok() { echo "ok   $1"; }
ux_bad() { echo "FAIL $1"; fail=1; }
UX_OUT=""
ux_launch() {  # script [env assignments...]
  local script="$1"; shift
  UX_OUT=""
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    UX_OUT="$(env "$@" timeout 900 "$BIN" --smoke 60 --script "$script" 2>&1)" || true
  else
    UX_OUT="$(env "$@" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$script" 2>&1)" || true
  fi
  if ! grep -q '^smoke:' <<<"$UX_OUT"; then echo "$UX_OUT" | tail -30; ux_bad "UX_run: the script did not finish (no smoke line): $script"; UX_OUT=""; return 1; fi
  return 0
}
ux_lines() {  # block
  awk -v name="$1" '$0 ~ ("Unknown command: UXBEGIN_" name "$") { on = 1; next } on && /Unknown command: UXBEGIN_/ { exit } on { print }' <<<"$UX_OUT" |
    { grep -v '^history: Command: ' || true; } | { grep -v '^GLFW error' || true; } | sed -e 's/^history: //'
}
ux_eq() {  # prefix block label expected-lines...
  local prefix="$1" block="$2" label="$3"; shift 3
  local want got
  want="$(printf '%s\n' "$@")"
  got="$(ux_lines "$block")"
  if [ "$got" = "$want" ]; then ux_ok "$prefix: $label"; else ux_bad "$prefix: $label"; { diff <(echo "$want") <(echo "$got") || true; } | head -12 | sed "s/^/     /" || true; fi
}
ux_has() {  # prefix block label needle
  if grep -qF -- "$4" <<<"$(ux_lines "$2")"; then ux_ok "$1: $3"; else ux_bad "$1: $3"; echo "     expected to contain: $4"; { ux_lines "$2" || true; } | head -8 | sed "s/^/     actual: /" || true; fi
}
