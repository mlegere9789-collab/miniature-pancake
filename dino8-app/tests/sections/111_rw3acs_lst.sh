# ---------------------------------------------------------------------------
# RW3ACS_LST: cons, last, subst and nth of the AutoLISP-style dialect (src/script/Lisp.cpp).
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0139 (cons), D8-AUTOCAD-LISP-0208 (last), D8-AUTOCAD-LISP-0291 (subst), D8-AUTOCAD-LISP-0244 (nth).
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic, never with the interpreter under test. Where the expected text
# is a documented worked example it is marked "(documented)" in the check label. The vendor pages could not be fetched when
# this section was written: every "(documented)" value is from recollection of the Autodesk help and needs an independent check.
# One real app process runs rw3acs_lst_script.txt; each result is read from the "Lisp: <value>" line printed after the typed
# expression. Self-contained (own helpers) so that `run_sections.sh RW3ACS_LST` works alone.
# ---------------------------------------------------------------------------

r3t_exec() {  # r3t_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r3t_ok() { echo "ok   $1"; }
r3t_bad() { echo "FAIL $1"; fail=1; }
r3t_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | PU_CMD="Command: $2" PU_NTH="$3" awk 'BEGIN { n = ENVIRON["PU_NTH"] + 0; cmd = ENVIRON["PU_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r3t_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r3t_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r3t_ok "$5 ($2 -> $got)"; else r3t_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r3t_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r3t_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r3t_ok "$5"; else r3t_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r3t_r() {  # a real the way the app prints it (six decimals, trailing zeros trimmed to one), from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}

if [ "$IS_WINDOWS" = 1 ]; then R3T_SUB="Dino8"; else R3T_SUB="dino8"; fi
R3T_CFG="$TMP/r3t_cfg"
mkdir -p "$R3T_CFG/$R3T_SUB"
R3T="$(r3t_exec "$R3T_CFG" "$HERE/rw3acs_lst_script.txt")" || { echo "$R3T" | tail -20; r3t_bad "RW3ACS_LST script exited non-zero"; }
grep -q "^smoke:" <<<"$R3T" || { echo "$R3T" | tail -20; r3t_bad "RW3ACS_LST: no smoke line (the app did not reach its normal end)"; }
r3t_is "$R3T" '(cons '"'"'a '"'"'(b c d))' 1 '(A B C D)' 'R3T_cons: an atom onto a list (documented)'
r3t_is "$R3T" '(cons '"'"'(a) '"'"'(b c d))' 1 '((A) B C D)' 'R3T_cons: a list onto a list (documented)'
r3t_is "$R3T" '(cons '"'"'a 2)' 1 '(A . 2)' 'R3T_cons: an atom as second argument makes a dotted pair (documented)'
r3t_is "$R3T" '(cons '"'"'a nil)' 1 '(A)' 'R3T_cons: onto nil gives a one-element list'
r3t_is "$R3T" '(car (cons 1 2))' 1 '1' 'R3T_cons: the first element of the pair'
r3t_is "$R3T" '(cdr (cons 1 2))' 1 '2' 'R3T_cons: the second element of the pair is the atom, not a list'
r3t_is "$R3T" '(length (cons 0 '"'"'(1 2 3)))' 1 '4' 'R3T_cons: the new element counts, length 4'
r3t_is "$R3T" '(last '"'"'(a b c d e))' 1 'E' 'R3T_last: the last atom (documented)'
r3t_is "$R3T" '(last '"'"'(a b c (d e)))' 1 '(D E)' 'R3T_last: the last element may be a list (documented)'
r3t_is "$R3T" '(last nil)' 1 'nil' 'R3T_last: the empty list has no last element'
r3t_is "$R3T" '(last '"'"'(7))' 1 '7' 'R3T_last: a one-element list'
r3t_is "$R3T" '(subst '"'"'new '"'"'(c d) sample)' 1 '(A B NEW E)' 'R3T_subst: the sub-list (c d) is replaced by new (documented)'
r3t_is "$R3T" '(progn sample)' 1 '(A B (C D) E)' 'R3T_subst: the original list is not modified, a copy is returned'
r3t_is "$R3T" '(subst '"'"'x '"'"'b '"'"'(a b c b))' 1 '(A X C X)' 'R3T_subst: every occurrence of the old item is replaced'
r3t_is "$R3T" '(subst '"'"'x '"'"'z '"'"'(a b))' 1 '(A B)' 'R3T_subst: an absent old item leaves the list unchanged'
r3t_is "$R3T" '(subst 5 2 '"'"'(1 2 3 2))' 1 '(1 5 3 5)' 'R3T_subst: numbers'
r3t_is "$R3T" '(nth 3 '"'"'(a b c d e))' 1 'D' 'R3T_nth: position 3 (documented)'
r3t_is "$R3T" '(nth 0 '"'"'(a b c d e))' 1 'A' 'R3T_nth: position 0 is the first element'
r3t_is "$R3T" '(nth 4 '"'"'(a b c d e))' 1 'E' 'R3T_nth: the last position'
r3t_is "$R3T" '(nth 5 '"'"'(a b c d e))' 1 'nil' 'R3T_nth: beyond the last element is nil (documented)'
r3t_is "$R3T" '(nth 1 '"'"'(a (b c) d))' 1 '(B C)' 'R3T_nth: an element that is a list'
r3t_is "$R3T" '(nth 0 nil)' 1 'nil' 'R3T_nth: the empty list'
