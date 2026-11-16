# ---------------------------------------------------------------------------
# RW2SCR_DIST: rtos (all unit modes) and distof of the AutoLISP-style dialect (src/script/Lisp.cpp).
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0266 (rtos), D8-AUTOCAD-LISP-0152 (distof).
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic (their own printf, integer and floating point), never with the
# interpreter under test. One real app process runs rw2scr_dist_script.txt; each result is read from the "Lisp: <value>" line
# printed after the typed expression. Self-contained (own helpers) so that `run_sections.sh RW2SCR_DIST` works alone.
# ---------------------------------------------------------------------------

r2d_exec() {  # r2d_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r2d_ok() { echo "ok   $1"; }
r2d_bad() { echo "FAIL $1"; fail=1; }
r2d_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | R2D_CMD="Command: $2" R2D_NTH="$3" awk 'BEGIN { n = ENVIRON["R2D_NTH"] + 0; cmd = ENVIRON["R2D_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r2d_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r2d_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r2d_ok "$5 ($2 -> $got)"; else r2d_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r2d_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r2d_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r2d_ok "$5"; else r2d_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r2d_r() {  # a real the way the app prints it, from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}
r2d_f() {  # <printf format> <awk expression>: a formatted number
  awk -v PI=3.14159265358979323846 -v fmt="$1" "BEGIN { printf fmt, $2 }"
}
r2d_arch() {  # <inches> <p>: the architectural string with denominator 2^p, as the app prints it (rounded, reduced with gcd in awk)
  awk -v x="$1" -v p="$2" 'BEGIN { den = 2 ^ p; n = int(x * den + 0.5); w = int(n / den); nu = n - w * den; d = den; a = nu; b = d
    while (b) { t = a % b; a = b; b = t }
    if (nu) { nu /= a; d /= a }
    f = int(w / 12); i = w - f * 12; s = f "\047-" i; if (nu) s = s " " nu "/" d
    printf "\"%s\\\"\"", s }'
}

if [ "$IS_WINDOWS" = 1 ]; then R2D_SUB="Dino8"; else R2D_SUB="dino8"; fi
R2D_CFG="$TMP/r2d_cfg"
mkdir -p "$R2D_CFG/$R2D_SUB"
R2D="$(r2d_exec "$R2D_CFG" "$HERE/rw2scr_dist_script.txt")" || { echo "$R2D" | tail -20; r2d_bad "RW2SCR_DIST script exited non-zero"; }
grep -q "^smoke:" <<<"$R2D" || { echo "$R2D" | tail -20; r2d_bad "RW2SCR_DIST: no smoke line (the app did not reach its normal end)"; }

# ---- rtos (D8-AUTOCAD-LISP-0266): the five linear unit modes (the inch is the drawing unit of modes 3 to 5) ---
r2d_is "$R2D" '(rtos 17.5 1 4)' 1 '"1.7500E+01"' 'R2D_rtos: scientific, precision 4 (documented)'
r2d_is "$R2D" '(rtos 17.5 1 2)' 1 '"1.75E+01"' 'R2D_rtos: scientific, precision 2'
r2d_is "$R2D" '(rtos 17.5 2 2)' 1 '"17.50"' 'R2D_rtos: decimal (documented)'
r2d_is "$R2D" '(rtos 17.5 3 2)' 1 '"1'"'"'-5.50\""' 'R2D_rtos: engineering 1 foot 5.50 inches (documented)'
r2d_is "$R2D" '(rtos 17.5 4 2)' 1 '"1'"'"'-5 1/2\""' 'R2D_rtos: architectural 1 foot 5 1/2 inches (documented)'
r2d_is "$R2D" '(rtos 17.5 5 2)' 1 '"17 1/2"' 'R2D_rtos: fractional (documented)'
r2d_is "$R2D" '(rtos 3.14159 2 2)' 1 '"3.14"' 'R2D_rtos: decimal to two places'
r2d_is "$R2D" '(rtos 17.5)' 1 '"17.5000"' 'R2D_rtos: the defaults are LUNITS 2 and LUPREC 4'
r2d_is "$R2D" '(rtos 17.5 2)' 1 '"17.5000"' 'R2D_rtos: the default precision is 4'
r2d_is "$R2D" '(rtos -17.5 4 2)' 1 '"-1'"'"'-5 1/2\""' 'R2D_rtos: a negative architectural length'
r2d_is "$R2D" '(rtos 0.5 5 2)' 1 '"1/2"' 'R2D_rtos: a fraction alone'
r2d_is "$R2D" '(rtos 0 5 2)' 1 '"0"' 'R2D_rtos: zero is 0 in fractional units'
r2d_is "$R2D" '(rtos 17.25 4 2)' 1 '"1'"'"'-5 1/4\""' 'R2D_rtos: quarter inch'
r2d_is "$R2D" '(rtos 17.26 4 2)' 1 '"1'"'"'-5 1/4\""' 'R2D_rtos: 17.26 rounds to the nearest quarter, 17.25'
r2d_is "$R2D" '(rtos 11.999 4 2)' 1 '"1'"'"'-0\""' 'R2D_rtos: 11.999 rounds up to a whole foot'
r2d_is "$R2D" '(rtos 3.5 3 2)' 1 '"0'"'"'-3.50\""' 'R2D_rtos: less than a foot has 0'"'"''
r2d_is "$R2D" '(rtos 100 3 1)' 1 '"8'"'"'-4.0\""' 'R2D_rtos: 100 inches is 8 feet 4 inches'
r2d_is "$R2D" '(rtos 100 4 0)' 1 '"8'"'"'-4\""' 'R2D_rtos: 100 inches architectural'
r2d_is "$R2D" '(rtos 17.5 4 0)' 1 '"1'"'"'-6\""' 'R2D_rtos: precision 0 is whole inches, 17.5 rounds to 18'
r2d_is "$R2D" '(rtos 17.1875 4 4)' 1 '"1'"'"'-5 3/16\""' 'R2D_rtos: sixteenths'
r2d_is "$R2D" '(rtos 1234.5 2 1)' 1 '"1234.5"' 'R2D_rtos: one decimal'
r2d_err "$R2D" '(rtos 17.5 6 2)' 1 'bad argument value' 'R2D_rtos_neg: mode 6 does not exist'
r2d_err "$R2D" '(rtos "a")' 1 'bad argument type' 'R2D_rtos_neg: a string is not a number'
r2d_is "$R2D" '(rtos 7.3 4 3)' 1 "$(r2d_arch 7.3 3)" 'R2D_rtos_arch: 7.3 inches at 1/8 inch (bash rounding and gcd)'
r2d_is "$R2D" '(rtos 100.7 4 2)' 1 "$(r2d_arch 100.7 2)" 'R2D_rtos_arch: 100.7 inches at 1/4 inch (bash rounding and gcd)'
r2d_is "$R2D" '(rtos 35.99 4 4)' 1 "$(r2d_arch 35.99 4)" 'R2D_rtos_arch: 35.99 inches at 1/16 inch (bash rounding and gcd)'
r2d_is "$R2D" '(rtos 0.26 4 3)' 1 "$(r2d_arch 0.26 3)" 'R2D_rtos_arch: 0.26 inches at 1/8 inch (bash rounding and gcd)'
r2d_is "$R2D" '(rtos 143.999 4 5)' 1 "$(r2d_arch 143.999 5)" 'R2D_rtos_arch: 143.999 inches at 1/32 inch (bash rounding and gcd)'

