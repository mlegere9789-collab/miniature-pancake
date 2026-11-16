# ---------------------------------------------------------------------------
# RW2SCR_FORMS: special forms and small numeric functions of the AutoLISP-style dialect (src/script/Lisp.cpp).
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0138 (cond), -0254 (progn), -0257 (quote), -0264 (repeat), -0375 (while), -0174 (foreach), -0093 (1+), -0092 (1-), -0094 (abs).
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic (their own printf, integer and floating point), never with the
# interpreter under test. One real app process runs rw2scr_forms_script.txt; each result is read from the "Lisp: <value>" line
# printed after the typed expression. Self-contained (own helpers) so that `run_sections.sh RW2SCR_FORMS` works alone.
# ---------------------------------------------------------------------------

r2f_exec() {  # r2f_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r2f_ok() { echo "ok   $1"; }
r2f_bad() { echo "FAIL $1"; fail=1; }
r2f_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | R2F_CMD="Command: $2" R2F_NTH="$3" awk 'BEGIN { n = ENVIRON["R2F_NTH"] + 0; cmd = ENVIRON["R2F_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r2f_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r2f_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r2f_ok "$5 ($2 -> $got)"; else r2f_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r2f_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r2f_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r2f_ok "$5"; else r2f_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r2f_r() {  # a real the way the app prints it, from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}
r2f_f() {  # <printf format> <awk expression>: a formatted number
  awk -v PI=3.14159265358979323846 -v fmt="$1" "BEGIN { printf fmt, $2 }"
}

if [ "$IS_WINDOWS" = 1 ]; then R2F_SUB="Dino8"; else R2F_SUB="dino8"; fi
R2F_CFG="$TMP/r2f_cfg"
mkdir -p "$R2F_CFG/$R2F_SUB"
R2F="$(r2f_exec "$R2F_CFG" "$HERE/rw2scr_forms_script.txt")" || { echo "$R2F" | tail -20; r2f_bad "RW2SCR_FORMS script exited non-zero"; }
grep -q "^smoke:" <<<"$R2F" || { echo "$R2F" | tail -20; r2f_bad "RW2SCR_FORMS: no smoke line (the app did not reach its normal end)"; }

# ---- cond (D8-AUTOCAD-LISP-0138) -------------------------------------------------------------------------
r2f_is "$R2F" '(setq rw2a -10)' 1 -10 'R2F_cond: setup'
r2f_is "$R2F" '(cond ((minusp rw2a) (- rw2a)) (t rw2a))' 1 10 'R2F_cond: the first true clause (documented)'
r2f_is "$R2F" '(setq rw2a 10)' 1 10 'R2F_cond: setup'
r2f_is "$R2F" '(cond ((minusp rw2a) (- rw2a)) (t rw2a))' 2 10 'R2F_cond: the t clause'
r2f_is "$R2F" '(cond ((= 1 2) 1))' 1 nil 'R2F_cond: no clause true is nil'
r2f_is "$R2F" '(cond)' 1 nil 'R2F_cond: no clause'
r2f_is "$R2F" '(cond ((= 1 1)))' 1 T 'R2F_cond: a clause of a lone test returns the test value'
r2f_is "$R2F" '(cond (nil 1) ((+ 2 3)))' 1 5 'R2F_cond: a lone test that is a number'
r2f_is "$R2F" '(cond ((= 1 1) 1 2 3))' 1 3 'R2F_cond: the last expression of the clause'
r2f_is "$R2F" '(cond ((= 1 2) (/ 1 0)) (t 9))' 1 9 'R2F_cond: later clauses and failed branches are not evaluated'

# ---- progn and quote (D8-AUTOCAD-LISP-0254, -0257) -------------------------------------------------------
r2f_is "$R2F" '(progn (setq rw2p 1) (setq rw2q 2) (+ rw2p rw2q))' 1 3 'R2F_progn: the last value (documented)'
r2f_is "$R2F" '(list rw2p rw2q)' 1 '(1 2)' 'R2F_progn: both variables were assigned'
r2f_is "$R2F" '(progn)' 1 nil 'R2F_progn: nothing is nil'
r2f_is "$R2F" '(if T (progn 1 2 3))' 1 3 'R2F_progn: several forms in an if branch'
r2f_is "$R2F" '(quote (a b))' 1 '(A B)' 'R2F_quote: a list unevaluated (documented)'
r2f_is "$R2F" '(progn '"'"'a)' 1 A 'R2F_quote: the apostrophe shorthand (documented)'
r2f_is "$R2F" '(progn '"'"'(1 "x" 2.5))' 1 '(1 "x" 2.5)' 'R2F_quote: mixed atoms'
r2f_is "$R2F" '(progn '"'"'(+ 1 2))' 1 '(+ 1 2)' 'R2F_quote: a form is data'
r2f_is "$R2F" '(quote rw2unbound)' 1 RW2UNBOUND 'R2F_quote: no value is looked up'
r2f_is "$R2F" '(car '"'"'(a b c))' 1 A 'R2F_quote: a quoted list is a list'

# ---- repeat, while and foreach (D8-AUTOCAD-LISP-0264, -0375, -0174) --------------------------------------
r2f_is "$R2F" '(setq rw2a 10 rw2b 100)' 1 100 'R2F_repeat: setup'
r2f_is "$R2F" '(repeat 4 (setq rw2a (+ rw2a 10)) (setq rw2b (+ rw2b 100)))' 1 500 'R2F_repeat: the value of the last form (documented)'
r2f_is "$R2F" '(list rw2a rw2b)' 1 '(50 500)' 'R2F_repeat: four passes (documented)'
r2f_is "$R2F" '(repeat 0 (setq rw2a 99))' 1 nil 'R2F_repeat: zero passes is nil'
r2f_is "$R2F" '(progn rw2a)' 1 50 'R2F_repeat: and the body did not run'
r2f_is "$R2F" '(repeat -1 5)' 1 nil 'R2F_repeat: a negative count does nothing'
r2f_err "$R2F" '(repeat 2.5 1)' 1 'bad argument type' 'R2F_repeat_neg: the count must be an integer'
r2f_is "$R2F" '(setq rw2t 1)' 1 1 'R2F_while: setup'
r2f_is "$R2F" '(while (<= rw2t 10) (setq rw2t (1+ rw2t)))' 1 11 'R2F_while: the last body value is returned (documented)'
r2f_is "$R2F" '(progn rw2t)' 1 11 'R2F_while: the test variable ends at 11 (documented)'
r2f_is "$R2F" '(while nil 1)' 1 nil 'R2F_while: a false test at the start is nil'
r2f_is "$R2F" '(setq rw2i 0 rw2s 0)' 1 0 'R2F_while: setup of a sum'
r2f_is "$R2F" '(while (< rw2i 5) (setq rw2s (+ rw2s rw2i)) (setq rw2i (1+ rw2i)))' 1 5 'R2F_while: two body forms, the last one counts'
r2f_is "$R2F" '(progn rw2s)' 1 10 'R2F_while: 0+1+2+3+4'
r2f_is "$R2F" '(foreach rw2n '"'"'(a b c) rw2n)' 1 C 'R2F_foreach: the last body value (documented)'
r2f_is "$R2F" '(foreach rw2n '"'"'(1 2 3) (setq rw2s (+ rw2s rw2n)))' 1 16 'R2F_foreach: adds 1+2+3 to the 10'
r2f_is "$R2F" '(foreach rw2n '"'"'(1 2))' 1 nil 'R2F_foreach: no body is nil (documented)'
r2f_is "$R2F" '(foreach rw2n '"'"'() 1)' 1 nil 'R2F_foreach: an empty list is nil'
r2f_is "$R2F" '(setq rw2n 99)' 1 99 'R2F_foreach: setup, the variable exists before'
r2f_is "$R2F" '(foreach rw2n '"'"'(1 2) rw2n)' 1 2 'R2F_foreach: runs with the element'
r2f_is "$R2F" '(progn rw2n)' 1 99 'R2F_foreach: the variable is restored afterwards'

# ---- 1+, 1- and abs (D8-AUTOCAD-LISP-0093, -0092, -0094): the numeric type is kept -----------------------
r2f_is "$R2F" '(1+ 5)' 1 6 'R2F_1+: an integer (documented)'
r2f_is "$R2F" '(1+ -17.5)' 1 -16.5 'R2F_1+: a real (documented)'
r2f_is "$R2F" '(1+ 2.5)' 1 3.5 'R2F_1+: another real'
r2f_is "$R2F" '(1- 5)' 1 4 'R2F_1-: an integer (documented)'
r2f_is "$R2F" '(1- -17.5)' 1 -18.5 'R2F_1-: a real (documented)'
r2f_is "$R2F" '(1- 0)' 1 -1 'R2F_1-: below zero'
r2f_is "$R2F" '(abs -100)' 1 100 'R2F_abs: an integer (documented)'
r2f_is "$R2F" '(abs -99.25)' 1 99.25 'R2F_abs: a real (documented)'
r2f_is "$R2F" '(abs 7)' 1 7 'R2F_abs: a positive number is unchanged'
r2f_is "$R2F" '(type (1+ 5))' 1 INT 'R2F_1+: an integer stays an integer'
r2f_is "$R2F" '(type (1+ 5.0))' 1 REAL 'R2F_1+: a real stays a real'
r2f_is "$R2F" '(type (abs -3))' 1 INT 'R2F_abs: an integer stays an integer'
r2f_err "$R2F" '(1+ "a")' 1 'bad argument type' 'R2F_1+_neg: a string is not a number'
r2f_err "$R2F" '(1- "a")' 1 'bad argument type' 'R2F_1-_neg: a string is not a number'
r2f_err "$R2F" '(abs "x")' 1 'bad argument type' 'R2F_abs_neg: a string is not a number'
