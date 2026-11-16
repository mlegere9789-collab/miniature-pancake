# Shared helpers of the drafting-docs app sections (94_dd*.sh): run a script in the app and print ok / FAIL lines.
# Sourced by each section (the sections run alone, so none depends on another); every name starts with dd_.
dd_ok() { echo "ok   $1"; }
dd_bad() { echo "FAIL $1"; fail=1; }
dd_run() {  # dd_run <script file> -> the app's output in DD_OUT
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    DD_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$1" 2>&1)" || { echo "$DD_OUT" | tail -20; dd_bad "app exited non-zero on $1"; DD_OUT=""; }
  else
    DD_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$1" 2>&1)" || { echo "$DD_OUT" | tail -20; dd_bad "app exited non-zero on $1"; DD_OUT=""; }
  fi
}
dd_has() {  # dd_has <what> <fixed string expected in DD_OUT>
  if grep -qF -- "$2" <<<"$DD_OUT"; then dd_ok "$1"; else dd_bad "$1"; echo "     expected: $2"; { grep -E "history:" <<<"$DD_OUT" || true; } | tail -6 | sed 's/^/     app: /'; fi
}
dd_py() {  # dd_py <mode> <args...>: the independent stdlib checker prints its own ok / FAIL lines
  local out rc=0
  out="$(python3 "$HERE/dd_check.py" "$@" 2>&1)" || rc=$?
  echo "$out"
  if [ "$rc" != "0" ] || grep -q "^FAIL" <<<"$out"; then fail=1; fi
}
