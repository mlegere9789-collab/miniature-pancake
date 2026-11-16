# ---------------------------------------------------------------------------
# RW3ACS_SSE: selection sets (ssadd, ssdel, sslength, ssmemb, ssname) and entlast over real drawing entities.
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0279 (ssadd), D8-AUTOCAD-LISP-0280 (ssdel), D8-AUTOCAD-LISP-0282 (sslength), D8-AUTOCAD-LISP-0283 (ssmemb), D8-AUTOCAD-LISP-0284 (ssname), D8-AUTOCAD-LISP-0156 (entlast).
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic, never with the interpreter under test. Where the expected text
# is a documented worked example it is marked "(documented)" in the check label. The vendor pages could not be fetched when
# this section was written: every "(documented)" value is from recollection of the Autodesk help and needs an independent check.
# Entity names of this dialect are the plain object ids, a 1-based creation counter (1 line, 2 line, 3 circle).
# One real app process runs rw3acs_sse_script.txt; each result is read from the "Lisp: <value>" line printed after the typed
# expression. Self-contained (own helpers) so that `run_sections.sh RW3ACS_SSE` works alone.
# ---------------------------------------------------------------------------

r3s_exec() {  # r3s_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r3s_ok() { echo "ok   $1"; }
r3s_bad() { echo "FAIL $1"; fail=1; }
r3s_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | PU_CMD="Command: $2" PU_NTH="$3" awk 'BEGIN { n = ENVIRON["PU_NTH"] + 0; cmd = ENVIRON["PU_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r3s_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r3s_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r3s_ok "$5 ($2 -> $got)"; else r3s_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r3s_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r3s_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r3s_ok "$5"; else r3s_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r3s_r() {  # a real the way the app prints it (six decimals, trailing zeros trimmed to one), from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}

if [ "$IS_WINDOWS" = 1 ]; then R3S_SUB="Dino8"; else R3S_SUB="dino8"; fi
R3S_CFG="$TMP/r3s_cfg"
mkdir -p "$R3S_CFG/$R3S_SUB"
R3S="$(r3s_exec "$R3S_CFG" "$HERE/rw3acs_sse_script.txt")" || { echo "$R3S" | tail -20; r3s_bad "RW3ACS_SSE script exited non-zero"; }
grep -q "^smoke:" <<<"$R3S" || { echo "$R3S" | tail -20; r3s_bad "RW3ACS_SSE: no smoke line (the app did not reach its normal end)"; }
if grep -q "@expect_.*failed" <<<"$R3S"; then r3s_bad "RW3ACS_SSE: an @expect_ check failed"; grep "expect" <<<"$R3S" | head -3; else r3s_ok "RW3ACS_SSE: no @expect_objects check failed"; fi
R3S_N="$(grep -c "^ok   expect_objects 3 (got" <<<"$R3S" || true)"
if [ "$R3S_N" = 1 ]; then r3s_ok "RW3ACS_SSE: the drawing held 3 entities after the Line, Line, Circle commands"; else r3s_bad "RW3ACS_SSE: @expect_objects 3 held $R3S_N time(s), expected 1"; fi
r3s_is "$R3S" '(entlast)' 1 'nil' 'R3S_entlast: nil in an empty drawing (documented)'
r3s_is "$R3S" '(entlast)' 2 '1' 'R3S_entlast: names the line just drawn (the only entity)'
r3s_is "$R3S" '(cdr (assoc 0 (entget (entlast))))' 1 '"CIRCLE"' 'R3S_entlast: after a Circle command the last entity is the circle'
r3s_is "$R3S" '(sslength (ssget "X"))' 1 '3' 'R3S_sslength: ssget X selects all three entities drawn'
r3s_is "$R3S" '(sslength ss)' 1 '0' 'R3S_ssadd: a set made without arguments is empty, length 0'
r3s_is "$R3S" '(sslength (ssadd 2))' 1 '1' 'R3S_ssadd: (ssadd e) makes a new set with one member (documented)'
r3s_is "$R3S" '(sslength ss)' 2 '2' 'R3S_ssadd: two entities added in place, length 2 (documented)'
r3s_is "$R3S" '(eq ss (ssadd 2 ss))' 1 'T' 'R3S_ssadd: ssadd modifies the set in place and returns that same set'
r3s_is "$R3S" '(sslength ss)' 3 '3' 'R3S_ssadd: the third addition raised the length to 3'
r3s_is "$R3S" '(progn (ssadd 2 ss) (sslength ss))' 1 '3' 'R3S_ssadd: adding a member already in the set does not grow it'
r3s_err "$R3S" '(ssadd "x" ss)' 1 'bad argument type' 'R3S_ssadd_neg: a string is not an entity name'
r3s_is "$R3S" '(ssname ss 0)' 1 '1' 'R3S_ssname: index 0 is the first entity added (documented)'
r3s_is "$R3S" '(ssname ss 1)' 1 '3' 'R3S_ssname: index 1 is the second entity added'
r3s_is "$R3S" '(ssname ss 2)' 1 '2' 'R3S_ssname: index 2 is the last entity added (documented)'
r3s_is "$R3S" '(ssname ss 3)' 1 'nil' 'R3S_ssname: an index past the end is nil (documented)'
r3s_is "$R3S" '(ssname ss -1)' 1 'nil' 'R3S_ssname: a negative index is nil (documented)'
r3s_is "$R3S" '(ssname ss 1.0)' 1 '3' 'R3S_ssname: the index may be a real'
r3s_is "$R3S" '(ssmemb 2 ss)' 1 '2' 'R3S_ssmemb: a member returns its entity name (documented)'
r3s_is "$R3S" '(ssmemb 3 (ssadd 1))' 1 'nil' 'R3S_ssmemb: an entity that is not in the set is nil (documented)'
r3s_is "$R3S" '(ssmemb 1 (ssget "X" (quote ((0 . "CIRCLE")))))' 1 'nil' 'R3S_ssmemb: the line is not in the set of circles'
r3s_is "$R3S" '(ssmemb 3 (ssget "X" (quote ((0 . "CIRCLE")))))' 1 '3' 'R3S_ssmemb: the circle is in the set of circles'
r3s_is "$R3S" '(eq ss (ssdel 2 ss))' 1 'T' 'R3S_ssdel: removes the member in place and returns the set itself (documented)'
r3s_is "$R3S" '(sslength ss)' 4 '2' 'R3S_ssdel: the set is now one shorter'
r3s_is "$R3S" '(ssdel 2 ss)' 1 'nil' 'R3S_ssdel: removing an entity that is not a member again returns nil (documented)'
r3s_is "$R3S" '(sslength ss)' 5 '2' 'R3S_ssdel: a refused removal changed nothing'
r3s_is "$R3S" '(ssmemb 2 ss)' 2 'nil' 'R3S_ssdel: the removed entity is no longer a member'
r3s_is "$R3S" '(progn (ssdel 1 ss) (ssdel 3 ss) (sslength ss))' 1 '0' 'R3S_ssdel: deleting every member leaves an empty set'