# ---- distof (D8-AUTOCAD-LISP-0152): strings in the five formats back to numbers, nil on failure ----------
r2d_is "$R2D" '(distof "12.5" 2)' 1 12.5 'R2D_distof: decimal'
r2d_is "$R2D" '(distof "1.2500E+01" 1)' 1 12.5 'R2D_distof: scientific'
r2d_is "$R2D" '(distof (rtos 12.5 4 2) 4)' 1 12.5 'R2D_distof: architectural round trip (documented)'
r2d_is "$R2D" '(distof "1'"'"'-5 1/2\"" 4)' 1 17.5 'R2D_distof: 1 foot 5 1/2 inches'
r2d_is "$R2D" '(distof "1'"'"'-5.5\"" 3)' 1 17.5 'R2D_distof: engineering'
r2d_is "$R2D" '(distof "1'"'"'-5.5\"" 4)' 1 17.5 'R2D_distof: modes 3 and 4 read each other'"'"'s format'
r2d_is "$R2D" '(distof "1'"'"'-5 1/2\"" 3)' 1 17.5 'R2D_distof: modes 3 and 4 read each other'"'"'s format (fraction)'
r2d_is "$R2D" '(distof "17 1/2" 5)' 1 17.5 'R2D_distof: fractional'
r2d_is "$R2D" '(distof "-3 1/4" 5)' 1 -3.25 'R2D_distof: a negative fractional length'
r2d_is "$R2D" '(distof "1/2" 5)' 1 0.5 'R2D_distof: a fraction alone'
r2d_is "$R2D" '(distof "3'"'"'" 4)' 1 36.0 'R2D_distof: feet only'
r2d_is "$R2D" '(distof "6\"" 4)' 1 6.0 'R2D_distof: inches only'
r2d_is "$R2D" '(distof "-1'"'"'-5 1/2\"" 4)' 1 -17.5 'R2D_distof: a negative architectural length'
r2d_is "$R2D" '(distof "abc" 2)' 1 nil 'R2D_distof_neg: text is not a number'
r2d_is "$R2D" '(distof "1'"'"'-5 1/2\"" 2)' 1 nil 'R2D_distof_neg: decimal mode does not read feet and inches'
r2d_is "$R2D" '(distof "1/0" 5)' 1 nil 'R2D_distof_neg: a zero denominator'
r2d_is "$R2D" '(distof "" 2)' 1 nil 'R2D_distof_neg: the empty string'
r2d_is "$R2D" '(distof "12 1/2x" 5)' 1 nil 'R2D_distof_neg: trailing text'
r2d_err "$R2D" '(distof "1" 9)' 1 'bad argument value' 'R2D_distof_neg: mode 9 does not exist'
r2d_err "$R2D" '(distof 5)' 1 'bad argument type' 'R2D_distof_neg: a number is not a string'
r2d_is "$R2D" '(rtos (distof (rtos 12.5 1 6) 1) 2 4)' 1 '"12.5000"' 'R2D_distof_roundtrip: 12.5 through mode 1'
r2d_is "$R2D" '(rtos (distof (rtos 17.5 3 4) 3) 2 4)' 1 '"17.5000"' 'R2D_distof_roundtrip: 17.5 through mode 3'
r2d_is "$R2D" '(rtos (distof (rtos 0.75 5 3) 5) 2 4)' 1 '"0.7500"' 'R2D_distof_roundtrip: 0.75 through mode 5'
r2d_is "$R2D" '(rtos (distof (rtos 123.4375 4 4) 4) 2 4)' 1 '"123.4375"' 'R2D_distof_roundtrip: 123.4375 through mode 4'
r2d_is "$R2D" '(rtos (distof (rtos -9.5 5 1) 5) 2 4)' 1 '"-9.5000"' 'R2D_distof_roundtrip: -9.5 through mode 5'
r2d_is "$R2D" '(rtos (distof (rtos 1234.5678 2 4) 2) 2 4)' 1 '"1234.5678"' 'R2D_distof_roundtrip: 1234.5678 through mode 2'
