# Helpers shared by the RWI* sections (sections/94_rwi*.sh), sourced from them. Everything here is prefixed rwi_.
# rwi_ok / rwi_bad     print the "ok   ..." / "FAIL ..." lines the section runners read (rwi_bad also sets fail=1)
# rwi_app SCRIPT       run the application on SCRIPT (a path), output in RWI_LOG; the same display handling as the neighbouring sections
# rwi_has TEXT LABEL   the application output contains TEXT
# rwi_hasnt TEXT LABEL the application output does not contain TEXT
# rwi_oracle LABEL FMT FILE ARGS...   run the stand-alone parser rwi_oracle.py and compare with the hand-computed values in ARGS
# rwi_exists FILE      the file exists and is not empty
rwi_ok() { echo "ok   $1"; }
rwi_bad() { echo "FAIL $1"; fail=1; }
rwi_app() {
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    RWI_LOG="$(timeout 900 "$BIN" --smoke 40 --script "$1" 2>&1)" || { echo "$RWI_LOG" | tail -25; rwi_bad "the script $(basename "$1") exited non-zero"; }
  else
    RWI_LOG="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 40 --script "$1" 2>&1)" || { echo "$RWI_LOG" | tail -25; rwi_bad "the script $(basename "$1") exited non-zero"; }
  fi
  if echo "$RWI_LOG" | grep -q "^FAIL"; then echo "$RWI_LOG" | grep "^FAIL"; fail=1; fi
}
rwi_has() { if echo "$RWI_LOG" | grep -qF -- "$1"; then rwi_ok "$2"; else rwi_bad "$2"; echo "     expected in the application output: $1"; fi; }
rwi_hasnt() { if echo "$RWI_LOG" | grep -qF -- "$1"; then rwi_bad "$2"; echo "     unexpected in the application output: $1"; else rwi_ok "$2"; fi; }
rwi_exists() { if test -s "$1"; then return 0; else rwi_bad "$(basename "$1") was not written"; echo "$RWI_LOG" | grep -E "Could not|Nothing to export|Unsupported|Warning" | head -3 | sed 's/^/     app: /'; return 1; fi; }
rwi_oracle() {
  local label="$1" fmt="$2" file="$3"; shift 3
  rwi_exists "$file" || return 0
  local r
  r="$(python3 -I "$HERE/rwi_oracle.py" "$fmt" "$file" "$@" 2>&1)" && rwi_ok "$label: $r" || { rwi_bad "$label"; echo "     $r"; }
}
