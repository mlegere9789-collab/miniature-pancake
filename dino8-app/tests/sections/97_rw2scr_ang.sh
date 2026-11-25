# ---------------------------------------------------------------------------
# RW2SCR_ANG: angtos and angtof of the AutoLISP-style dialect (src/script/Lisp.cpp).
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0112 (angtof), D8-AUTOCAD-LISP-0113 (angtos).
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic (their own printf, integer and floating point), never with the
# interpreter under test. One real app process runs rw2scr_ang_script.txt; each result is read from the "Lisp: <value>" line
# printed after the typed expression. Self-contained (own helpers) so that `run_sections.sh RW2SCR_ANG` works alone.
# ---------------------------------------------------------------------------

r2a_exec() {  # r2a_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r2a_ok() { echo "ok   $1"; }
r2a_bad() { echo "FAIL $1"; fail=1; }
r2a_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | R2A_CMD="Command: $2" R2A_NTH="$3" awk 'BEGIN { n = ENVIRON["R2A_NTH"] + 0; cmd = ENVIRON["R2A_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r2a_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r2a_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r2a_ok "$5 ($2 -> $got)"; else r2a_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r2a_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r2a_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r2a_ok "$5"; else r2a_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r2a_r() {  # a real the way the app prints it, from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}
r2a_f() {  # <printf format> <awk expression>: a formatted number
  awk -v PI=3.14159265358979323846 -v fmt="$1" "BEGIN { printf fmt, $2 }"
}

if [ "$IS_WINDOWS" = 1 ]; then R2A_SUB="Dino8"; else R2A_SUB="dino8"; fi
R2A_CFG="$TMP/r2a_cfg"
mkdir -p "$R2A_CFG/$R2A_SUB"
R2A="$(r2a_exec "$R2A_CFG" "$HERE/rw2scr_ang_script.txt")" || { echo "$R2A" | tail -20; r2a_bad "RW2SCR_ANG script exited non-zero"; }
grep -q "^smoke:" <<<"$R2A" || { echo "$R2A" | tail -20; r2a_bad "RW2SCR_ANG: no smoke line (the app did not reach its normal end)"; }

# ---- angtos (D8-AUTOCAD-LISP-0113): degrees, grads, radians ----------------------------------------------
r2a_is "$R2A" '(angtos 0.785398 0 4)' 1 "$(r2a_f '"%.4f"' "0.785398 * 180 / PI")" 'R2A_angtos: 0.785398 rad is 45.0000 degrees (documented example)'
r2a_is "$R2A" '(angtos -0.785398 0 4)' 1 "$(r2a_f '"%.4f"' "360 + (-0.785398) * 180 / PI")" 'R2A_angtos: a negative angle is normalised to 315.0000 (documented)'
r2a_is "$R2A" '(angtos -0.785398 4)' 1 '"S 45d E"' 'R2A_angtos: surveyor'"'"'s units, -0.785398 rad is S 45d E (documented)'
r2a_is "$R2A" '(angtos 0.785398)' 1 '"45"' 'R2A_angtos: the defaults are AUNITS 0 and AUPREC 0'
r2a_is "$R2A" '(angtos pi 0 2)' 1 '"180.00"' 'R2A_angtos: pi rad is 180.00 degrees'
r2a_is "$R2A" '(angtos pi 2 3)' 1 '"200.000g"' 'R2A_angtos: pi rad is 200 grads, suffix g'
r2a_is "$R2A" '(angtos pi 3 4)' 1 "$(r2a_f '"%.4fr"' "PI")" 'R2A_angtos: radians, suffix r'
r2a_is "$R2A" '(angtos 1 0 8)' 1 '"57.29577951"' 'R2A_angtos: eight decimals, 1 rad is 57.29577951 degrees'
r2a_is "$R2A" '(angtos (* 2 pi) 0 2)' 1 '"0.00"' 'R2A_angtos: a full turn is 0.00, never 360.00'
r2a_is "$R2A" '(angtos 7 0 2)' 1 "$(r2a_f '"%.2f"' "(7 - 2 * PI) * 180 / PI")" 'R2A_angtos: angles above 2 pi are reduced'

# ---- angtos: degrees, minutes, seconds (mode 1); d, ' and " mark the parts -------------------------------
r2a_is "$R2A" '(angtos (* (/ pi 180) (+ 12 (/ 34 60.0) (/ 56 3600.0))) 1 4)' 1 '"12d34'"'"'56\""' 'R2A_angtos_dms: 12d34'"'"'56" at precision 4'
r2a_is "$R2A" '(angtos (* (/ pi 180) (+ 0 (/ 0 60.0) (/ 1 3600.0))) 1 4)' 1 '"0d0'"'"'1\""' 'R2A_angtos_dms: 0d0'"'"'1" at precision 4'
r2a_is "$R2A" '(angtos (* (/ pi 180) (+ 100 (/ 0 60.0) (/ 30 3600.0))) 1 4)' 1 '"100d0'"'"'30\""' 'R2A_angtos_dms: 100d0'"'"'30" at precision 4'
r2a_is "$R2A" '(angtos (* (/ pi 180) (+ 359 (/ 59 60.0) (/ 59 3600.0))) 1 4)' 1 '"359d59'"'"'59\""' 'R2A_angtos_dms: 359d59'"'"'59" at precision 4'
r2a_is "$R2A" '(angtos (* (/ pi 180) (+ 45 (/ 0 60.0) (/ 0 3600.0))) 1 4)' 1 '"45d0'"'"'0\""' 'R2A_angtos_dms: 45d0'"'"'0" at precision 4'
r2a_is "$R2A" '(angtos (* (/ pi 180) 30.508333333333) 1 2)' 1 '"30d30'"'"'"' 'R2A_angtos_dms: precision 2 stops at the minutes'
r2a_is "$R2A" '(angtos (* (/ pi 180) 30.508333333333) 1 0)' 1 '"31d"' 'R2A_angtos_dms: precision 0 rounds to whole degrees'
r2a_is "$R2A" '(angtos (* (/ pi 180) 30.508333333333) 1 6)' 1 '"30d30'"'"'30.00\""' 'R2A_angtos_dms: precision 6 gives two decimals of a second'
r2a_is "$R2A" '(angtos -0.785398 1 2)' 1 '"315d0'"'"'"' 'R2A_angtos_dms: a negative angle is normalised'

# ---- angtos: surveyor's units (mode 4): a bearing off north or south toward east or west -----------------
r2a_is "$R2A" '(angtos 0 4 0)' 1 '"E"' 'R2A_angtos_surv: due east'
r2a_is "$R2A" '(angtos (/ pi 2) 4 0)' 1 '"N"' 'R2A_angtos_surv: due north'
r2a_is "$R2A" '(angtos pi 4 0)' 1 '"W"' 'R2A_angtos_surv: due west'
r2a_is "$R2A" '(angtos (* 1.5 pi) 4 0)' 1 '"S"' 'R2A_angtos_surv: due south'
r2a_is "$R2A" '(angtos (* (/ pi 180) 60) 4 4)' 1 '"N 30d0'"'"'0\" E"' 'R2A_angtos_surv: precision 4 gives seconds'
r2a_is "$R2A" '(angtos (* (/ pi 180) 10) 4 0)' 1 '"N 80d E"' 'R2A_angtos_surv: 10 degrees is N 80d E'
r2a_is "$R2A" '(angtos (* (/ pi 180) 30) 4 0)' 1 '"N 60d E"' 'R2A_angtos_surv: 30 degrees is N 60d E'
r2a_is "$R2A" '(angtos (* (/ pi 180) 45) 4 0)' 1 '"N 45d E"' 'R2A_angtos_surv: 45 degrees is N 45d E'
r2a_is "$R2A" '(angtos (* (/ pi 180) 60) 4 0)' 1 '"N 30d E"' 'R2A_angtos_surv: 60 degrees is N 30d E'
r2a_is "$R2A" '(angtos (* (/ pi 180) 80) 4 0)' 1 '"N 10d E"' 'R2A_angtos_surv: 80 degrees is N 10d E'
r2a_is "$R2A" '(angtos (* (/ pi 180) 100) 4 0)' 1 '"N 10d W"' 'R2A_angtos_surv: 100 degrees is N 10d W'
r2a_is "$R2A" '(angtos (* (/ pi 180) 120) 4 0)' 1 '"N 30d W"' 'R2A_angtos_surv: 120 degrees is N 30d W'
r2a_is "$R2A" '(angtos (* (/ pi 180) 135) 4 0)' 1 '"N 45d W"' 'R2A_angtos_surv: 135 degrees is N 45d W'
r2a_is "$R2A" '(angtos (* (/ pi 180) 150) 4 0)' 1 '"N 60d W"' 'R2A_angtos_surv: 150 degrees is N 60d W'
r2a_is "$R2A" '(angtos (* (/ pi 180) 170) 4 0)' 1 '"N 80d W"' 'R2A_angtos_surv: 170 degrees is N 80d W'
r2a_is "$R2A" '(angtos (* (/ pi 180) 190) 4 0)' 1 '"S 80d W"' 'R2A_angtos_surv: 190 degrees is S 80d W'
r2a_is "$R2A" '(angtos (* (/ pi 180) 210) 4 0)' 1 '"S 60d W"' 'R2A_angtos_surv: 210 degrees is S 60d W'
r2a_is "$R2A" '(angtos (* (/ pi 180) 225) 4 0)' 1 '"S 45d W"' 'R2A_angtos_surv: 225 degrees is S 45d W'
r2a_is "$R2A" '(angtos (* (/ pi 180) 240) 4 0)' 1 '"S 30d W"' 'R2A_angtos_surv: 240 degrees is S 30d W'
r2a_is "$R2A" '(angtos (* (/ pi 180) 260) 4 0)' 1 '"S 10d W"' 'R2A_angtos_surv: 260 degrees is S 10d W'
r2a_is "$R2A" '(angtos (* (/ pi 180) 280) 4 0)' 1 '"S 10d E"' 'R2A_angtos_surv: 280 degrees is S 10d E'
r2a_is "$R2A" '(angtos (* (/ pi 180) 300) 4 0)' 1 '"S 30d E"' 'R2A_angtos_surv: 300 degrees is S 30d E'
r2a_is "$R2A" '(angtos (* (/ pi 180) 315) 4 0)' 1 '"S 45d E"' 'R2A_angtos_surv: 315 degrees is S 45d E'
r2a_is "$R2A" '(angtos (* (/ pi 180) 330) 4 0)' 1 '"S 60d E"' 'R2A_angtos_surv: 330 degrees is S 60d E'
r2a_is "$R2A" '(angtos (* (/ pi 180) 350) 4 0)' 1 '"S 80d E"' 'R2A_angtos_surv: 350 degrees is S 80d E'
r2a_err "$R2A" '(angtos 1 5)' 1 'bad argument value' 'R2A_angtos_neg: mode 5 does not exist'
r2a_err "$R2A" '(angtos 1 0 9)' 1 'bad argument value' 'R2A_angtos_neg: precision 9 is out of range'
r2a_err "$R2A" '(angtos "a")' 1 'bad argument type' 'R2A_angtos_neg: a string is not an angle'
r2a_err "$R2A" '(angtos)' 1 'too few arguments' 'R2A_angtos_neg: no argument'

# ---- angtof (D8-AUTOCAD-LISP-0112): the string to radians, nil when it does not fit the format -----------
r2a_is "$R2A" '(angtof "45.0000")' 1 "$(r2a_r "45 * PI / 180")" 'R2A_angtof: 45.0000 degrees is 0.785398 rad (documented)'
r2a_is "$R2A" '(angtof "45.0000" 3)' 1 "$(r2a_r "45 - int(45 / (2 * PI)) * 2 * PI")" 'R2A_angtof: in radians mode 45.0000 is 45 rad, reduced to 1.0177 (documented)'
r2a_is "$R2A" '(angtof "S 45d E" 4)' 1 "$(r2a_r "315 * PI / 180")" 'R2A_angtof: S 45d E is 315 degrees'
r2a_is "$R2A" '(angtof "N 30d W" 4)' 1 "$(r2a_r "120 * PI / 180")" 'R2A_angtof: N 30d W is 120 degrees'
r2a_is "$R2A" '(angtof "S 60d W" 4)' 1 "$(r2a_r "210 * PI / 180")" 'R2A_angtof: S 60d W is 210 degrees'
r2a_is "$R2A" '(angtof "N 60d E" 4)' 1 "$(r2a_r "30 * PI / 180")" 'R2A_angtof: N 60d E is 30 degrees'
r2a_is "$R2A" '(angtof "E" 4)' 1 0.0 'R2A_angtof: E is 0'
r2a_is "$R2A" '(angtof "N" 4)' 1 "$(r2a_r "PI / 2")" 'R2A_angtof: N is pi/2'
r2a_is "$R2A" '(angtof "W" 4)' 1 "$(r2a_r "PI")" 'R2A_angtof: W is pi'
r2a_is "$R2A" '(angtof "S" 4)' 1 "$(r2a_r "3 * PI / 2")" 'R2A_angtof: S is 3 pi/2'
r2a_is "$R2A" '(angtof "30d30'"'"'30\"" 1)' 1 "$(r2a_r "(30 + 30 / 60 + 30 / 3600) * PI / 180")" 'R2A_angtof: 30d30'"'"'30" in degrees, minutes, seconds'
r2a_is "$R2A" '(angtof "12d" 1)' 1 "$(r2a_r "12 * PI / 180")" 'R2A_angtof: whole degrees with d'
r2a_is "$R2A" '(angtof "50g" 2)' 1 "$(r2a_r "50 * PI / 200")" 'R2A_angtof: 50 grads is 45 degrees'
r2a_is "$R2A" '(angtof "100" 2)' 1 "$(r2a_r "100 * PI / 200")" 'R2A_angtof: 100 grads is a right angle'
r2a_is "$R2A" '(angtof "1.5r" 3)' 1 1.5 'R2A_angtof: 1.5 radians with the r suffix'
r2a_is "$R2A" '(angtof "-45")' 1 "$(r2a_r "315 * PI / 180")" 'R2A_angtof: a negative angle is normalised'
r2a_is "$R2A" '(angtof "abc")' 1 nil 'R2A_angtof_neg: text is not an angle'
r2a_is "$R2A" '(angtof "45x")' 1 nil 'R2A_angtof_neg: a wrong suffix'
r2a_is "$R2A" '(angtof "N 45d X" 4)' 1 nil 'R2A_angtof_neg: X is not a direction'
r2a_is "$R2A" '(angtof "N 95d E" 4)' 1 nil 'R2A_angtof_neg: a bearing is at most 90 degrees'
r2a_is "$R2A" '(angtof "")' 1 nil 'R2A_angtof_neg: the empty string'
r2a_is "$R2A" '(angtof "12d60" 1)' 1 nil 'R2A_angtof_neg: minutes need their '"'"' mark'
r2a_err "$R2A" '(angtof 5)' 1 'bad argument type' 'R2A_angtof_neg: a number is not a string'
r2a_err "$R2A" '(angtof "5" 7)' 1 'bad argument value' 'R2A_angtof_neg: mode 7 does not exist'

# ---- round trips: angtof reads what angtos writes --------------------------------------------------------
r2a_is "$R2A" '(rtos (angtof (angtos 1 0 8) 0) 2 6)' 1 '"1.000000"' 'R2A_angtof_roundtrip: angle 1 through mode 0'
r2a_is "$R2A" '(rtos (angtof (angtos 2.5 1 6) 1) 2 5)' 1 '"2.50000"' 'R2A_angtof_roundtrip: angle 2.5 through mode 1'
r2a_is "$R2A" '(rtos (angtof (angtos 4 4 6) 4) 2 5)' 1 '"4.00000"' 'R2A_angtof_roundtrip: angle 4 through mode 4'
r2a_is "$R2A" '(rtos (angtof (angtos 0.3 2 6) 2) 2 6)' 1 '"0.300000"' 'R2A_angtof_roundtrip: angle 0.3 through mode 2'
r2a_is "$R2A" '(rtos (angtof (angtos 5.9 3 8) 3) 2 6)' 1 '"5.900000"' 'R2A_angtof_roundtrip: angle 5.9 through mode 3'
