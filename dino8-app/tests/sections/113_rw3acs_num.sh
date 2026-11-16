# ---------------------------------------------------------------------------
# RW3ACS_NUM: atan, cos, exp, expt, fix, float and itoa of the AutoLISP-style dialect (src/script/Lisp.cpp).
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0121 (atan), D8-AUTOCAD-LISP-0140 (cos), D8-AUTOCAD-LISP-0167 (exp), D8-AUTOCAD-LISP-0169 (expt), D8-AUTOCAD-LISP-0172 (fix), D8-AUTOCAD-LISP-0173 (float), D8-AUTOCAD-LISP-0206 (itoa).
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic, never with the interpreter under test. Where the expected text
# is a documented worked example it is marked "(documented)" in the check label. The vendor pages could not be fetched when
# this section was written: every "(documented)" value is from recollection of the Autodesk help and needs an independent check.
# One real app process runs rw3acs_num_script.txt; each result is read from the "Lisp: <value>" line printed after the typed
# expression. Self-contained (own helpers) so that `run_sections.sh RW3ACS_NUM` works alone.
# ---------------------------------------------------------------------------

r3n_exec() {  # r3n_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r3n_ok() { echo "ok   $1"; }
r3n_bad() { echo "FAIL $1"; fail=1; }
r3n_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | PU_CMD="Command: $2" PU_NTH="$3" awk 'BEGIN { n = ENVIRON["PU_NTH"] + 0; cmd = ENVIRON["PU_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r3n_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r3n_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r3n_ok "$5 ($2 -> $got)"; else r3n_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r3n_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r3n_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r3n_ok "$5"; else r3n_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r3n_r() {  # a real the way the app prints it (six decimals, trailing zeros trimmed to one), from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}

if [ "$IS_WINDOWS" = 1 ]; then R3N_SUB="Dino8"; else R3N_SUB="dino8"; fi
R3N_CFG="$TMP/r3n_cfg"
mkdir -p "$R3N_CFG/$R3N_SUB"
R3N="$(r3n_exec "$R3N_CFG" "$HERE/rw3acs_num_script.txt")" || { echo "$R3N" | tail -20; r3n_bad "RW3ACS_NUM script exited non-zero"; }
grep -q "^smoke:" <<<"$R3N" || { echo "$R3N" | tail -20; r3n_bad "RW3ACS_NUM: no smoke line (the app did not reach its normal end)"; }
r3n_is "$R3N" '(atan 1)' 1 "$(r3n_r 'PI / 4')" 'R3N_atan: one argument, atan 1 is pi/4 (documented 0.785398)'
r3n_is "$R3N" '(atan -1)' 1 "$(r3n_r '-PI / 4')" 'R3N_atan: one argument is in -pi/2 .. pi/2'
r3n_is "$R3N" '(atan 0.5)' 1 "$(r3n_r 'atan2(0.5, 1)')" 'R3N_atan: atan 0.5'
r3n_is "$R3N" '(atan 2.0 -3.0)' 1 "$(r3n_r 'atan2(2, -3)')" 'R3N_atan: two arguments are the quotient 2/-3 in the second quadrant, 2.55359 (documented)'
r3n_is "$R3N" '(atan 1.0 0.0)' 1 "$(r3n_r 'PI / 2')" 'R3N_atan: a zero divisor gives +pi/2 for a positive numerator (documented 1.5708)'
r3n_is "$R3N" '(atan -1.0 0.0)' 1 "$(r3n_r '-PI / 2')" 'R3N_atan: a zero divisor gives -pi/2 for a negative numerator'
r3n_is "$R3N" '(atan 1 -1)' 1 "$(r3n_r '3 * PI / 4')" 'R3N_atan: (atan 1 -1) is 3 pi/4'
r3n_is "$R3N" '(atan 1e10)' 1 "$(r3n_r 'PI / 2')" 'R3N_atan: a huge argument tends to pi/2 and stays below it'
r3n_is "$R3N" '(cos 0.0)' 1 "$(r3n_r '1')" 'R3N_cos: cos 0 is 1.0 (documented)'
r3n_is "$R3N" '(cos pi)' 1 "$(r3n_r '-1')" 'R3N_cos: cos pi is -1.0 (documented)'
r3n_is "$R3N" '(cos (/ pi 3))' 1 "$(r3n_r '0.5')" 'R3N_cos: cos(pi/3) is 0.5'
r3n_is "$R3N" '(cos 1)' 1 "$(r3n_r 'cos(1)')" 'R3N_cos: an integer argument is read as radians'
r3n_is "$R3N" '(type (cos 0))' 1 'REAL' 'R3N_cos: the result is a real even for the integer 0'
r3n_is "$R3N" '(exp 1.0)' 1 "$(r3n_r 'exp(1)')" 'R3N_exp: e, 2.71828 (documented)'
r3n_is "$R3N" '(exp -0.4)' 1 "$(r3n_r 'exp(-0.4)')" 'R3N_exp: 0.67032 (documented)'
r3n_is "$R3N" '(exp 0)' 1 "$(r3n_r '1')" 'R3N_exp: exp 0 is 1.0'
r3n_is "$R3N" '(exp 2)' 1 "$(r3n_r 'exp(2)')" 'R3N_exp: an integer argument'
r3n_is "$R3N" '(log (exp 3))' 1 "$(r3n_r '3')" 'R3N_exp: log is the inverse of exp'
r3n_is "$R3N" '(expt 2 4)' 1 '16' 'R3N_expt: two integers give an integer, 16 (documented)'
r3n_is "$R3N" '(type (expt 2 4))' 1 'INT' 'R3N_expt: the result type of two integers is INT'
r3n_is "$R3N" '(expt 3.0 2.0)' 1 '9.0' 'R3N_expt: two reals give a real, 9.0 (documented)'
r3n_is "$R3N" '(type (expt 2 4.0))' 1 'REAL' 'R3N_expt: a real power gives a real'
r3n_is "$R3N" '(type (expt 2.0 4))' 1 'REAL' 'R3N_expt: a real base gives a real'
r3n_is "$R3N" '(expt 2 0.5)' 1 "$(r3n_r 'sqrt(2)')" 'R3N_expt: a fractional power is a root'
r3n_is "$R3N" '(expt -2 3)' 1 '-8' 'R3N_expt: a negative base with an odd integer power'
r3n_is "$R3N" '(expt 2.5 2)' 1 '6.25' 'R3N_expt: 2.5 squared'
r3n_is "$R3N" '(expt 5 0)' 1 '1' 'R3N_expt: anything to the power 0 is 1'
r3n_is "$R3N" '(fix 3.7)' 1 '3' 'R3N_fix: the fraction is dropped, 3 (documented)'
r3n_is "$R3N" '(fix 3)' 1 '3' 'R3N_fix: an integer is unchanged (documented)'
r3n_is "$R3N" '(fix -3.7)' 1 '-3' 'R3N_fix: truncation is toward zero for negatives'
r3n_is "$R3N" '(type (fix 3.7))' 1 'INT' 'R3N_fix: the result is an integer'
r3n_is "$R3N" '(fix 2147483647.0)' 1 '2147483647' 'R3N_fix: the largest 32-bit integer is still an integer'
r3n_is "$R3N" '(fix -2147483648.0)' 1 '-2147483648' 'R3N_fix: the smallest 32-bit integer is still an integer'
r3n_is "$R3N" '(fix 3000000000.5)' 1 '3000000000.0' 'R3N_fix: beyond the 32-bit integer range the truncated REAL is returned'
r3n_is "$R3N" '(type (fix 3000000000.5))' 1 'REAL' 'R3N_fix: beyond the 32-bit integer range the type is REAL'
r3n_is "$R3N" '(fix -3000000000.5)' 1 '-3000000000.0' 'R3N_fix: the same for a large negative value'
r3n_is "$R3N" '(float 3)' 1 '3.0' 'R3N_float: an integer becomes the real 3.0 (documented)'
r3n_is "$R3N" '(float 3.75)' 1 '3.75' 'R3N_float: a real is unchanged (documented)'
r3n_is "$R3N" '(float -2)' 1 '-2.0' 'R3N_float: a negative integer'
r3n_is "$R3N" '(float 0)' 1 '0.0' 'R3N_float: zero'
r3n_is "$R3N" '(type (float 3))' 1 'REAL' 'R3N_float: the type is REAL'
r3n_is "$R3N" '(itoa 33)' 1 '"33"' 'R3N_itoa: a positive integer (documented)'
r3n_is "$R3N" '(itoa -17)' 1 '"-17"' 'R3N_itoa: a negative integer (documented)'
r3n_is "$R3N" '(itoa 0)' 1 '"0"' 'R3N_itoa: zero'
r3n_is "$R3N" '(itoa 2147483647)' 1 '"2147483647"' 'R3N_itoa: the largest 32-bit integer'
r3n_is "$R3N" '(itoa -2147483647)' 1 '"-2147483647"' 'R3N_itoa: a large negative integer'
r3n_is "$R3N" '(atoi (itoa 12345))' 1 '12345' 'R3N_itoa: atoi reads back what itoa wrote'
r3n_err "$R3N" '(itoa "5")' 1 'bad argument type' 'R3N_itoa_neg: a string is not an integer'
