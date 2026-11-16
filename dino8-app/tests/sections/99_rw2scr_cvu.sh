# ---------------------------------------------------------------------------
# RW2SCR_CVU: cvunit of the AutoLISP-style dialect (src/script/Lisp.cpp).
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0141 (cvunit).
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic (their own printf, integer and floating point), never with the
# interpreter under test. One real app process runs rw2scr_cvu_script.txt; each result is read from the "Lisp: <value>" line
# printed after the typed expression. Self-contained (own helpers) so that `run_sections.sh RW2SCR_CVU` works alone.
# ---------------------------------------------------------------------------

r2c_exec() {  # r2c_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r2c_ok() { echo "ok   $1"; }
r2c_bad() { echo "FAIL $1"; fail=1; }
r2c_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | R2C_CMD="Command: $2" R2C_NTH="$3" awk 'BEGIN { n = ENVIRON["R2C_NTH"] + 0; cmd = ENVIRON["R2C_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r2c_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r2c_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r2c_ok "$5 ($2 -> $got)"; else r2c_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r2c_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r2c_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r2c_ok "$5"; else r2c_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r2c_r() {  # a real the way the app prints it, from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}
r2c_f() {  # <printf format> <awk expression>: a formatted number
  awk -v PI=3.14159265358979323846 -v fmt="$1" "BEGIN { printf fmt, $2 }"
}

if [ "$IS_WINDOWS" = 1 ]; then R2C_SUB="Dino8"; else R2C_SUB="dino8"; fi
R2C_CFG="$TMP/r2c_cfg"
mkdir -p "$R2C_CFG/$R2C_SUB"
R2C="$(r2c_exec "$R2C_CFG" "$HERE/rw2scr_cvu_script.txt")" || { echo "$R2C" | tail -20; r2c_bad "RW2SCR_CVU script exited non-zero"; }
grep -q "^smoke:" <<<"$R2C" || { echo "$R2C" | tail -20; r2c_bad "RW2SCR_CVU: no smoke line (the app did not reach its normal end)"; }

# ---- cvunit (D8-AUTOCAD-LISP-0141): conversions between units of the same kind, nil otherwise ------------
r2c_is "$R2C" '(cvunit 1 "minute" "second")' 1 60.0 'R2C_cvunit: one minute is 60 seconds (documented)'
r2c_is "$R2C" '(cvunit 1 "gallon" "furlong")' 1 nil 'R2C_cvunit: a volume is not a length (documented)'
r2c_is "$R2C" '(cvunit 1.0 "inch" "cm")' 1 2.54 'R2C_cvunit: one inch is 2.54 cm (documented)'
r2c_is "$R2C" '(cvunit '"'"'(1.0 2.5) "ft" "in")' 1 '(12.0 30.0)' 'R2C_cvunit: a 2D point list converts each coordinate'
r2c_is "$R2C" '(cvunit '"'"'(1 2 3) "m" "cm")' 1 '(100.0 200.0 300.0)' 'R2C_cvunit: a 3D point list'
r2c_is "$R2C" '(cvunit 1 "INCH" "CM")' 1 2.54 'R2C_cvunit: unit names ignore case'
r2c_is "$R2C" '(cvunit 1 "mile" "km")' 1 "$(r2c_r "5280 * 12 * 2.54 / 100000")" 'R2C_cvunit: a mile is 5280 ft of 12 in of 2.54 cm'
r2c_is "$R2C" '(cvunit 1 "yard" "ft")' 1 3.0 'R2C_cvunit: a yard is 3 feet'
r2c_is "$R2C" '(cvunit 1 "furlong" "yd")' 1 220.0 'R2C_cvunit: a furlong is 220 yards'
r2c_is "$R2C" '(cvunit 1 "hour" "minute")' 1 60.0 'R2C_cvunit: an hour is 60 minutes'
r2c_is "$R2C" '(cvunit 2 "day" "hour")' 1 48.0 'R2C_cvunit: two days are 48 hours'
r2c_is "$R2C" '(cvunit 1 "sq ft" "sq in")' 1 144.0 'R2C_cvunit: a square foot is 144 square inches'
r2c_is "$R2C" '(cvunit 1 "acre" "sq ft")' 1 43560.0 'R2C_cvunit: an acre is 43560 square feet'
r2c_is "$R2C" '(cvunit 1 "cu ft" "cu in")' 1 1728.0 'R2C_cvunit: a cubic foot is 1728 cubic inches'
r2c_is "$R2C" '(cvunit 1 "gallon" "liter")' 1 "$(r2c_r "231 * 2.54 * 2.54 * 2.54 / 1000")" 'R2C_cvunit: a US gallon is 231 cubic inches'
r2c_is "$R2C" '(cvunit 1 "gallon" "quart")' 1 4.0 'R2C_cvunit: a gallon is 4 quarts'
r2c_is "$R2C" '(cvunit 1 "liter" "ml")' 1 1000.0 'R2C_cvunit: a liter is 1000 ml'
r2c_is "$R2C" '(cvunit 180 "degree" "radian")' 1 "$(r2c_r "PI")" 'R2C_cvunit: 180 degrees is pi radians'
r2c_is "$R2C" '(cvunit 200 "grad" "degree")' 1 180.0 'R2C_cvunit: 200 grads are 180 degrees'
r2c_is "$R2C" '(cvunit 1 "pound" "kg")' 1 0.453592 'R2C_cvunit: a pound is 0.45359237 kg'
r2c_is "$R2C" '(cvunit 1 "kg" "g")' 1 1000.0 'R2C_cvunit: a kilogram is 1000 g'
r2c_is "$R2C" '(cvunit 100 "celsius" "fahrenheit")' 1 212.0 'R2C_cvunit: water boils at 212 F'
r2c_is "$R2C" '(cvunit 32 "fahrenheit" "celsius")' 1 0.0 'R2C_cvunit: water freezes at 0 C'
r2c_is "$R2C" '(cvunit 0 "celsius" "kelvin")' 1 273.15 'R2C_cvunit: 0 C is 273.15 K'
r2c_is "$R2C" '(cvunit 1 "m/s" "km/h")' 1 3.6 'R2C_cvunit: a compound unit, 1 m/s is 3.6 km/h'
r2c_is "$R2C" '(cvunit 1 "kg*m/s^2" "newton")' 1 1.0 'R2C_cvunit: a newton is a kg m per s squared'
r2c_is "$R2C" '(cvunit 1 "foo" "m")' 1 nil 'R2C_cvunit_neg: an unknown unit'
r2c_is "$R2C" '(cvunit 1 "m" "foo")' 1 nil 'R2C_cvunit_neg: an unknown target unit'
r2c_is "$R2C" '(cvunit 1 "kg" "m")' 1 nil 'R2C_cvunit_neg: mass is not length'
r2c_is "$R2C" '(cvunit 1 "celsius" "m")' 1 nil 'R2C_cvunit_neg: temperature is not length'
r2c_is "$R2C" '(cvunit 1 "sq ft" "ft")' 1 nil 'R2C_cvunit_neg: area is not length'
r2c_err "$R2C" '(cvunit "a" "m" "cm")' 1 'bad argument type' 'R2C_cvunit_neg: the value must be a number'
r2c_err "$R2C" '(cvunit 1 "m")' 1 'too few arguments' 'R2C_cvunit_neg: two arguments'
