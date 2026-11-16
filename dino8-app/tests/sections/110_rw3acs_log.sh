# ---------------------------------------------------------------------------
# RW3ACS_LOG: and, or, eq and equal of the AutoLISP-style dialect (src/script/Lisp.cpp).
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0110 (and), D8-AUTOCAD-LISP-0248 (or), D8-AUTOCAD-LISP-0163 (eq), D8-AUTOCAD-LISP-0164 (equal).
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic, never with the interpreter under test. Where the expected text
# is a documented worked example it is marked "(documented)" in the check label. The vendor pages could not be fetched when
# this section was written: every "(documented)" value is from recollection of the Autodesk help and needs an independent check.
# NOTE (checked against the documentation): AutoLISP and / or return T or nil, never the last or first non-nil value (unlike Common Lisp).
# The older check LSP_logic in 60_lsp.sh pinned (or nil 7) to 7; that contradicted the documented return value and was corrected to T.
# One real app process runs rw3acs_log_script.txt; each result is read from the "Lisp: <value>" line printed after the typed
# expression. Self-contained (own helpers) so that `run_sections.sh RW3ACS_LOG` works alone.
# ---------------------------------------------------------------------------

r3l_exec() {  # r3l_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r3l_ok() { echo "ok   $1"; }
r3l_bad() { echo "FAIL $1"; fail=1; }
r3l_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | PU_CMD="Command: $2" PU_NTH="$3" awk 'BEGIN { n = ENVIRON["PU_NTH"] + 0; cmd = ENVIRON["PU_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r3l_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r3l_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r3l_ok "$5 ($2 -> $got)"; else r3l_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r3l_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r3l_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r3l_ok "$5"; else r3l_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r3l_r() {  # a real the way the app prints it (six decimals, trailing zeros trimmed to one), from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}

if [ "$IS_WINDOWS" = 1 ]; then R3L_SUB="Dino8"; else R3L_SUB="dino8"; fi
R3L_CFG="$TMP/r3l_cfg"
mkdir -p "$R3L_CFG/$R3L_SUB"
R3L="$(r3l_exec "$R3L_CFG" "$HERE/rw3acs_log_script.txt")" || { echo "$R3L" | tail -20; r3l_bad "RW3ACS_LOG script exited non-zero"; }
grep -q "^smoke:" <<<"$R3L" || { echo "$R3L" | tail -20; r3l_bad "RW3ACS_LOG: no smoke line (the app did not reach its normal end)"; }
r3l_is "$R3L" '(and 1.4 a c)' 1 'T' 'R3L_and: (and 1.4 a c) with a=103 and c="string" is T (documented)'
r3l_is "$R3L" '(and 1.4 a b c)' 1 'nil' 'R3L_and: with b=nil among the arguments it is nil (documented)'
r3l_is "$R3L" '(and)' 1 'T' 'R3L_and: no arguments gives T (documented)'
r3l_is "$R3L" '(and 0 "" (quote (1)))' 1 'T' 'R3L_and: 0, the empty string and a list are all non-nil, the value is T and not the last value'
r3l_is "$R3L" '(and 5 7)' 1 'T' 'R3L_and: the result is T, never the last value 7'
r3l_is "$R3L" '(and nil (setq r3zz 5))' 1 'nil' 'R3L_and: nil at the first argument'
r3l_is "$R3L" '(boundp (quote r3zz))' 1 'nil' 'R3L_and_order: evaluation stops at the first nil, the later setq never ran'
r3l_is "$R3L" '(and (setq r3q1 1) (setq r3q2 2))' 1 'T' 'R3L_and_order: all arguments non-nil gives T'
r3l_is "$R3L" '(list r3q1 r3q2)' 1 '(1 2)' 'R3L_and_order: arguments are evaluated left to right, both setq ran'
r3l_is "$R3L" '(and 1 nil (quote x))' 1 'nil' 'R3L_and: nil in the middle gives nil'
r3l_is "$R3L" '(or nil 45 (quote ()))' 1 'T' 'R3L_or: (or nil 45 (quote ())) is T (documented)'
r3l_is "$R3L" '(or nil (quote ()))' 1 'nil' 'R3L_or: all nil gives nil (documented)'
r3l_is "$R3L" '(or)' 1 'nil' 'R3L_or: no arguments gives nil (documented)'
r3l_is "$R3L" '(or nil 7)' 1 'T' 'R3L_or: the result is T, never the first non-nil value 7'
r3l_is "$R3L" '(or 7 nil)' 1 'T' 'R3L_or: a non-nil first argument gives T'
r3l_is "$R3L" '(or (setq r3w1 1) (setq r3w2 2))' 1 'T' 'R3L_or_order: the first non-nil argument decides'
r3l_is "$R3L" '(boundp (quote r3w2))' 1 'nil' 'R3L_or_order: evaluation stops at the first non-nil argument, the second setq never ran'
r3l_is "$R3L" '(or nil nil (setq r3w3 3))' 1 'T' 'R3L_or_order: later arguments are tried while the earlier ones are nil'
r3l_is "$R3L" '(list r3w3)' 1 '(3)' 'R3L_or_order: the third argument ran'
r3l_is "$R3L" '(eq f1 f3)' 1 'nil' 'R3L_eq: two separately created equal-valued lists are not eq (documented)'
r3l_is "$R3L" '(eq f3 f2)' 1 'T' 'R3L_eq: f3 was set to the very object f2 is eq (documented)'
r3l_is "$R3L" '(eq f1 f2)' 1 'nil' 'R3L_eq: f1 and f2 were created separately, not eq'
r3l_is "$R3L" '(eq '"'"'a '"'"'a)' 1 'T' 'R3L_eq: the same symbol is eq to itself'
r3l_is "$R3L" '(eq '"'"'a '"'"'b)' 1 'nil' 'R3L_eq: different symbols are not eq'
r3l_is "$R3L" '(eq nil nil)' 1 'T' 'R3L_eq: nil is eq to nil'
r3l_is "$R3L" '(equal f1 f2)' 1 'T' 'R3L_equal: equal-valued separately created lists are equal'
r3l_is "$R3L" '(equal f3 f2)' 1 'T' 'R3L_equal: whatever eq finds identical is also equal'
r3l_is "$R3L" '(equal '"'"'(1.0 2.0) '"'"'(1.0 2.0))' 1 'T' 'R3L_equal: two identical point lists (documented)'
r3l_is "$R3L" '(equal 1.0 1.0000001 0.001)' 1 'T' 'R3L_equal: reals within the fuzz are equal (documented)'
r3l_is "$R3L" '(equal 1.0 1.1)' 1 'nil' 'R3L_equal: without a fuzz 1.0 and 1.1 differ (documented)'
r3l_is "$R3L" '(equal '"'"'(1.0 2.0) '"'"'(1.0 2.05) 0.1)' 1 'T' 'R3L_equal: a point list is compared coordinate by coordinate within the fuzz'
r3l_is "$R3L" '(equal '"'"'(1.0 2.0) '"'"'(1.0 2.5) 0.1)' 1 'nil' 'R3L_equal: a coordinate off by 0.5 is outside a fuzz of 0.1'
r3l_is "$R3L" '(equal '"'"'(1.0 2.0) '"'"'(1.0 2.05))' 1 'nil' 'R3L_equal: no fuzz means exact comparison of the coordinates'
r3l_is "$R3L" '(equal '"'"'a '"'"'a)' 1 'T' 'R3L_equal: the same symbol'
r3l_is "$R3L" '(equal "abc" "abc")' 1 'T' 'R3L_equal: equal strings'
r3l_is "$R3L" '(equal "abc" "abd")' 1 'nil' 'R3L_equal: different strings'
