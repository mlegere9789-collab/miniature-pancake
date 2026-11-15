# ---------------------------------------------------------------------------
# ROWSCR_BITS: bitwise and number-theory functions of the AutoLISP-style dialect (src/script/Lisp.cpp): ~, boole, gcd, logand, logior, lsh.
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0091, -0128, -0176, -0229, -0230, -0231.
# The oracle is the documented behaviour of each function (Autodesk AutoLISP reference, the URL is in each Register row), worked out
# by hand: values from the vendor pages (examples quoted in each description) and recomputed in bash with its own integer arithmetic and truth tables
# One real app process runs rowscr_bits_script.txt, the section reads each result from the "Lisp: <value>" line printed after the typed expression.
# Self-contained (own helpers) so that `run_sections.sh ROWSCR_BITS` works alone.
# ---------------------------------------------------------------------------
rsb_exec() {  # rsb_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
rsb_ok() { echo "ok   $1"; }
rsb_bad() { echo "FAIL $1"; fail=1; }
rsb_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | RSB_CMD="Command: $2" RSB_NTH="$3" awk 'BEGIN { n = ENVIRON["RSB_NTH"] + 0; cmd = ENVIRON["RSB_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
rsb_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(rsb_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then rsb_ok "$5 ($2 -> $got)"; else rsb_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
rsb_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(rsb_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then rsb_ok "$5"; else rsb_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}

# independent bash oracles: boole by its truth table, gcd by Euclid, lsh by a 32-bit unsigned word read back as signed
rsb_boole() {  # <op> <a> <b>: bit 1 of op answers (1,1), bit 2 (1,0), bit 4 (0,1), bit 8 (0,0)
  local op="$1" a="$2" b="$3" out=0 k ab bb r
  for k in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31; do
    ab=$(( (a >> k) & 1 )); bb=$(( (b >> k) & 1 ))
    if   [ "$ab$bb" = 11 ]; then r=$(( op & 1 ))
    elif [ "$ab$bb" = 10 ]; then r=$(( (op >> 1) & 1 ))
    elif [ "$ab$bb" = 01 ]; then r=$(( (op >> 2) & 1 ))
    else r=$(( (op >> 3) & 1 )); fi
    out=$(( out | (r << k) ))
  done
  [ "$out" -ge 2147483648 ] && out=$(( out - 4294967296 ))
  echo "$out"
}
rsb_gcd() { local x="$1" y="$2" t; while [ "$y" -ne 0 ]; do t=$(( x % y )); x=$y; y=$t; done; echo "$x"; }
rsb_lsh() {  # <int> <numbits>
  local w=$(( $1 & 4294967295 )) n="$2"
  if [ "$n" -ge 32 ] || [ "$n" -le -32 ]; then w=0; elif [ "$n" -ge 0 ]; then w=$(( (w << n) & 4294967295 )); else w=$(( w >> (-n) )); fi
  [ "$w" -ge 2147483648 ] && w=$(( w - 4294967296 ))
  echo "$w"
}

if [ "$IS_WINDOWS" = 1 ]; then RSB_SUB="Dino8"; else RSB_SUB="dino8"; fi
RSB_CFG="$TMP/rsb_cfg"
mkdir -p "$RSB_CFG/$RSB_SUB"
RSB="$(rsb_exec "$RSB_CFG" "$HERE/rowscr_bits_script.txt")" || { echo "$RSB" | tail -20; rsb_bad "ROWSCR_BITS script exited non-zero"; }
grep -q "^smoke:" <<<"$RSB" || { echo "$RSB" | tail -20; rsb_bad "ROWSCR_BITS: no smoke line (the app did not reach its normal end)"; }

# ---- ~ (bitwise NOT), row D8-AUTOCAD-LISP-0091 -----------------------------------------------------------
rsb_is "$RSB" '(~ 3)' 1 "-4" "RSB_not: (~ 3) is -4 (documented)"
rsb_is "$RSB" '(~ 100)' 1 "-101" "RSB_not: (~ 100) is -101"
rsb_is "$RSB" '(~ -4)' 1 "3" "RSB_not: (~ -4) is 3"
rsb_is "$RSB" '(~ 0)' 1 "$((~0))" "RSB_not: (~ 0) is -1"
rsb_is "$RSB" '(~ (~ 12345))' 1 "12345" "RSB_not: applying it twice gives the argument back"
rsb_err "$RSB" '(~ 2.5)' 1 'bad argument type' "RSB_not_neg: a real is not an integer"
rsb_err "$RSB" '(~)' 1 'too few arguments' "RSB_not_neg: no argument"

# ---- boole, row D8-AUTOCAD-LISP-0128 (operator 1 AND, 6 XOR, 7 OR, 8 NOR) --------------------------------
rsb_is "$RSB" '(boole 1 12 10)' 1 "8" "RSB_boole: operator 1 is AND: 1100 and 1010 give 1000 = 8"
rsb_is "$RSB" '(boole 7 12 10)' 1 "14" "RSB_boole: operator 7 is OR: 1110 = 14"
rsb_is "$RSB" '(boole 6 12 10)' 1 "6" "RSB_boole: operator 6 is XOR: 0110 = 6"
rsb_is "$RSB" '(boole 8 12 10)' 1 "$(rsb_boole 8 12 10)" "RSB_boole: operator 8 is NOR: the complement of the OR, -15"
rsb_is "$RSB" '(boole 1 12 5)' 1 "4" "RSB_boole: AND of 12 and 5 is 4 (documented example)"
rsb_is "$RSB" '(boole 6 12 5)' 1 "9" "RSB_boole: XOR of 12 and 5 is 9 (documented example)"
rsb_is "$RSB" '(boole 7 12 5)' 1 "13" "RSB_boole: OR of 12 and 5 is 13 (documented example)"
rsb_is "$RSB" '(boole 0 12 10)' 1 "0" "RSB_boole: operator 0 clears every bit"
rsb_is "$RSB" '(boole 15 12 10)' 1 "-1" "RSB_boole: operator 15 sets every bit"
rsb_is "$RSB" '(boole 2 12 10)' 1 "$(rsb_boole 2 12 10)" "RSB_boole: operator 2 answers (1,0): 12 and not 10 = 0100 = 4"
rsb_is "$RSB" '(boole 4 12 10)' 1 "$(rsb_boole 4 12 10)" "RSB_boole: operator 4 answers (0,1): not 12 and 10 = 0010 = 2"
rsb_is "$RSB" '(boole 9 12 10)' 1 "$(rsb_boole 9 12 10)" "RSB_boole: operator 9 (XNOR) is the truth table 1 + 8"
rsb_is "$RSB" '(boole 1 15 7 5)' 1 "5" "RSB_boole: the function runs across successive arguments: (15 and 7) and 5 = 5"
rsb_is "$RSB" '(boole 6 1 2 4)' 1 "7" "RSB_boole: XOR across three arguments: 1 xor 2 xor 4 = 7"
rsb_err "$RSB" '(boole 16 1 2)' 1 'bad argument value' "RSB_boole_neg: operator 16 is outside 0 to 15"
rsb_err "$RSB" '(boole 1 1.5 2)' 1 'bad argument type' "RSB_boole_neg: a real argument"
rsb_err "$RSB" '(boole 1)' 1 'too few arguments' "RSB_boole_neg: no integers to combine"

# ---- gcd, row D8-AUTOCAD-LISP-0176 -----------------------------------------------------------------------
rsb_is "$RSB" '(gcd 81 57)' 1 "3" "RSB_gcd: (gcd 81 57) is 3 (documented)"
rsb_is "$RSB" '(gcd 12 20)' 1 "4" "RSB_gcd: (gcd 12 20) is 4 (documented)"
rsb_is "$RSB" '(gcd 7 13)' 1 "$(rsb_gcd 7 13)" "RSB_gcd: coprime numbers give 1"
rsb_is "$RSB" '(gcd 5 5)' 1 "5" "RSB_gcd: equal numbers give that number"
rsb_is "$RSB" '(gcd 1071 462)' 1 "$(rsb_gcd 1071 462)" "RSB_gcd: 1071 and 462 give 21"
rsb_is "$RSB" '(gcd 462 1071)' 1 "21" "RSB_gcd: the order of the arguments does not matter"
rsb_err "$RSB" '(gcd 0 5)' 1 'bad argument value' "RSB_gcd_neg: zero is not a positive integer"
rsb_err "$RSB" '(gcd -4 6)' 1 'bad argument value' "RSB_gcd_neg: a negative number"
rsb_err "$RSB" '(gcd 1.5 3)' 1 'bad argument type' "RSB_gcd_neg: a real"
rsb_err "$RSB" '(gcd 4)' 1 'too few arguments' "RSB_gcd_neg: one argument"

# ---- logand, row D8-AUTOCAD-LISP-0229 --------------------------------------------------------------------
rsb_is "$RSB" '(logand 7 15 3)' 1 "3" "RSB_logand: 0111 and 1111 and 0011 = 3 (documented)"
rsb_is "$RSB" '(logand 2 3 15)' 1 "2" "RSB_logand: 2 (documented)"
rsb_is "$RSB" '(logand 8 3 4)' 1 "0" "RSB_logand: no common bit gives 0 (documented)"
rsb_is "$RSB" '(logand)' 1 "0" "RSB_logand: no arguments give 0"
rsb_is "$RSB" '(logand 12 10)' 1 "$((12 & 10))" "RSB_logand: 12 and 10 is 8"
rsb_is "$RSB" '(logand -1 255)' 1 "255" "RSB_logand: all bits set and 255 is 255"
rsb_err "$RSB" '(logand 1.0 2)' 1 'bad argument type' "RSB_logand_neg: a real argument"

# ---- logior, row D8-AUTOCAD-LISP-0230 --------------------------------------------------------------------
rsb_is "$RSB" '(logior 1 2 4)' 1 "7" "RSB_logior: 1 or 2 or 4 is 7 (documented)"
rsb_is "$RSB" '(logior 9 3)' 1 "11" "RSB_logior: 1001 or 0011 = 1011 = 11 (documented)"
rsb_is "$RSB" '(logior)' 1 "0" "RSB_logior: no arguments give 0"
rsb_is "$RSB" '(logior 12 10)' 1 "$((12 | 10))" "RSB_logior: 12 or 10 is 14"
rsb_is "$RSB" '(logior 0 0)' 1 "0" "RSB_logior: no bit set stays 0"
rsb_err "$RSB" '(logior "a" 2)' 1 'bad argument type' "RSB_logior_neg: a string argument"

# ---- lsh, row D8-AUTOCAD-LISP-0231 (32-bit word, zero fill, bit 31 is the sign) --------------------------
rsb_is "$RSB" '(lsh 2 1)' 1 "4" "RSB_lsh: (lsh 2 1) is 4 (documented)"
rsb_is "$RSB" '(lsh 2 -1)' 1 "1" "RSB_lsh: (lsh 2 -1) is 1 (documented)"
rsb_is "$RSB" '(lsh 40 2)' 1 "160" "RSB_lsh: (lsh 40 2) is 160 (documented)"
rsb_is "$RSB" '(lsh 8 -2)' 1 "$(rsb_lsh 8 -2)" "RSB_lsh: right shift by two: 8 becomes 2"
rsb_is "$RSB" '(lsh 1 31)' 1 "$(rsb_lsh 1 31)" "RSB_lsh: a result with bit 31 set is the negative integer -2147483648"
rsb_is "$RSB" '(lsh 3 30)' 1 "$(rsb_lsh 3 30)" "RSB_lsh: 3 shifted left by 30 sets bits 30 and 31: -1073741824"
rsb_is "$RSB" '(lsh 1 32)' 1 "0" "RSB_lsh: a bit shifted out of the 32-bit word is discarded"
rsb_is "$RSB" '(lsh 5 0)' 1 "5" "RSB_lsh: a shift of zero leaves the value"
rsb_is "$RSB" '(lsh 5)' 1 "5" "RSB_lsh: a missing count means no shift"
rsb_is "$RSB" '(lsh)' 1 "0" "RSB_lsh: no arguments give 0"
rsb_err "$RSB" '(lsh 2.0 1)' 1 'bad argument type' "RSB_lsh_neg: a real argument"
